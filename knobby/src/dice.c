#include "dice.h"
#include "game.h"
#include "esp_random.h"
#include "lang.h"
#include "settings.h"

// ---------- modes ----------
typedef enum {
    DICE_MODE_D6 = 0,
    DICE_MODE_D12,
    DICE_MODE_D20,
    DICE_MODE_COIN,
    DICE_MODE_COUNT,
} dice_mode_t;

static dice_mode_t dice_mode = DICE_MODE_D20; /* matches the tool's original d20-only behavior */
static bool coin_heads = false;

// ---------- state ----------
lv_obj_t *screen_dice_menu = NULL;
lv_obj_t *screen_dice = NULL;
static lv_obj_t *coin_circle = NULL;
static lv_obj_t *label_dice_result = NULL;
static lv_obj_t *label_dice_hint = NULL;

static int dice_mode_sides(dice_mode_t m)
{
    switch (m) {
        case DICE_MODE_D6:  return 6;
        case DICE_MODE_D12: return 12;
        default:            return 20;
    }
}

static void roll_current_mode(void)
{
    if (dice_mode == DICE_MODE_COIN) {
        coin_heads = (esp_random() % 2U) == 0;
    } else {
        dice_result = (int)(esp_random() % (uint32_t)dice_mode_sides(dice_mode)) + 1;
    }
}

// ---------- refresh ----------
void refresh_dice_ui(void)
{
    char buf[8];

    if (coin_circle != NULL) {
        if (dice_mode == DICE_MODE_COIN) {
            lv_obj_clear_flag(coin_circle, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(coin_circle, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (label_dice_result == NULL) return;

    if (dice_mode == DICE_MODE_COIN) {
        lv_obj_set_style_text_font(label_dice_result, &lv_font_es_32, 0);
        lv_obj_align(label_dice_result, LV_ALIGN_CENTER, 0, 60);
        lv_label_set_text(label_dice_result, t(coin_heads ? STR_COIN_HEADS : STR_COIN_TAILS));
        if (label_dice_hint != NULL) {
            lv_label_set_text(label_dice_hint, t(STR_COIN_HOLD_TO_REFLIP));
            lv_obj_align(label_dice_hint, LV_ALIGN_CENTER, 0, 100);
        }
    } else {
        lv_obj_set_style_text_font(label_dice_result, &lv_font_montserrat_bold_116, 0);
        lv_obj_align(label_dice_result, LV_ALIGN_CENTER, 0, -10);
        if (dice_result <= 0) {
            lv_label_set_text(label_dice_result, "--");
        } else {
            snprintf(buf, sizeof(buf), "%d", dice_result);
            lv_label_set_text(label_dice_result, buf);
        }
        if (label_dice_hint != NULL) {
            lv_label_set_text(label_dice_hint, t(STR_DICE_HOLD_TO_REROLL));
            lv_obj_align(label_dice_hint, LV_ALIGN_CENTER, 0, 42);
        }
    }
}

// ---------- coin spin animation ----------
static void anim_coin_width_cb(void *obj, int32_t v)
{
    lv_obj_t *o = (lv_obj_t *)obj;
    lv_obj_set_width(o, v);
    lv_obj_align(o, LV_ALIGN_CENTER, 0, -20);
}

static void coin_spin_ready_cb(lv_anim_t *a)
{
    (void)a;
    if (coin_circle != NULL) {
        lv_obj_set_width(coin_circle, 120);
        lv_obj_align(coin_circle, LV_ALIGN_CENTER, 0, -20);
    }
    refresh_dice_ui(); /* reveal the result now that the coin has "landed" */
}

static void play_coin_spin(void)
{
    lv_anim_t anim;
    if (coin_circle == NULL) return;

    if (label_dice_result != NULL) lv_label_set_text(label_dice_result, "");

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

// ---------- open ----------
void open_dice_screen(void)
{
    lv_anim_t anim;

    load_screen_if_needed(screen_dice);

    if (dice_mode == DICE_MODE_COIN) {
        if (coin_circle != NULL) lv_obj_clear_flag(coin_circle, LV_OBJ_FLAG_HIDDEN);
        play_coin_spin();
        return;
    }

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

static void event_dice_pick_d6(lv_event_t *e)   { (void)e; event_dice_pick(DICE_MODE_D6); }
static void event_dice_pick_d12(lv_event_t *e)  { (void)e; event_dice_pick(DICE_MODE_D12); }
static void event_dice_pick_d20(lv_event_t *e)  { (void)e; event_dice_pick(DICE_MODE_D20); }
static void event_dice_pick_coin(lv_event_t *e) { (void)e; event_dice_pick(DICE_MODE_COIN); }

void open_dice_menu_screen(void)
{
    load_screen_if_needed(screen_dice_menu);
}

void event_tool_dice(lv_event_t *e)
{
    (void)e;
    open_dice_menu_screen();
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

    coin_circle = lv_obj_create(screen_dice);
    lv_obj_remove_style_all(coin_circle);
    lv_obj_set_size(coin_circle, 120, 120);
    lv_obj_set_style_radius(coin_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(coin_circle, lv_color_hex(0xFFC93C), 0);
    lv_obj_set_style_bg_opa(coin_circle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(coin_circle, 4, 0);
    lv_obj_set_style_border_color(coin_circle, lv_color_hex(0xC9971E), 0);
    lv_obj_clear_flag(coin_circle, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(coin_circle, LV_ALIGN_CENTER, 0, -20);
    lv_obj_add_flag(coin_circle, LV_OBJ_FLAG_HIDDEN); /* only shown in coin mode */

    label_dice_result = lv_label_create(screen_dice);
    lv_label_set_text(label_dice_result, "--");
    lv_obj_set_style_text_color(label_dice_result, lv_color_hex(0x06D6A0), 0);
    lv_obj_set_style_text_font(label_dice_result, &lv_font_montserrat_bold_116, 0);
    lv_obj_align(label_dice_result, LV_ALIGN_CENTER, 0, -10);

    label_dice_hint = lv_label_create(screen_dice);
    lv_label_set_text(label_dice_hint, t(STR_DICE_HOLD_TO_REROLL));
    lv_obj_set_style_text_color(label_dice_hint, lv_color_hex(0x8A8A8A), 0);
    lv_obj_set_style_text_font(label_dice_hint, &lv_font_es_14, 0);
    lv_obj_align(label_dice_hint, LV_ALIGN_CENTER, 0, 42);
}

void build_dice_menu_screen(void)
{
    quad_item_t items[4] = {
        {t(STR_DICE_MODE_D6), event_dice_pick_d6, true, LV_EVENT_CLICKED},
        {t(STR_DICE_MODE_D12), event_dice_pick_d12, true, LV_EVENT_CLICKED},
        {t(STR_DICE_MODE_D20), event_dice_pick_d20, true, LV_EVENT_CLICKED},
        {t(STR_DICE_MODE_COIN), event_dice_pick_coin, true, LV_EVENT_CLICKED},
    };
    build_quad_screen(&screen_dice_menu, items);
}
