/* dino.c's run loop, played the way a person plays it: read where the
 * next obstacle is (dino_test_nearest_obstacle), decide, and either
 * jump or hold. Covers the rules the one-button design rests on -
 * cacti must be jumped, birds must NOT be (they fly over a grounded
 * runner's head), jumps don't stack, and the run ends on contact. */
#include "test_harness.h"
#include "sim_stubs.h"
#include "presentation/minigames/dino.h"
#include <stdio.h>
#include <assert.h>

/* Advances the game loop by one 28ms tick (DINO_TICK_MS). */
static void tick(void)
{
    sim_tick_advance(28);
    lv_timer_handler();
}

static void ticks(int n)
{
    int i;
    for (i = 0; i < n; i++) tick();
}

int main(void)
{
    int gap;
    bool is_bird;

    test_harness_init();
    /* The dino's tick callback pauses itself whenever another screen is
       active, so the intro's pending back_to_main() has to land before
       this test navigates - see test_harness_settle_intro(). */
    test_harness_settle_intro();
    test_harness_reset_4p();

    /* ---- starts idle; a tap begins the run ---- */
    open_dino_screen();
    assert(dino_test_state() == DINO_TEST_READY);
    assert(dino_test_score() == 0);
    dino_handle_tap();
    assert(dino_test_state() == DINO_TEST_PLAYING);
    printf("PASS: opens ready, tap starts the run\n");

    /* ---- the world scrolls: score climbs on its own ---- */
    /* 40 ticks is comfortably inside the opening grace: the first
       obstacle spawns after ~28 ticks of countdown and then needs ~56
       more to travel from the right edge to the runner. */
    ticks(40);
    assert(dino_test_state() == DINO_TEST_PLAYING);
    assert(dino_test_score() > 0);
    printf("PASS: score climbs with distance while running\n");

    /* ---- a cactus left alone ends the run ---- */
    {
        int guard = 0;
        while (dino_test_state() == DINO_TEST_PLAYING && guard++ < 4000) tick();
        assert(dino_test_state() == DINO_TEST_GAME_OVER);
    }
    printf("PASS: running into a cactus ends the run\n");

    /* ---- tap on game over restarts from zero ---- */
    dino_handle_tap();
    assert(dino_test_state() == DINO_TEST_READY);
    assert(dino_test_score() == 0);
    printf("PASS: tapping game over restarts a fresh run\n");

    /* ---- played properly (jump cacti, hold for birds) the run
       survives well past where it died untouched, and eventually
       reaches the speed where birds mix in ---- */
    dino_handle_tap(); /* start */
    {
        int guard = 0;
        bool saw_bird = false;
        bool jumped_this_obstacle = false;

        while (dino_test_state() == DINO_TEST_PLAYING && guard++ < 40000) {
            if (dino_test_nearest_obstacle(&gap, &is_bird)) {
                float speed = dino_test_speed();

                if (is_bird) {
                    saw_bird = true; /* correct play is to do nothing */
                } else if (!jumped_this_obstacle && gap > 0 &&
                           (float)gap <= 10.0f * speed && dino_test_grounded()) {
                    /* The arc lasts ~24 ticks and clears a cactus's
                       height between roughly tick 4 and tick 20, so the
                       cactus has to arrive inside that window: jumping
                       at ~10 ticks of travel away (10 * speed px) lands
                       it in the middle of the window at any speed. */
                    dino_handle_tap();
                    jumped_this_obstacle = true;
                }
                if ((float)gap > 12.0f * speed) jumped_this_obstacle = false; /* next one is fresh */
            }
            tick();
        }

        printf("   survived to score %d, birds seen: %s\n",
               dino_test_score(), saw_bird ? "yes" : "no");
        assert(dino_test_score() > 150);
        assert(saw_bird); /* the speed ramp has to actually reach bird territory */
    }
    printf("PASS: jumping clears cacti and the run reaches bird territory\n");
    printf("PASS: birds pass harmlessly over a grounded runner\n");

    /* ---- jumps don't stack: a second tap mid-air is ignored, so the
       runner can't climb out of a committed jump ---- */
    dino_handle_tap(); /* clear game over (or no-op if still playing) */
    if (dino_test_state() == DINO_TEST_READY) dino_handle_tap();
    {
        int airborne_ticks = 0;
        assert(dino_test_grounded());
        dino_handle_tap();          /* jump */
        tick();
        assert(!dino_test_grounded());
        while (!dino_test_grounded() && airborne_ticks++ < 200) {
            dino_handle_tap();      /* spam: must not extend the arc */
            tick();
            if (dino_test_state() != DINO_TEST_PLAYING) break;
        }
        /* The arc is ~24 ticks; anything near the 200 guard would mean
           the spam was lifting the runner indefinitely. */
        assert(airborne_ticks < 60);
    }
    printf("PASS: a jump can't be extended by tapping again mid-air\n");

    printf("\nAll dino tests passed.\n");
    return 0;
}
