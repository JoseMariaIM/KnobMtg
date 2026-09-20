#include "settings.h"
#include "hw.h"
#include "storage.h"
#include <string.h>
#include "dice.h"
#include "lang.h"
#include "game_mode.h"
#include "damage_log.h"
#include "rename.h"
#include "game.h"
#include "mana.h"
#include "net_sync.h"
#include "ui_1p.h"
#include "ui_mp.h"
#include "ui_player_menu.h"
#include "ui_wifi.h"
#include "round_safe.h"
#include "snake.h"
#include "pong.h"
#include "dino.h"
#include "tetris.h"
#include "breakout.h"
#include "flappy.h"
#include "eggs.h"
#include "invaders.h"
#include "rps.h"
#include "asteroids.h"

// Forward declarations for cross-module calls
extern void reset_all_values(void);
extern void back_to_main(void);

// ---------- screens ----------
lv_obj_t *screen_quad_menu = NULL;
lv_obj_t *screen_tools_menu = NULL;
lv_obj_t *screen_settings = NULL;
lv_obj_t *screen_battery = NULL;
lv_obj_t *screen_minigames_menu = NULL;
/* Page 0 of the minigames menu IS screen_minigames_menu, so the
   settings item that opens it (and settings_handle_back's scan over
   nav_screen pointers) keeps working unchanged. */
lv_obj_t *minigames_pages[MINIGAMES_PAGE_MAX] = {NULL};
int minigames_page_count = 0;

// ---------- widgets ----------
static lv_obj_t *arc_brightness = NULL;
static lv_obj_t *label_settings_value = NULL;
static lv_obj_t *label_settings_hint = NULL;
static lv_obj_t *label_settings_battery = NULL;
static lv_obj_t *label_settings_battery_detail = NULL;

// ---------- quadrant menu builder ----------
void build_quad_screen(lv_obj_t **screen, quad_item_t items[4])
{
    int i;
    static const lv_coord_t qx[4] = {0,   182, 0,   182};
    static const lv_coord_t qy[4] = {0,   0,   182, 182};
    static const lv_coord_t lx[4] = {10, -10, 10, -10};
    static const lv_coord_t ly[4] = {15,  15, -15, -15};

    *screen = lv_obj_create(NULL);
    lv_obj_set_size(*screen, 360, 360);
    lv_obj_set_style_bg_color(*screen, lv_color_black(), 0);
    lv_obj_set_style_border_width(*screen, 0, 0);
    lv_obj_set_scrollbar_mode(*screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(*screen, 0, 0);

    for (i = 0; i < 4; i++) {
        lv_obj_t *btn = lv_btn_create(*screen);
        lv_obj_remove_style_all(btn);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_set_size(btn, 178, 178);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_pos(btn, qx[i], qy[i]);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);

        if (items[i].cb != NULL && items[i].enabled) {
            lv_obj_add_event_cb(btn, items[i].cb, items[i].event, items[i].user_data);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x1A1A2E), 0);
        } else {
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x111111), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_60, 0);
            lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        }

        if (items[i].icon != NULL && items[i].icon_font != NULL) {
            lv_obj_t *icon_lbl = lv_label_create(btn);
            lv_label_set_text(icon_lbl, items[i].icon);
            lv_obj_set_style_text_font(icon_lbl, items[i].icon_font, 0);
            lv_obj_set_style_text_color(icon_lbl,
                items[i].enabled ? lv_color_white() : lv_color_hex(0x555555), 0);
            lv_obj_align(icon_lbl, LV_ALIGN_CENTER, lx[i], ly[i] - 18);
        }

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, items[i].label);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_es_16, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, lx[i],
            (items[i].icon != NULL) ? ly[i] + 10 : ly[i]);

        if (!items[i].enabled) {
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x555555), 0);
        }
    }
}

// ---------- refresh ----------
static void refresh_brightness_ring(void)
{
    lv_arc_set_value(arc_brightness, brightness_percent);

    lv_obj_set_style_arc_color(arc_brightness, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_brightness, 18, LV_PART_MAIN);

    lv_obj_set_style_arc_color(arc_brightness, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_brightness, 18, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc_brightness, true, LV_PART_INDICATOR);
}

void refresh_settings_ui(void)
{
    char buf[32];
    snprintf(buf, sizeof(buf), t(STR_BRIGHTNESS_FMT), brightness_percent);
    lv_label_set_text(label_settings_value, buf);
    refresh_brightness_ring();
}

void refresh_battery_ui(void)
{
    char buf[32];
    char detail_buf[48];

    battery_percent = read_battery_percent();
    if (label_settings_battery == NULL) return;

    if (battery_percent < 0) {
        lv_label_set_text(label_settings_battery, t(STR_BATTERY_UNKNOWN));
        if (label_settings_battery_detail != NULL) {
            lv_label_set_text(label_settings_battery_detail, t(STR_BATTERY_NOT_CALIBRATED));
        }
        return;
    }

    snprintf(buf, sizeof(buf), t(STR_BATTERY_FMT), battery_percent);
    lv_label_set_text(label_settings_battery, buf);
    if (label_settings_battery_detail != NULL) {
        snprintf(detail_buf, sizeof(detail_buf), t(STR_BATTERY_CALIBRATED_FMT), battery_voltage);
        lv_label_set_text(label_settings_battery_detail, detail_buf);
    }
}

// ---------- navigation ----------
void open_quad_menu(void)
{
    load_screen_if_needed(screen_quad_menu);
}

void open_settings_screen(void)
{
    // No forced battery read here – the brightness settings page does not display
    // battery info. The battery screen entry point forces its own sample.
    refresh_settings_ui();
    load_screen_if_needed(screen_settings);
}

// ---------- events ----------
// ---------- toggle colors ----------
static const uint32_t TOGGLE_ON  = 0x1B5E20;  /* dark green */
static const uint32_t TOGGLE_OFF = 0x4A1010;  /* dark red */

static void set_btn_color(lv_obj_t *btn, uint32_t color)
{
    if (btn != NULL) lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
}

static uint32_t autodim_color(int index)
{
    static const uint32_t colors[AUTO_DIM_COUNT] = {
        0x37474F, 0x1B5E20, 0x0D47A1, 0x4A148C  /* grey, green, blue, purple */
    };
    return (index >= 0 && index < AUTO_DIM_COUNT) ? colors[index] : 0x37474F;
}
static uint32_t orientation_color(int mode)
{
    switch (mode) {
        case ORIENTATION_MODE_CENTRIC: return TOGGLE_ON;
        case ORIENTATION_MODE_TABLETOP:  return 0x0D47A1;
        default:                   return TOGGLE_OFF;
    }
}

static uint32_t color_mode_color(int mode)
{
    return (mode == COLOR_MODE_LIFE) ? 0x4A148C : 0x0D47A1; /* purple / blue */
}

static uint32_t deselect_color(int index)
{
    static const uint32_t colors[DESELECT_COUNT] = {
        0x37474F, 0x1B5E20, 0x0D47A1, 0x4A148C  /* grey, green, blue, purple */
    };
    return (index >= 0 && index < DESELECT_COUNT) ? colors[index] : 0x1A1A2E;
}

static void event_quad_screen_settings(lv_event_t *e)
{
    (void)e;
    lv_scr_load(settings_pages[0]);
}

static const char *autodim_label(int index)
{
    switch (index) {
        case AUTO_DIM_15S: return t(STR_SETTING_AUTODIM_15S);
        case AUTO_DIM_30S: return t(STR_SETTING_AUTODIM_30S);
        case AUTO_DIM_60S: return t(STR_SETTING_AUTODIM_60S);
        default:           return t(STR_SETTING_AUTODIM_OFF);
    }
}

static const char *color_mode_label(int mode)
{
    switch (mode) {
        case COLOR_MODE_LIFE:   return t(STR_SETTING_COLORS_LIFE);
        default:                return t(STR_SETTING_COLORS_PLAYER);
    }
}

static const char *deselect_label(int index)
{
    switch (index) {
        case DESELECT_5S:    return t(STR_SETTING_DESELECT_5S);
        case DESELECT_15S:   return t(STR_SETTING_DESELECT_15S);
        case DESELECT_30S:   return t(STR_SETTING_DESELECT_30S);
        default:             return t(STR_SETTING_DESELECT_NEVER);
    }
}

static const char *orientation_mode_label(int mode)
{
    switch (mode) {
        case ORIENTATION_MODE_CENTRIC:  return t(STR_SETTING_ORIENTATION_CENTRIC);
        case ORIENTATION_MODE_TABLETOP: return t(STR_SETTING_ORIENTATION_TABLETOP);
        default:                        return t(STR_SETTING_ORIENTATION_ABSOLUTE);
    }
}

static const char *auto_eliminate_label(int val)
{
    return val ? t(STR_SETTING_AUTO_ELIM_ON) : t(STR_SETTING_AUTO_ELIM_OFF);
}

// ---------- declarative settings ----------
/* Every user setting lives in this one table. Pages, "More" chaining,
   back-navigation, and sim navigation are all derived from it: to add,
   remove, or reorder a setting, edit only this table (plus its label,
   color, and NVS functions). Label fns must return strings that
   lv_label_set_text may copy — never switch the refresh to
   lv_label_set_text_static. */

static int autodim_get(void) { return nvs_get_auto_dim(); }
static void autodim_set(int v)
{
    nvs_set_auto_dim(v);
    if (v == AUTO_DIM_OFF && dimmed) {
        dimmed = false;
        brightness_apply();
    }
}

static uint32_t toggle_color(int val)
{
    return val ? TOGGLE_ON : TOGGLE_OFF;
}

void open_battery_screen(void)
{
    update_battery_measurement(true);
    refresh_battery_ui();
    lv_scr_load(screen_battery);
}

/* Player-scoped menu screens that should face the acting player when
   menu facing is enabled. screen_select/screen_damage are the commander
   damage picker and editor: player-scoped in multiplayer (opened from the
   player menu, which sets cmd_damage_target), and a no-op in 1-player
   since mp_player_seat_rotation() returns 0 there. */
static bool screen_is_player_menu(lv_obj_t *screen)
{
    return screen == screen_player_menu ||
           screen == screen_eliminated_player_menu ||
           screen == screen_player_all_damage ||
           screen == screen_counter_menu ||
           screen == screen_counter_edit ||
           screen == screen_player_color_menu ||
           screen == screen_player_color_picker ||
           screen == screen_player_name ||
           screen == screen_select ||
           screen == screen_damage;
}

/* Single applier for the effective display rotation: the user's physical
   rotation, plus the acting player's seat on player menus when the
   "Menus: Face Player" toggle is on. Idempotent — recomputes from the
   active screen, so any navigation path can call it safely. */
void menu_facing_refresh(void)
{
    static int applied = -1;
    int target = nvs_get_display_rotation();

    if (nvs_get_menu_facing() && screen_is_player_menu(lv_scr_act()))
        target = (target + mp_player_seat_rotation(menu_player)) & 3;
    if (target == applied) return;
    applied = target;
    display_apply_rotation(target);
    /* Repaint the whole frame immediately: the panel's GRAM still holds the
       old orientation the instant the MADCTL flags change. */
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL);
}

static const char *random_first_label(int val)
{
    return val ? t(STR_SETTING_RANDOM_FIRST_ON) : t(STR_SETTING_RANDOM_FIRST_OFF);
}

static const char *menu_facing_label(int val)
{
    return val ? t(STR_SETTING_MENU_FACE_PLAYER) : t(STR_SETTING_MENU_FIXED);
}

static const char *multi_select_label(int val)
{
    return val ? t(STR_SETTING_MULTI_SELECT_ON) : t(STR_SETTING_MULTI_SELECT_OFF);
}

static void multi_select_set(int v)
{
    nvs_set_multi_select(v);
    if (v == 0) {
        /* Turning multi-select off: drop any lingering multi-selection so the
           single-select rules apply cleanly on return to the life screen. */
        selection_clear();
    }
}

// ---------- table sync screen ----------
lv_obj_t *screen_table_sync = NULL;
lv_obj_t *screen_partners = NULL;
lv_obj_t *screen_language_picker = NULL;
static lv_obj_t *table_sync_action_lbl; /* Start <-> Invite quadrant */
static lv_obj_t *table_sync_status_lbl; /* status tile */
static lv_timer_t *table_sync_timer;
static bool table_sync_radio_error;

void refresh_table_sync_ui(void)
{
    static char status_buf[24];
    int status = net_sync_status();
    int code = net_sync_code();

    switch (status) {
        case NET_SYNC_JOINING:
            snprintf(status_buf, sizeof(status_buf), "%s", t(STR_TABLE_SYNC_JOINING));
            break;
        case NET_SYNC_HOSTING:
            snprintf(status_buf, sizeof(status_buf), t(STR_TABLE_SYNC_INVITING), code);
            break;
        case NET_SYNC_IN_GAME:
            snprintf(status_buf, sizeof(status_buf), t(STR_TABLE_SYNC_IN_GAME), code);
            break;
        default:
            /* Sync is mirror-mode: a 1p view can't represent the shared
               game, so pairing refuses below and the tile says why. */
            if (nvs_get_players_to_track() <= 1)
                snprintf(status_buf, sizeof(status_buf), "%s", t(STR_TABLE_SYNC_1P_NO_SYNC));
            else
                snprintf(status_buf, sizeof(status_buf), "%s",
                         table_sync_radio_error ? t(STR_TABLE_SYNC_RADIO_ERROR) : t(STR_TABLE_SYNC_OFF));
            break;
    }
    lv_label_set_text(table_sync_status_lbl, status_buf);
    /* In a game the host action re-opens the invite window for the same
       session (late joiners, rebooted devices) instead of re-keying. */
    lv_label_set_text(table_sync_action_lbl,
        (status == NET_SYNC_HOSTING || status == NET_SYNC_IN_GAME)
            ? t(STR_TABLE_SYNC_HOLD_INVITE) : t(STR_TABLE_SYNC_HOLD_START));
}

static void event_table_sync_start(lv_event_t *e)
{
    (void)e;
    if (nvs_get_players_to_track() <= 1) return;
    table_sync_radio_error = !net_sync_start_game();
    refresh_table_sync_ui();
}

static void event_table_sync_join(lv_event_t *e)
{
    (void)e;
    if (nvs_get_players_to_track() <= 1) return;
    table_sync_radio_error = !net_sync_join_game();
    refresh_table_sync_ui();
}

static void event_table_sync_leave(lv_event_t *e)
{
    (void)e;
    net_sync_leave_game();
    table_sync_radio_error = false;
    refresh_table_sync_ui();
}

/* Pairing runs in the background (invite window, join listening), so the
   status tile has to track it while the screen is up. The timer pauses
   itself when the user navigates away and is resumed on open. */
static void table_sync_timer_cb(lv_timer_t *timer)
{
    if (lv_scr_act() != screen_table_sync) {
        lv_timer_pause(timer);
        return;
    }
    refresh_table_sync_ui();
}

void open_table_sync_screen(void)
{
    refresh_table_sync_ui();
    lv_timer_resume(table_sync_timer);
    lv_scr_load(screen_table_sync);
}

void build_table_sync_screen(void)
{
    quad_item_t items[4];

    /* All three actions are destructive to a live game (Start opens or
       re-invites, Join drops the current session, Leave exits), so they
       require a long press. */
    memset(items, 0, sizeof(items));
    items[0].label = t(STR_TABLE_SYNC_HOLD_START);
    items[0].cb = event_table_sync_start;
    items[0].enabled = true;
    items[0].event = LV_EVENT_LONG_PRESSED;
    items[1].label = t(STR_TABLE_SYNC_HOLD_JOIN);
    items[1].cb = event_table_sync_join;
    items[1].enabled = true;
    items[1].event = LV_EVENT_LONG_PRESSED;
    items[2].label = t(STR_TABLE_SYNC_HOLD_LEAVE);
    items[2].cb = event_table_sync_leave;
    items[2].enabled = true;
    items[2].event = LV_EVENT_LONG_PRESSED;
    items[3].label = t(STR_TABLE_SYNC_OFF); /* status tile, refreshed live */
    items[3].enabled = false;
    items[3].event = LV_EVENT_CLICKED;

    build_quad_screen(&screen_table_sync, items);
    table_sync_action_lbl =
        lv_obj_get_child(lv_obj_get_child(screen_table_sync, 0), 0);
    table_sync_status_lbl =
        lv_obj_get_child(lv_obj_get_child(screen_table_sync, 3), 0);
    table_sync_timer = lv_timer_create(table_sync_timer_cb, 500, NULL);
    lv_timer_pause(table_sync_timer);
}

// ---------- language picker ----------
static lv_obj_t *language_list_container = NULL;

/* Only ever holds LANG_COUNT (currently 2) short rows, so instead of a
 * top-anchored rectangle we can afford to center this small container
 * on the display's vertical middle, where the circle is widest - see
 * round_safe.h. That keeps it essentially full-width with no clipping,
 * unlike a list long enough to need scrolling (compare scan_list_width
 * in ui_wifi.c, which can't be centered the same way). */
#define LANGUAGE_LIST_Y1 125
#define LANGUAGE_LIST_Y2 235
static int language_list_width = 280;

static void event_language_row_click(lv_event_t *e)
{
    lang_t lang = (lang_t)(intptr_t)lv_event_get_user_data(e);
    lang_set(lang); /* persists + restarts the device to relabel every screen */
}

static void add_language_row(lang_t lang)
{
    bool is_current = (lang == lang_get());

    lv_obj_t *row = lv_obj_create(language_list_container);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, language_list_width - 20, 40);
    lv_obj_set_style_radius(row, 4, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(is_current ? TOGGLE_ON : 0x1E1E2E), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, event_language_row_click, LV_EVENT_CLICKED, (void *)(intptr_t)lang);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, t(lang == LANG_ES ? STR_LANGUAGE_ES : STR_LANGUAGE_EN));
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_es_16, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 12, 0);
}

void open_language_picker_screen(void)
{
    int i;
    lv_obj_clean(language_list_container);
    for (i = 0; i < LANG_COUNT; i++) add_language_row((lang_t)i);
    load_screen_if_needed(screen_language_picker);
}

void build_language_picker_screen(void)
{
    screen_language_picker = lv_obj_create(NULL);
    lv_obj_set_size(screen_language_picker, 360, 360);
    lv_obj_set_style_bg_color(screen_language_picker, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_language_picker, 0, 0);
    lv_obj_set_scrollbar_mode(screen_language_picker, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title = lv_label_create(screen_language_picker);
    lv_label_set_text(title, t(STR_SETTING_LANGUAGE));
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_es_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    language_list_width = round_safe_width(LANGUAGE_LIST_Y1, LANGUAGE_LIST_Y2);
    language_list_container = lv_obj_create(screen_language_picker);
    lv_obj_remove_style_all(language_list_container);
    lv_obj_set_size(language_list_container, language_list_width, LANGUAGE_LIST_Y2 - LANGUAGE_LIST_Y1);
    lv_obj_align(language_list_container, LV_ALIGN_TOP_MID, 0, LANGUAGE_LIST_Y1);
    lv_obj_set_flex_flow(language_list_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(language_list_container, 8, 0);
    lv_obj_set_scrollbar_mode(language_list_container, LV_SCROLLBAR_MODE_OFF);
}

// ---------- partners ----------
/* Which players field a partner commander.
 *
 * A game-setup choice, so it lives beside the player count and the
 * starting life rather than inside any one player's menu: you set the
 * table up once, and every partner control on the device - the second
 * commander-damage tally, the partner-tax counter, the attack dial's
 * second commander mode - appears or disappears from here.
 *
 * Default is nobody. Most tables have no partners at all, and showing
 * those controls to everyone made the device look like it was tracking
 * something that was not on the table.
 *
 * Laid out as quarters like every other menu on this device rather
 * than as a list of rows: a row list wastes the corners of a round
 * display and reads as a form, and this screen is a menu of players.
 *
 * ONE screen, repainted, rather than one screen per page the way the
 * settings and minigames menus do it. Those have a fixed number of
 * pages; this one is sized by the player count, and at a full table of
 * eight the three pages it would need cost about 13KB of a 128KB pool
 * - enough to fail the memory budget on their own. Repainting four
 * tiles costs nothing and there is no screen transition between pages,
 * which for a settings grid reads better anyway. */
#define PARTNERS_PER_PAGE 3   /* when paging is needed: three players + "More" */
static int partners_page = 0;

static int partners_page_count(void)
{
    int num = nvs_get_num_players();
    if (num <= 4) return 1;
    return (num + PARTNERS_PER_PAGE - 1) / PARTNERS_PER_PAGE;
}

/* The player a quarter stands for on the page being shown, or -1 when
   the quarter is "More" or past the end of the table. */
static int partners_tile_player(int slot)
{
    int num = nvs_get_num_players();
    int player;

    if (slot < 0 || slot >= 4) return -1;
    /* A table that fits uses all four quarters for players: spending
       one on a "More" that leads back to the same page would be silly. */
    if (num <= 4) return (slot < num) ? slot : -1;
    if (slot >= PARTNERS_PER_PAGE) return -1;   /* the "More" quarter */
    player = partners_page * PARTNERS_PER_PAGE + slot;
    return (player < num) ? player : -1;
}

static void refresh_partners_ui(void)
{
    int slot;

    if (screen_partners == NULL) return;

    for (slot = 0; slot < 4; slot++) {
        int player = partners_tile_player(slot);
        lv_obj_t *tile = lv_obj_get_child(screen_partners, slot);
        lv_obj_t *text;
        char buf[48];

        if (tile == NULL) continue;
        text = lv_obj_get_child(tile, 0);
        if (text == NULL) continue;

        if (player < 0) {
            bool is_more = (partners_page_count() > 1 && slot == 3);
            set_btn_color(tile, is_more ? 0x1A1A2E : 0x111111);
            lv_label_set_text(text, is_more ? t(STR_SETTINGS_MORE) : "");
            continue;
        }

        /* Spelled out under the name rather than left to the colour:
           this is the one screen where the answer has to be
           unambiguous, and "green means yes" is a convention the
           reader has to be told. */
        snprintf(buf, sizeof(buf), "%s\n%s", player_names[player],
                 t(player_has_partner(player) ? STR_PARTNER_YES : STR_PARTNER_NO));
        lv_label_set_text(text, buf);
        set_btn_color(tile, player_has_partner(player) ? TOGGLE_ON : 0x1A1A2E);
    }
}

static void event_partner_tile(lv_event_t *e)
{
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    int player = partners_tile_player(slot);

    /* The "More" quarter and the empty ones share this handler, since
       which is which depends on the page and the player count - both
       of which move without the screen being rebuilt. */
    if (player < 0) {
        int pages = partners_page_count();
        if (pages > 1 && slot == 3) partners_page = (partners_page + 1) % pages;
        refresh_partners_ui();
        return;
    }

    set_player_has_partner(player, !player_has_partner(player));
    settings_save();
    refresh_partners_ui();
}

void build_partners_screen(void)
{
    quad_item_t q[4];
    int s;

    memset(q, 0, sizeof(q));
    for (s = 0; s < 4; s++) {
        q[s].label = "";
        q[s].cb = event_partner_tile;
        q[s].enabled = true;
        q[s].event = LV_EVENT_CLICKED;
        q[s].user_data = (void *)(intptr_t)s;
    }
    build_quad_screen(&screen_partners, q);
}

void open_partners_screen(void)
{
    if (screen_partners == NULL) build_partners_screen();
    /* The table can have shrunk since the last visit; landing on a
       page that no longer exists would show four empty quarters. */
    if (partners_page >= partners_page_count()) partners_page = 0;
    refresh_partners_ui();
    load_screen_if_needed(screen_partners);
}

/* Knob flips pages, with wraparound - the same gesture that flips
   settings pages, so a big table pages the way everything else does. */
bool partners_knob_page(int dir)
{
    int pages = partners_page_count();

    if (lv_scr_act() != screen_partners || screen_partners == NULL) return false;
    if (pages > 1) {
        partners_page = (partners_page + dir + pages) % pages;
        refresh_partners_ui();
    }
    return true;
}

/* ---------- read-only accessors (unit tests) ---------- */
int partners_test_page_count(void)      { return partners_page_count(); }
int partners_test_page(void)            { return partners_page; }
void partners_page_reset_for_test(void) { partners_page = 0; refresh_partners_ui(); }
int partners_test_tile_player(int slot) { return partners_tile_player(slot); }

static const setting_item_t settings_items[] = {
    { .id = "brightness",     .fixed_label_id = STR_SETTING_BRIGHTNESS, .navigate = open_settings_screen, .nav_screen = &screen_settings },
    { .id = "autodim",        .label = autodim_label,          .color = autodim_color,     .get = autodim_get,              .set = autodim_set,              .count = AUTO_DIM_COUNT },
    { .id = "battery",        .fixed_label_id = STR_SETTING_BATTERY, .navigate = open_battery_screen, .nav_screen = &screen_battery },
    { .id = "color-mode",     .label = color_mode_label,       .color = color_mode_color,  .get = nvs_get_color_mode,       .set = nvs_set_color_mode,       .count = COLOR_MODE_COUNT },
    { .id = "deselect",       .label = deselect_label,         .color = deselect_color,    .get = nvs_get_deselect_timeout, .set = nvs_set_deselect_timeout, .count = DESELECT_COUNT },
    { .id = "orientation",    .label = orientation_mode_label, .color = orientation_color, .get = nvs_get_orientation,      .set = nvs_set_orientation,      .count = ORIENTATION_MODE_COUNT },
    { .id = "auto-eliminate", .label = auto_eliminate_label,   .color = toggle_color,      .get = nvs_get_auto_eliminate,   .set = nvs_set_auto_eliminate,   .count = 2 },
    { .id = "random-first",   .label = random_first_label,     .color = toggle_color,      .get = nvs_get_random_first,     .set = nvs_set_random_first,     .count = 2 },
    { .id = "multi-select",   .label = multi_select_label,     .color = toggle_color,      .get = nvs_get_multi_select,     .set = multi_select_set,         .count = 2 },
    { .id = "table-sync",     .fixed_label_id = STR_SETTING_TABLE_SYNC, .navigate = open_table_sync_screen, .nav_screen = &screen_table_sync },
    { .id = "minigames",      .fixed_label_id = STR_SETTING_MINIGAMES, .navigate = open_minigames_menu, .nav_screen = &screen_minigames_menu },
    { .id = "menu-facing",    .label = menu_facing_label,      .color = toggle_color,      .get = nvs_get_menu_facing,      .set = nvs_set_menu_facing,      .count = 2 },
    { .id = "language",       .fixed_label_id = STR_SETTING_LANGUAGE, .navigate = open_language_picker_screen, .nav_screen = &screen_language_picker },
    { .id = "wifi",           .fixed_label_id = STR_SETTING_WIFI, .navigate = open_wifi_settings_screen, .nav_screen = &screen_wifi_settings },
    { .id = "updates",        .fixed_label_id = STR_SETTING_UPDATES, .navigate = open_ota_update_screen, .nav_screen = &screen_ota_update },
};
#define SETTINGS_ITEM_COUNT ((int)(sizeof(settings_items) / sizeof(settings_items[0])))
#define MAX_SETTINGS_PAGES  ((SETTINGS_ITEM_COUNT + 2) / 3)

lv_obj_t *settings_pages[MAX_SETTINGS_PAGES];
int settings_page_count = 0;
static lv_obj_t *setting_btns[SETTINGS_ITEM_COUNT];
static lv_obj_t *setting_lbls[SETTINGS_ITEM_COUNT];
static int setting_page_of[SETTINGS_ITEM_COUNT];

void refresh_settings_pages_ui(void)
{
    int i;
    for (i = 0; i < SETTINGS_ITEM_COUNT; i++) {
        const setting_item_t *it = &settings_items[i];
        int v;
        if (setting_btns[i] == NULL || it->get == NULL) continue;
        v = it->get();
        lv_label_set_text(setting_lbls[i], it->label(v));
        set_btn_color(setting_btns[i], it->color ? it->color(v) : 0x1A1A2E);
    }
}

static void event_setting_item(lv_event_t *e)
{
    const setting_item_t *it = lv_event_get_user_data(e);
    if (it == NULL) return;
    if (it->navigate != NULL) {
        it->navigate();
        return;
    }
    it->set((it->get() + 1) % it->count);
    refresh_settings_pages_ui();
}

static void event_setting_more(lv_event_t *e)
{
    int page = (int)(intptr_t)lv_event_get_user_data(e);
    if (page >= 0 && page < settings_page_count)
        lv_scr_load(settings_pages[page]);
}

/* Chunk the flat item list into quad pages: 3 items + "More" per page.
   "More" always advances and wraps from the last page to the first, so
   tap navigation cycles just like the knob. */
static void build_settings_pages(void)
{
    int idx = 0;
    int page = 0;
    int total_pages = (SETTINGS_ITEM_COUNT + 2) / 3;

    while (idx < SETTINGS_ITEM_COUNT) {
        int remaining = SETTINGS_ITEM_COUNT - idx;
        int on_page = (remaining < 3) ? remaining : 3;
        int first = idx;
        int s;
        quad_item_t q[4];

        memset(q, 0, sizeof(q));
        for (s = 0; s < 4; s++) q[s].label = "";
        for (s = 0; s < on_page; s++, idx++) {
            const setting_item_t *it = &settings_items[idx];
            q[s].label = (it->label != NULL) ? it->label(it->get()) : t(it->fixed_label_id);
            q[s].cb = event_setting_item;
            q[s].enabled = true;
            q[s].event = (it->event != 0) ? it->event : LV_EVENT_CLICKED;
            q[s].user_data = (void *)it;
            setting_page_of[idx] = page;
        }
        q[3].label = t(STR_SETTINGS_MORE);
        q[3].cb = event_setting_more;
        q[3].enabled = true;
        q[3].event = LV_EVENT_CLICKED;
        q[3].user_data = (void *)(intptr_t)((page + 1) % total_pages);
        build_quad_screen(&settings_pages[page], q);
        for (s = 0; s < on_page; s++) {
            setting_btns[first + s] = lv_obj_get_child(settings_pages[page], s);
            setting_lbls[first + s] = lv_obj_get_child(setting_btns[first + s], 0);
        }
        page++;
    }
    settings_page_count = page;
    refresh_settings_pages_ui();
}

bool settings_handle_back(lv_obj_t *screen)
{
    int i;

    for (i = 0; i < SETTINGS_ITEM_COUNT; i++) {
        if (settings_items[i].nav_screen != NULL && screen == *settings_items[i].nav_screen) {
            if (screen == screen_settings) settings_save();
            lv_scr_load(settings_pages[setting_page_of[i]]);
            return true;
        }
    }
    /* Back from any settings page exits to the quad menu — back means
       "leave settings", not "previous page" (the knob flips pages). */
    for (i = 0; i < settings_page_count; i++) {
        if (screen == settings_pages[i]) {
            settings_save();
            lv_scr_load(screen_quad_menu);
            return true;
        }
    }
    return false;
}

/* Knob left/right flips between settings pages, with wraparound.
   Returns false when the active screen is not a settings page. */
bool settings_knob_page(int dir)
{
    int i;

    for (i = 0; i < settings_page_count; i++) {
        if (lv_scr_act() == settings_pages[i]) {
            lv_scr_load(settings_pages[(i + dir + settings_page_count) % settings_page_count]);
            return true;
        }
    }
    return false;
}

int settings_item_page(const char *id)
{
    int i;
    for (i = 0; i < SETTINGS_ITEM_COUNT; i++) {
        if (strcmp(settings_items[i].id, id) == 0)
            return setting_page_of[i];
    }
    return -1;
}

static void event_quad_tools(lv_event_t *e)
{
    (void)e;
    lv_scr_load(screen_tools_menu);
}

static void event_general_game_mode(lv_event_t *e)
{
    (void)e;
    open_game_mode_menu();
}

static void event_open_damage_log(lv_event_t *e)
{
    (void)e;
    open_damage_log_screen();
}

static void event_general_reset(lv_event_t *e)
{
    (void)e;
    reset_all_values();
    back_to_main();
    lv_indev_wait_release(lv_indev_get_act());
}

// ---------- screen builders ----------
void build_quad_menus(void)
{
    quad_item_t main_items[4] = {
        {t(STR_MENU_SETTINGS), event_quad_screen_settings, true, LV_EVENT_CLICKED},
        {t(STR_MENU_GAME_MODE), event_general_game_mode, true, LV_EVENT_CLICKED},
        {t(STR_MENU_TOOLS),             event_quad_tools, true, LV_EVENT_CLICKED},
        {t(STR_MENU_RESET_HOLD), event_general_reset, true, LV_EVENT_LONG_PRESSED},
    };
    build_quad_screen(&screen_quad_menu, main_items);

    quad_item_t tools_items[4] = {
        {t(STR_TOOL_DICE),        event_tool_dice, true, LV_EVENT_CLICKED},
        {t(STR_TOOL_COIN),        event_tool_coin, true, LV_EVENT_CLICKED},
        {t(STR_TOOL_EVENT_LOG),  event_open_damage_log, true, LV_EVENT_CLICKED},
        {t(STR_TOOL_MANA_POOL),  event_tool_mana, true, LV_EVENT_CLICKED},
    };
    build_quad_screen(&screen_tools_menu, tools_items);

    build_settings_pages();
    /* The minigames menu is NOT built here: three quad pages of tiles
       for a screen many sessions never open is ~3.5KB of the LVGL pool
       held permanently. open_minigames_menu() builds it on first
       entry, same as the games themselves. */
}

void build_settings_screen(void)
{
    screen_settings = lv_obj_create(NULL);
    lv_obj_set_size(screen_settings, 360, 360);
    lv_obj_set_style_bg_color(screen_settings, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_settings, 0, 0);
    lv_obj_set_scrollbar_mode(screen_settings, LV_SCROLLBAR_MODE_OFF);

    arc_brightness = lv_arc_create(screen_settings);
    lv_obj_set_size(arc_brightness, 280, 280);
    lv_obj_align(arc_brightness, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_rotation(arc_brightness, 90);
    lv_arc_set_bg_angles(arc_brightness, 0, 360);
    lv_arc_set_range(arc_brightness, 0, 100);
    lv_arc_set_value(arc_brightness, brightness_percent);
    lv_obj_remove_style(arc_brightness, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc_brightness, LV_OBJ_FLAG_CLICKABLE);

    label_settings_value = lv_label_create(screen_settings);
    lv_label_set_text(label_settings_value, "Brightness: 80%"); /* placeholder, overwritten by refresh_settings_ui() */
    lv_obj_set_style_text_color(label_settings_value, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_settings_value, &lv_font_es_32, 0);
    lv_obj_align(label_settings_value, LV_ALIGN_CENTER, 0, -14);

    label_settings_hint = lv_label_create(screen_settings);
    lv_label_set_text(label_settings_hint, t(STR_BRIGHTNESS_HINT));
    lv_obj_set_style_text_color(label_settings_hint, lv_color_hex(0x6A6A6A), 0);
    lv_obj_set_style_text_font(label_settings_hint, &lv_font_es_14, 0);
    lv_obj_align(label_settings_hint, LV_ALIGN_CENTER, 0, 24);
}

void build_battery_screen(void)
{
    screen_battery = lv_obj_create(NULL);
    lv_obj_set_size(screen_battery, 360, 360);
    lv_obj_set_style_bg_color(screen_battery, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_battery, 0, 0);
    lv_obj_set_scrollbar_mode(screen_battery, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title = lv_label_create(screen_battery);
    lv_label_set_text(title, t(STR_BATTERY_TITLE));
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_es_22, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);

    label_settings_battery = lv_label_create(screen_battery);
    lv_label_set_text(label_settings_battery, "Battery: --%");
    lv_obj_set_style_text_color(label_settings_battery, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_settings_battery, &lv_font_es_32, 0);
    lv_obj_align(label_settings_battery, LV_ALIGN_CENTER, 0, -10);

    label_settings_battery_detail = lv_label_create(screen_battery);
    lv_label_set_text(label_settings_battery_detail, "No calibrated reading");
    lv_obj_set_style_text_color(label_settings_battery_detail, lv_color_hex(0x7A7A7A), 0);
    lv_obj_set_style_text_font(label_settings_battery_detail, &lv_font_es_16, 0);
    lv_obj_align(label_settings_battery_detail, LV_ALIGN_CENTER, 0, 30);
}

// ---------- minigames menu ----------
/* Every game on the device, in menu order. This table is the only place
   a game has to be listed for it to appear, paginate and open: adding
   one is a row here plus its screen_registry[] row in knob.c (for the
   knob/back dispatch, which is per-screen, not per-menu-entry).

   Ordered oldest-first so the games people already have records in stay
   on the first page where they have always been. */
static const minigame_entry_t minigame_entries[] = {
    { STR_MINIGAME_SNAKE,    open_snake_screen    },
    { STR_MINIGAME_PONG,     open_pong_screen     },
    { STR_MINIGAME_DINO,     open_dino_screen     },
    { STR_MINIGAME_TETRIS,   open_tetris_screen   },
    { STR_MINIGAME_BREAKOUT, open_breakout_screen },
    { STR_MINIGAME_FLAPPY,   open_flappy_screen   },
    { STR_MINIGAME_EGGS,     open_eggs_screen     },
    { STR_MINIGAME_INVADERS, open_invaders_screen },
    { STR_MINIGAME_RPS,      open_rps_screen      },
    { STR_MINIGAME_ASTEROIDS, open_asteroids_screen },
};
#define MINIGAME_ENTRY_COUNT \
    ((int)(sizeof(minigame_entries) / sizeof(minigame_entries[0])))

/* Which menu page the running game was started from, so leaving it
   comes back to the tile the user actually pressed rather than dumping
   them on page 1 to page their way back. Recorded at launch rather than
   derived from a game->page table because the page the user was looking
   at IS the page that game's tile is on - and this stays right if a
   game is ever listed twice or the order changes. */
static int minigames_launch_page = 0;

static void event_open_minigame(lv_event_t *e)
{
    const minigame_entry_t *entry = lv_event_get_user_data(e);
    int i;

    for (i = 0; i < minigames_page_count; i++) {
        if (lv_scr_act() == minigames_pages[i]) {
            minigames_launch_page = i;
            break;
        }
    }
    if (entry != NULL && entry->open != NULL) entry->open();
}

static void event_minigames_more(lv_event_t *e)
{
    int page = (int)(intptr_t)lv_event_get_user_data(e);
    if (page >= 0 && page < minigames_page_count)
        lv_scr_load(minigames_pages[page]);
}

/* Fresh entry from Settings: always page 1. */
void open_minigames_menu(void)
{
    if (screen_minigames_menu == NULL) build_minigames_menu_screen();
    minigames_launch_page = 0;
    load_screen_if_needed(screen_minigames_menu);
}

/* Leaving a game: back to the page it was started from. */
void open_minigames_menu_at_launch_page(void)
{
    if (screen_minigames_menu == NULL) build_minigames_menu_screen();
    if (minigames_launch_page < 0 || minigames_launch_page >= minigames_page_count)
        minigames_launch_page = 0;
    load_screen_if_needed(minigames_pages[minigames_launch_page]);
}

/* Same 3-items-plus-"More" chunking as build_settings_pages(); see the
   comment there for why "More" wraps rather than dead-ending. */
void build_minigames_menu_screen(void)
{
    int idx = 0;
    int page = 0;
    int total_pages = (MINIGAME_ENTRY_COUNT + 2) / 3;

    while (idx < MINIGAME_ENTRY_COUNT && page < MINIGAMES_PAGE_MAX) {
        int remaining = MINIGAME_ENTRY_COUNT - idx;
        int on_page = (remaining < 3) ? remaining : 3;
        int s;
        quad_item_t q[4];

        memset(q, 0, sizeof(q));
        for (s = 0; s < 4; s++) q[s].label = "";
        for (s = 0; s < on_page; s++, idx++) {
            q[s].label = t(minigame_entries[idx].name);
            q[s].cb = event_open_minigame;
            q[s].enabled = true;
            q[s].event = LV_EVENT_CLICKED;
            q[s].user_data = (void *)&minigame_entries[idx];
        }
        q[3].label = t(STR_SETTINGS_MORE);
        q[3].cb = event_minigames_more;
        q[3].enabled = true;
        q[3].event = LV_EVENT_CLICKED;
        q[3].user_data = (void *)(intptr_t)((page + 1) % total_pages);
        build_quad_screen(&minigames_pages[page], q);
        page++;
    }
    minigames_page_count = page;
    /* Page 0 doubles as the menu's public screen global - see the
       comment where it is declared. */
    screen_minigames_menu = minigames_pages[0];
}

bool minigames_handle_back(lv_obj_t *screen)
{
    int i;

    /* Back from any page leaves the menu entirely rather than stepping
       back a page (the knob flips pages) - same rule settings uses.
       Page 0 is screen_minigames_menu, which settings_handle_back()
       already knows how to exit, so every page defers to it. */
    for (i = 1; i < minigames_page_count; i++) {
        if (screen == minigames_pages[i]) {
            return settings_handle_back(screen_minigames_menu);
        }
    }
    return false;
}

bool minigames_knob_page(int dir)
{
    int i;

    for (i = 0; i < minigames_page_count; i++) {
        if (lv_scr_act() == minigames_pages[i]) {
            lv_scr_load(minigames_pages[(i + dir + minigames_page_count) %
                                        minigames_page_count]);
            return true;
        }
    }
    return false;
}
