/* What the games cost the battery, and the correctness rule that makes
 * it safe to cost less.
 *
 * On this device every pixel handed to the panel is a QSPI byte and a
 * slice of an 80MHz software renderer. Repainting all 360x360 is about
 * 259KB and a full render, and LVGL does it 25 times a second while a
 * game is up - by a wide margin the largest thing the device does
 * outside the backlight itself. A game that tells LVGL which pixels
 * actually changed does a fraction of that work, and the CPU gets to
 * idle between frames instead of rendering scenery that did not move.
 *
 * Two things are checked per game:
 *
 * 1. The traffic, as pixels flushed over four seconds of play. The
 *    ceilings below are roughly twice what each game currently does,
 *    so ordinary changes pass and a game that quietly goes back to
 *    repainting everything does not.
 *
 * 2. That the partial repaint is HONEST: play for a while, keep what
 *    the panel was actually sent, then repaint the same state in full
 *    and compare. Any pixel that differs is one the game changed and
 *    forgot to invalidate - a trail behind a sprite, a stale score - and
 *    that is the one bug this optimisation can introduce. It cannot be
 *    seen in a screenshot, because a screenshot takes the full repaint.
 */
#include "test_harness.h"
#include "sim_stubs.h"
#include "presentation/minigames/minigame.h"
#include "presentation/minigames/snake.h"
#include "presentation/minigames/pong.h"
#include "presentation/minigames/dino.h"
#include "presentation/minigames/tetris.h"
#include "presentation/minigames/breakout.h"
#include "presentation/minigames/flappy.h"
#include "presentation/minigames/eggs.h"
#include "presentation/minigames/invaders.h"
#include "presentation/minigames/asteroids.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

typedef struct {
    const char *name;
    void (*open)(void);
    void (*tap)(void);
    void (*turn)(int dir);
    lv_obj_t **screen;
    unsigned long max_px;   /* over 4s of play */
} game_probe_t;

static unsigned long flushed_px;
static void (*real_flush)(lv_disp_drv_t *, const lv_area_t *, lv_color_t *);

static void counting_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    flushed_px += (unsigned long)(area->x2 - area->x1 + 1) *
                  (unsigned long)(area->y2 - area->y1 + 1);
    real_flush(drv, area, color_p);
}

static void run(int ms)
{
    int i;
    for (i = 0; i < ms / 10; i++) {
        sim_tick_advance(10);
        lv_timer_handler();
    }
}

/* A copy of the panel as the game left it, before the comparison
   repaint overwrites it. */
static lv_color_t snapshot[TEST_SCREEN_W * TEST_SCREEN_H];

static void probe(const game_probe_t *g)
{
    size_t bytes = sizeof(snapshot);
    int mismatches = 0;
    size_t i;

    g->open();
    if (g->tap != NULL) g->tap();
    /* A turn as well as a tap: some games start on one and not the
       other, and a game that never left READY would measure as free. */
    if (g->turn != NULL) g->turn(1);
    run(500);

    flushed_px = 0;
    run(4000);
    if (flushed_px > g->max_px) {
        printf("FAIL: %s pushed %lu px over 4s of play, ceiling is %lu\n",
               g->name, flushed_px, g->max_px);
        assert(0);
    }

    /* Drain whatever the last tick invalidated before snapshotting:
       a tick that ran after the final refresh would otherwise show up
       as "stale" pixels that are simply not drawn yet. */
    lv_refr_now(NULL);

    /* Whatever the panel holds now is the sum of every partial repaint
       since the game started. Repainting the same state from scratch
       has to land on exactly the same image. */
    memcpy(snapshot, test_framebuffer, bytes);
    lv_obj_invalidate(*g->screen);
    lv_refr_now(NULL);

    for (i = 0; i < TEST_SCREEN_W * TEST_SCREEN_H; i++) {
        if (memcmp(&snapshot[i], &test_framebuffer[i], sizeof(lv_color_t)) != 0) {
            if (mismatches == 0) {
                printf("FAIL: %s - first stale pixel at (%d,%d)\n", g->name,
                       (int)(i % TEST_SCREEN_W), (int)(i / TEST_SCREEN_W));
            }
            mismatches++;
        }
    }
    if (mismatches != 0) {
        printf("       %d of %d pixels were never repainted after they changed\n",
               mismatches, TEST_SCREEN_W * TEST_SCREEN_H);
        assert(0);
    }

    printf("PASS: %-10s %8lu px over 4s of play, and its partial repaint is complete\n",
           g->name, flushed_px);
}

int main(void)
{
    static const game_probe_t games[] = {
        { "snake",     open_snake_screen,     snake_handle_tap,     snake_turn,     &screen_snake,     3000000 },
        { "pong",      open_pong_screen,      pong_handle_tap,      pong_turn,      &screen_pong,     10000000 },
        { "dino",      open_dino_screen,      dino_handle_tap,      NULL,           &screen_dino,     14000000 },
        { "tetris",    open_tetris_screen,    tetris_handle_tap,    tetris_turn,    &screen_tetris,     400000 },
        { "breakout",  open_breakout_screen,  breakout_handle_tap,  breakout_turn,  &screen_breakout,   600000 },
        { "flappy",    open_flappy_screen,    flappy_handle_tap,    NULL,           &screen_flappy,    2000000 },
        { "eggs",      open_eggs_screen,      eggs_handle_tap,      eggs_turn,      &screen_eggs,     26000000 },
        { "invaders",  open_invaders_screen,  invaders_handle_tap,  invaders_turn,  &screen_invaders, 26000000 },
        { "asteroids", open_asteroids_screen, asteroids_handle_tap, asteroids_turn, &screen_asteroids,26000000 },
    };
    size_t i;

    setvbuf(stdout, NULL, _IOLBF, 0);

    test_harness_init();
    test_harness_settle_intro();
    test_harness_reset_4p();

    real_flush = lv_disp_get_default()->driver->flush_cb;
    lv_disp_get_default()->driver->flush_cb = counting_flush;

    for (i = 0; i < sizeof(games) / sizeof(games[0]); i++) probe(&games[i]);

    printf("\nAll frame cost tests passed.\n");
    return 0;
}
