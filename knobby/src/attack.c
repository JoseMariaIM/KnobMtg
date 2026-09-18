#include "attack.h"
#include "game.h"
#include "lang.h"

extern void back_to_main(void);

/* The resolve screen for an attack drag.
 *
 * Rebuilt to speak the same visual language as the rest of the device.
 * It used to be hairline white outlines, a stock LVGL checkbox and two
 * stock buttons - and those buttons came out in LVGL's default theme
 * blue, a colour that appears nowhere else on this device, which is
 * what made the screen read as borrowed from somewhere else.
 *
 * Now it is built from the two things the rest of the UI is built
 * from: the dark tile (the same 0x1A1A2E the quad menus use) and the
 * players' own colours. The header shows both players as chips in
 * their panel colours, so the screen is visibly continuous with the
 * drag that opened it - which already draws the beam in the source's
 * colour and the reticle in the target's.
 *
 * One accent colour per mode runs through the whole screen: the
 * selected tab, the amount, and the Resolve button all take it. That
 * means the button you are about to press is the colour of the thing
 * it is about to do. */

// ---------- screens ----------
lv_obj_t *screen_attack = NULL;

// ---------- state ----------
static int attack_source = -1;
static int attack_target = -1;
static int attack_source_color = 0;
static int attack_target_color = 0;
static attack_mode_t attack_mode = ATTACK_MODE_DAMAGE;
static int attack_amount = 1;

#define ATTACK_AMOUNT_MIN 0
#define ATTACK_AMOUNT_MAX 99

// ---------- palette ----------
/* Tile colours shared with the quad menus, so this screen sits in the
   same family as the menu it is reached from. */
#define ATTACK_TILE_BG     0x1A1A2E
#define ATTACK_TILE_TEXT   0x9AA0B4
#define ATTACK_CANCEL_EDGE 0x39405A
#define ATTACK_CANCEL_TEXT 0xC9CEDD
#define ATTACK_HINT_TEXT   0x6E7486

/* One per mode, in enum order. Red for damage, amber for lifelink
   (distinct from both the red it is a variant of and the green infect
   uses), purple for commander, toxic green for infect. */
static const uint32_t attack_mode_accent[ATTACK_MODE_COUNT] = {
    0xEF5350, 0xFFB300, 0xAB47BC, 0x66BB6A,
};

static const string_id_t attack_mode_labels[ATTACK_MODE_COUNT] = {
    STR_ATTACK_MODE_DAMAGE, STR_ATTACK_LIFELINK,
    STR_ATTACK_MODE_CMDR, STR_ATTACK_MODE_INFECT,
};

// ---------- widgets ----------
static lv_obj_t *chip_source = NULL;
static lv_obj_t *chip_target = NULL;
static lv_obj_t *label_source = NULL;
static lv_obj_t *label_target = NULL;
static lv_obj_t *btn_mode[ATTACK_MODE_COUNT];
static lv_obj_t *label_mode[ATTACK_MODE_COUNT];
static lv_obj_t *label_amount = NULL;
static lv_obj_t *btn_attack_resolve = NULL;
static lv_obj_t *label_attack_resolve = NULL;

// ---------- refresh ----------
static void refresh_attack_ui(void)
{
    lv_color_t accent;
    int i;

    if (attack_source < 0 || attack_target < 0) return;

    accent = lv_color_hex(attack_mode_accent[attack_mode]);

    /* Header chips carry each player's own colour, the same one their
       panel and the drag beam used. */
    if (chip_source != NULL) {
        lv_obj_set_style_bg_color(chip_source,
            get_effective_player_color(attack_source, attack_source_color, LIFE_VIB_MID), 0);
        lv_label_set_text(label_source, player_names[attack_source]);
    }
    if (chip_target != NULL) {
        lv_obj_set_style_bg_color(chip_target,
            get_effective_player_color(attack_target, attack_target_color, LIFE_VIB_MID), 0);
        lv_label_set_text(label_target, player_names[attack_target]);
    }

    for (i = 0; i < ATTACK_MODE_COUNT; i++) {
        bool active = (attack_mode == i);
        if (btn_mode[i] == NULL) continue;
        lv_obj_set_style_bg_color(btn_mode[i],
            active ? lv_color_hex(attack_mode_accent[i]) : lv_color_hex(ATTACK_TILE_BG), 0);
        lv_obj_set_style_text_color(label_mode[i],
            active ? lv_color_black() : lv_color_hex(ATTACK_TILE_TEXT), 0);
    }

    if (label_amount != NULL) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", attack_amount);
        lv_label_set_text(label_amount, buf);
        lv_obj_set_style_text_color(label_amount, accent, 0);
    }

    if (btn_attack_resolve != NULL) {
        lv_obj_set_style_bg_color(btn_attack_resolve, accent, 0);
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
void open_attack_screen(int source, int source_color,
                        int target, int target_color)
{
    if (source < 0 || source >= MAX_DISPLAY_PLAYERS) return;
    if (target < 0 || target >= MAX_DISPLAY_PLAYERS) return;

    attack_source = source;
    attack_target = target;
    attack_source_color = source_color;
    attack_target_color = target_color;
    attack_mode = ATTACK_MODE_DAMAGE;
    attack_amount = 1;

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
            break;
        case ATTACK_MODE_LIFELINK:
            /* Damage, and the attacker gains as much - which is exactly
               what the old Damage + lifelink tick box did. */
            apply_life_delta(attack_target, -attack_amount);
            apply_life_delta(attack_source, attack_amount);
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
/* Everything below is sized against the round glass: the widest corner
   of each row has to stay inside ROUND_SAFE_RADIUS (see round_safe.h).
   The tab row is the tightest - four 70px tabs sit 149px out at their
   top corners, against a 171px allowance at that height. */
/* Chip row: 100 wide at y=46 puts the outer top corners 175px from
   centre. The first attempt (104 wide at y=40) put them at 183 and the
   bezel clipped them. */
#define ATTACK_CHIP_W   100
#define ATTACK_CHIP_H    32
#define ATTACK_CHIP_Y    46
#define ATTACK_ARROW_GAP  26

#define ATTACK_TAB_W     70
#define ATTACK_TAB_H     38
#define ATTACK_TAB_GAP    6
#define ATTACK_TAB_Y     96

#define ATTACK_ACT_W    104
#define ATTACK_ACT_H     46

static lv_obj_t *make_chip(lv_obj_t *parent, int x, lv_obj_t **out_label)
{
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_t *lbl;

    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, ATTACK_CHIP_W, ATTACK_CHIP_H);
    lv_obj_set_pos(chip, x, ATTACK_CHIP_Y);
    lv_obj_set_style_radius(chip, 10, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lbl = lv_label_create(chip);
    /* Names are user-editable and can be long; ellipsise rather than
       let one overflow its chip into the other's. */
    lv_obj_set_width(lbl, ATTACK_CHIP_W - 12);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(lbl, lv_color_black(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_es_16, 0);
    lv_obj_center(lbl);

    *out_label = lbl;
    return chip;
}

void build_attack_screen(void)
{
    int i;
    int row_w = ATTACK_MODE_COUNT * ATTACK_TAB_W + (ATTACK_MODE_COUNT - 1) * ATTACK_TAB_GAP;
    int start_x = (360 - row_w) / 2;
    int header_w = ATTACK_CHIP_W * 2 + ATTACK_ARROW_GAP;
    int header_x = (360 - header_w) / 2;
    lv_obj_t *arrow, *hint, *btn_cancel, *lbl;

    screen_attack = lv_obj_create(NULL);
    lv_obj_set_size(screen_attack, 360, 360);
    lv_obj_set_style_bg_color(screen_attack, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_attack, 0, 0);
    lv_obj_set_scrollbar_mode(screen_attack, LV_SCROLLBAR_MODE_OFF);

    chip_source = make_chip(screen_attack, header_x, &label_source);
    chip_target = make_chip(screen_attack,
                            header_x + ATTACK_CHIP_W + ATTACK_ARROW_GAP, &label_target);

    arrow = lv_label_create(screen_attack);
    lv_label_set_text(arrow, ">");
    lv_obj_set_style_text_color(arrow, lv_color_hex(ATTACK_TILE_TEXT), 0);
    lv_obj_set_style_text_font(arrow, &lv_font_es_22, 0);
    lv_obj_align(arrow, LV_ALIGN_TOP_MID, 0, ATTACK_CHIP_Y + 4);

    for (i = 0; i < ATTACK_MODE_COUNT; i++) {
        lv_obj_t *btn = lv_btn_create(screen_attack);
        lv_obj_set_size(btn, ATTACK_TAB_W, ATTACK_TAB_H);
        lv_obj_set_pos(btn, start_x + i * (ATTACK_TAB_W + ATTACK_TAB_GAP), ATTACK_TAB_Y);
        lv_obj_set_style_radius(btn, 9, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, event_attack_mode, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        btn_mode[i] = btn;

        lbl = lv_label_create(btn);
        lv_label_set_text(lbl, t(attack_mode_labels[i]));
        lv_obj_set_style_text_font(lbl, &lv_font_es_14, 0);
        lv_obj_center(lbl);
        label_mode[i] = lbl;
    }

    /* The amount gets the same weight the life counter gives a life
       total - it is the same kind of number, and it is the one thing on
       this screen the knob is editing. */
    label_amount = lv_label_create(screen_attack);
    lv_label_set_text(label_amount, "1");
    lv_obj_set_style_text_font(label_amount, &lv_font_montserrat_bold_56, 0);
    lv_obj_align(label_amount, LV_ALIGN_CENTER, 0, 8);

    hint = lv_label_create(screen_attack);
    lv_label_set_text(hint, t(STR_TURN_KNOB_THEN_APPLY));
    lv_obj_set_style_text_color(hint, lv_color_hex(ATTACK_HINT_TEXT), 0);
    lv_obj_set_style_text_font(hint, &lv_font_es_14, 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 52);

    btn_cancel = lv_btn_create(screen_attack);
    lv_obj_set_size(btn_cancel, ATTACK_ACT_W, ATTACK_ACT_H);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_MID, -57, -46);
    lv_obj_set_style_radius(btn_cancel, 10, 0);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(ATTACK_TILE_BG), 0);
    lv_obj_set_style_bg_opa(btn_cancel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn_cancel, 1, 0);
    lv_obj_set_style_border_color(btn_cancel, lv_color_hex(ATTACK_CANCEL_EDGE), 0);
    lv_obj_set_style_shadow_width(btn_cancel, 0, 0);
    lv_obj_add_event_cb(btn_cancel, event_attack_cancel, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(btn_cancel);
    lv_label_set_text(lbl, t(STR_CANCEL));
    lv_obj_set_style_text_color(lbl, lv_color_hex(ATTACK_CANCEL_TEXT), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_es_16, 0);
    lv_obj_center(lbl);

    btn_attack_resolve = lv_btn_create(screen_attack);
    lv_obj_set_size(btn_attack_resolve, ATTACK_ACT_W, ATTACK_ACT_H);
    lv_obj_align(btn_attack_resolve, LV_ALIGN_BOTTOM_MID, 57, -46);
    lv_obj_set_style_radius(btn_attack_resolve, 10, 0);
    lv_obj_set_style_bg_opa(btn_attack_resolve, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn_attack_resolve, 0, 0);
    lv_obj_set_style_shadow_width(btn_attack_resolve, 0, 0);
    lv_obj_add_event_cb(btn_attack_resolve, event_attack_resolve, LV_EVENT_CLICKED, NULL);
    label_attack_resolve = lv_label_create(btn_attack_resolve);
    lv_label_set_text(label_attack_resolve, t(STR_RESOLVE));
    lv_obj_set_style_text_color(label_attack_resolve, lv_color_black(), 0);
    lv_obj_set_style_text_font(label_attack_resolve, &lv_font_es_16, 0);
    lv_obj_center(label_attack_resolve);

    refresh_attack_ui();
}
