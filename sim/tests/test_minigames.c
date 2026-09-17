/* The six games added alongside snake/pong/dino, each played rather
 * than merely poked: the test reads the same state a player reads off
 * the screen (where the gap is, which alien is lowest, how full the
 * well is) and drives the same two inputs the hardware has.
 *
 * What each section is really guarding is the rule that makes its game
 * that game - a bird that can sit on the ceiling, a paddle that misses
 * the ball it visually covers, a tetromino that can rotate through a
 * wall, or a knob that keeps working after a run has ended would all
 * compile perfectly happily. */
#include "test_harness.h"
#include "sim_stubs.h"
#include "minigame.h"
#include "flappy.h"
#include "eggs.h"
#include "breakout.h"
#include "invaders.h"
#include "tetris.h"
#include "rps.h"
#include <stdio.h>
#include <assert.h>

static void tick_ms(int ms)
{
    sim_tick_advance((uint32_t)ms);
    lv_timer_handler();
}

static void ticks(int n, int ms)
{
    int i;
    for (i = 0; i < n; i++) tick_ms(ms);
}

/* ---------------------------------------------------------------- */
static void test_flappy(void)
{
    int dx, gap, guard;

    open_flappy_screen();
    assert(minigame_test_state(screen_flappy) == MINIGAME_READY);
    flappy_handle_tap();
    assert(minigame_test_state(screen_flappy) == MINIGAME_PLAYING);
    printf("PASS: flappy starts on a tap\n");

    /* Left alone the bird falls and dies: gravity is actually applied. */
    guard = 0;
    while (minigame_test_state(screen_flappy) == MINIGAME_PLAYING && guard++ < 500) {
        tick_ms(28);
    }
    assert(minigame_test_state(screen_flappy) == MINIGAME_OVER);
    assert(minigame_test_score(screen_flappy) == 0);
    printf("PASS: flappy - an unflapped bird falls and the run ends\n");

    /* Flown properly it clears pipes and scores. The rule being tested
       is that the gap is passable at all: aim for its centre and the
       bird must get through. */
    flappy_handle_tap();                 /* clear game over */
    assert(minigame_test_state(screen_flappy) == MINIGAME_READY);
    flappy_handle_tap();                 /* start */
    assert(minigame_test_state(screen_flappy) == MINIGAME_PLAYING);
    guard = 0;
    while (minigame_test_state(screen_flappy) == MINIGAME_PLAYING &&
           minigame_test_score(screen_flappy) < 5 && guard++ < 4000) {
        int target = 170;
        if (flappy_test_next_gap(&dx, &gap)) target = gap;
        /* Flap whenever the bird has sunk below where it needs to be -
           the one-button equivalent of "hold altitude". */
        if (flappy_test_bird_y() > target - 6) flappy_turn(1);
        tick_ms(28);
    }
    assert(minigame_test_score(screen_flappy) >= 5);
    printf("   flappy cleared %d pipes\n", minigame_test_score(screen_flappy));
    printf("PASS: flappy - flying the gaps scores\n");

    /* The ceiling is fatal too, so parking at the top is not a strategy.
       Re-opening the screen is the deterministic way back to READY -
       tapping only restarts from GAME OVER, never mid-run. */
    open_flappy_screen();
    flappy_handle_tap();
    guard = 0;
    while (minigame_test_state(screen_flappy) == MINIGAME_PLAYING && guard++ < 400) {
        flappy_turn(1);   /* flap every single tick: straight up */
        tick_ms(28);
    }
    assert(minigame_test_state(screen_flappy) == MINIGAME_OVER);
    printf("PASS: flappy - holding the flap flies into the ceiling\n");
}

/* ---------------------------------------------------------------- */
static void test_eggs(void)
{
    int guard;
    int start_lives;

    open_eggs_screen();
    eggs_handle_tap();
    assert(minigame_test_state(screen_eggs) == MINIGAME_PLAYING);
    start_lives = eggs_test_lives();
    assert(start_lives == 3);

    /* Parked at one edge, eggs spawn across the whole band and get
       missed: lives must actually drain, and running out must end it. */
    guard = 0;
    while (minigame_test_state(screen_eggs) == MINIGAME_PLAYING && guard++ < 6000) {
        eggs_turn(-1);
        tick_ms(26);
    }
    assert(minigame_test_state(screen_eggs) == MINIGAME_OVER);
    assert(eggs_test_lives() <= 0);
    printf("PASS: eggs - missed eggs cost lives and end the run\n");

    /* Played properly - basket chasing the lowest egg - the score
       climbs and the lives survive. */
    open_eggs_screen();
    eggs_handle_tap();
    assert(minigame_test_state(screen_eggs) == MINIGAME_PLAYING);
    guard = 0;
    while (minigame_test_state(screen_eggs) == MINIGAME_PLAYING &&
           minigame_test_score(screen_eggs) < 8 && guard++ < 8000) {
        int ex, ey;
        if (eggs_test_lowest_egg(&ex, &ey)) {
            int basket = eggs_test_basket_x();
            if (ex < basket - 4) eggs_turn(-1);
            else if (ex > basket + 4) eggs_turn(1);
        }
        tick_ms(26);
    }
    assert(minigame_test_score(screen_eggs) >= 8);
    printf("   eggs caught %d\n", minigame_test_score(screen_eggs));
    printf("PASS: eggs - tracking the egg with the knob catches it\n");
}

/* ---------------------------------------------------------------- */
static void test_breakout(void)
{
    int guard;
    int bricks_at_start;

    open_breakout_screen();
    breakout_handle_tap();               /* start */
    assert(minigame_test_state(screen_breakout) == MINIGAME_PLAYING);

    bricks_at_start = breakout_test_bricks_left();
    assert(bricks_at_start == 28);
    assert(breakout_test_lives() == 3);

    /* The ball rides the paddle until released - a run can't start with
       the ball already falling past a player who hasn't aimed yet. */
    ticks(20, 20);
    assert(breakout_test_bricks_left() == bricks_at_start);
    breakout_handle_tap();               /* release */

    /* Paddle tracking the ball: bricks must come down, and the ball must
       not be lost while the paddle is under it. */
    guard = 0;
    while (minigame_test_state(screen_breakout) == MINIGAME_PLAYING &&
           breakout_test_bricks_left() > bricks_at_start - 10 && guard++ < 20000) {
        int bx, by;
        breakout_test_ball(&bx, &by);
        if (bx < breakout_test_paddle_x() - 5) breakout_turn(-1);
        else if (bx > breakout_test_paddle_x() + 5) breakout_turn(1);
        (void)by;
        tick_ms(20);
    }
    assert(breakout_test_bricks_left() <= bricks_at_start - 10);
    assert(breakout_test_lives() == 3);
    assert(minigame_test_score(screen_breakout) >= 100);
    printf("   breakout broke %d bricks without losing a ball\n",
           bricks_at_start - breakout_test_bricks_left());
    printf("PASS: breakout - a tracking paddle keeps the ball and clears bricks\n");

    /* Abandon the paddle at one edge and the ball is lost, three times,
       and then the run is over. Only a parked ball gets tapped: a tap
       with the ball in flight means "pause", which would stall the loop
       rather than advance it. */
    guard = 0;
    while (minigame_test_state(screen_breakout) == MINIGAME_PLAYING && guard++ < 40000) {
        breakout_turn(-1);
        if (breakout_test_ball_parked()) breakout_handle_tap();
        tick_ms(20);
    }
    assert(minigame_test_state(screen_breakout) == MINIGAME_OVER);
    assert(breakout_test_lives() <= 0);
    printf("PASS: breakout - three lost balls end the run\n");
}

/* ---------------------------------------------------------------- */
static void test_invaders(void)
{
    int guard;

    open_invaders_screen();
    invaders_handle_tap();
    assert(minigame_test_state(screen_invaders) == MINIGAME_PLAYING);
    assert(invaders_test_aliens_left() == 18);

    /* Aim at the lowest alien, fire when lined up. One shot at a time is
       the rule: a second tap while one is in flight must do nothing. */
    guard = 0;
    while (minigame_test_state(screen_invaders) == MINIGAME_PLAYING &&
           invaders_test_aliens_left() > 10 && guard++ < 20000) {
        int ax, ay;
        if (invaders_test_lowest_alien(&ax, &ay)) {
            int ship = invaders_test_ship_x();
            if (ax < ship - 6) invaders_turn(-1);
            else if (ax > ship + 6) invaders_turn(1);
            else if (!invaders_test_shot_in_flight()) invaders_handle_tap();
        }
        tick_ms(26);
    }
    assert(invaders_test_aliens_left() <= 10);
    assert(minigame_test_score(screen_invaders) >= 80);
    printf("   invaders shot down %d aliens\n", 18 - invaders_test_aliens_left());
    printf("PASS: invaders - aiming and firing kills aliens and scores\n");

    /* Never firing, the formation eventually lands: the march really
       does descend rather than sliding sideways forever. */
    open_invaders_screen();
    invaders_handle_tap();
    guard = 0;
    while (minigame_test_state(screen_invaders) == MINIGAME_PLAYING && guard++ < 40000) {
        tick_ms(26);
    }
    assert(minigame_test_state(screen_invaders) == MINIGAME_OVER);
    printf("PASS: invaders - an unopposed formation reaches the ship\n");
}

/* ---------------------------------------------------------------- */
static void test_tetris(void)
{
    int guard;
    int rot0;

    open_tetris_screen();
    tetris_handle_tap();
    assert(minigame_test_state(screen_tetris) == MINIGAME_PLAYING);

    /* The knob rotates, and turning back undoes it rather than cycling
       forward - the property that makes an over-rotation recoverable. */
    rot0 = tetris_test_rotation();
    tetris_turn(1);
    tetris_turn(1);
    assert(tetris_test_rotation() == (rot0 + 2) % 4);
    tetris_turn(-1);
    assert(tetris_test_rotation() == (rot0 + 1) % 4);
    printf("PASS: tetris - the knob rotates both ways\n");

    /* Tap zones: left of the well moves left, right of it moves right. */
    {
        int col_before = tetris_test_piece_col();
        tetris_test_tap_at(10);
        assert(tetris_test_piece_col() <= col_before);
        col_before = tetris_test_piece_col();
        tetris_test_tap_at(350);
        assert(tetris_test_piece_col() >= col_before);
        printf("PASS: tetris - side taps move the piece\n");
    }

    /* A drop tap in the middle band lands the piece immediately: cells
       appear in the well without waiting for gravity. */
    {
        int filled_before = tetris_test_filled_cells();
        tetris_test_tap_at(180);
        assert(tetris_test_filled_cells() > filled_before);
        printf("PASS: tetris - a centre tap hard-drops and locks the piece\n");
    }

    /* Left to itself the well fills and the run ends by topping out. */
    guard = 0;
    while (minigame_test_state(screen_tetris) == MINIGAME_PLAYING && guard++ < 60000) {
        tetris_test_tap_at(180);   /* drop everything into the middle */
        tick_ms(30);
    }
    assert(minigame_test_state(screen_tetris) == MINIGAME_OVER);
    assert(tetris_test_stack_height() > 0);
    printf("PASS: tetris - stacking to the ceiling ends the run\n");

    /* Rotation can never push a piece outside the well. */
    open_tetris_screen();
    tetris_handle_tap();
    assert(minigame_test_state(screen_tetris) == MINIGAME_PLAYING);
    for (guard = 0; guard < 400; guard++) {
        tetris_test_tap_at(10);    /* jam it against the left wall */
        tetris_turn(1);
        assert(tetris_test_piece_col() >= -2);
        assert(tetris_test_piece_col() <= 10);
    }
    printf("PASS: tetris - rotation never pushes a piece out of the well\n");
}

/* ---------------------------------------------------------------- */
static void test_rps(void)
{
    int guard;
    int seen_win = 0, seen_loss = 0, seen_draw = 0;

    /* The rule, exhaustively - nine combinations, no sampling. */
    {
        int m, o;
        for (m = 0; m < RPS_THROW_COUNT; m++) {
            for (o = 0; o < RPS_THROW_COUNT; o++) {
                int v = rps_beats((rps_throw_t)m, (rps_throw_t)o);
                if (m == o) assert(v == 0);
                else assert(v == 1 || v == -1);
                /* Antisymmetry: if I beat you, you lose to me. */
                assert(v == -rps_beats((rps_throw_t)o, (rps_throw_t)m));
            }
        }
        assert(rps_beats(RPS_ROCK, RPS_SCISSORS) == 1);
        assert(rps_beats(RPS_PAPER, RPS_ROCK) == 1);
        assert(rps_beats(RPS_SCISSORS, RPS_PAPER) == 1);
        printf("PASS: rps - the win table is complete and antisymmetric\n");
    }

    open_rps_screen();
    rps_handle_tap();
    assert(minigame_test_state(screen_rps) == MINIGAME_PLAYING);

    /* The knob cycles all three throws. */
    {
        int start = (int)rps_test_choice();
        rps_turn(1);
        assert((int)rps_test_choice() == (start + 1) % 3);
        rps_turn(1);
        assert((int)rps_test_choice() == (start + 2) % 3);
        rps_turn(-1);
        assert((int)rps_test_choice() == (start + 1) % 3);
        printf("PASS: rps - the knob cycles the throw\n");
    }

    /* Play until a loss ends the streak, checking that the score only
       ever moves on a win and that the reveal locks the throw in. */
    guard = 0;
    while (minigame_test_state(screen_rps) == MINIGAME_PLAYING && guard++ < 4000) {
        if (!rps_test_revealing()) {
            int before = minigame_test_score(screen_rps);
            rps_throw_t locked;
            rps_handle_tap();               /* throw */
            locked = rps_test_choice();
            assert(rps_test_revealing());
            /* Locked in: the knob must not change the throw mid-reveal. */
            rps_turn(1);
            assert(rps_test_choice() == locked);

            if (rps_test_verdict() > 0) {
                seen_win = 1;
                assert(minigame_test_score(screen_rps) == before + 1);
            } else {
                if (rps_test_verdict() < 0) seen_loss = 1;
                else seen_draw = 1;
                assert(minigame_test_score(screen_rps) == before);
            }
        }
        tick_ms(60);
    }
    assert(minigame_test_state(screen_rps) == MINIGAME_OVER);
    assert(seen_loss);
    assert(seen_win || seen_draw);
    printf("PASS: rps - wins score, a loss ends the streak, throws lock on reveal\n");
}

int main(void)
{
    /* Line-buffered so the PASS trail survives an assert() abort - fully
       buffered stdout loses everything printed before the failure, which
       is exactly the context needed to read it. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    test_harness_init();
    /* The intro's pending back_to_main() would otherwise navigate away
       mid-test and silently pause whichever game is running - see
       test_harness_settle_intro(). */
    test_harness_settle_intro();
    test_harness_reset_4p();

    printf("---- flappy ----\n");   test_flappy();
    printf("---- eggs ----\n");     test_eggs();
    printf("---- breakout ----\n"); test_breakout();
    printf("---- invaders ----\n"); test_invaders();
    printf("---- tetris ----\n");   test_tetris();
    printf("---- rps ----\n");      test_rps();

    printf("\nAll minigame tests passed.\n");
    return 0;
}
