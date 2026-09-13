#ifndef _TEST_HARNESS_H
#define _TEST_HARNESS_H

/* Shared boot sequence for headless unit tests. Every test binary has
 * its own main() (see sim/Makefile's `test` target) so LVGL's global
 * state starts fresh each time - trying to share one process across
 * tests would mean unwinding whatever screens/timers the previous test
 * left behind, which is more fragile than just paying the ~instant
 * boot cost again per binary. */

#include "board_detect.h"
#include <lvgl.h>
#include "knob.h"
#include "game.h"
#include "storage.h"
#include "hw.h"

#define TEST_SCREEN_W 360
#define TEST_SCREEN_H 360

static lv_color_t test_draw_buf_data[TEST_SCREEN_W * 72];
static lv_disp_draw_buf_t test_draw_buf;
static lv_disp_drv_t test_disp_drv;

static void test_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    (void)area;
    (void)color_p;
    lv_disp_flush_ready(drv);
}

/* Boots LVGL + the full UI (knob_gui builds all 35 screens, same as
 * firmware boot) so any game.c/damage_log.c function that touches a
 * screen global or fires a refresh_*_ui() callback has something valid
 * to call into. Call once at the top of main(). */
static void test_harness_init(void)
{
    board_detect();
    lv_init();
    lv_disp_draw_buf_init(&test_draw_buf, test_draw_buf_data, NULL, TEST_SCREEN_W * 72);
    lv_disp_drv_init(&test_disp_drv);
    test_disp_drv.hor_res = TEST_SCREEN_W;
    test_disp_drv.ver_res = TEST_SCREEN_H;
    test_disp_drv.flush_cb = test_flush_cb;
    test_disp_drv.draw_buf = &test_draw_buf;
    lv_disp_drv_register(&test_disp_drv);
    knob_gui();
}

/* Resets to a known 4-player multiplayer game, the shape most rules
 * (commander damage, auto-elimination, table sync) assume. Individual
 * tests that need a different player count call
 * nvs_set_num_players()/nvs_set_players_to_track() first.
 *
 * "Random first player" is switched off here: it's ON by default (see
 * cached_random_first in storage.c), and boot's intro sequence starts
 * a roulette-style selection animation for it a few hundred ms after
 * the logo screen (see start_player_selection_animation() in intro.c) -
 * which change_player_life() deliberately locks out life changes
 * during (a delta dialed mid-spin would land on whichever player the
 * wheel stops at). Any test that pumps simulated time past that point
 * (life preview's 3s commit window, for instance) would otherwise have
 * the animation silently start mid-test and make change_player_life()
 * a no-op with no assertion failure pointing at why. */
static void test_harness_reset_4p(void)
{
    nvs_set_num_players(4);
    nvs_set_players_to_track(4);
    nvs_set_random_first(0);
    stop_player_selection_animation();
    knob_life_reset();
}

#endif /* _TEST_HARNESS_H */
