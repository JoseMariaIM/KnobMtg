#include "dice.h"
#include "game.h"
#include "esp_random.h"
#include "lang.h"
#include "settings.h"

// ---------- dice modes ----------
typedef enum {
    DICE_MODE_D6 = 0,
    DICE_MODE_D12,
    DICE_MODE_D20,
} dice_mode_t;

#define DICE_QTY_MIN     1
#define DICE_QTY_MAX     10
#define DICE_RESULTS_MAX DICE_QTY_MAX

static dice_mode_t dice_mode = DICE_MODE_D20; /* matches the tool's original d20-only behavior */
static int dice_quantity = 1;
static int dice_results[DICE_RESULTS_MAX];
static int dice_result_count = 0;

#define PERFECT_PHRASE_COUNT  5
#define DISASTER_PHRASE_COUNT 5
static const string_id_t perfect_phrases[PERFECT_PHRASE_COUNT] = {
    STR_DICE_PERFECT_1, STR_DICE_PERFECT_2, STR_DICE_PERFECT_3,
    STR_DICE_PERFECT_4, STR_DICE_PERFECT_5,
};
static const string_id_t disaster_phrases[DISASTER_PHRASE_COUNT] = {
    STR_DICE_DISASTER_1, STR_DICE_DISASTER_2, STR_DICE_DISASTER_3,
    STR_DICE_DISASTER_4, STR_DICE_DISASTER_5,
};

// ---------- state: dice screens ----------
lv_obj_t *screen_dice_menu = NULL;
lv_obj_t *screen_dice = NULL;
static lv_obj_t *label_dice_result = NULL;
static lv_obj_t *label_dice_phrase = NULL;
static lv_obj_t *label_dice_hint = NULL;
static lv_obj_t *label_dice_quantity = NULL;

// ---------- state: coin screen ----------
lv_obj_t *screen_coin = NULL;
static lv_obj_t *coin_circle = NULL;
static lv_obj_t *label_coin_result = NULL;
static bool coin_heads = false;

static int dice_mode_sides(dice_mode_t m)
{
    switch (m) {
        case DICE_MODE_D6:  return 6;
        case DICE_MODE_D12: return 12;
        default:             return 20;
    }
}

// ---------- quantity (dice_menu screen, knob-driven) ----------
void change_dice_quantity(int delta)
{
    char buf[4];

    dice_quantity += delta;
    if (dice_quantity < DICE_QTY_MIN) dice_quantity = DICE_QTY_MIN;
    if (dice_quantity > DICE_QTY_MAX) dice_quantity = DICE_QTY_MAX;

    if (label_dice_quantity == NULL) return;
    snprintf(buf, sizeof(buf), "%d", dice_quantity);
    lv_label_set_text(label_dice_quantity, buf);
}

static void roll_current_mode(void)
{
    int i, sides = dice_mode_sides(dice_mode);

    dice_result_count = dice_quantity;
    for (i = 0; i < dice_result_count; i++) {
        dice_results[i] = (int)(esp_random() % (uint32_t)sides) + 1;
    }
    dice_result = dice_results[0]; /* kept for game.c's reset_all_values() */
}

// ---------- refresh ----------
void refresh_dice_ui(void)
{
    char buf[96];

    if (label_dice_result == NULL) return;

    if (dice_result_count <= 0) {
        lv_obj_set_style_text_font(label_dice_result, &lv_font_montserrat_bold_116, 0);
        lv_label_set_text(label_dice_result, "--");
        lv_obj_add_flag(label_dice_phrase, LV_OBJ_FLAG_HIDDEN);
    } else if (dice_result_count == 1) {
        int sides = dice_mode_sides(dice_mode);
        int result = dice_results[0];

        lv_obj_set_style_text_font(label_dice_result, &lv_font_montserrat_bold_116, 0);
        snprintf(buf, sizeof(buf), "%d", result);
        lv_label_set_text(label_dice_result, buf);

        if (result == sides) {
            lv_label_set_text(label_dice_phrase, t(perfect_phrases[esp_random() % PERFECT_PHRASE_COUNT]));
            lv_obj_set_style_text_color(label_dice_phrase, lv_color_hex(0x06D6A0), 0);
            lv_obj_clear_flag(label_dice_phrase, LV_OBJ_FLAG_HIDDEN);
        } else if (result == 1) {
            lv_label_set_text(label_dice_phrase, t(disaster_phrases[esp_random() % DISASTER_PHRASE_COUNT]));
            lv_obj_set_style_text_color(label_dice_phrase, lv_color_hex(0xE63946), 0);
            lv_obj_clear_flag(label_dice_phrase, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(label_dice_phrase, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        /* Smaller font as the list grows so up to DICE_QTY_MAX results
           still fit the wrapped 260px-wide label without spilling past
           the bezel. */
        const lv_font_t *f = (dice_result_count <= 4) ? &lv_font_es_32
                            : (dice_result_count <= 7) ? &lv_font_es_22
                                                        : &lv_font_es_16;
        size_t pos = 0;
        int i, total = 0;

        for (i = 0; i < dice_result_count; i++) {
            total += dice_results[i];
            pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos,
                                     "%s%d", (i == 0) ? "" : "  ", dice_results[i]);
            if (pos >= sizeof(buf)) break;
        }
        lv_obj_set_style_text_font(label_dice_result, f, 0);
        lv_label_set_text(label_dice_result, buf);

        snprintf(buf, sizeof(buf), t(STR_DICE_TOTAL_FMT), total);
        lv_label_set_text(label_dice_phrase, buf);
        lv_obj_set_style_text_color(label_dice_phrase, lv_color_hex(0x06D6A0), 0);
        lv_obj_clear_flag(label_dice_phrase, LV_OBJ_FLAG_HIDDEN);
    }

    if (label_dice_hint != NULL) {
        lv_label_set_text(label_dice_hint, t(STR_DICE_HOLD_TO_REROLL));
    }
}

// ---------- open ----------
void open_dice_screen(void)
{
    lv_anim_t anim;

    load_screen_if_needed(screen_dice);
    refresh_dice_ui();

    if (label_dice_result != NULL) {
        lv_obj_set_y(label_dice_result, -10);
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, label_dice_result);
        lv_anim_set_values(&anim, -10, -24);
        lv_anim_set_time(&anim, 120);
        lv_anim_set_playback_time(&anim, 140);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_obj_set_y);
        lv_anim_start(&anim);
    }
}

// ---------- events ----------
static void event_dice_tap(lv_event_t *e)
{
    (void)e;
    roll_current_mode();
    open_dice_screen();
}

static void event_dice_pick(dice_mode_t mode)
{
    dice_mode = mode;
    roll_current_mode();
    open_dice_screen();
}

static void event_dice_pick_d6(lv_event_t *e)  { (void)e; event_dice_pick(DICE_MODE_D6); }
static void event_dice_pick_d12(lv_event_t *e) { (void)e; event_dice_pick(DICE_MODE_D12); }
static void event_dice_pick_d20(lv_event_t *e) { (void)e; event_dice_pick(DICE_MODE_D20); }

void open_dice_menu_screen(void)
{
    load_screen_if_needed(screen_dice_menu);
}

void event_tool_dice(lv_event_t *e)
{
    (void)e;
    open_dice_menu_screen();
}

// ---------- coin ----------
static void anim_coin_width_cb(void *obj, int32_t v)
{
    lv_obj_t *o = (lv_obj_t *)obj;
    lv_obj_set_width(o, v);
    lv_obj_align(o, LV_ALIGN_CENTER, 0, -20);
}

static void refresh_coin_ui(void)
{
    if (label_coin_result == NULL) return;
    lv_label_set_text(label_coin_result, t(coin_heads ? STR_COIN_HEADS : STR_COIN_TAILS));
}

static void coin_spin_ready_cb(lv_anim_t *a)
{
    (void)a;
    if (coin_circle != NULL) {
        lv_obj_set_width(coin_circle, 120);
        lv_obj_align(coin_circle, LV_ALIGN_CENTER, 0, -20);
    }
    refresh_coin_ui(); /* reveal the result now that the coin has "landed" */
}

static void play_coin_spin(void)
{
    lv_anim_t anim;
    if (coin_circle == NULL) return;

    if (label_coin_result != NULL) lv_label_set_text(label_coin_result, "");

    /* Squish the coin's width down to a sliver and back a few times to
       fake it spinning edge-on, then reveal Heads/Tails once it settles. */
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, coin_circle);
    lv_anim_set_values(&anim, 120, 16);
    lv_anim_set_time(&anim, 90);
    lv_anim_set_playback_time(&anim, 90);
    lv_anim_set_repeat_count(&anim, 6);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&anim, anim_coin_width_cb);
    lv_anim_set_ready_cb(&anim, coin_spin_ready_cb);
    lv_anim_start(&anim);
}

void open_coin_screen(void)
{
    load_screen_if_needed(screen_coin);
    coin_heads = (esp_random() % 2U) == 0;
    play_coin_spin();
}

static void event_coin_tap(lv_event_t *e)
{
    (void)e;
    open_coin_screen();
}

void event_tool_coin(lv_event_t *e)
{
    (void)e;
    open_coin_screen();
}

// ---------- build ----------
void build_dice_screen(void)
{
    screen_dice = lv_obj_create(NULL);
    lv_obj_set_size(screen_dice, 360, 360);
    lv_obj_set_style_bg_color(screen_dice, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_dice, 0, 0);
    lv_obj_set_scrollbar_mode(screen_dice, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(screen_dice, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen_dice, event_dice_tap, LV_EVENT_LONG_PRESSED, NULL);

    label_dice_result = lv_label_create(screen_dice);
    lv_label_set_text(label_dice_result, "--");
    lv_obj_set_style_text_color(label_dice_result, lv_color_hex(0x06D6A0), 0);
    lv_obj_set_style_text_font(label_dice_result, &lv_font_montserrat_bold_116, 0);
    lv_obj_set_style_text_align(label_dice_result, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label_dice_result, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_dice_result, 260);
    lv_obj_align(label_dice_result, LV_ALIGN_CENTER, 0, -10);

    /* Doubles as the perfect/disaster flavor line (single die) and the
       running total (multiple dice) - only one of those is ever shown
       for a given roll, so one label covers both. */
    label_dice_phrase = lv_label_create(screen_dice);
    lv_label_set_text(label_dice_phrase, "");
    lv_obj_set_style_text_font(label_dice_phrase, &lv_font_es_16, 0);
    lv_obj_set_style_text_align(label_dice_phrase, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label_dice_phrase, 260);
    lv_obj_align(label_dice_phrase, LV_ALIGN_CENTER, 0, 50);
    lv_obj_add_flag(label_dice_phrase, LV_OBJ_FLAG_HIDDEN);

    label_dice_hint = lv_label_create(screen_dice);
    lv_label_set_text(label_dice_hint, t(STR_DICE_HOLD_TO_REROLL));
    lv_obj_set_style_text_color(label_dice_hint, lv_color_hex(0x8A8A8A), 0);
    lv_obj_set_style_text_font(label_dice_hint, &lv_font_es_14, 0);
    lv_obj_align(label_dice_hint, LV_ALIGN_CENTER, 0, 96);
}

void build_coin_screen(void)
{
    screen_coin = lv_obj_create(NULL);
    lv_obj_set_size(screen_coin, 360, 360);
    lv_obj_set_style_bg_color(screen_coin, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_coin, 0, 0);
    lv_obj_set_scrollbar_mode(screen_coin, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(screen_coin, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen_coin, event_coin_tap, LV_EVENT_LONG_PRESSED, NULL);

    coin_circle = lv_obj_create(screen_coin);
    lv_obj_remove_style_all(coin_circle);
    lv_obj_set_size(coin_circle, 120, 120);
    lv_obj_set_style_radius(coin_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(coin_circle, lv_color_hex(0xFFC93C), 0);
    lv_obj_set_style_bg_opa(coin_circle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(coin_circle, 4, 0);
    lv_obj_set_style_border_color(coin_circle, lv_color_hex(0xC9971E), 0);
    lv_obj_clear_flag(coin_circle, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(coin_circle, LV_ALIGN_CENTER, 0, -20);

    label_coin_result = lv_label_create(screen_coin);
    lv_label_set_text(label_coin_result, "");
    lv_obj_set_style_text_color(label_coin_result, lv_color_hex(0x06D6A0), 0);
    lv_obj_set_style_text_font(label_coin_result, &lv_font_es_32, 0);
    lv_obj_align(label_coin_result, LV_ALIGN_CENTER, 0, 60);

    lv_obj_t *hint = lv_label_create(screen_coin);
    lv_label_set_text(hint, t(STR_COIN_HOLD_TO_REFLIP));
    lv_obj_set_style_text_color(hint, lv_color_hex(0x8A8A8A), 0);
    lv_obj_set_style_text_font(hint, &lv_font_es_14, 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 100);
}

void build_dice_menu_screen(void)
{
    static const lv_coord_t qx[4] = {0,   182, 0,   182};
    static const lv_coord_t qy[4] = {0,   0,   182, 182};
    static const lv_coord_t lx[4] = {10, -10, 10, -10};
    static const lv_coord_t ly[4] = {15,  15, -15, -15};
    static lv_event_cb_t const dice_cbs[3] = {
        event_dice_pick_d6, event_dice_pick_d12, event_dice_pick_d20,
    };
    static const string_id_t dice_labels[3] = {STR_DICE_MODE_D6, STR_DICE_MODE_D12, STR_DICE_MODE_D20};
    int i;

    screen_dice_menu = lv_obj_create(NULL);
    lv_obj_set_size(screen_dice_menu, 360, 360);
    lv_obj_set_style_bg_color(screen_dice_menu, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_dice_menu, 0, 0);
    lv_obj_set_scrollbar_mode(screen_dice_menu, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(screen_dice_menu, 0, 0);

    for (i = 0; i < 3; i++) {
        lv_obj_t *btn = lv_btn_create(screen_dice_menu);
        lv_obj_t *lbl;

        lv_obj_remove_style_all(btn);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_set_size(btn, 178, 178);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_pos(btn, qx[i], qy[i]);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1A1A2E), 0);
        lv_obj_add_event_cb(btn, dice_cbs[i], LV_EVENT_CLICKED, NULL);

        lbl = lv_label_create(btn);
        lv_label_set_text(lbl, t(dice_labels[i]));
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_es_16, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, lx[i], ly[i]);
    }

    /* Fourth quadrant: a live quantity readout rather than a button -
       turning the knob while this screen is open adjusts it directly
       (see change_dice_quantity() / handle_knob_event()), then tapping
       one of the three dice above rolls that many. */
    {
        lv_obj_t *box = lv_obj_create(screen_dice_menu);
        lv_obj_t *title;

        lv_obj_remove_style_all(box);
        lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(box, 178, 178);
        lv_obj_set_style_radius(box, 0, 0);
        lv_obj_set_pos(box, qx[3], qy[3]);
        lv_obj_set_style_bg_color(box, lv_color_hex(0x1A1A2E), 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);

        title = lv_label_create(box);
        lv_label_set_text(title, t(STR_DICE_QUANTITY_TITLE));
        lv_obj_set_style_text_color(title, lv_color_hex(0x8A8A8A), 0);
        lv_obj_set_style_text_font(title, &lv_font_es_14, 0);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(title, LV_ALIGN_CENTER, lx[3], ly[3] - 14);

        label_dice_quantity = lv_label_create(box);
        lv_obj_set_style_text_color(label_dice_quantity, lv_color_white(), 0);
        lv_obj_set_style_text_font(label_dice_quantity, &lv_font_es_32, 0);
        lv_obj_align(label_dice_quantity, LV_ALIGN_CENTER, lx[3], ly[3] + 18);
    }

    change_dice_quantity(0); /* paint the initial value */
}
