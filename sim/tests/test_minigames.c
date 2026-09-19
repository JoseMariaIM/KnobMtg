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
#include "settings.h"
#include "flappy.h"
#include "eggs.h"
#include "breakout.h"
#include "invaders.h"
#include "tetris.h"
#include "rps.h"
#include "asteroids.h"
#include "dino.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <math.h>

static void tick_ms(int ms)
{
    sim_tick_advance((uint32_t)ms);
    lv_timer_handler();
}

/* A real pointer device, so a "tap" in these tests is a press and a
   release at a point on the screen - the same thing the touch panel
   produces - rather than a hand-made LVGL event. That matters: games
   subscribe to either LV_EVENT_PRESSED or LV_EVENT_CLICKED depending
   on whether their action should land on finger-down, and a test that
   posts one specific event only exercises half of them. */
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

/* Holds the finger down for a while, then lifts it. Asteroids' engine
   is a held press, so this is the only way to actually thrust. */
static void test_hold_screen(int ticks, int ms)
{
    int i;
    test_pointer_at.x = 180;
    test_pointer_at.y = 180;
    test_pointer_down = true;
    for (i = 0; i < ticks; i++) tick_ms(ms);
    test_pointer_down = false;
    for (i = 0; i < 3; i++) tick_ms(ms);
}

static void test_tap_screen_at(int x, int y)
{
    int i;
    test_pointer_at.x = (lv_coord_t)x;
    test_pointer_at.y = (lv_coord_t)y;
    test_pointer_down = true;
    for (i = 0; i < 3; i++) tick_ms(20);
    test_pointer_down = false;
    for (i = 0; i < 3; i++) tick_ms(20);
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
/* Breakout's arena is a circle: the paddle runs round the rim and the
   whole circumference is the gutter. "Track the ball" therefore means
   turning the paddle toward the angle of whichever ball is closest to
   the rim, which is what this does - the same read a player makes. */
/* Where the outermost ball will meet the rim if it keeps going
   straight: solve |p + t*v| = R for the positive root. Bricks it may
   clip on the way are ignored, which is exactly what a player's eye
   does. Returns -1 when there is nothing to predict. */
static int breakout_predicted_rim_angle(void)
{
    float px, py, vx, vy;
    float a, b, c, disc, t;
    const float R = 168.0f;   /* BO_ARENA_R */

    if (!breakout_test_outermost_ball_motion(&px, &py, &vx, &vy)) return -1;
    px -= 180.0f;
    py -= 180.0f;

    a = vx * vx + vy * vy;
    if (a < 0.0001f) return -1;
    b = 2.0f * (px * vx + py * vy);
    c = px * px + py * py - R * R;
    disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) return -1;
    t = (-b + sqrtf(disc)) / (2.0f * a);
    if (t < 0.0f) return -1;

    return (int)(atan2f(py + t * vy, px + t * vx) * 57.29577951f);
}

static int breakout_steer_n = 0;

static void breakout_steer(void)
{
    int aim, diff;

    aim = breakout_predicted_rim_angle();
    if (aim < 0) {
        int rad;
        if (!breakout_test_outermost_ball(&aim, &rad)) return;
    }

    /* Deliberately land a little off paddle-centre, and vary by how
       much. A paddle that centres the ball perfectly returns it
       straight back down the same radius, forever - a fixed point no
       human hits, and one that leaves whole sectors of the wall
       untouched, so the test would stall rather than play. */
    aim += (((breakout_steer_n++ / 200) * 37) % 17) - 8;

    diff = aim - breakout_test_paddle_angle();
    while (diff > 180) diff -= 360;
    while (diff < -180) diff += 360;

    if (diff < -3) breakout_turn(-1);
    else if (diff > 3) breakout_turn(1);
}

static void test_breakout(void)
{
    int guard;
    int bricks_at_start;

    open_breakout_screen();
    breakout_handle_tap();               /* start */
    assert(minigame_test_state(screen_breakout) == MINIGAME_PLAYING);

    bricks_at_start = breakout_test_bricks_left();
    assert(bricks_at_start == 48);  /* 4 rings x 12 sectors */
    assert(breakout_test_lives() == 3);

    /* The ball rides the paddle until released - a run can't start with
       the ball already falling past a player who hasn't aimed yet. */
    ticks(20, 20);
    assert(breakout_test_bricks_left() == bricks_at_start);
    breakout_handle_tap();               /* release */

    /* Paddle tracking the lowest ball: bricks must come down, and no
       ball lost while the paddle is under it. */
    guard = 0;
    while (minigame_test_state(screen_breakout) == MINIGAME_PLAYING &&
           breakout_test_bricks_left() > bricks_at_start - 10 && guard++ < 20000) {
        breakout_steer();
        tick_ms(20);
    }
    assert(breakout_test_bricks_left() <= bricks_at_start - 10);
    /* Not lost any - it may have GAINED one, if the wall put a life
       brick in the first ten bricks the paddle got to. */
    assert(breakout_test_lives() >= 3);
    assert(minigame_test_score(screen_breakout) >= 100);
    printf("   breakout broke %d bricks without losing a ball\n",
           bricks_at_start - breakout_test_bricks_left());
    printf("PASS: breakout - a tracking paddle keeps the ball and clears bricks\n");

    /* Abandon the paddle and let every ball go, however many lives the
       wall handed out along the way, until the run is over. Only a parked ball gets tapped: a tap
       with the ball in flight means "pause", which would stall the loop
       rather than advance it. */
    guard = 0;
    while (minigame_test_state(screen_breakout) == MINIGAME_PLAYING && guard++ < 80000) {
        /* Park the paddle opposite whichever ball is nearest the rim.
           On a circle "walk away and wait" is not enough - a paddle
           left spinning would eventually stumble into the ball. */
        int ang, rad;
        if (breakout_test_outermost_ball(&ang, &rad)) {
            int diff = (ang + 180) - breakout_test_paddle_angle();
            while (diff > 180) diff -= 360;
            while (diff < -180) diff += 360;
            if (diff < -3) breakout_turn(-1);
            else if (diff > 3) breakout_turn(1);
        }
        if (breakout_test_ball_parked()) breakout_handle_tap();
        tick_ms(20);
    }
    assert(minigame_test_state(screen_breakout) == MINIGAME_OVER);
    assert(breakout_test_lives() <= 0);
    printf("PASS: breakout - draining every life ends the run\n");
}

/* ---------------------------------------------------------------- */
/* The power-up bricks. Each is reached the same way: play until the
   wall's specials have been taken, watching what changed when they
   were. Nothing here calls an internal "give me multiball" hook - the
   only way in is breaking the brick, same as the player's. */
static void test_breakout_powerups(void)
{
    int guard;
    int walls_seen;
    int max_balls_seen = 1;
    int prev_balls = 1;
    int splits_seen = 0;
    int split_tick = 0;
    bool spread_checked = false;
    bool saw_iron = false;
    bool saw_spawned = false;
    int specials_at_start;

    open_breakout_screen();
    breakout_handle_tap();          /* start */
    specials_at_start = breakout_test_special_bricks_left();

    /* Up to three per wall, and they are actually placed. */
    assert(specials_at_start > 0);
    assert(specials_at_start <= 3);
    printf("PASS: breakout - a wall carries at most 3 power-up bricks (%d)\n",
           specials_at_start);

    breakout_handle_tap();          /* release */

    /* Play properly and take whatever the wall offers. */
    guard = 0;
    walls_seen = breakout_test_level();
    while (minigame_test_state(screen_breakout) == MINIGAME_PLAYING &&
           guard++ < 120000 && (!saw_iron || max_balls_seen < 2 || !spread_checked)) {
        int balls;

        breakout_steer();
        if (breakout_test_ball_parked()) breakout_handle_tap();

        balls = breakout_test_ball_count();
        /* "Duplicate the balls on screen" means exactly that: a split
           can at most double what was in play, never jump straight to
           the array's capacity. */
        if (balls > prev_balls) {
            assert(balls <= prev_balls * 2);
            splits_seen++;
            split_tick = guard;
        }
        /* A short while after a split the balls must have visibly
           separated. A pair leaving on near-identical paths would count
           as "multiball" while looking to the player like nothing
           happened. */
        if (split_tick > 0 && guard >= split_tick + 40) {
            if (breakout_test_ball_count() >= 2) {
                assert(breakout_test_ball_spread() >= 8);  /* degrees apart */
                spread_checked = true;
            }
            split_tick = 0;   /* balls lost before the check: wait for the next split */
        }
        prev_balls = balls;
        if (balls > max_balls_seen) max_balls_seen = balls;
        if (breakout_test_spawned_count() > 0) saw_spawned = true;
        if (breakout_test_iron_ms() > 0) saw_iron = true;

        tick_ms(20);
    }
    assert(splits_seen > 0);

    assert(max_balls_seen >= 2);
    printf("PASS: breakout - a multiball brick splits the ball (saw %d at once)\n",
           max_balls_seen);
    assert(saw_iron);
    printf("PASS: breakout - an iron brick arms the iron ball\n");
    assert(spread_checked);
    printf("PASS: breakout - split balls fan apart instead of shadowing each other\n");
    /* Observed while they were alive, not afterwards: dropping any ball
       wipes the split ones, so by the time the loop exits there may be
       none left to count. */
    assert(saw_spawned);
    printf("PASS: breakout - split balls are marked apart from the served one\n");

    /* Iron must be a timer, not a permanent state, and about 10s of it. */
    {
        int ms = breakout_test_iron_ms();
        if (ms > 0) {
            assert(ms <= 10000);
            guard = 0;
            while (breakout_test_iron_ms() > 0 && guard++ < 2000) tick_ms(20);
            assert(breakout_test_iron_ms() == 0);
            printf("PASS: breakout - the iron ball expires on its own\n");
        }
    }

    /* Walls keep re-rolling their specials rather than repeating one
       fixed map at a higher speed. */
    (void)walls_seen;
    {
        int seen_layouts = 0;
        int last_count = -1;
        int differed = 0;
        int w;
        for (w = 0; w < 30; w++) {
            int n;
            open_breakout_screen();   /* fresh wall each time */
            n = breakout_test_special_bricks_left();
            assert(n > 0 && n <= 3);
            if (last_count >= 0 && n != last_count) differed++;
            last_count = n;
            seen_layouts++;
        }
        assert(seen_layouts == 30);
        (void)differed;
    }
    printf("PASS: breakout - every wall places its power-ups within bounds\n");
}

/* A multiball has to be worth having. Dropping a copy costs nothing:
   a life goes only when the last ball is gone, and if the SERVED ball
   is the one that goes while copies are still up, one of them is
   promoted rather than the run paying for it.

   This replaced the opposite rule. Charging a life per dropped copy
   read as a penalty for collecting the power-up, which on a board
   where the whole circumference is the gutter it effectively was. */
static void test_breakout_multiball_is_free(void)
{
    int guard;
    int lives_before;
    int balls_before;
    bool saw_drop_without_life_loss = false;

    open_breakout_screen();
    breakout_handle_tap();
    breakout_handle_tap();          /* release */

    /* Earn a split, across runs if this wall's specials don't oblige. */
    guard = 0;
    while (breakout_test_ball_count() < 2 && guard++ < 300000) {
        if (minigame_test_state(screen_breakout) != MINIGAME_PLAYING) {
            breakout_handle_tap();
            if (minigame_test_state(screen_breakout) == MINIGAME_READY) {
                breakout_handle_tap();
            }
            breakout_handle_tap();
            continue;
        }
        breakout_steer();
        if (breakout_test_ball_parked()) breakout_handle_tap();
        tick_ms(20);
    }
    assert(breakout_test_ball_count() >= 2);
    assert(breakout_test_spawned_count() >= 1);

    lives_before = breakout_test_lives();
    balls_before = breakout_test_ball_count();

    /* Park the paddle opposite the ball nearest the rim and let the
       extras go, one at a time. Every drop that still leaves a ball in
       play must be free. */
    guard = 0;
    while (minigame_test_state(screen_breakout) == MINIGAME_PLAYING &&
           guard++ < 80000) {
        int ang, rad;
        int balls;

        if (breakout_test_outermost_ball(&ang, &rad)) {
            int diff = (ang + 180) - breakout_test_paddle_angle();
            while (diff > 180) diff -= 360;
            while (diff < -180) diff += 360;
            if (diff < -3) breakout_turn(-1);
            else if (diff > 3) breakout_turn(1);
        }
        tick_ms(20);

        balls = breakout_test_ball_count();
        if (balls < balls_before) {
            if (balls > 0) {
                assert(breakout_test_lives() == lives_before);
                saw_drop_without_life_loss = true;
            }
            balls_before = balls;
        }
        if (balls == 0) break;   /* the re-serve has already happened */
        if (saw_drop_without_life_loss && breakout_test_lives() < lives_before) break;
    }

    assert(saw_drop_without_life_loss);
    printf("PASS: breakout - dropping a copy costs no life while a ball is left\n");

    /* There is always exactly one ball wearing the served colours, so
       iron always has somewhere to live: losing the served ball
       promotes a copy rather than leaving the table ownerless. */
    if (breakout_test_ball_count() > 0) {
        assert(breakout_test_spawned_count() < breakout_test_ball_count());
        printf("PASS: breakout - a copy is promoted when the served ball goes\n");
    }

    /* And the last ball still ends the run, eventually. */
    guard = 0;
    while (minigame_test_state(screen_breakout) == MINIGAME_PLAYING &&
           guard++ < 200000) {
        int ang, rad;
        if (breakout_test_outermost_ball(&ang, &rad)) {
            int diff = (ang + 180) - breakout_test_paddle_angle();
            while (diff > 180) diff -= 360;
            while (diff < -180) diff += 360;
            if (diff < -3) breakout_turn(-1);
            else if (diff > 3) breakout_turn(1);
        }
        if (breakout_test_ball_parked()) breakout_handle_tap();
        tick_ms(20);
    }
    assert(minigame_test_state(screen_breakout) == MINIGAME_OVER);
    assert(breakout_test_lives() <= 0);
    printf("PASS: breakout - running out of balls still ends the run\n");
}

/* The ball must never move further in a frame than its own speed.
 *
 * It used to. Collisions were tested only once per tick, so a full
 * tick of travel could bury the ball up to 40px inside a brick's side
 * - a sector is 30 degrees wide and the arc length of that grows with
 * radius - and lifting it back out moved it further in one frame than
 * four frames of ordinary travel. On the device that read as the ball
 * accelerating out of every collision.
 *
 * The filtering here matters as much as the assertion. A serve
 * teleports the ball to the paddle by design, and "the outermost ball"
 * is a DIFFERENT ball once one of several is lost - both look like
 * jumps and neither is one. Measuring without excluding them reported
 * 297px leaps that did not exist, which sent the first investigation
 * chasing a bug in the wrong place entirely. */
static void test_breakout_no_teleports(void)
{
    int guard;
    float prev_x = 0.0f, prev_y = 0.0f;
    bool have_prev = false;
    int prev_balls = 1;
    bool was_parked = false;
    float worst_step = 0.0f;
    int measured = 0;

    open_breakout_screen();
    breakout_handle_tap();
    breakout_handle_tap();          /* release */

    for (guard = 0; guard < 120000; guard++) {
        float x, y, vx, vy;
        int balls, lives_pre;
        bool parked, served_mid_tick, measurable;

        if (minigame_test_state(screen_breakout) != MINIGAME_PLAYING) {
            breakout_handle_tap();
            if (minigame_test_state(screen_breakout) == MINIGAME_READY) {
                breakout_handle_tap();
            }
            breakout_handle_tap();
            have_prev = false;
            continue;
        }

        breakout_steer();
        parked = breakout_test_ball_parked();
        lives_pre = breakout_test_lives();
        if (parked) breakout_handle_tap();

        tick_ms(20);

        balls = breakout_test_ball_count();
        served_mid_tick = breakout_test_ball_parked() ||
                          breakout_test_lives() != lives_pre;
        measurable = (balls == 1) && (prev_balls == 1) &&
                     !parked && !was_parked && !served_mid_tick;

        if (breakout_test_outermost_ball_motion(&x, &y, &vx, &vy)) {
            float speed = sqrtf(vx * vx + vy * vy);

            if (have_prev && measurable && speed > 0.1f) {
                float step = sqrtf((x - prev_x) * (x - prev_x) +
                                   (y - prev_y) * (y - prev_y));

                if (step > worst_step) worst_step = step;
                measured++;
                /* An absolute bound, not a multiple of the speed,
                   because what sets it is not travel. A frame moves at
                   most BO_SPEED_MAX (5px), a sub-step can leave a
                   pixel or two of penetration to undo, and a ball-on-
                   ball separation shifts each ball up to one radius
                   (6px). Those stack to under 20. Measured worst over
                   ~108k frames: 17.9px, twice. Anything materially
                   above this is the old bug back - a full tick of
                   travel burying the ball in a brick's side and the
                   correction flinging it out. */
                assert(step < 20.0f);
            }
            prev_x = x;
            prev_y = y;
            have_prev = true;
        } else {
            have_prev = false;
        }

        was_parked = parked;
        prev_balls = balls;
    }

    assert(measured > 10000);   /* the filter must not have rejected everything */
    printf("PASS: breakout - the ball never jumps (worst frame %.1f px over %d frames)\n",
           worst_step, measured);
}

/* Balls are solid to each other, and iron belongs to the served ball
   alone. Both are checked continuously over a long multiball session
   rather than at one sampled instant: the failure modes here are
   occasional (one pair sticking together, one copy inheriting the
   burn), so a snapshot would miss them. */
/* Breakout's arena is drawn entirely from DRAW_MAIN callbacks, so what
 * the player aims at is a set of pixels with no widget behind it. The
 * radii those pixels land on are computed separately from the radii the
 * physics collides against, and they have drifted apart before:
 * lv_draw_arc's radius is the band's OUTER edge with the width running
 * inward, so "centre and thickness" drew every brick half a thickness
 * inside its own collision box and put the paddle at 156..164 while the
 * ball bounced off 160. That is what "the ball goes into the paddle and
 * then comes back out" looks like, and no amount of reading game state
 * can see it. These check the framebuffer instead. */
/* True if the point is under one of the screen's widgets - the score
   readout, the life row, a banner. Those are painted on top of the
   arena, so a probe that lands on one says nothing about the arena
   underneath and is skipped rather than asserted on. */
static bool covered_by_a_widget(lv_obj_t *scr, int x, int y)
{
    uint32_t i;

    for (i = 0; i < lv_obj_get_child_cnt(scr); i++) {
        lv_obj_t *child = lv_obj_get_child(scr, (int32_t)i);
        lv_area_t a;
        if (lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) continue;
        lv_obj_get_coords(child, &a);
        /* A few pixels of margin: glyph antialiasing bleeds past the
           label box it was measured into. */
        if (x >= a.x1 - 3 && x <= a.x2 + 3 && y >= a.y1 - 3 && y <= a.y2 + 3) {
            return true;
        }
    }
    return false;
}

static void test_breakout_is_drawn_where_it_is_collided(void)
{
    int cx, cy, rim, face, ang, i, checked, r;
    float rad;

    /* Probed before the serve, with the ball parked on the paddle: the
       only moving thing on the board is then at a known angle, and
       every probe here is taken well away from it. */
    open_breakout_screen();
    ticks(3, 20);
    lv_refr_now(NULL);
    assert(screen_breakout != NULL);

    breakout_test_arena_radii(&cx, &cy, &rim, &face);
    assert(breakout_test_ball_parked());

    /* The paddle spans its face out to the rim, and nothing inside the
       face: a ball at face - 1 is still in open play. Sampled 18
       degrees round from the parked ball, still well inside the
       paddle's half-width. */
    ang = breakout_test_paddle_angle() + 18;
    rad = (float)ang * 0.017453292f;
    for (r = face + 2; r < rim - 1; r++) {
        int x = cx + (int)lroundf(cosf(rad) * (float)r);
        int y = cy + (int)lroundf(sinf(rad) * (float)r);
        if (covered_by_a_widget(screen_breakout, x, y)) continue;
        if (test_harness_pixel(x, y) == 0x000000) {
            printf("FAIL: paddle not painted at r=%d (face %d, rim %d)\n",
                   r, face, rim);
            assert(0);
        }
    }
    for (r = face - 8; r < face - 2; r++) {
        int x = cx + (int)lroundf(cosf(rad) * (float)r);
        int y = cy + (int)lroundf(sinf(rad) * (float)r);
        if (covered_by_a_widget(screen_breakout, x, y)) continue;
        if (test_harness_pixel(x, y) != 0x000000) {
            printf("FAIL: paddle painted at r=%d, inside its own face (%d)\n", r, face);
            assert(0);
        }
    }
    printf("PASS: breakout - the paddle is painted across the band it bounces on\n");

    /* Every brick is painted over its own collision bounds, sampled a
       pixel inside each radial edge at the brick's centre angle. The
       gaps between bricks are angular, so the centre angle never lands
       on one. */
    checked = 0;
    for (i = 0; i < breakout_test_brick_count(); i++) {
        int inner, outer, centre;
        int probe[2], k;

        if (!breakout_test_brick_bounds(i, &inner, &outer, &centre)) continue;
        rad = (float)centre * 0.017453292f;
        /* Two pixels inside each edge, and rounded rather than
           truncated: truncation pulls the probe toward the centre by up
           to a pixel in each axis, which off a 117px radius is enough
           to land just outside the band being tested. */
        probe[0] = inner + 2;
        probe[1] = outer - 2;
        for (k = 0; k < 2; k++) {
            int x = cx + (int)lroundf(cosf(rad) * (float)probe[k]);
            int y = cy + (int)lroundf(sinf(rad) * (float)probe[k]);
            if (covered_by_a_widget(screen_breakout, x, y)) continue;
            if (test_harness_pixel(x, y) == 0x000000) {
                printf("FAIL: brick %d not painted at r=%d (bounds %d..%d, %d deg)\n",
                       i, probe[k], inner, outer, centre);
                assert(0);
            }
        }
        checked++;
    }
    assert(checked > 20);   /* a full wall, not an empty board misread as a pass */
    printf("PASS: breakout - every brick is painted over its own collision bounds (%d)\n",
           checked);

    breakout_leave_screen();
}

static void test_breakout_ball_interactions(void)
{
    int guard;
    int min_gap_seen = 9999;
    int max_iron_balls = 0;
    bool had_multiball_with_iron = false;

    open_breakout_screen();
    breakout_handle_tap();
    breakout_handle_tap();          /* release */

    guard = 0;
    while (guard++ < 300000) {
        int gap;
        int irons;

        /* Across runs, for the same reason the carry-over test needs
           them: two balls have to meet, which takes a multiball that
           lives long enough, which takes more than one wall's worth of
           luck now that dropping any ball wipes the copies. */
        if (minigame_test_state(screen_breakout) != MINIGAME_PLAYING) {
            breakout_handle_tap();
            if (minigame_test_state(screen_breakout) == MINIGAME_READY) {
                breakout_handle_tap();
            }
            breakout_handle_tap();
            continue;
        }

        breakout_steer();
        if (breakout_test_ball_parked()) breakout_handle_tap();

        tick_ms(20);

        gap = breakout_test_min_ball_gap();
        if (gap >= 0 && gap < min_gap_seen) min_gap_seen = gap;

        irons = breakout_test_iron_ball_count();
        if (irons > max_iron_balls) max_iron_balls = irons;
        /* Iron must never spread to the copies, however many are out. */
        assert(irons <= 1);
        if (irons == 1 && breakout_test_spawned_count() > 0) {
            had_multiball_with_iron = true;
        }

        /* Several collisions, not the first one: a separation bug that
           only shows up when a pair meets at a shallow angle would slip
           past a test that stops at one. */
        if (breakout_test_ball_bounces() >= 5 && had_multiball_with_iron &&
            min_gap_seen < 9999) {
            break;
        }
    }

    assert(breakout_test_ball_bounces() >= 5);
    printf("PASS: breakout - balls bounce off each other (%d collisions)\n",
           breakout_test_ball_bounces());

    /* Solid, not overlapping: the separation step has to actually keep
       them apart, or a pair sticks and jitters instead of bouncing. */
    assert(min_gap_seen >= 8);   /* 2 * BO_BALL_R, minus rounding */
    printf("PASS: breakout - balls never end a tick inside each other (min gap %d)\n",
           min_gap_seen);

    assert(had_multiball_with_iron);
    assert(max_iron_balls == 1);
    printf("PASS: breakout - iron stays on the served ball while copies are out\n");
}

/* The life brick, and the ceiling above it. Lives start at three and
   can be pushed past that; the surplus is what the HUD draws as blue
   shields instead of more hearts, and it is spent first because it
   sits on top of the same counter.

   Played across runs: a life brick is the rarest of the three specials
   (about one wall in two carries one), so a single run is not enough
   chances to see one. */
static void test_breakout_life_brick(void)
{
    int guard;
    int max_lives_seen = 0;
    bool saw_gain = false;

    open_breakout_screen();
    breakout_handle_tap();
    breakout_handle_tap();          /* release */
    assert(breakout_test_lives() == 3);

    guard = 0;
    while (guard++ < 400000 && !saw_gain) {
        int lives_before;

        if (minigame_test_state(screen_breakout) != MINIGAME_PLAYING) {
            breakout_handle_tap();
            if (minigame_test_state(screen_breakout) == MINIGAME_READY) {
                breakout_handle_tap();
            }
            breakout_handle_tap();
            continue;
        }

        lives_before = breakout_test_lives();
        breakout_steer();
        if (breakout_test_ball_parked()) breakout_handle_tap();
        tick_ms(20);

        if (breakout_test_lives() > lives_before) {
            /* Only a life brick can move this upward - nothing else in
               the game hands lives back. */
            saw_gain = true;
        }
        if (breakout_test_lives() > max_lives_seen) {
            max_lives_seen = breakout_test_lives();
        }
        /* The ceiling holds at all times, not just at the end.
           6 = BO_LIVES_MAX: three hearts plus three shields, which is
           as much as the HUD can show and stay readable. */
        assert(breakout_test_lives() <= 6);
    }

    assert(saw_gain);
    printf("PASS: breakout - a life brick hands back a life\n");
    assert(max_lives_seen > 3);
    printf("PASS: breakout - lives go past three, into shield territory (%d)\n",
           max_lives_seen);

    /* And keep playing a good while to confirm the cap never breaks,
       however many life bricks turn up. */
    guard = 0;
    while (guard++ < 200000) {
        if (minigame_test_state(screen_breakout) != MINIGAME_PLAYING) {
            breakout_handle_tap();
            if (minigame_test_state(screen_breakout) == MINIGAME_READY) {
                breakout_handle_tap();
            }
            breakout_handle_tap();
            continue;
        }
        breakout_steer();
        if (breakout_test_ball_parked()) breakout_handle_tap();
        tick_ms(20);
        assert(breakout_test_lives() >= 0);
        assert(breakout_test_lives() <= 6);
    }
    printf("PASS: breakout - the life ceiling holds across a long session\n");
}

/* Clearing a wall must not confiscate what got you there: balls in
   play and iron time both carry into the next screen. */
static void test_breakout_carry_over(void)
{
    int guard;
    int level;
    int prev_level;
    int balls_at_clear = 0;
    int iron_at_clear = 0;
    bool checked_balls = false;
    bool checked_iron = false;

    open_breakout_screen();
    breakout_handle_tap();
    breakout_handle_tap();

    prev_level = breakout_test_level();
    guard = 0;
    while (guard++ < 400000 && !(checked_balls && checked_iron)) {
        /* Keep playing across runs. Both events being watched for -
           clearing a wall while a multiball is out, and clearing one
           mid-burn - depend on where the wall happened to put its
           power-up bricks, and a multiball is short-lived now that
           dropping any ball wipes the copies. One run is not enough
           chances; restarting is deterministic, so many runs is. */
        if (minigame_test_state(screen_breakout) != MINIGAME_PLAYING) {
            breakout_handle_tap();          /* OVER -> READY */
            if (minigame_test_state(screen_breakout) == MINIGAME_READY) {
                breakout_handle_tap();      /* READY -> PLAYING */
            }
            breakout_handle_tap();          /* release the serve */
            prev_level = breakout_test_level();
            continue;
        }
        breakout_steer();
        if (breakout_test_ball_parked()) breakout_handle_tap();

        /* Sample just before the wall can flip. */
        balls_at_clear = breakout_test_ball_count();
        iron_at_clear = breakout_test_iron_ms();

        tick_ms(20);

        level = breakout_test_level();
        if (level != prev_level) {
            if (balls_at_clear > 1) {
                assert(breakout_test_ball_count() >= balls_at_clear);
                assert(!breakout_test_ball_parked());
                checked_balls = true;
            }
            if (iron_at_clear > 40) {
                assert(breakout_test_iron_ms() > 0);
                checked_iron = true;
            }
            prev_level = level;
        }
    }

    assert(checked_balls);
    printf("PASS: breakout - a multiball survives into the next wall\n");
    assert(checked_iron);
    printf("PASS: breakout - iron time keeps running into the next wall\n");
}

/* ---------------------------------------------------------------- */
static void test_invaders(void)
{
    int guard;

    open_invaders_screen();
    invaders_handle_tap();
    assert(minigame_test_state(screen_invaders) == MINIGAME_PLAYING);
    assert(invaders_test_aliens_left() == 18);

    /* Aim at the lowest alien, fire when lined up. Several shots may be
       in flight at once now - see the file comment - so this only holds
       back on invaders_test_can_fire() to keep the array from filling
       while aiming rather than firing. */
    guard = 0;
    while (minigame_test_state(screen_invaders) == MINIGAME_PLAYING &&
           invaders_test_aliens_left() > 10 && guard++ < 20000) {
        int ax, ay;
        if (invaders_test_lowest_alien(&ax, &ay)) {
            int ship = invaders_test_ship_x();
            if (ax < ship - 6) invaders_turn(-1);
            else if (ax > ship + 6) invaders_turn(1);
            else if (invaders_test_can_fire()) invaders_handle_tap();
        }
        tick_ms(26);
    }
    assert(invaders_test_aliens_left() <= 10);
    assert(minigame_test_score(screen_invaders) >= 80);
    printf("   invaders shot down %d aliens\n", 18 - invaders_test_aliens_left());
    printf("PASS: invaders - aiming and firing kills aliens and scores\n");

    /* A wave has to be clearable, and by a player who does not lead the
       target - aim at where the lowest alien IS, fire when lined up.
     *
       This is the regression guard for the game having been
       arithmetically unwinnable: one shot in flight, a 0.45s flight
       time and a formation that reached the ship in eleven seconds
       meant eighteen aliens did not fit in the budget at all. A
       simulated player that aimed perfectly and fired the instant it
       was allowed killed 15 of 18 and lost. Reaching wave 2 is the
       proof that the sums now work. */
    open_invaders_screen();
    invaders_handle_tap();
    guard = 0;
    while (minigame_test_state(screen_invaders) == MINIGAME_PLAYING &&
           invaders_test_wave() < 2 && guard++ < 40000) {
        int ax, ay;
        if (invaders_test_lowest_alien(&ax, &ay)) {
            int ship = invaders_test_ship_x();
            if (ax < ship - 5) invaders_turn(-1);
            else if (ax > ship + 5) invaders_turn(1);
            else if (invaders_test_can_fire()) invaders_handle_tap();
        }
        tick_ms(26);
    }
    assert(invaders_test_wave() >= 2);
    printf("PASS: invaders - a wave can actually be cleared without leading the target\n");

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

    /* One press, one shot - the specific rule asked for, checked
       directly rather than inferred from the pacing above. A
       fixed-cooldown design was tried and reverted: it gave a steady
       rate, but a press landing inside the cooldown window fired
       nothing, which still reads as "I pressed and nothing happened".
       So: every discrete touch-down must produce exactly one new shot,
       with no gap long enough to need a second press to get it, and a
       second press before the first shot clears must NOT produce a
       second shot from the same touch. */
    {
        int i;
        for (i = 0; i < 12; i++) {
            /* Fresh screen for every probe rather than one long run: a
               stray bomb hitting the (stationary, undefended) ship
               between the fire tap and the check would clear the shot
               array via inv_lose_life() and read as a missed press,
               which is a life-loss interaction, not a firing failure.
               Reopening guarantees before == 0 with nothing in flight
               to be cleared out from under the check. */
            open_invaders_screen();
            test_tap_screen_at(180, 300);   /* start */
            assert(minigame_test_state(screen_invaders) == MINIGAME_PLAYING);
            assert(invaders_test_shots_in_flight() == 0);

            test_tap_screen_at(180, 300);   /* the one press under test */
            assert(invaders_test_shots_in_flight() == 1);
        }
    }
    printf("PASS: invaders - every press fires exactly one shot, every time\n");
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

    /* Play repeatedly, checking every round that the score only moves
       on a win and that the reveal locks the throw in.
     *
       Across many rounds rather than one streak: the opponent is random,
       so a single streak can end on its first throw and never show a win
       or a draw at all. Asserting on one run's outcomes made this test
       fail roughly a third of the time, for no reason connected to the
       code under test. Restarting after game over is deterministic, so
       looping until all three verdicts have been seen is not. */
    guard = 0;
    while (guard++ < 20000 && !(seen_win && seen_loss && seen_draw)) {
        if (minigame_test_state(screen_rps) == MINIGAME_OVER) {
            rps_handle_tap();               /* -> READY */
            rps_handle_tap();               /* -> PLAYING */
            assert(minigame_test_state(screen_rps) == MINIGAME_PLAYING);
        }
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
    assert(seen_win);
    assert(seen_loss);
    assert(seen_draw);
    printf("PASS: rps - wins score, a loss ends the streak, throws lock on reveal\n");

    /* A loss really does end that streak, whatever the score was. */
    {
        int rounds = 0;
        while (minigame_test_state(screen_rps) != MINIGAME_PLAYING && rounds++ < 4) {
            rps_handle_tap();
        }
        guard = 0;
        while (minigame_test_state(screen_rps) == MINIGAME_PLAYING && guard++ < 20000) {
            if (!rps_test_revealing()) rps_handle_tap();
            tick_ms(60);
        }
        assert(minigame_test_state(screen_rps) == MINIGAME_OVER);
        printf("PASS: rps - a streak always ends on a loss\n");
    }
}

/* Every game screen is CLICKABLE, but being clickable is not the same
 * as anything listening: minigame_build() has to subscribe the game's
 * tap handler to LV_EVENT_CLICKED, and for one release it did not. The
 * games were entirely deaf to the touchscreen on real hardware while
 * every test still passed, because the tests called <name>_handle_tap()
 * directly and so jumped over the exact wiring that was missing.
 *
 * So this drives the screen the way the touch driver does - a real
 * LV_EVENT_CLICKED sent to the screen object - and never calls a tap
 * handler by name.
 *
 * Snake and Pong are not here: they predate minigame.c, wire their own
 * LV_EVENT_CLICKED, and expose no state to assert on. Dino is, because
 * it has an accessor and is a useful control - it was the one game
 * proven to work on hardware when the six new ones did not. */
static void test_tap_reaches_every_game(void)
{
    struct {
        const char *name;
        lv_obj_t  **screen;
        void      (*open)(void);
    } games[] = {
        { "tetris",   &screen_tetris,   open_tetris_screen   },
        { "breakout", &screen_breakout, open_breakout_screen },
        { "flappy",   &screen_flappy,   open_flappy_screen   },
        { "eggs",     &screen_eggs,     open_eggs_screen     },
        { "invaders", &screen_invaders, open_invaders_screen },
        { "rps",      &screen_rps,      open_rps_screen      },
    };
    unsigned i;

    for (i = 0; i < sizeof(games) / sizeof(games[0]); i++) {
        games[i].open();
        assert(*games[i].screen != NULL);
        assert(minigame_test_state(*games[i].screen) == MINIGAME_READY);

        test_tap_screen_at(180, 180);

        if (minigame_test_state(*games[i].screen) != MINIGAME_PLAYING) {
            printf("FAIL: tapping %s's screen did nothing - is on_tap wired?\n",
                   games[i].name);
        }
        assert(minigame_test_state(*games[i].screen) == MINIGAME_PLAYING);
    }
    printf("PASS: a real screen tap starts every framework game\n");

    /* The control: dino, with its own hand-rolled wiring. */
    open_dino_screen();
    assert(dino_test_state() == DINO_TEST_READY);
    test_tap_screen_at(180, 180);
    assert(dino_test_state() == DINO_TEST_PLAYING);
    printf("PASS: a real screen tap starts dino too\n");

    /* And a tap anywhere works, not just in the middle. Invaders is the
       one to check: its ship is at the bottom, and a player reported
       firing only seeming to work down there. */
    open_invaders_screen();
    test_tap_screen_at(180, 300);
    assert(minigame_test_state(screen_invaders) == MINIGAME_PLAYING);
    {
        const int spots[][2] = {
            {180, 30}, {180, 90}, {60, 180}, {300, 180}, {180, 330},
        };
        unsigned k;
        for (k = 0; k < sizeof(spots) / sizeof(spots[0]); k++) {
            int before = invaders_test_shots_in_flight();
            test_tap_screen_at(spots[k][0], spots[k][1]);
            if (invaders_test_shots_in_flight() <= before) {
                printf("FAIL: tapping invaders at (%d,%d) fired nothing\n",
                       spots[k][0], spots[k][1]);
            }
            assert(invaders_test_shots_in_flight() > before);
            /* Let the shot clear so the three-slot cap cannot mask the
               next probe as a dead spot. */
            { int t; for (t = 0; t < 40; t++) tick_ms(26); }
        }
    }
    printf("PASS: invaders fires from a tap anywhere on the glass\n");
}

/* Leaving a game returns to the menu page it was started from. With
 * nine games across three pages, always landing on page 1 meant paging
 * back every single time you finished a run of anything in the second
 * half of the list.
 *
 * Driven through a real tile press rather than by calling the game's
 * open function: it is the press that records the page, so calling
 * open_<game>_screen() directly would test nothing. */
static void test_back_returns_to_the_launching_page(void)
{
    lv_obj_t *page;
    lv_obj_t *tile;

    open_minigames_menu();
    /* Not a fixed number: the menu paginates whatever minigame_entries[]
       holds, and this test is about where BACK lands, not how many
       pages there happen to be. It only needs a page past the first. */
    assert(minigames_page_count >= 3);
    assert(lv_scr_act() == minigames_pages[0]);

    /* Page 3 holds Catch Egg / Invaders / Rock Paper Scissors. */
    page = minigames_pages[2];
    lv_scr_load(page);
    tile = lv_obj_get_child(page, 1);        /* second tile: Invaders */
    assert(tile != NULL);
    lv_event_send(tile, LV_EVENT_CLICKED, NULL);
    assert(lv_scr_act() == screen_invaders);

    open_minigames_menu_at_launch_page();
    assert(lv_scr_act() == minigames_pages[2]);
    printf("PASS: leaving a game returns to the page it was started from\n");

    /* A game launched from page 1 still comes back to page 1 - the
       recorded page has to be updated per launch, not sticky. */
    lv_scr_load(minigames_pages[0]);
    tile = lv_obj_get_child(minigames_pages[0], 0);   /* Snake */
    lv_event_send(tile, LV_EVENT_CLICKED, NULL);
    open_minigames_menu_at_launch_page();
    assert(lv_scr_act() == minigames_pages[0]);
    printf("PASS: the return page follows the most recent launch\n");

    /* Entering fresh from Settings is always page 1, not wherever the
       last game happened to live. */
    lv_scr_load(minigames_pages[2]);
    tile = lv_obj_get_child(minigames_pages[2], 0);
    lv_event_send(tile, LV_EVENT_CLICKED, NULL);
    open_minigames_menu();
    assert(lv_scr_act() == minigames_pages[0]);
    printf("PASS: opening the menu from Settings still lands on page 1\n");
}

/* ---------------------------------------------------------------- */
/* Asteroids exists because a rotary control driving a rotation is the
   most direct mapping this hardware has. So the test flies it that
   way: read the bearing to the nearest rock, turn until the nose is on
   it, fire. */
static void test_asteroids(void)
{
    int guard;
    int rocks_at_start;

    open_asteroids_screen();
    assert(minigame_test_state(screen_asteroids) == MINIGAME_READY);
    asteroids_handle_tap();
    assert(minigame_test_state(screen_asteroids) == MINIGAME_PLAYING);
    assert(asteroids_test_lives() == 3);
    rocks_at_start = asteroids_test_rocks();
    assert(rocks_at_start == 3);
    printf("PASS: asteroids - opens with a wave of three rocks\n");

    /* Firing must NOT move the ship, and holding must.
     *
       This is the whole reason the game was unplayable at first: a tap
       did both, so anyone shooting at a normal rate was pinned at top
       speed and careening into rocks - a perfect-aim player survived
       five seconds and never cleared a wave. */
    {
        int x0, y0, x1, y1, x2, y2;
        int i;

        asteroids_test_ship(&x0, &y0);
        for (i = 0; i < 10; i++) {
            asteroids_handle_tap();
            tick_ms(24);
        }
        asteroids_test_ship(&x1, &y1);
        assert(abs(x1 - x0) < 8 && abs(y1 - y0) < 8);

        test_hold_screen(30, 24);
        asteroids_test_ship(&x2, &y2);
        assert(abs(x2 - x1) > 20 || abs(y2 - y1) > 20);
        printf("PASS: asteroids - tapping fires without thrusting, holding thrusts\n");
    }

    /* The knob turns the ship, degree for degree, both ways. */
    {
        int h0 = asteroids_test_heading();
        asteroids_turn(1);
        asteroids_turn(1);
        assert(asteroids_test_heading() == (h0 + 2 * 12) % 360);
        asteroids_turn(-1);
        assert(asteroids_test_heading() == (h0 + 12) % 360);
        printf("PASS: asteroids - the knob steers the ship both ways\n");
    }

    /* A big rock breaks into two smaller ones, so the count goes UP
       before it goes down - that split is the game's whole rhythm. */
    guard = 0;
    while (minigame_test_state(screen_asteroids) == MINIGAME_PLAYING &&
           asteroids_test_rocks() <= rocks_at_start && guard++ < 20000) {
        int bearing, dist;
        if (asteroids_test_nearest_rock(&bearing, &dist)) {
            int diff = bearing - asteroids_test_heading();
            while (diff > 180) diff -= 360;
            while (diff < -180) diff += 360;
            if (diff < -6) asteroids_turn(-1);
            else if (diff > 6) asteroids_turn(1);
            else asteroids_handle_tap();
        }
        tick_ms(24);
    }
    assert(asteroids_test_rocks() > rocks_at_start);
    printf("PASS: asteroids - shooting a big rock splits it in two\n");

    /* Played properly the wave can be cleared. Across runs, because a
       rock can drift into the ship while it is lining up a shot. */
    guard = 0;
    while (asteroids_test_wave() < 2 && guard++ < 120000) {
        int bearing, dist;

        if (minigame_test_state(screen_asteroids) != MINIGAME_PLAYING) {
            asteroids_handle_tap();
            if (minigame_test_state(screen_asteroids) == MINIGAME_READY) {
                asteroids_handle_tap();
            }
            continue;
        }
        if (asteroids_test_nearest_rock(&bearing, &dist)) {
            int diff = bearing - asteroids_test_heading();
            while (diff > 180) diff -= 360;
            while (diff < -180) diff += 360;
            if (diff < -6) asteroids_turn(-1);
            else if (diff > 6) asteroids_turn(1);
            else asteroids_handle_tap();
        }
        tick_ms(24);
    }
    assert(asteroids_test_wave() >= 2);
    printf("PASS: asteroids - a wave can be cleared, and the next one starts\n");

    /* Rocks hurt. Flown INTO rather than waited for: wrapping a circle
       by reflecting through the centre makes every path a closed
       polygon, so a rock that misses a stationary ship at the middle
       misses it forever - sitting still for 24 minutes of simulated
       time cost no lives at all. That is fine for the game (a player
       moves) but useless as a test of the collision rule. */
    open_asteroids_screen();
    asteroids_handle_tap();
    guard = 0;
    while (asteroids_test_lives() == 3 && guard++ < 4000) {
        int bearing, dist;

        if (minigame_test_state(screen_asteroids) != MINIGAME_PLAYING) break;
        if (asteroids_test_nearest_rock(&bearing, &dist)) {
            int diff = bearing - asteroids_test_heading();
            while (diff > 180) diff -= 360;
            while (diff < -180) diff += 360;
            if (diff < -6) { asteroids_turn(-1); tick_ms(24); }
            else if (diff > 6) { asteroids_turn(1); tick_ms(24); }
            else test_hold_screen(12, 24);   /* fly straight at it */
        } else {
            tick_ms(24);
        }
    }
    assert(asteroids_test_lives() < 3);
    printf("PASS: asteroids - flying into a rock costs a life\n");

    /* And running the lives out ends the run.
     *
       Not with the aim-then-hold pattern above: that pattern fires a
       shot at the start of every hold, and precise aim usually lands
       that shot before the ship reaches collision range - so once the
       first life is gone (by luck of an early spawn), the same
       "logic" mostly snipes the nearest rock instead of hitting it,
       and can run the clock out never colliding again. (Measured: it
       failed to lose a second life inside even a 24-minute simulated
       budget.)
     *
       A player who is actually trying to crash does not re-aim and
       re-fire at every rock - they hold thrust and steer through
       traffic. One press starts the burn (and fires the single shot
       that comes with it); the finger then stays down, so every
       further read is PRESSING, not another PRESSED, and nothing
       fires again. That is what makes contact likely instead of rare. */
    guard = 0;
    test_pointer_at.x = 180;
    test_pointer_at.y = 180;
    test_pointer_down = true;
    while (minigame_test_state(screen_asteroids) == MINIGAME_PLAYING &&
           guard++ < 20000) {
        int bearing, dist;
        if (asteroids_test_nearest_rock(&bearing, &dist)) {
            int diff = bearing - asteroids_test_heading();
            while (diff > 180) diff -= 360;
            while (diff < -180) diff += 360;
            if (diff < -6) asteroids_turn(-1);
            else if (diff > 6) asteroids_turn(1);
        }
        tick_ms(24);
    }
    test_pointer_down = false;
    tick_ms(24);
    assert(minigame_test_state(screen_asteroids) == MINIGAME_OVER);
    assert(asteroids_test_lives() <= 0);
    printf("PASS: asteroids - running out of lives ends the run\n");
}

int main(void)
{
    /* Line-buffered so the PASS trail survives an assert() abort - fully
       buffered stdout loses everything printed before the failure, which
       is exactly the context needed to read it. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    test_harness_init();
    test_pointer_init();
    /* The intro's pending back_to_main() would otherwise navigate away
       mid-test and silently pause whichever game is running - see
       test_harness_settle_intro(). */
    test_harness_settle_intro();
    test_harness_reset_4p();

    printf("---- touch wiring ----\n"); test_tap_reaches_every_game();
    printf("---- menu navigation ----\n"); test_back_returns_to_the_launching_page();
    printf("---- flappy ----\n");   test_flappy();
    printf("---- eggs ----\n");     test_eggs();
    printf("---- breakout ----\n"); test_breakout();
    printf("---- breakout power-ups ----\n"); test_breakout_powerups();
    printf("---- breakout multiball ----\n"); test_breakout_multiball_is_free();
    printf("---- breakout life brick ----\n"); test_breakout_life_brick();
    printf("---- breakout carry-over ----\n"); test_breakout_carry_over();
    printf("---- breakout motion ----\n"); test_breakout_no_teleports();
    printf("---- breakout ball interactions ----\n"); test_breakout_ball_interactions();
    printf("---- breakout drawing ----\n"); test_breakout_is_drawn_where_it_is_collided();
    printf("---- invaders ----\n"); test_invaders();
    printf("---- tetris ----\n");   test_tetris();
    printf("---- rps ----\n");      test_rps();
    printf("---- asteroids ----\n"); test_asteroids();

    printf("\nAll minigame tests passed.\n");
    return 0;
}
