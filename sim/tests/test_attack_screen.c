/* The attack-resolve screen, which is a dial rather than a form.
 *
 * Four sectors of an annulus are the four modes and the hub in the
 * middle is the amount plus Resolve, so there are no button widgets to
 * hit-test against: which mode a touch selects is worked out from
 * where the finger landed in polar coordinates. That makes two things
 * worth guarding, and both of them have already gone wrong once.
 *
 * The first is that the drawn geometry and the pressed geometry are
 * the same geometry. They are computed in two different places from
 * the same constants, and lv_draw_arc's `radius` argument is the OUTER
 * edge of the band with the width running inward - read as "centre and
 * thickness" it silently draws the sectors 40px in from where they are
 * pressed, which is exactly what happened: the labels ended up sitting
 * on bare black outside their own sector and read as half-erased text.
 * So the test checks every mode label's box lies inside the band, and
 * that a tap aimed at the middle of a sector selects that sector.
 *
 * The second is that the hub is the Resolve button. It has no border
 * and no widget - if the radial test that separates it from the
 * sectors were wrong, the screen would either resolve when the player
 * meant to pick a mode, or refuse to resolve at all. */
#include "test_harness.h"
#include "sim_stubs.h"
#include "attack.h"
#include "game.h"
#include "lang.h"
#include "prefs_table.h"
#include "game_state.h"
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <string.h>

#define DEG2RAD 0.017453292f

/* Mirrors of attack.c's palette. Duplicated deliberately: a test that
   imported the constants would still pass if the palette changed to
   something unreadable, and these are asserted on for contrast as much
   as for position. */
#define ATTACK_SECTOR_BG     0x232B45
#define ATTACK_HUB_BG        0x12151F
#define ATTACK_ACCENT_DAMAGE 0xEF5350

static lv_indev_drv_t test_pointer_drv;
static lv_point_t test_pointer_at;
static bool test_pointer_down;

static void test_pointer_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    data->point = test_pointer_at;
    data->state = test_pointer_down ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

static void test_pointer_init(void)
{
    lv_indev_drv_init(&test_pointer_drv);
    test_pointer_drv.type = LV_INDEV_TYPE_POINTER;
    test_pointer_drv.read_cb = test_pointer_read;
    lv_indev_drv_register(&test_pointer_drv);
}

static void tick_ms(int ms)
{
    sim_tick_advance((uint32_t)ms);
    lv_timer_handler();
}

static void tap_at(int x, int y)
{
    int i;
    test_pointer_at.x = (lv_coord_t)x;
    test_pointer_at.y = (lv_coord_t)y;
    test_pointer_down = true;
    for (i = 0; i < 3; i++) tick_ms(20);
    test_pointer_down = false;
    for (i = 0; i < 3; i++) tick_ms(20);
}

/* Polar point, in the same convention the screen uses: degrees
   clockwise from 3 o'clock. */
static void polar(int deg, int radius, int *x, int *y)
{
    int cx, cy;
    attack_test_geometry(&cx, &cy, NULL, NULL);
    *x = cx + (int)(cosf((float)deg * DEG2RAD) * (float)radius);
    *y = cy + (int)(sinf((float)deg * DEG2RAD) * (float)radius);
}

static void tap_sector(attack_mode_t mode)
{
    int inner, outer, x, y;
    attack_test_geometry(NULL, NULL, &inner, &outer);
    polar(attack_test_mode_mid_deg((int)mode), (inner + outer) / 2, &x, &y);
    tap_at(x, y);
}

static void open_attack(void)
{
    open_attack_screen(0, 0, 1, 1);
    tick_ms(20);
    assert(lv_scr_act() == screen_attack);
}

/* 16-bit colour: a 24-bit value does not survive the round trip, so
   compare channel by channel with a little slack. */
static bool colour_is(uint32_t got, uint32_t want)
{
    int i;
    for (i = 0; i < 3; i++) {
        int g = (int)((got >> (i * 8)) & 0xFF);
        int w = (int)((want >> (i * 8)) & 0xFF);
        if (g - w > 8 || w - g > 8) return false;
    }
    return true;
}

static void expect_pixel(const char *what, int deg, int radius, uint32_t want)
{
    int x, y;
    uint32_t got;

    polar(deg, radius, &x, &y);
    got = test_harness_pixel(x, y);
    if (!colour_is(got, want)) {
        printf("FAIL: %s - pixel at %d deg r=%d (%d,%d) is %06X, expected %06X\n",
               what, deg, radius, x, y, got, want);
        assert(0);
    }
}

/* Where the sectors are actually PAINTED, sampled from the framebuffer.
 *
 * This is the assertion the earlier bug needed. lv_draw_arc's radius is
 * the band's outer edge with the width running inward; read as "centre
 * and thickness" it drew the ring at 56..136 while every touch was
 * tested against 96..176. Reading widget geometry could never catch
 * that - the band is not a widget - so the check has to be on pixels:
 * paint at both edges of the band, hub colour inside it, black outside.
 *
 * Sampled well off each sector's mid-angle so the labels, which sit on
 * the mid-angle, cannot colour the result. */
static void test_the_sectors_are_painted_where_they_are_pressed(void)
{
    int inner, outer, i;

    open_attack();
    lv_refr_now(NULL);
    attack_test_geometry(NULL, NULL, &inner, &outer);

    for (i = 0; i < ATTACK_MODE_COUNT; i++) {
        int mid = attack_test_mode_mid_deg(i);
        uint32_t fill = (i == (int)attack_test_mode())
                            ? ATTACK_ACCENT_DAMAGE : ATTACK_SECTOR_BG;
        int off;

        for (off = -25; off <= 25; off += 50) {
            expect_pixel("sector inner edge", mid + off, inner + 4, fill);
            expect_pixel("sector mid", mid + off, (inner + outer) / 2, fill);
            expect_pixel("sector outer edge", mid + off, outer - 4, fill);
        }
        /* Inside the hub and outside the rim, nothing of the band. */
        expect_pixel("hub", mid, inner - 12, ATTACK_HUB_BG);
        expect_pixel("outside the rim", mid, outer + 3, 0x000000);
    }
    printf("PASS: the sector band is painted over exactly the radii it is pressed on\n");
}

/* ---------------------------------------------------------------- */
/* Every corner of every mode label has to be inside the band that is
   painted behind it, or part of the word is drawn on black. Corners
   rather than the centre: the labels sit on the diagonals precisely
   because a word laid across the band reaches further than the band is
   thick, so the centre being inside proves very little. */
static void test_labels_sit_inside_their_sector(void)
{
    int cx, cy, inner, outer, i;

    open_attack();
    lv_refr_now(NULL);

    attack_test_geometry(&cx, &cy, &inner, &outer);

    for (i = 0; i < ATTACK_MODE_COUNT; i++) {
        lv_obj_t *lbl = attack_test_mode_label(i);
        lv_coord_t x1, y1, x2, y2;
        int mid = attack_test_mode_mid_deg(i);
        int c;
        static const int corner[4][2] = { {0,0}, {1,0}, {0,1}, {1,1} };

        assert(lbl != NULL);
        x1 = lv_obj_get_x(lbl);
        y1 = lv_obj_get_y(lbl);
        x2 = x1 + lv_obj_get_width(lbl) - 1;
        y2 = y1 + lv_obj_get_height(lbl) - 1;
        assert(lv_obj_get_width(lbl) > 0 && lv_obj_get_height(lbl) > 0);

        for (c = 0; c < 4; c++) {
            float dx = (float)(corner[c][0] ? x2 : x1) - (float)cx;
            float dy = (float)(corner[c][1] ? y2 : y1) - (float)cy;
            float r = sqrtf(dx * dx + dy * dy);
            int deg = (int)(atan2f(dy, dx) / DEG2RAD);
            int diff;

            if (deg < 0) deg += 360;
            diff = deg - mid;
            while (diff > 180) diff -= 360;
            while (diff < -180) diff += 360;

            if (r < (float)inner || r > (float)outer || diff < -45 || diff > 45) {
                printf("FAIL: mode %d label corner %d at r=%.1f, %d deg off centre;"
                       " band is %d..%d and the sector spans +-45\n",
                       i, c, (double)r, diff, inner, outer);
                assert(0);
            }
        }
    }
    printf("PASS: every mode label lies inside the sector drawn behind it\n");
}

/* A tap in the middle of a sector selects that sector's mode - the
   sectors are where they look like they are. */
static void test_tapping_a_sector_selects_its_mode(void)
{
    int inner, outer, i;

    attack_test_geometry(NULL, NULL, &inner, &outer);

    for (i = ATTACK_MODE_COUNT - 1; i >= 0; i--) {
        int x, y;
        open_attack();   /* opens on Damage, so every hop is a real change */
        polar(attack_test_mode_mid_deg(i), (inner + outer) / 2, &x, &y);
        tap_at(x, y);
        if (attack_test_mode() != (attack_mode_t)i) {
            printf("FAIL: tap at (%d,%d) for mode %d selected mode %d\n",
                   x, y, i, (int)attack_test_mode());
            assert(0);
        }
    }
    printf("PASS: a tap in a sector selects that sector's mode\n");
}

/* The gaps between sectors are decoration, not dead zones: a tap just
   inside a sector's edge still counts as that sector. Drawn gaps are
   3 degrees, so 43 degrees off centre lands on painted black but
   inside the sector's 45. */
static void test_the_drawn_gaps_are_not_dead_zones(void)
{
    int inner, outer, x, y;

    attack_test_geometry(NULL, NULL, &inner, &outer);
    open_attack();
    polar(attack_test_mode_mid_deg(ATTACK_MODE_INFECT) - 43,
          (inner + outer) / 2, &x, &y);
    tap_at(x, y);
    assert(attack_test_mode() == ATTACK_MODE_INFECT);
    printf("PASS: the drawn gap between sectors still selects its own sector\n");
}

/* The hub resolves, and it resolves the mode that is selected. Damage
   and Lifelink differ only in what happens to the attacker, so running
   both proves the mode actually reached the resolve. */
static void test_the_hub_resolves(void)
{
    int inner, target_before, source_before;
    int x, y;

    attack_test_geometry(NULL, NULL, &inner, NULL);

    /* Damage: the target loses life, the attacker is untouched. */
    open_attack();
    change_attack_amount(2);     /* opens at 1 */
    assert(attack_test_amount() == 3);
    source_before = player_life[0];
    target_before = player_life[1];
    tap_at(180, 180);
    tick_ms(20);
    assert(lv_scr_act() != screen_attack);
    assert(player_life[1] == target_before - 3);
    assert(player_life[0] == source_before);

    /* Lifelink: the same damage, and the attacker gains it back. */
    open_attack();
    polar(attack_test_mode_mid_deg(ATTACK_MODE_LIFELINK),
          inner + 40, &x, &y);
    tap_at(x, y);
    assert(attack_test_mode() == ATTACK_MODE_LIFELINK);
    change_attack_amount(4);
    source_before = player_life[0];
    target_before = player_life[1];
    tap_at(180, 180);
    tick_ms(20);
    assert(lv_scr_act() != screen_attack);
    assert(player_life[1] == target_before - 5);
    assert(player_life[0] == source_before + 5);

    printf("PASS: the hub resolves the selected mode\n");
}

/* Opening fresh always starts on Damage with 1 - the common case is
   "one creature got through", and a screen that remembered the last
   mode would silently apply infect to the next attack. */
static void test_opening_resets_mode_and_amount(void)
{
    int inner, outer, x, y;

    attack_test_geometry(NULL, NULL, &inner, &outer);
    open_attack();
    polar(attack_test_mode_mid_deg(ATTACK_MODE_CMDR), (inner + outer) / 2, &x, &y);
    tap_at(x, y);
    change_attack_amount(6);
    assert(attack_test_mode() == ATTACK_MODE_CMDR);
    assert(attack_test_amount() == 7);

    open_attack();
    assert(attack_test_mode() == ATTACK_MODE_DAMAGE);
    assert(attack_test_amount() == 1);
    printf("PASS: reopening resets to Damage 1\n");
}

/* All four names share one size, and none of them had to fall back.
 *
 * Sizing each label to its own word gives three at 22pt beside one at
 * 16, which reads as a bug rather than as a longer word - and the odd
 * one out is "Commander", the mode whose name matters most. The size
 * is therefore whatever the longest of the four can take.
 *
 * Run in Spanish as well as English, because the longest word is not
 * the same word in both: "Cmdr" fit at any size, "Comandante" is 146px
 * at 22pt and no 22pt line in a 90-degree sector can exceed 118. A
 * layout that only ever ran in English would ship a clipped label to
 * every Spanish user. */
static void check_labels_in_current_language(const char *lang_name)
{
    const lv_font_t *font = NULL;
    int cx, cy, inner, outer, i;

    attack_test_geometry(&cx, &cy, &inner, &outer);

    for (i = 0; i < ATTACK_MODE_COUNT; i++) {
        lv_obj_t *lbl = attack_test_mode_label(i);
        const lv_font_t *f;
        lv_coord_t x1, y1, x2, y2;
        int mid = attack_test_mode_mid_deg(i);
        int c;
        static const int corner[4][2] = { {0,0}, {1,0}, {0,1}, {1,1} };

        assert(lbl != NULL);
        f = lv_obj_get_style_text_font(lbl, LV_PART_MAIN);
        if (font == NULL) font = f;
        if (f != font) {
            printf("FAIL: %s - mode %d uses a different font from mode 0\n",
                   lang_name, i);
            assert(0);
        }

        x1 = lv_obj_get_x(lbl);
        y1 = lv_obj_get_y(lbl);
        x2 = x1 + lv_obj_get_width(lbl) - 1;
        y2 = y1 + lv_obj_get_height(lbl) - 1;
        assert(lv_obj_get_width(lbl) > 0 && lv_obj_get_height(lbl) > 0);

        for (c = 0; c < 4; c++) {
            float dx = (float)(corner[c][0] ? x2 : x1) - (float)cx;
            float dy = (float)(corner[c][1] ? y2 : y1) - (float)cy;
            float r = sqrtf(dx * dx + dy * dy);
            int deg = (int)(atan2f(dy, dx) / DEG2RAD);
            int diff;

            if (deg < 0) deg += 360;
            diff = deg - mid;
            while (diff > 180) diff -= 360;
            while (diff < -180) diff += 360;

            if (r < (float)inner || r > (float)outer || diff < -45 || diff > 45) {
                printf("FAIL: %s - mode %d ('%s') corner %d at r=%.1f, %d deg off"
                       " centre; band is %d..%d and the sector spans +-45\n",
                       lang_name, i, lv_label_get_text(lbl), c, (double)r, diff,
                       inner, outer);
                assert(0);
            }
        }
    }
}

/* Both of the commander sector's names, not just the one showing.
   The sector swaps to the partner's name mid-attack, and a layout
   measured only against "Comandante" would let that swap push the word
   off its own paint. */
static void check_labels_including_the_partner_swap(const char *lang_name)
{
    check_labels_in_current_language(lang_name);

    set_player_has_partner(0, true);
    open_attack();
    tap_sector(ATTACK_MODE_CMDR);
    tap_sector(ATTACK_MODE_CMDR);
    lv_obj_update_layout(screen_attack);
    lv_refr_now(NULL);
    check_labels_in_current_language(lang_name);
    set_player_has_partner(0, false);
}

static void test_labels_fit_in_every_language(void)
{
    check_labels_including_the_partner_swap("English");
    printf("PASS: the four mode names share one size and fit their sectors (English)\n");

    /* Rebuilt rather than relabelled: the firmware only ever builds
       this screen once, in whatever language was stored at boot, so
       rebuilding is what a Spanish device actually does. */
    prefs_set_language(1);
    lang_init();
    build_attack_screen();
    open_attack();
    lv_refr_now(NULL);
    check_labels_including_the_partner_swap("Spanish");
    printf("PASS: the four mode names share one size and fit their sectors (Spanish)\n");

    prefs_set_language(0);
    lang_init();
    build_attack_screen();
}

/* Nothing in the hub may grow past the hub.
 *
 * The hub's radius is the number's: "99" in the 116pt face spans an
 * 85px half-diagonal, and 88 is that plus clearance. Everything else
 * in there - the Resolve word, the "who hits whom" row - is small
 * enough today, and the temptation when this screen next gets tidied
 * will be to make one of them bigger. Doing so pushes it out over the
 * sector ring, where it lands on a bright accent colour and vanishes.
 *
 * Checked at 99 rather than at the 1 the screen opens on, because the
 * widest amount is the one that decides the geometry. */
static void test_the_hub_holds_its_contents(void)
{
    lv_obj_t *widgets[3];
    const char *names[3] = { "amount", "Resolve", "source name" };
    int cx, cy, hub, i;

    /* A name far longer than its box, and the screen REBUILT with it
       already set.
     *
     * That ordering is the real one and it matters: the stored names
     * are restored before any screen is built (see knob_hw_init), so a
     * player called Maximiliano is the first text this label ever
     * holds. LV_LABEL_LONG_DOT only ellipsises what does not fit the
     * box, and a label that has never been laid out has no height to
     * not fit - it grows a second line instead and climbs out of the
     * hub over the sector ring. Setting the long name on a label that
     * has already been laid out once hides the bug completely, which
     * is why this rebuilds rather than just renaming. */
    snprintf(player_names[0], sizeof(player_names[0]), "%s", "Maximiliano");
    snprintf(player_names[1], sizeof(player_names[1]), "%s", "Bartolomeo");
    build_attack_screen();

    open_attack();
    change_attack_amount(98);
    assert(attack_test_amount() == 99);
    lv_obj_update_layout(screen_attack);
    lv_refr_now(NULL);

    attack_test_geometry(&cx, &cy, &hub, NULL);
    widgets[0] = attack_test_amount_label();
    widgets[1] = attack_test_resolve_label();
    widgets[2] = attack_test_source_label();

    for (i = 0; i < 3; i++) {
        lv_area_t a;
        int c;
        static const int corner[4][2] = { {0,0}, {1,0}, {0,1}, {1,1} };

        assert(widgets[i] != NULL);
        lv_obj_get_coords(widgets[i], &a);
        for (c = 0; c < 4; c++) {
            float dx = (float)(corner[c][0] ? a.x2 : a.x1) - (float)cx;
            float dy = (float)(corner[c][1] ? a.y2 : a.y1) - (float)cy;
            float r = sqrtf(dx * dx + dy * dy);

            if (r > (float)hub) {
                printf("FAIL: the %s reaches r=%.1f, past the hub's %d\n",
                       names[i], (double)r, hub);
                assert(0);
            }
        }
    }
    snprintf(player_names[0], sizeof(player_names[0]), "%s", "P1");
    snprintf(player_names[1], sizeof(player_names[1]), "%s", "P2");
    printf("PASS: the hub holds its contents at the widest amount and longest name\n");
}

/* The commander sector carries two tallies behind one slice.
 *
 * A partner's 21 is its own 21, so the attacker's two commanders need
 * separate totals - but they are the same kind of thing and you use
 * one at a time, so they share a sector instead of taking a fifth.
 * Tapping the sector you are already on swaps between them, and the
 * word in the sector changes so the swap is visible.
 *
 * The flag consulted is the SOURCE player's: it is their commander
 * dealing the damage, not the target's. Getting that backwards would
 * put the control in front of exactly the wrong players. */
static void test_the_commander_sector_carries_the_partner(void)
{
    lv_obj_t *lbl;
    /* Copied, not aliased: lv_label_get_text hands back a pointer into
       the label's own buffer, so holding it would compare the new text
       against itself and pass no matter what. */
    char primary[24];
    int target_before;

    /* Nobody has a partner: the sector is a plain mode and tapping it
       again must not quietly arm a second tally. */
    set_player_has_partner(0, false);
    open_attack();
    tap_sector(ATTACK_MODE_CMDR);
    lbl = attack_test_mode_label((int)ATTACK_MODE_CMDR);
    snprintf(primary, sizeof(primary), "%s", lv_label_get_text(lbl));
    assert(attack_test_mode() == ATTACK_MODE_CMDR);
    tap_sector(ATTACK_MODE_CMDR);
    assert(attack_test_mode() == ATTACK_MODE_CMDR);
    if (strcmp(lv_label_get_text(lbl), primary) != 0) {
        printf("FAIL: the sector swapped to '%s' for an attacker with no partner\n",
               lv_label_get_text(lbl));
        assert(0);
    }
    change_attack_amount(4);
    target_before = player_life[1];
    tap_at(180, 180);
    tick_ms(20);
    assert(cmd_damage_totals[0][1] == 5);
    assert(partner_cmd_damage_totals[0][1] == 0);
    assert(player_life[1] == target_before - 5);
    printf("PASS: with no partner, the commander sector is one mode with one tally\n");

    /* The attacker has a partner: the second tap swaps the word, and
       the damage lands on the partner's own total. */
    set_player_has_partner(0, true);
    open_attack();
    tap_sector(ATTACK_MODE_CMDR);
    assert(strcmp(lv_label_get_text(lbl), primary) == 0);
    tap_sector(ATTACK_MODE_CMDR);
    if (strcmp(lv_label_get_text(lbl), primary) == 0) {
        printf("FAIL: the sector kept saying '%s' for an attacker with a partner\n",
               primary);
        assert(0);
    }
    change_attack_amount(2);
    tap_at(180, 180);
    tick_ms(20);
    assert(partner_cmd_damage_totals[0][1] == 3);
    assert(cmd_damage_totals[0][1] == 5);   /* the primary tally is untouched */
    printf("PASS: the second tap moves the damage to the partner's own tally\n");

    /* A third tap comes back, and leaving the sector resets to the
       primary - arriving at commander damage should never silently be
       "the partner" because of what you did last time. */
    open_attack();
    tap_sector(ATTACK_MODE_CMDR);
    tap_sector(ATTACK_MODE_CMDR);
    tap_sector(ATTACK_MODE_CMDR);
    assert(strcmp(lv_label_get_text(lbl), primary) == 0);
    tap_sector(ATTACK_MODE_CMDR);
    tap_sector(ATTACK_MODE_INFECT);
    tap_sector(ATTACK_MODE_CMDR);
    assert(strcmp(lv_label_get_text(lbl), primary) == 0);
    printf("PASS: the swap is reversible and does not survive leaving the sector\n");

    set_player_has_partner(0, false);
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);

    test_harness_init();
    test_pointer_init();
    test_harness_settle_intro();
    test_harness_reset_4p();

    test_the_sectors_are_painted_where_they_are_pressed();
    test_labels_sit_inside_their_sector();
    test_tapping_a_sector_selects_its_mode();
    test_the_drawn_gaps_are_not_dead_zones();
    test_the_hub_resolves();
    test_opening_resets_mode_and_amount();
    test_the_commander_sector_carries_the_partner();
    test_the_hub_holds_its_contents();
    test_labels_fit_in_every_language();

    printf("\nAll attack screen tests passed.\n");
    return 0;
}
