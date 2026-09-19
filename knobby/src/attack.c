#include "attack.h"
#include "game.h"
#include "lang.h"
#include <math.h>

extern void back_to_main(void);

/* The resolve screen for an attack drag.
 *
 * Built around the circle rather than down it. The first two versions
 * were a stack of rectangular rows - a chip row, a tab row, a number,
 * a button row - which is a form that happens to be displayed on a
 * round panel. Everything else on this device is radial: the life
 * counter's wedges, Pong and Breakout's rims, the dice. This screen
 * now is too.
 *
 * Four sectors of the disc are the four modes, and the hub in the
 * middle is the amount and the Resolve button in one. That is five
 * touch targets, all of them enormous, instead of ten small ones -
 * and it drops the mode labels' font from 14 to 22 because a sector
 * has room a 70px tab never did.
 *
 * The labels sit on the DIAGONALS, not at the cardinal points. At the
 * right-hand cardinal point a horizontal label only has the band's
 * radial width to live in (70px, which "Infectar" overflows); on the
 * diagonal the same band offers about 125px because the label cuts
 * across it. That is the whole reason the modes are arranged as
 * corners rather than as up/down/left/right.
 *
 * Cancel is the swipe-back every other screen uses (see back_attack in
 * knob.c), which is what buys the room for everything else to be big.
 *
 * One accent colour per mode runs through its sector, the hub's ring
 * and the amount, so the number you are about to apply is the colour
 * of the thing it is about to do. */

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
/* Lighter than the quad menus' 0x1A1A2E on purpose: those tiles are
   separated by gaps on a black screen, but an unselected sector here
   has to read as a distinct AREA against black, and 0x1A1A2E is close
   enough to black that the four sectors vanished and their labels
   looked like floating text. */
#define ATTACK_TILE_BG     0x232B45
#define ATTACK_HUB_BG      0x12151F
#define ATTACK_TILE_TEXT   0xAEB4C6

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

// ---------- geometry ----------
#define ATTACK_CX 180
#define ATTACK_CY 180
/* The mode sectors are an annulus from ATTACK_HUB_R out to
   ATTACK_RING_R; the hub inside it is the amount and the Resolve
   target. Both are well inside the 180px glass. */
#define ATTACK_HUB_R     96
#define ATTACK_RING_R   176
#define ATTACK_SECTOR_GAP_DEG 3
/* Where a sector's label sits: the middle of the band, not its inner
   edge - at the inner edge a label reads as floating over the hub
   rather than as belonging to its sector. */
#define ATTACK_LABEL_R  ((ATTACK_HUB_R + ATTACK_RING_R) / 2)
#define ATTACK_DEG2RAD  0.017453292f

/* Mid-angle of each mode's sector, in LVGL's convention (0 = 3
   o'clock, clockwise). Corners, not cardinal points - see the file
   comment for why. Reading order: Dmg top-left, Lifelink top-right,
   Cmdr bottom-left, Infect bottom-right. */
static const int attack_mode_mid_deg[ATTACK_MODE_COUNT] = { 225, 315, 135, 45 };

// ---------- widgets ----------
static lv_obj_t *label_source = NULL;
static lv_obj_t *label_arrow = NULL;
static lv_obj_t *label_target = NULL;
static lv_obj_t *label_mode[ATTACK_MODE_COUNT];
static lv_obj_t *label_amount = NULL;
static lv_obj_t *label_attack_resolve = NULL;

// ---------- refresh ----------
static void refresh_attack_ui(void)
{
    lv_color_t accent;
    int i;

    if (attack_source < 0 || attack_target < 0) return;
    if (screen_attack == NULL) return;

    accent = lv_color_hex(attack_mode_accent[attack_mode]);

    /* Each player's name in their own panel colour, so this screen
       carries on from the drag that opened it - which already draws the
       beam in the source's colour and the reticle in the target's. */
    if (label_source != NULL) {
        lv_label_set_text(label_source, player_names[attack_source]);
        lv_obj_set_style_text_color(label_source,
            get_effective_player_color(attack_source, attack_source_color, LIFE_VIB_VIV), 0);
    }
    if (label_target != NULL) {
        lv_label_set_text(label_target, player_names[attack_target]);
        lv_obj_set_style_text_color(label_target,
            get_effective_player_color(attack_target, attack_target_color, LIFE_VIB_VIV), 0);
    }

    for (i = 0; i < ATTACK_MODE_COUNT; i++) {
        bool active = (attack_mode == i);
        if (label_mode[i] == NULL) continue;
        lv_obj_set_style_text_color(label_mode[i],
            active ? lv_color_black() : lv_color_hex(ATTACK_TILE_TEXT), 0);
    }

    if (label_amount != NULL) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", attack_amount);
        lv_label_set_text(label_amount, buf);
        lv_obj_set_style_text_color(label_amount, accent, 0);
    }
    if (label_attack_resolve != NULL) {
        lv_obj_set_style_text_color(label_attack_resolve, accent, 0);
    }

    lv_obj_invalidate(screen_attack);
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

/* One handler for the whole screen: which sector (or the hub) was
   touched is a question about where the finger landed, and answering it
   in polar coordinates is both shorter and gives far bigger targets
   than four buttons and a fifth could. */
static void event_attack_press(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t p;
    float dx, dy, r;
    int deg, i;

    (void)e;
    if (indev == NULL) return;
    lv_indev_get_point(indev, &p);

    dx = (float)p.x - (float)ATTACK_CX;
    dy = (float)p.y - (float)ATTACK_CY;
    r = sqrtf(dx * dx + dy * dy);

    if (r <= (float)ATTACK_HUB_R) {
        event_attack_resolve(NULL);
        return;
    }
    if (r > (float)ATTACK_RING_R) return;   /* the rim, outside any sector */

    deg = (int)(atan2f(dy, dx) * 57.29577951f);
    if (deg < 0) deg += 360;

    for (i = 0; i < ATTACK_MODE_COUNT; i++) {
        int diff = deg - attack_mode_mid_deg[i];
        while (diff > 180) diff -= 360;
        while (diff < -180) diff += 360;
        if (diff < -45 || diff > 45) continue;
        attack_mode = (attack_mode_t)i;
        refresh_attack_ui();
        return;
    }
}

// ---------- drawing ----------
/* Band between two radii. lv_draw_arc's `radius` is the band's OUTER
   edge and its width runs inward from there, which is easy to read as
   "centre and thickness" - and was: the sectors came out spanning
   56..136 instead of 96..176, so the labels at 136 sat on the outer
   rim of their own sector, half of each one over bare black. Naming
   both edges makes that mistake impossible. */
static void attack_band(lv_draw_ctx_t *ctx, uint32_t color,
                        int inner, int outer, int start_deg, int end_deg)
{
    lv_draw_arc_dsc_t dsc;
    lv_point_t c = { ATTACK_CX, ATTACK_CY };

    lv_draw_arc_dsc_init(&dsc);
    dsc.color = lv_color_hex(color);
    dsc.width = (lv_coord_t)(outer - inner);
    dsc.opa = LV_OPA_COVER;
    lv_draw_arc(ctx, &dsc, &c, (uint16_t)outer,
                (uint16_t)((start_deg + 360) % 360),
                (uint16_t)((end_deg + 360) % 360));
}

static void attack_disc(lv_draw_ctx_t *ctx, uint32_t color, int radius)
{
    lv_draw_rect_dsc_t dsc;
    lv_area_t a;

    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(color);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = LV_RADIUS_CIRCLE;
    a.x1 = ATTACK_CX - radius;
    a.y1 = ATTACK_CY - radius;
    a.x2 = ATTACK_CX + radius;
    a.y2 = ATTACK_CY + radius;
    lv_draw_rect(ctx, &dsc, &a);
}

static void event_attack_draw(lv_event_t *e)
{
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    int i;

    for (i = 0; i < ATTACK_MODE_COUNT; i++) {
        bool active = (attack_mode == i);
        int mid = attack_mode_mid_deg[i];
        attack_band(ctx, active ? attack_mode_accent[i] : ATTACK_TILE_BG,
                    ATTACK_HUB_R, ATTACK_RING_R,
                    mid - 45 + ATTACK_SECTOR_GAP_DEG,
                    mid + 45 - ATTACK_SECTOR_GAP_DEG);
    }

    /* The hub: a filled disc a shade off black, ringed in the current
       mode's colour. The fill is what makes the middle read as a thing
       to press rather than as the hole in the middle of a donut - on
       pure black the ring alone looked like a decorative outline. */
    attack_disc(ctx, ATTACK_HUB_BG, ATTACK_HUB_R - 3);
    /* Two halves rather than 0..359: a single arc leaves the one
       degree it did not sweep as a black spoke across the hub. */
    attack_band(ctx, attack_mode_accent[attack_mode],
                ATTACK_HUB_R - 3, ATTACK_HUB_R, 0, 180);
    attack_band(ctx, attack_mode_accent[attack_mode],
                ATTACK_HUB_R - 3, ATTACK_HUB_R, 180, 360);
}

// ---------- screen builder ----------
static lv_obj_t *attack_make_label(lv_obj_t *parent, const lv_font_t *font,
                                   uint32_t color, int width)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    if (width > 0) {
        lv_obj_set_width(lbl, width);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    }
    return lbl;
}

void build_attack_screen(void)
{
    int i;

    screen_attack = lv_obj_create(NULL);
    lv_obj_set_size(screen_attack, 360, 360);
    lv_obj_set_style_bg_color(screen_attack, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_attack, 0, 0);
    lv_obj_set_style_pad_all(screen_attack, 0, 0);
    lv_obj_set_scrollbar_mode(screen_attack, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(screen_attack, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen_attack, event_attack_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(screen_attack, event_attack_press, LV_EVENT_CLICKED, NULL);

    /* Mode names, one per sector, placed out along its diagonal. */
    for (i = 0; i < ATTACK_MODE_COUNT; i++) {
        float rad = (float)attack_mode_mid_deg[i] * ATTACK_DEG2RAD;
        int x = ATTACK_CX + (int)(cosf(rad) * (float)ATTACK_LABEL_R);
        int y = ATTACK_CY + (int)(sinf(rad) * (float)ATTACK_LABEL_R);

        /* Auto-sized and centred on the point, not a fixed-width box:
           a fixed width needs LV_LABEL_LONG_DOT to handle overflow, and
           that ellipsises against a width of zero before the first
           layout pass has run - which mangles the text permanently. */
        label_mode[i] = attack_make_label(screen_attack, &lv_font_es_22,
                                          ATTACK_TILE_TEXT, 0);
        lv_label_set_text(label_mode[i], t(attack_mode_labels[i]));
        lv_obj_align(label_mode[i], LV_ALIGN_CENTER,
                     x - ATTACK_CX, y - ATTACK_CY);
    }

    /* Who is hitting whom, small, at the top of the hub. */
    label_source = attack_make_label(screen_attack, &lv_font_es_16, 0xFFFFFF, 62);
    lv_obj_align(label_source, LV_ALIGN_CENTER, -42, -66);

    label_arrow = attack_make_label(screen_attack, &lv_font_es_16, ATTACK_TILE_TEXT, 0);
    lv_label_set_text(label_arrow, ">");
    lv_obj_align(label_arrow, LV_ALIGN_CENTER, 0, -66);

    label_target = attack_make_label(screen_attack, &lv_font_es_16, 0xFFFFFF, 62);
    lv_obj_align(label_target, LV_ALIGN_CENTER, 42, -66);

    /* The amount, at life-counter scale - it is the number the knob is
       editing and the reason the screen exists. */
    label_amount = attack_make_label(screen_attack, &lv_font_montserrat_bold_116,
                                     0xFFFFFF, 0);
    lv_label_set_text(label_amount, "1");
    lv_obj_align(label_amount, LV_ALIGN_CENTER, 0, -2);

    label_attack_resolve = attack_make_label(screen_attack, &lv_font_es_22,
                                             0xFFFFFF, 0);
    lv_label_set_text(label_attack_resolve, t(STR_RESOLVE));
    lv_obj_align(label_attack_resolve, LV_ALIGN_CENTER, 0, 62);

    refresh_attack_ui();
}

// ---------- test accessors ----------
attack_mode_t attack_test_mode(void)   { return attack_mode; }
int attack_test_amount(void)           { return attack_amount; }

lv_obj_t *attack_test_mode_label(int mode)
{
    if (mode < 0 || mode >= ATTACK_MODE_COUNT) return NULL;
    return label_mode[mode];
}

void attack_test_geometry(int *cx, int *cy, int *inner_r, int *outer_r)
{
    if (cx != NULL)      *cx = ATTACK_CX;
    if (cy != NULL)      *cy = ATTACK_CY;
    if (inner_r != NULL) *inner_r = ATTACK_HUB_R;
    if (outer_r != NULL) *outer_r = ATTACK_RING_R;
}

int attack_test_mode_mid_deg(int mode)
{
    if (mode < 0 || mode >= ATTACK_MODE_COUNT) return 0;
    return attack_mode_mid_deg[mode];
}
