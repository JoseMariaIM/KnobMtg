#include "attack.h"
#include "game.h"
#include "lang.h"

extern void back_to_main(void);

// ---------- screens ----------
lv_obj_t *screen_attack = NULL;

// ---------- state ----------
static int attack_source = -1;
static int attack_target = -1;
static attack_mode_t attack_mode = ATTACK_MODE_DAMAGE;
static int attack_amount = 1;
static bool attack_lifelink = false;

#define ATTACK_AMOUNT_MIN 0
#define ATTACK_AMOUNT_MAX 99

// ---------- widgets ----------
static lv_obj_t *label_attack_title = NULL;
static lv_obj_t *btn_mode[ATTACK_MODE_COUNT];
static lv_obj_t *label_mode[ATTACK_MODE_COUNT];
static lv_obj_t *cb_lifelink = NULL;
static lv_obj_t *label_amount = NULL;

static const string_id_t attack_mode_labels[ATTACK_MODE_COUNT] = {
    STR_ATTACK_MODE_DAMAGE, STR_ATTACK_MODE_HEAL,
    STR_ATTACK_MODE_CMDR, STR_ATTACK_MODE_INFECT,
};

// ---------- refresh ----------
static void refresh_attack_ui(void)
{
    char title_buf[40];
    int i;

    if (attack_source < 0 || attack_target < 0) return;

    if (label_attack_title != NULL) {
        snprintf(title_buf, sizeof(title_buf), t(STR_ATTACK_TITLE_FMT),
                 player_names[attack_source], player_names[attack_target]);
        lv_label_set_text(label_attack_title, title_buf);
    }

    for (i = 0; i < ATTACK_MODE_COUNT; i++) {
        bool active = (attack_mode == i);
        if (btn_mode[i] == NULL) continue;
        lv_obj_set_style_bg_opa(btn_mode[i], active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(btn_mode[i], lv_color_white(), 0);
        lv_obj_set_style_text_color(label_mode[i], active ? lv_color_black() : lv_color_white(), 0);
    }

    /* Lifelink only makes sense while dealing damage - hidden rather than
       just disabled so the layout reads as "not applicable" here. */
    if (cb_lifelink != NULL) {
        if (attack_mode == ATTACK_MODE_DAMAGE) {
            lv_obj_clear_flag(cb_lifelink, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(cb_lifelink, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (label_amount != NULL) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", attack_amount);
        lv_label_set_text(label_amount, buf);
    }
}

void change_attack_amount(int delta)
{
    attack_amount += delta;
    if (attack_amount < ATTACK_AMOUNT_MIN) attack_amount = ATTACK_AMOUNT_MIN;
    if (attack_amount > ATTACK_AMOUNT_MAX) attack_amount = ATTACK_AMOUNT_MAX;
    refresh_attack_ui();
}

// ---------- navigation ----------
void open_attack_screen(int source, int target)
{
    if (source < 0 || source >= MAX_DISPLAY_PLAYERS) return;
    if (target < 0 || target >= MAX_DISPLAY_PLAYERS) return;

    attack_source = source;
    attack_target = target;
    attack_mode = ATTACK_MODE_DAMAGE;
    attack_amount = 1;
    attack_lifelink = false;
    if (cb_lifelink != NULL) lv_obj_clear_state(cb_lifelink, LV_STATE_CHECKED);

    refresh_attack_ui();
    load_screen_if_needed(screen_attack);
}

// ---------- events ----------
static void event_attack_mode(lv_event_t *e)
{
    int mode = (int)(intptr_t)lv_event_get_user_data(e);
    if (mode < 0 || mode >= ATTACK_MODE_COUNT) return;
    attack_mode = (attack_mode_t)mode;
    refresh_attack_ui();
}

static void event_attack_lifelink_toggle(lv_event_t *e)
{
    (void)e;
    if (cb_lifelink == NULL) return;
    attack_lifelink = lv_obj_has_state(cb_lifelink, LV_STATE_CHECKED);
}

static void event_attack_cancel(lv_event_t *e)
{
    (void)e;
    back_to_main();
}

static void event_attack_resolve(lv_event_t *e)
{
    (void)e;

    if (attack_source >= 0 && attack_target >= 0 && attack_amount != 0) {
        switch (attack_mode) {
        case ATTACK_MODE_DAMAGE:
            apply_life_delta(attack_target, -attack_amount);
            if (attack_lifelink) apply_life_delta(attack_source, attack_amount);
            break;
        case ATTACK_MODE_HEAL:
            apply_life_delta(attack_target, attack_amount);
            break;
        case ATTACK_MODE_CMDR:
            apply_attack_cmd_damage(attack_source, attack_target, attack_amount);
            break;
        case ATTACK_MODE_INFECT:
            apply_attack_poison(attack_target, attack_amount);
            break;
        default:
            break;
        }
    }

    back_to_main();
}

// ---------- screen builder ----------
#define ATTACK_TAB_W 62
#define ATTACK_TAB_H 32
#define ATTACK_TAB_GAP 6
#define ATTACK_TAB_Y 78

void build_attack_screen(void)
{
    int i;
    int row_w = ATTACK_MODE_COUNT * ATTACK_TAB_W + (ATTACK_MODE_COUNT - 1) * ATTACK_TAB_GAP;
    int start_x = (360 - row_w) / 2;
    lv_obj_t *btn_cancel, *btn_resolve;

    screen_attack = lv_obj_create(NULL);
    lv_obj_set_size(screen_attack, 360, 360);
    lv_obj_set_style_bg_color(screen_attack, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_attack, 0, 0);
    lv_obj_set_scrollbar_mode(screen_attack, LV_SCROLLBAR_MODE_OFF);

    label_attack_title = lv_label_create(screen_attack);
    lv_label_set_text(label_attack_title, "P1 > P2");
    lv_obj_set_style_text_color(label_attack_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_attack_title, &lv_font_es_22, 0);
    lv_obj_set_style_text_align(label_attack_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label_attack_title, 220);
    lv_label_set_long_mode(label_attack_title, LV_LABEL_LONG_WRAP);
    lv_obj_align(label_attack_title, LV_ALIGN_TOP_MID, 0, 40);

    for (i = 0; i < ATTACK_MODE_COUNT; i++) {
        lv_obj_t *btn = lv_btn_create(screen_attack);
        lv_obj_set_size(btn, ATTACK_TAB_W, ATTACK_TAB_H);
        lv_obj_set_pos(btn, start_x + i * (ATTACK_TAB_W + ATTACK_TAB_GAP), ATTACK_TAB_Y);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_set_style_border_color(btn, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, event_attack_mode, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        btn_mode[i] = btn;

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, t(attack_mode_labels[i]));
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_set_style_text_font(label, &lv_font_es_14, 0);
        lv_obj_center(label);
        label_mode[i] = label;
    }

    cb_lifelink = lv_checkbox_create(screen_attack);
    lv_checkbox_set_text(cb_lifelink, t(STR_ATTACK_LIFELINK));
    lv_obj_set_style_text_color(cb_lifelink, lv_color_white(), 0);
    lv_obj_set_style_text_font(cb_lifelink, &lv_font_es_14, 0);
    lv_obj_set_style_pad_column(cb_lifelink, 8, 0);
    lv_obj_align(cb_lifelink, LV_ALIGN_TOP_MID, 0, ATTACK_TAB_Y + ATTACK_TAB_H + 14);
    lv_obj_add_event_cb(cb_lifelink, event_attack_lifelink_toggle, LV_EVENT_VALUE_CHANGED, NULL);

    label_amount = lv_label_create(screen_attack);
    lv_label_set_text(label_amount, "1");
    lv_obj_set_style_text_color(label_amount, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_amount, &lv_font_es_32, 0);
    lv_obj_align(label_amount, LV_ALIGN_CENTER, 0, 15);

    lv_obj_t *hint = lv_label_create(screen_attack);
    lv_label_set_text(hint, t(STR_TURN_KNOB_THEN_APPLY));
    lv_obj_set_style_text_color(hint, lv_color_hex(0x7A7A7A), 0);
    lv_obj_set_style_text_font(hint, &lv_font_es_14, 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 50);

    /* Corners kept inside ROUND_SAFE_RADIUS (round_safe.h): at this size/
       offset the farthest corner sits at ~174px from center, under 180. */
    btn_cancel = make_button(screen_attack, t(STR_CANCEL), 100, 44, event_attack_cancel);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_MID, -58, -44);

    btn_resolve = make_button(screen_attack, t(STR_RESOLVE), 100, 44, event_attack_resolve);
    lv_obj_align(btn_resolve, LV_ALIGN_BOTTOM_MID, 58, -44);
}
