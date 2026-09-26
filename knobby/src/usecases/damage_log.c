#include "usecases/damage_log.h"
#include "usecases/game.h"
#include "adapters/lang.h"
#include "usecases/round_safe.h"
#include <string.h>

// ---------- data ----------
typedef struct {
    uint32_t timestamp_ms;
    int8_t   player;       // target player index, 0..MAX_GAME_PLAYERS-1
    int8_t   source;       // source for cmd damage or counter type, -1 if N/A
    uint8_t  event_type;   // log_event_type_t
    int16_t  delta;
} damage_log_entry_t;

static damage_log_entry_t damage_log[DAMAGE_LOG_MAX];
static int damage_log_count = 0;
static int damage_log_head = 0;

// ---------- screen ----------
/* Labels are rendered one page at a time: a full 256-entry ring as one
   widget per entry would eat most of the 128KB LVGL heap and hard-freeze
   the device on alloc failure (LV_ASSERT_HANDLER). */
#define LOG_PAGE_SIZE 32

/* The timestamp sits behind the event it belongs to. */
#define LOG_TIME_COLOR 0x8A8A8Au

lv_obj_t *screen_damage_log = NULL;
static lv_obj_t *damage_log_container = NULL;
static lv_obj_t *delete_btn = NULL;
static lv_obj_t *page_label = NULL;
static int damage_log_selected = -1;  // index into the log, 0 = newest
static int damage_log_page = 0;       // rendered page, derived from selection
static lv_style_t log_label_style;    // shared by all entry labels

// ---------- log operations ----------
void damage_log_add(int player, int delta, uint8_t event_type, int source)
{
    if (delta == 0) return;
    damage_log[damage_log_head].timestamp_ms = lv_tick_get();
    damage_log[damage_log_head].player     = (int8_t)player;
    damage_log[damage_log_head].source     = (int8_t)source;
    damage_log[damage_log_head].event_type = event_type;
    damage_log[damage_log_head].delta      = (int16_t)delta;
    damage_log_head = (damage_log_head + 1) % DAMAGE_LOG_MAX;
    if (damage_log_count < DAMAGE_LOG_MAX) damage_log_count++;
}

void damage_log_reset(void)
{
    damage_log_count = 0;
    damage_log_head = 0;
}

/* ---------- read-only accessors (unit tests) ----------
 * The UI never needs the raw entries - it renders through
 * refresh_damage_log_ui() - but a test that wants to check ring
 * ordering/capacity/undo without driving lv_obj_t widgets needs a way
 * to look inside. index_from_newest 0 is the most recent entry. */
int damage_log_test_count(void)
{
    return damage_log_count;
}

bool damage_log_test_peek(int index_from_newest, int *player, int *delta,
                          uint8_t *event_type, int *source)
{
    int idx;
    if (index_from_newest < 0 || index_from_newest >= damage_log_count) return false;
    idx = (damage_log_head - 1 - index_from_newest + DAMAGE_LOG_MAX) % DAMAGE_LOG_MAX;
    if (player != NULL) *player = damage_log[idx].player;
    if (delta != NULL) *delta = damage_log[idx].delta;
    if (event_type != NULL) *event_type = damage_log[idx].event_type;
    if (source != NULL) *source = damage_log[idx].source;
    return true;
}

/* Remove the newest entry matching player + event_type (used by elimination
   undo so the eliminating event can't also be undone from the log). */
void damage_log_remove_last_for(int player, uint8_t event_type)
{
    int i, j;
    for (i = 0; i < damage_log_count; i++) {
        int idx = (damage_log_head - 1 - i + DAMAGE_LOG_MAX) % DAMAGE_LOG_MAX;
        if (damage_log[idx].player == player &&
            damage_log[idx].event_type == event_type) {
            for (j = i; j > 0; j--) {
                int dst = (damage_log_head - 1 - j + DAMAGE_LOG_MAX) % DAMAGE_LOG_MAX;
                int src = (damage_log_head - j + DAMAGE_LOG_MAX) % DAMAGE_LOG_MAX;
                damage_log[dst] = damage_log[src];
            }
            damage_log_head = (damage_log_head - 1 + DAMAGE_LOG_MAX) % DAMAGE_LOG_MAX;
            damage_log_count--;
            return;
        }
    }
}

static void update_selection_highlight(void);
static void refresh_damage_log_ui(void);

void damage_log_select_next(void)
{
    if (damage_log_count == 0) return;
    if (damage_log_selected < damage_log_count - 1) {
        damage_log_selected++;
    }
    if (damage_log_selected / LOG_PAGE_SIZE != damage_log_page) {
        refresh_damage_log_ui();
    } else {
        update_selection_highlight();
    }
}

void damage_log_select_prev(void)
{
    if (damage_log_count == 0) return;
    if (damage_log_selected > 0) {
        damage_log_selected--;
    }
    if (damage_log_selected / LOG_PAGE_SIZE != damage_log_page) {
        refresh_damage_log_ui();
    } else {
        update_selection_highlight();
    }
}

void damage_log_undo_selected(void)
{
    int buf_idx, i;
    damage_log_entry_t *entry;

    if (damage_log_selected < 0 || damage_log_selected >= damage_log_count) return;

    buf_idx = (damage_log_head - 1 - damage_log_selected + DAMAGE_LOG_MAX) % DAMAGE_LOG_MAX;
    entry = &damage_log[buf_idx];

    if (entry->event_type == LOG_EVT_LIFE) {
        undo_life_change(entry->player, entry->delta);
    } else if (entry->event_type == LOG_EVT_CMD_DAMAGE) {
        undo_life_change(entry->player, entry->delta);
        undo_cmd_damage(entry->source, entry->player, entry->delta);
    } else if (entry->event_type == LOG_EVT_COUNTER) {
        undo_counter_change(entry->player, entry->source, entry->delta);
    }

    /* Remove entry by shifting newer entries down */
    for (i = damage_log_selected; i > 0; i--) {
        int dst = (damage_log_head - 1 - i + DAMAGE_LOG_MAX) % DAMAGE_LOG_MAX;
        int src = (damage_log_head - i + DAMAGE_LOG_MAX) % DAMAGE_LOG_MAX;
        damage_log[dst] = damage_log[src];
    }
    damage_log_head = (damage_log_head - 1 + DAMAGE_LOG_MAX) % DAMAGE_LOG_MAX;
    damage_log_count--;

    /* Adjust selection */
    if (damage_log_count == 0) {
        damage_log_selected = -1;
    } else if (damage_log_selected >= damage_log_count) {
        damage_log_selected = damage_log_count - 1;
    }

    refresh_damage_log_ui();
}

// ---------- UI ----------
static void format_elapsed(uint32_t elapsed_s, char *out, size_t out_sz)
{
    if (elapsed_s >= 60) {
        snprintf(out, out_sz, t(STR_LOG_AGO_MIN), (unsigned long)(elapsed_s / 60));
    } else {
        snprintf(out, out_sz, t(STR_LOG_AGO_SEC), (unsigned long)elapsed_s);
    }
}

/* time_len comes back as the length of the leading "<when>" column that every
   one of the formats below opens with, so the caller can paint it in a
   quieter colour than the event itself. Without that split a row is one
   flat colour and the eye has nothing to anchor on. */
static void format_log_line(damage_log_entry_t *entry, char *buf, size_t buf_sz,
                            int *time_len)
{
    uint32_t elapsed_s = lv_tick_elaps(entry->timestamp_ms) / 1000;
    int abs_delta = entry->delta > 0 ? entry->delta : -entry->delta;
    char time_str[16];

    buf[0] = '\0';  /* ensure a defined string if no branch below matches */
    format_elapsed(elapsed_s, time_str, sizeof(time_str));
    if (time_len != NULL) *time_len = (int)strlen(time_str);

    if (entry->event_type == LOG_EVT_CMD_DAMAGE && entry->source >= 0 &&
        entry->source < MAX_GAME_PLAYERS * 2 && entry->player >= 0 &&
        entry->player < MAX_GAME_PLAYERS) {
        int source, slot;
        decode_cmd_source(entry->source, &source, &slot);
        snprintf(buf, buf_sz, t(slot ? STR_LOG_CMD_DEALT_PARTNER : STR_LOG_CMD_DEALT),
                 time_str,
                 player_names[source],
                 player_names[entry->player],
                 abs_delta);
    } else if (entry->event_type == LOG_EVT_COUNTER && entry->source >= 0 &&
               entry->player >= 0 && entry->player < MAX_GAME_PLAYERS) {
        const counter_definition_t *definition = get_counter_definition((counter_type_t)entry->source);
        const char *counter_name = (definition != NULL) ? definition->log_name : t(STR_LOG_COUNTER_FALLBACK);
        snprintf(buf, buf_sz, t(entry->delta > 0 ? STR_LOG_COUNTER_INCREASED : STR_LOG_COUNTER_DECREASED),
                 time_str,
                 player_names[entry->player],
                 counter_name,
                 abs_delta);
    } else if (entry->player >= 0 && entry->player < MAX_GAME_PLAYERS) {
        snprintf(buf, buf_sz, t(entry->delta > 0 ? STR_LOG_LIFE_GAINED : STR_LOG_LIFE_LOST),
                 time_str, player_names[entry->player], abs_delta);
    }
}

static void update_selection_highlight(void)
{
    int i;
    int first = damage_log_page * LOG_PAGE_SIZE;
    int sel_child = damage_log_selected - first;
    uint32_t child_count = lv_obj_get_child_cnt(damage_log_container);

    for (i = 0; i < (int)child_count && first + i < damage_log_count; i++) {
        lv_obj_t *lbl = lv_obj_get_child(damage_log_container, i);

        if (i == sel_child) {
            lv_obj_set_style_bg_color(lbl, lv_color_hex(0x333333), 0);
            lv_obj_set_style_bg_opa(lbl, LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, 0);
        }
    }

    /* Scroll selected item into view. Force the flex layout first: rows
       recreated this pass still have {0,0,0,0} coords until LVGL lays them
       out, which would scroll to the wrong place. */
    if (sel_child >= 0 && sel_child < (int)child_count) {
        lv_obj_t *sel = lv_obj_get_child(damage_log_container, sel_child);
        lv_obj_update_layout(damage_log_container);
        lv_coord_t sel_y = lv_obj_get_y(sel);
        lv_coord_t sel_h = lv_obj_get_height(sel);
        lv_coord_t cont_h = lv_obj_get_height(damage_log_container);
        lv_coord_t scroll_y = lv_obj_get_scroll_y(damage_log_container);

        if (sel_y - scroll_y < 0) {
            lv_obj_scroll_to_y(damage_log_container, sel_y, LV_ANIM_ON);
        } else if (sel_y + sel_h - scroll_y > cont_h) {
            lv_obj_scroll_to_y(damage_log_container, sel_y + sel_h - cont_h, LV_ANIM_ON);
        }
    }

    /* Show/hide delete button */
    if (delete_btn != NULL) {
        if (damage_log_selected >= 0) {
            lv_obj_clear_flag(delete_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(delete_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void refresh_damage_log_ui(void)
{
    int i, idx, first, last;
    char line[80];   /* the sentence, as the language file writes it */
    char buf[128];   /* the same, wrapped in recolour markers */

    lv_obj_clean(damage_log_container);

    if (damage_log_count == 0) {
        lv_obj_t *lbl = lv_label_create(damage_log_container);
        /* Centred in the viewport rather than parked in its top-left
           corner: with no rows to anchor it, a message pinned to the
           corner reads as a row that failed to draw. */
        lv_obj_set_flex_align(damage_log_container, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_label_set_text(lbl, t(STR_LOG_EMPTY));
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x6E6E6E), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_es_16, 0);
        damage_log_selected = -1;
        damage_log_page = 0;
        if (page_label != NULL) lv_obj_add_flag(page_label, LV_OBJ_FLAG_HIDDEN);
        update_selection_highlight();
        return;
    }

    if (damage_log_selected >= damage_log_count) {
        damage_log_selected = damage_log_count - 1;
    }
    damage_log_page = (damage_log_selected > 0) ? damage_log_selected / LOG_PAGE_SIZE : 0;

    lv_obj_set_flex_align(damage_log_container, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    first = damage_log_page * LOG_PAGE_SIZE;
    last = first + LOG_PAGE_SIZE;
    if (last > damage_log_count) last = damage_log_count;

    for (i = first; i < last; i++) {
        uint32_t accent;
        int time_len = 0;

        idx = (damage_log_head - 1 - i + DAMAGE_LOG_MAX) % DAMAGE_LOG_MAX;

        format_log_line(&damage_log[idx], line, sizeof(line), &time_len);

        if (damage_log[idx].event_type == LOG_EVT_COUNTER) {
            const counter_definition_t *definition =
                get_counter_definition((counter_type_t)damage_log[idx].source);
            accent = (definition != NULL) ? definition->accent_color : 0xFFB74Du;
        } else {
            accent = (damage_log[idx].delta > 0) ? 0x66BB6Au : 0xEF5350u;
        }

        /* One label per row, not a row container with a time column and
           a message column: the page holds 32 of these and the LVGL
           heap is 128KB (see LOG_PAGE_SIZE above), so the hierarchy is
           bought with inline recolouring rather than with three times
           the widgets. */
        snprintf(buf, sizeof(buf), "#%06X %.*s# #%06X %s#",
                 (unsigned)LOG_TIME_COLOR, time_len, line,
                 (unsigned)accent, line + time_len);

        lv_obj_t *lbl = lv_label_create(damage_log_container);
        lv_label_set_recolor(lbl, true);
        lv_label_set_text(lbl, buf);
        lv_obj_add_style(lbl, &log_label_style, 0);
    }

    if (page_label != NULL) {
        if (damage_log_count > LOG_PAGE_SIZE) {
            char page_buf[24];
            snprintf(page_buf, sizeof(page_buf), t(STR_LOG_PAGE_FMT), first + 1, last, damage_log_count);
            lv_label_set_text(page_label, page_buf);
            lv_obj_clear_flag(page_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(page_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    lv_obj_scroll_to_y(damage_log_container, 0, LV_ANIM_OFF);
    update_selection_highlight();
}

// ---------- navigation ----------
static void event_delete_pressed(lv_event_t *e)
{
    (void)e;
    damage_log_undo_selected();
}

void open_damage_log_screen(void)
{
    damage_log_selected = (damage_log_count > 0) ? 0 : -1;
    refresh_damage_log_ui();
    load_screen_if_needed(screen_damage_log);
}

static void event_open_damage_log(lv_event_t *e)
{
    (void)e;
    open_damage_log_screen();
}

// ---------- build ----------
/* The header band, the list viewport and the button band, as y ranges.
 * Every width below is asked of round_safe_width() rather than written
 * down: this is a 360x360 canvas with only the inscribed circle
 * visible, so a box that is wide enough at the middle of the screen is
 * sliced by the bezel near the top. "Registro de Eventos" is 222px
 * wide at lv_font_es_22, and the title used to sit at y=34 where only
 * 210px of the circle is available, so it lost a letter off each end;
 * the 300px list was 7px over its own limit too. The title band is
 * one line tall on purpose - given a taller box LVGL wraps a long
 * translation instead of ellipsising it, and half a word tucked under
 * the rule reads worse than a clean cut. */
#define LOG_TITLE_Y      46
#define LOG_TITLE_H      26   /* exactly one line of lv_font_es_22 */
#define LOG_COUNT_Y      78
#define LOG_LIST_Y      106
#define LOG_LIST_H      140
#define LOG_ROW_PAD_X     8

void build_damage_log_screen(void)
{
    lv_obj_t *btn_label;
    lv_obj_t *rule;
    int title_w = round_safe_width(LOG_TITLE_Y, LOG_TITLE_Y + LOG_TITLE_H);
    int list_w  = round_safe_width(LOG_LIST_Y, LOG_LIST_Y + LOG_LIST_H);

    screen_damage_log = lv_obj_create(NULL);
    lv_obj_set_size(screen_damage_log, 360, 360);
    lv_obj_set_style_bg_color(screen_damage_log, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_damage_log, 0, 0);
    lv_obj_set_scrollbar_mode(screen_damage_log, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title = lv_label_create(screen_damage_log);
    lv_label_set_text(title, t(STR_LOG_TITLE));
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_es_22, 0);
    lv_obj_set_size(title, title_w, LOG_TITLE_H);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    /* Ellipsis rather than silent slicing: a longer translation should
       say so on screen instead of disappearing under the glass. */
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, LOG_TITLE_Y);

    page_label = lv_label_create(screen_damage_log);
    lv_label_set_text(page_label, "");
    lv_obj_set_style_text_color(page_label, lv_color_hex(0x6E6E6E), 0);
    lv_obj_set_style_text_font(page_label, &lv_font_es_14, 0);
    lv_obj_align(page_label, LV_ALIGN_TOP_MID, 0, LOG_COUNT_Y);
    lv_obj_add_flag(page_label, LV_OBJ_FLAG_HIDDEN);

    /* Hairline under the header: the list scrolls under it, and without
       a line the top row reads as part of the heading. */
    rule = lv_obj_create(screen_damage_log);
    lv_obj_remove_style_all(rule);
    lv_obj_set_size(rule, list_w, 1);
    lv_obj_align(rule, LV_ALIGN_TOP_MID, 0, LOG_LIST_Y - 10);
    lv_obj_set_style_bg_color(rule, lv_color_hex(0x262626), 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);

    lv_style_init(&log_label_style);
    lv_style_set_pad_left(&log_label_style, LOG_ROW_PAD_X);
    lv_style_set_pad_right(&log_label_style, LOG_ROW_PAD_X);
    lv_style_set_pad_top(&log_label_style, 4);
    lv_style_set_pad_bottom(&log_label_style, 4);
    lv_style_set_radius(&log_label_style, 6);
    lv_style_set_text_font(&log_label_style, &lv_font_es_14);
    lv_style_set_width(&log_label_style, list_w);

    damage_log_container = lv_obj_create(screen_damage_log);
    lv_obj_remove_style_all(damage_log_container);
    lv_obj_set_size(damage_log_container, list_w, LOG_LIST_H);
    lv_obj_align(damage_log_container, LV_ALIGN_TOP_MID, 0, LOG_LIST_Y);
    lv_obj_set_flex_flow(damage_log_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(damage_log_container, 1, 0);
    lv_obj_set_scrollbar_mode(damage_log_container, LV_SCROLLBAR_MODE_OFF);

    /* Delete / Undo button */
    delete_btn = lv_btn_create(screen_damage_log);
    lv_obj_remove_style_all(delete_btn);
    lv_obj_set_size(delete_btn, 168, 44);
    lv_obj_align(delete_btn, LV_ALIGN_BOTTOM_MID, 0, -48);
    lv_obj_set_ext_click_area(delete_btn, 20);
    lv_obj_set_style_bg_color(delete_btn, lv_color_hex(0x8E1B1B), 0);
    lv_obj_set_style_bg_opa(delete_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(delete_btn, 22, 0);
    lv_obj_add_event_cb(delete_btn, event_delete_pressed, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_flag(delete_btn, LV_OBJ_FLAG_HIDDEN);

    btn_label = lv_label_create(delete_btn);
    lv_label_set_recolor(btn_label, true);
    lv_label_set_text(btn_label, t(STR_LOG_UNDO_HOLD));
    lv_obj_set_style_text_align(btn_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(btn_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(btn_label, &lv_font_es_14, 0);
    lv_obj_center(btn_label);
}
