#include "breakout.h"
#include "minigame.h"
#include "esp_random.h"
#include <math.h>
#include <string.h>

/* Breakout on a round panel. The playfield is the largest rectangle
 * that stays inside the visible circle across its whole height: at the
 * paddle's y the chord is narrower than at the brick rows', so the
 * field's width is set by the LOWER edge - hence 224px wide rather than
 * the 254 an inscribed square would suggest. Anything wider would have
 * the paddle's ends disappear under the bezel exactly when the player
 * most needs to see them.
 *
 * Three balls, and clearing the wall starts a faster level rather than
 * ending the run, so a good player's score keeps climbing.
 *
 * Two power-up bricks are scattered through every wall:
 *   MULTI - splits every ball in play
 *   IRON  - the ball burns through bricks without bouncing, briefly
 * Both are earned by hitting a brick you can see and choose to aim at,
 * rather than dropping as capsules that have to be caught: the knob
 * moves the paddle and nothing else, so a "go and catch it" pick-up
 * would compete with the ball for the only control the player has.
 *
 * The rules around them, which are what give them weight:
 *
 * - Split balls are drawn differently from the served one, because
 *   they are not equivalent to it: dropping ANY ball costs a life AND
 *   wipes every split ball still in play. Multiball is a burst of
 *   scoring you have to actively survive, not free lives.
 * - Balls and iron time both carry across a cleared wall. Finishing a
 *   screen on a multiball, or with the iron ball still burning, starts
 *   the next one that way - so the reward is not confiscated by the
 *   thing it helped you achieve. Iron ends on its timer, or on a
 *   re-serve.
 * - The iron brick itself is passed straight through, keeping the
 *   ball's trajectory. It is the one brick that never deflects you.
 * - Iron is a modifier on the SERVED ball, not on the table: split
 *   balls never burn. Losing the served ball loses the iron with it
 *   and re-serves.
 * - Balls collide with each other, so a crowded table plays like a
 *   table rather than like several independent games sharing a screen. */

#define BO_TICK_MS        20

#define BO_FIELD_LEFT     68
#define BO_FIELD_RIGHT   292
#define BO_FIELD_TOP      64
#define BO_FIELD_BOTTOM  318

#define BO_PADDLE_Y      300
#define BO_PADDLE_W       52
#define BO_PADDLE_H        9
#define BO_PADDLE_STEP    13   /* px per knob detent */

#define BO_BALL_R          5
/* Pacier than the first cut of this game, which played sluggishly once
   the wall thinned out: the opening serve is roughly where the old
   version's third level was, and the ceiling is high enough that a long
   run genuinely gets hard to track. */
#define BO_SPEED_START  4.10f
#define BO_SPEED_LEVEL  0.55f  /* added per cleared wall */
#define BO_SPEED_MAX    8.00f

#define BO_COLS            7
#define BO_ROWS            4
#define BO_BRICK_W        32   /* (BO_FIELD_RIGHT - BO_FIELD_LEFT) / BO_COLS */
#define BO_BRICK_H        15
#define BO_BRICK_TOP      76
#define BO_BRICK_GAP       2
#define BO_BRICK_COUNT   (BO_COLS * BO_ROWS)
#define BO_BRICK_POINTS   10
/* Power-up bricks are worth more: they sit wherever the wall put them,
   so reaching one is usually a deliberate detour. */
#define BO_SPECIAL_POINTS 25

#define BO_LIVES           3

/* Up to this many power-up bricks per wall, placed at random. Three in
   twenty-eight is often enough to shape how a wall gets attacked
   without making the specials the only thing anyone aims at. */
#define BO_SPECIAL_MAX     3

#define BO_BALL_MAX        6
#define BO_IRON_MS     10000
#define BO_IRON_TICKS  (BO_IRON_MS / BO_TICK_MS)

typedef enum {
    BO_BRICK_EMPTY = 0,
    BO_BRICK_NORMAL,
    BO_BRICK_MULTI,
    BO_BRICK_IRON,
} bo_brick_t;

typedef struct {
    bool  active;
    bool  spawned;   /* born from a multiball split, not served */
    float x, y;
    float vx, vy;
} bo_ball_t;

lv_obj_t *screen_breakout = NULL;

static int   bo_paddle_x;      /* centre */
static bo_ball_t bo_balls[BO_BALL_MAX];
static float bo_speed;
static uint8_t bo_bricks[BO_BRICK_COUNT];
static int   bo_bricks_left;
static int   bo_lives;
static int   bo_level;
static bool  bo_ball_stuck;    /* the serve rides the paddle until released */
static int   bo_iron_ticks;    /* iron ball time left, in ticks */
static int   bo_ball_bounces; /* ball-on-ball collisions this run (tests) */
static int   bo_flash;         /* ticks of power-up pickup flash left */

static void breakout_reset(minigame_t *g);
static void breakout_tick(minigame_t *g);
static void breakout_draw(lv_event_t *e);

static minigame_t breakout_game = {
    .screen   = &screen_breakout,
    .score_id = GAME_SCORE_BREAKOUT,
    .hint     = STR_BREAKOUT_HINT,
    .tick_ms  = BO_TICK_MS,
    .pausable = true,
    .on_reset = breakout_reset,
    .on_tick  = breakout_tick,
    .on_draw  = breakout_draw,
    .on_tap   = breakout_handle_tap,
};

static int bo_active_balls(void)
{
    int i, n = 0;
    for (i = 0; i < BO_BALL_MAX; i++) if (bo_balls[i].active) n++;
    return n;
}

/* Fills the wall and scatters the power-up bricks through it. Positions
   are redrawn for every wall, so clearing one does not hand the player
   the same map again at a higher speed. */
static void bo_fill_wall(void)
{
    int i;
    int placed;

    for (i = 0; i < BO_BRICK_COUNT; i++) bo_bricks[i] = BO_BRICK_NORMAL;
    bo_bricks_left = BO_BRICK_COUNT;

    for (placed = 0; placed < BO_SPECIAL_MAX; placed++) {
        int tries;
        /* Retry rather than reject-and-give-up: with 28 slots and at
           most 3 specials a free slot is always minutes away, and a
           bounded loop can't hang if that ever stops being true. */
        for (tries = 0; tries < 20; tries++) {
            int idx = (int)(esp_random() % BO_BRICK_COUNT);
            if (bo_bricks[idx] != BO_BRICK_NORMAL) continue;
            bo_bricks[idx] = (esp_random() % 2U) ? BO_BRICK_MULTI : BO_BRICK_IRON;
            break;
        }
    }
}

/* Parks one ball on the paddle and aims it up at a slight angle. The
   sign is random so a player can't memorise the opening trajectory. */
static void bo_serve(void)
{
    memset(bo_balls, 0, sizeof(bo_balls));
    bo_balls[0].active = true;
    bo_balls[0].spawned = false;
    bo_balls[0].x = (float)bo_paddle_x;
    bo_balls[0].y = (float)(BO_PADDLE_Y - BO_BALL_R - 1);
    bo_balls[0].vx = ((esp_random() % 2U) ? 0.62f : -0.62f) * bo_speed;
    bo_balls[0].vy = -0.78f * bo_speed;
    bo_ball_stuck = true;
    bo_iron_ticks = 0;
}

static void breakout_reset(minigame_t *g)
{
    (void)g;
    bo_paddle_x = (BO_FIELD_LEFT + BO_FIELD_RIGHT) / 2;
    bo_speed = BO_SPEED_START;
    bo_level = 1;
    bo_lives = BO_LIVES;
    bo_flash = 0;
    bo_ball_bounces = 0;
    bo_fill_wall();
    bo_serve();
}

static void bo_brick_rect(int idx, int *x1, int *y1, int *x2, int *y2)
{
    int col = idx % BO_COLS;
    int row = idx / BO_COLS;
    *x1 = BO_FIELD_LEFT + col * BO_BRICK_W + BO_BRICK_GAP;
    *y1 = BO_BRICK_TOP + row * BO_BRICK_H + BO_BRICK_GAP;
    *x2 = BO_FIELD_LEFT + (col + 1) * BO_BRICK_W - BO_BRICK_GAP;
    *y2 = BO_BRICK_TOP + (row + 1) * BO_BRICK_H - BO_BRICK_GAP;
}

/* Every ball in play splits in two. The copy leaves at a mirrored
   horizontal velocity so the pair fans out instead of travelling as one
   indistinguishable dot. */
static void bo_split_balls(void)
{
    /* Which balls existed BEFORE the split, snapshotted because the
       copies go into free slots that this same loop would otherwise
       reach and split again - turning one brick into a full array
       rather than doubling what was in play. */
    bool was_active[BO_BALL_MAX];
    int count;
    int i;

    for (i = 0; i < BO_BALL_MAX; i++) was_active[i] = bo_balls[i].active;
    count = bo_active_balls();

    for (i = 0; i < BO_BALL_MAX && count < BO_BALL_MAX; i++) {
        int slot;
        if (!was_active[i]) continue;

        for (slot = 0; slot < BO_BALL_MAX; slot++) {
            float mag;

            if (bo_balls[slot].active) continue;

            /* The pair has to visibly fan apart. A ball struck near the
               middle of the paddle leaves with vx close to zero, and
               copying that with the sign flipped gives two balls on
               almost the same line - the power-up would look like it
               did nothing. So the split enforces a minimum horizontal
               speed and sends one each way. */
            mag = fabsf(bo_balls[i].vx);
            if (mag < 0.35f * bo_speed) mag = 0.35f * bo_speed;

            bo_balls[slot] = bo_balls[i];
            bo_balls[slot].spawned = true;
            bo_balls[i].vx    = (bo_balls[i].vx >= 0.0f) ?  mag : -mag;
            bo_balls[slot].vx = (bo_balls[i].vx >= 0.0f) ? -mag :  mag;
            /* Nudge upward as well, so a split at a shallow angle does
               not leave both balls crawling along the same line. */
            bo_balls[slot].vy = -fabsf(bo_balls[i].vy);
            count++;
            break;
        }
    }
}

static void bo_collect_special(minigame_t *g, bo_brick_t kind)
{
    bo_flash = 8;
    if (kind == BO_BRICK_MULTI) {
        bo_split_balls();
    } else {
        /* Re-triggering refreshes the full duration rather than adding
           to it - otherwise a lucky run of iron bricks could bank most
           of a level's worth of invulnerability. */
        bo_iron_ticks = BO_IRON_TICKS;
    }
    minigame_add_score(g, BO_SPECIAL_POINTS - BO_BRICK_POINTS);
}

static bool bo_ball_overlaps_brick(const bo_ball_t *b, int idx,
                                   int *overlap_x, int *overlap_y)
{
    int x1, y1, x2, y2;
    int bx = (int)b->x;
    int by = (int)b->y;

    bo_brick_rect(idx, &x1, &y1, &x2, &y2);
    if (bx + BO_BALL_R < x1 || bx - BO_BALL_R > x2) return false;
    if (by + BO_BALL_R < y1 || by - BO_BALL_R > y2) return false;

    if (overlap_x != NULL)
        *overlap_x = (b->vx > 0.0f) ? (bx + BO_BALL_R - x1) : (x2 - bx + BO_BALL_R);
    if (overlap_y != NULL)
        *overlap_y = (b->vy > 0.0f) ? (by + BO_BALL_R - y1) : (y2 - by + BO_BALL_R);
    return true;
}

static void bo_break_brick(minigame_t *g, int idx)
{
    bo_brick_t kind = (bo_brick_t)bo_bricks[idx];

    bo_bricks[idx] = BO_BRICK_EMPTY;
    bo_bricks_left--;
    minigame_add_score(g, BO_BRICK_POINTS);
    if (kind == BO_BRICK_MULTI || kind == BO_BRICK_IRON) {
        bo_collect_special(g, kind);
    }
}

/* Iron ball: burn through every brick touched this tick and keep going
   straight. Normal ball: take the first brick and bounce, choosing the
   axis by which way the ball has penetrated further - that is what stops
   a corner clip reversing the wrong way and appearing to pass through
   the wall. */
/* Iron rides the served ball and nothing else. It is a modifier on
   your ball, not a property of the table: a split ball burning through
   the wall too would multiply the effect by however many copies happen
   to be out, and the copies are already the cheap half of a multiball. */
static bool bo_ball_is_iron(const bo_ball_t *b)
{
    return bo_iron_ticks > 0 && !b->spawned;
}

static void bo_hit_bricks(minigame_t *g, bo_ball_t *b)
{
    int i;

    if (bo_ball_is_iron(b)) {
        for (i = 0; i < BO_BRICK_COUNT; i++) {
            if (bo_bricks[i] == BO_BRICK_EMPTY) continue;
            if (!bo_ball_overlaps_brick(b, i, NULL, NULL)) continue;
            bo_break_brick(g, i);
        }
        return;
    }

    for (i = 0; i < BO_BRICK_COUNT; i++) {
        int overlap_x, overlap_y;

        if (bo_bricks[i] == BO_BRICK_EMPTY) continue;
        if (!bo_ball_overlaps_brick(b, i, &overlap_x, &overlap_y)) continue;

        /* The iron brick is the one brick that never deflects the ball:
           hitting it hands you the iron ball and you carry straight on
           through the hole you just made, on the trajectory you had.
           Bouncing off the brick that grants pass-through would read as
           the power-up arriving one beat late. */
        if (bo_bricks[i] == BO_BRICK_IRON) {
            bo_break_brick(g, i);
            return;
        }

        if (overlap_x < overlap_y) b->vx = -b->vx;
        else                       b->vy = -b->vy;
        bo_break_brick(g, i);
        return;
    }
}

/* Elastic bounce between two balls of equal mass: exchange the part of
   their relative velocity that lies along the line joining them, leave
   the perpendicular part alone.
 *
   Each ball is then rescaled back to the speed it arrived with. A
   textbook exchange conserves the pair's energy but not each ball's
   own, so without this a glancing hit can leave one ball crawling and
   another screaming - and a crawling ball on a 20ms tick is a ball the
   player has to wait for. */
static void bo_bounce_pair(bo_ball_t *a, bo_ball_t *b)
{
    float dx = b->x - a->x;
    float dy = b->y - a->y;
    float dist = sqrtf(dx * dx + dy * dy);
    float nx, ny;
    float rel;
    float mag_a, mag_b, mag;
    float overlap;

    /* Exactly concentric: no line to bounce along, so nudge them apart
       on an arbitrary axis and let the next tick sort it out. */
    if (dist < 0.0001f) {
        a->x -= (float)BO_BALL_R;
        b->x += (float)BO_BALL_R;
        return;
    }

    nx = dx / dist;
    ny = dy / dist;

    rel = (a->vx - b->vx) * nx + (a->vy - b->vy) * ny;
    if (rel > 0.0f) {
        /* Approaching: exchange the along-the-normal component. */
        mag_a = sqrtf(a->vx * a->vx + a->vy * a->vy);
        mag_b = sqrtf(b->vx * b->vx + b->vy * b->vy);

        a->vx -= rel * nx;
        a->vy -= rel * ny;
        b->vx += rel * nx;
        b->vy += rel * ny;

        mag = sqrtf(a->vx * a->vx + a->vy * a->vy);
        if (mag > 0.0001f) { a->vx = a->vx / mag * mag_a; a->vy = a->vy / mag * mag_a; }
        mag = sqrtf(b->vx * b->vx + b->vy * b->vy);
        if (mag > 0.0001f) { b->vx = b->vx / mag * mag_b; b->vy = b->vy / mag * mag_b; }
    }

    /* Separate them whether or not they were approaching: two balls
       left overlapping would re-trigger this every tick and stick
       together instead of bouncing apart. */
    overlap = (float)(2 * BO_BALL_R) - dist;
    if (overlap > 0.0f) {
        a->x -= nx * overlap * 0.5f;
        a->y -= ny * overlap * 0.5f;
        b->x += nx * overlap * 0.5f;
        b->y += ny * overlap * 0.5f;
    }
}

static void bo_clamp_to_field(bo_ball_t *b)
{
    if (b->x < (float)(BO_FIELD_LEFT + BO_BALL_R))
        b->x = (float)(BO_FIELD_LEFT + BO_BALL_R);
    if (b->x > (float)(BO_FIELD_RIGHT - BO_BALL_R))
        b->x = (float)(BO_FIELD_RIGHT - BO_BALL_R);
    if (b->y < (float)(BO_FIELD_TOP + BO_BALL_R))
        b->y = (float)(BO_FIELD_TOP + BO_BALL_R);
}

/* All pairs, which at six balls is fifteen checks - not worth being
   clever about on a table this small. */
static void bo_collide_balls(void)
{
    int i, j;

    for (i = 0; i < BO_BALL_MAX; i++) {
        if (!bo_balls[i].active) continue;
        for (j = i + 1; j < BO_BALL_MAX; j++) {
            float dx, dy;
            if (!bo_balls[j].active) continue;
            dx = bo_balls[j].x - bo_balls[i].x;
            dy = bo_balls[j].y - bo_balls[i].y;
            if (dx * dx + dy * dy > (float)(4 * BO_BALL_R * BO_BALL_R)) continue;
            bo_bounce_pair(&bo_balls[i], &bo_balls[j]);
            bo_ball_bounces++;
        }
    }

    /* The separation above can shove a ball through a wall; putting it
       back is cheaper and steadier than solving the two constraints
       together. */
    for (i = 0; i < BO_BALL_MAX; i++) {
        if (bo_balls[i].active) bo_clamp_to_field(&bo_balls[i]);
    }
}

static void bo_bounce_off_paddle(bo_ball_t *b)
{
    int half = BO_PADDLE_W / 2;
    float offset = (b->x - (float)bo_paddle_x) / (float)half; /* -1..1 */

    if (offset < -1.0f) offset = -1.0f;
    if (offset >  1.0f) offset =  1.0f;

    /* Where the ball lands on the paddle sets the outgoing angle - the
       control that makes Breakout a game of aim rather than reflexes.
       vy keeps a floor so a near-edge hit can't come off so flat that
       the ball skims sideways forever. */
    b->vx = offset * 0.86f * bo_speed;
    b->vy = -bo_speed;
    if (b->vy > -0.55f * bo_speed) b->vy = -0.55f * bo_speed;
    b->y = (float)(BO_PADDLE_Y - BO_BALL_R - 1);
}

static void breakout_tick(minigame_t *g)
{
    int half = BO_PADDLE_W / 2;
    bool lost = false;
    int i;

    if (bo_flash > 0) bo_flash--;
    if (bo_iron_ticks > 0) bo_iron_ticks--;

    if (bo_ball_stuck) {
        /* Riding the paddle: the player aims, then any tap releases. */
        bo_balls[0].x = (float)bo_paddle_x;
        return;
    }

    for (i = 0; i < BO_BALL_MAX; i++) {
        bo_ball_t *b = &bo_balls[i];

        if (!b->active) continue;

        b->x += b->vx;
        b->y += b->vy;

        if (b->x - BO_BALL_R < BO_FIELD_LEFT) {
            b->x = (float)(BO_FIELD_LEFT + BO_BALL_R);
            b->vx = -b->vx;
        } else if (b->x + BO_BALL_R > BO_FIELD_RIGHT) {
            b->x = (float)(BO_FIELD_RIGHT - BO_BALL_R);
            b->vx = -b->vx;
        }
        if (b->y - BO_BALL_R < BO_FIELD_TOP) {
            b->y = (float)(BO_FIELD_TOP + BO_BALL_R);
            b->vy = -b->vy;
        }

        bo_hit_bricks(g, b);

        if (b->vy > 0.0f &&
            b->y + BO_BALL_R >= BO_PADDLE_Y &&
            b->y - BO_BALL_R <= BO_PADDLE_Y + BO_PADDLE_H &&
            b->x >= (float)(bo_paddle_x - half) &&
            b->x <= (float)(bo_paddle_x + half)) {
            bo_bounce_off_paddle(b);
        }

        if (b->y - BO_BALL_R > BO_FIELD_BOTTOM) {
            b->active = false;
            lost = true;
        }
    }

    /* Balls interact only after every one of them has moved this tick:
       resolving a pair mid-sweep would have the second ball collide
       against the first's already-updated position and the third
       against a mixture, making the outcome depend on array order. */
    if (bo_active_balls() > 1) bo_collide_balls();

    /* Dropping any ball costs a life AND clears every split ball still
       in play. Extra balls are not extra lives: a multiball is a window
       to score in that you have to actually survive, and letting one
       slip closes it.
       One life per tick however many balls fell together - two balls
       crossing the line in the same frame is one mistake, not two. */
    if (lost) {
        for (i = 0; i < BO_BALL_MAX; i++) {
            if (bo_balls[i].spawned) bo_balls[i].active = false;
        }
        if (--bo_lives <= 0) {
            minigame_over(g);
            return;
        }
        /* Only re-serve when nothing is left: if the served ball is
           still up there, play simply continues without it. */
        if (bo_active_balls() == 0) {
            bo_serve();
            return;
        }
    }

    if (bo_bricks_left == 0) {
        /* Cleared: a fresh, faster wall rather than a win screen, so one
           good run keeps scoring.
           Deliberately NOT a re-serve - the balls in play and whatever
           iron time is left both carry into the new wall. Clearing a
           screen on a multiball, or mid-burn, should start the next one
           that way rather than have the reward taken back. */
        bo_level++;
        if (bo_speed < BO_SPEED_MAX) bo_speed += BO_SPEED_LEVEL;
        bo_fill_wall();
    }
}

void breakout_turn(int dir)
{
    if (minigame_handle_turn_start(&breakout_game)) return;

    bo_paddle_x += dir * BO_PADDLE_STEP;
    if (bo_paddle_x < BO_FIELD_LEFT + BO_PADDLE_W / 2)
        bo_paddle_x = BO_FIELD_LEFT + BO_PADDLE_W / 2;
    if (bo_paddle_x > BO_FIELD_RIGHT - BO_PADDLE_W / 2)
        bo_paddle_x = BO_FIELD_RIGHT - BO_PADDLE_W / 2;
    lv_obj_invalidate(screen_breakout);
}

void breakout_handle_tap(void)
{
    if (minigame_handle_tap(&breakout_game)) return;
    /* Mid-run a tap launches a parked ball; with the ball already in
       play there is nothing else a tap could mean, so it pauses. */
    if (bo_ball_stuck) {
        bo_ball_stuck = false;
        return;
    }
    minigame_toggle_pause(&breakout_game);
}

void breakout_leave_screen(void) { minigame_leave(&breakout_game); }
void build_breakout_screen(void) { minigame_build(&breakout_game); }
void open_breakout_screen(void)  { minigame_open(&breakout_game); }

// ---------- test accessors ----------
int breakout_test_paddle_x(void)    { return bo_paddle_x; }
int breakout_test_lives(void)       { return bo_lives; }
int breakout_test_bricks_left(void) { return bo_bricks_left; }
int breakout_test_level(void)       { return bo_level; }
int breakout_test_ball_count(void)  { return bo_active_balls(); }
bool breakout_test_ball_parked(void){ return bo_ball_stuck; }
int breakout_test_iron_ms(void)     { return bo_iron_ticks * BO_TICK_MS; }

int breakout_test_special_bricks_left(void)
{
    int i, n = 0;
    for (i = 0; i < BO_BRICK_COUNT; i++) {
        if (bo_bricks[i] == BO_BRICK_MULTI || bo_bricks[i] == BO_BRICK_IRON) n++;
    }
    return n;
}

int breakout_test_ball_bounces(void) { return bo_ball_bounces; }

/* How many balls in play are actually burning through bricks. By
   design this is never more than one. */
int breakout_test_iron_ball_count(void)
{
    int i, n = 0;
    for (i = 0; i < BO_BALL_MAX; i++) {
        if (bo_balls[i].active && bo_ball_is_iron(&bo_balls[i])) n++;
    }
    return n;
}

/* Closest two ball centres in play, or -1 with fewer than two balls.
   Never meaningfully below 2*BO_BALL_R once collisions have run. */
int breakout_test_min_ball_gap(void)
{
    int i, j;
    float best = -1.0f;

    for (i = 0; i < BO_BALL_MAX; i++) {
        if (!bo_balls[i].active) continue;
        for (j = i + 1; j < BO_BALL_MAX; j++) {
            float dx, dy, d;
            if (!bo_balls[j].active) continue;
            dx = bo_balls[j].x - bo_balls[i].x;
            dy = bo_balls[j].y - bo_balls[i].y;
            d = sqrtf(dx * dx + dy * dy);
            if (best < 0.0f || d < best) best = d;
        }
    }
    return (best < 0.0f) ? -1 : (int)best;
}

int breakout_test_spawned_count(void)
{
    int i, n = 0;
    for (i = 0; i < BO_BALL_MAX; i++) {
        if (bo_balls[i].active && bo_balls[i].spawned) n++;
    }
    return n;
}

int breakout_test_ball_spread(void)
{
    int i, j, spread = 0;

    for (i = 0; i < BO_BALL_MAX; i++) {
        if (!bo_balls[i].active) continue;
        for (j = i + 1; j < BO_BALL_MAX; j++) {
            int d;
            if (!bo_balls[j].active) continue;
            d = (int)(bo_balls[i].x - bo_balls[j].x);
            if (d < 0) d = -d;
            if (d > spread) spread = d;
        }
    }
    return spread;
}

bool breakout_test_lowest_ball(int *x, int *y)
{
    int i;
    float best = -1.0f;
    int best_x = 0;

    for (i = 0; i < BO_BALL_MAX; i++) {
        if (!bo_balls[i].active) continue;
        if (bo_balls[i].y > best) {
            best = bo_balls[i].y;
            best_x = (int)bo_balls[i].x;
        }
    }
    if (best < 0.0f) return false;
    if (x != NULL) *x = best_x;
    if (y != NULL) *y = (int)best;
    return true;
}

// ---------- drawing ----------
static void bo_fill(lv_draw_ctx_t *ctx, lv_draw_rect_dsc_t *dsc,
                    int x1, int y1, int x2, int y2)
{
    lv_area_t a;
    a.x1 = (lv_coord_t)x1; a.y1 = (lv_coord_t)y1;
    a.x2 = (lv_coord_t)x2; a.y2 = (lv_coord_t)y2;
    lv_draw_rect(ctx, dsc, &a);
}

/* Power-up bricks have to be identifiable at a glance from across a
   table, so each carries a mark rather than relying on colour alone:
   two dots for the ball that splits in two, a solid core for iron. */
static void bo_draw_brick_mark(lv_draw_ctx_t *ctx, bo_brick_t kind,
                               int x1, int y1, int x2, int y2)
{
    lv_draw_rect_dsc_t mark;
    int cy = (y1 + y2) / 2;
    int cx = (x1 + x2) / 2;

    lv_draw_rect_dsc_init(&mark);
    mark.bg_opa = LV_OPA_COVER;

    if (kind == BO_BRICK_MULTI) {
        mark.bg_color = lv_color_hex(0x00303A);
        mark.radius = LV_RADIUS_CIRCLE;
        bo_fill(ctx, &mark, cx - 8, cy - 3, cx - 2, cy + 3);
        bo_fill(ctx, &mark, cx + 2, cy - 3, cx + 8, cy + 3);
    } else {
        mark.bg_color = lv_color_hex(0x263238);
        mark.radius = 2;
        bo_fill(ctx, &mark, cx - 5, cy - 3, cx + 5, cy + 3);
    }
}

static void breakout_draw(lv_event_t *e)
{
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_draw_rect_dsc_t wall, brick, paddle, ball, life;
    /* Row colour doubles as depth cue: the back row is the one worth
       reaching, so it gets the hottest colour. */
    static const uint32_t row_color[BO_ROWS] = {
        0xEF5350, 0xFFA726, 0x66BB6A, 0x42A5F5,
    };
    bool iron = bo_iron_ticks > 0;
    int i;

    lv_draw_rect_dsc_init(&wall);
    wall.bg_opa = LV_OPA_TRANSP;
    wall.border_color = iron ? lv_color_hex(0xFF7043) : lv_color_hex(0x263238);
    wall.border_width = 2;
    wall.border_opa = LV_OPA_COVER;
    wall.radius = 4;
    bo_fill(ctx, &wall, BO_FIELD_LEFT - 3, BO_FIELD_TOP - 3,
            BO_FIELD_RIGHT + 3, BO_FIELD_BOTTOM + 3);

    lv_draw_rect_dsc_init(&brick);
    brick.bg_opa = LV_OPA_COVER;
    brick.radius = 2;
    for (i = 0; i < BO_BRICK_COUNT; i++) {
        int x1, y1, x2, y2;
        bo_brick_t kind = (bo_brick_t)bo_bricks[i];

        if (kind == BO_BRICK_EMPTY) continue;
        bo_brick_rect(i, &x1, &y1, &x2, &y2);
        switch (kind) {
        case BO_BRICK_MULTI: brick.bg_color = lv_color_hex(0x00E5FF); break;
        case BO_BRICK_IRON:  brick.bg_color = lv_color_hex(0xB0BEC5); break;
        default:             brick.bg_color = lv_color_hex(row_color[i / BO_COLS]); break;
        }
        bo_fill(ctx, &brick, x1, y1, x2, y2);
        if (kind != BO_BRICK_NORMAL) bo_draw_brick_mark(ctx, kind, x1, y1, x2, y2);
    }

    lv_draw_rect_dsc_init(&paddle);
    paddle.bg_color = (bo_flash > 0 && (bo_flash % 2))
                          ? lv_color_hex(0x00E5FF) : lv_color_hex(0xE0E0E0);
    paddle.bg_opa = LV_OPA_COVER;
    paddle.radius = 4;
    bo_fill(ctx, &paddle, bo_paddle_x - BO_PADDLE_W / 2, BO_PADDLE_Y,
            bo_paddle_x + BO_PADDLE_W / 2, BO_PADDLE_Y + BO_PADDLE_H);

    lv_draw_rect_dsc_init(&ball);
    ball.bg_opa = LV_OPA_COVER;
    ball.radius = LV_RADIUS_CIRCLE;
    for (i = 0; i < BO_BALL_MAX; i++) {
        int bx, by;
        if (!bo_balls[i].active) continue;
        bx = (int)bo_balls[i].x;
        by = (int)bo_balls[i].y;
        if (iron && !bo_balls[i].spawned) {
            /* The halo marks the one ball that is actually burning -
               only the served ball carries iron, so putting the halo on
               every ball would promise pass-through the copies do not
               have. */
            ball.bg_color = lv_color_hex(0xFF7043);
            bo_fill(ctx, &ball, bx - BO_BALL_R - 3, by - BO_BALL_R - 3,
                    bx + BO_BALL_R + 3, by + BO_BALL_R + 3);
        }
        if (bo_balls[i].spawned) {
            /* Split balls wear the multiball brick's cyan, with a white
               pip so they still read as balls. They have to be tellable
               apart from the served one at a glance: they are the ones
               that will be wiped out if any ball is dropped, and losing
               track of which is which is losing track of the stake. */
            ball.bg_color = lv_color_hex(0x00E5FF);
            bo_fill(ctx, &ball, bx - BO_BALL_R, by - BO_BALL_R,
                    bx + BO_BALL_R, by + BO_BALL_R);
            ball.bg_color = lv_color_hex(0xFFFFFF);
            bo_fill(ctx, &ball, bx - 2, by - 2, bx + 2, by + 2);
        } else {
            ball.bg_color = lv_color_hex(0xFFFFFF);
            bo_fill(ctx, &ball, bx - BO_BALL_R, by - BO_BALL_R,
                    bx + BO_BALL_R, by + BO_BALL_R);
        }
    }

    /* Iron timer, as a bar that drains just inside the top of the field
       - a countdown the player can read without taking their eye off
       the ball's height. Inside the frame rather than above it: above,
       it runs straight through the score label. */
    if (iron) {
        lv_draw_rect_dsc_t bar;
        int span = BO_FIELD_RIGHT - BO_FIELD_LEFT;
        int w = span * bo_iron_ticks / BO_IRON_TICKS;

        lv_draw_rect_dsc_init(&bar);
        /* Yellow, not the same orange as the field border it sits just
           inside - two orange horizontal lines a few pixels apart read
           as one thick line, and the draining one is the informative
           half. */
        bar.bg_color = lv_color_hex(0xFFEB3B);
        bar.bg_opa = LV_OPA_COVER;
        bar.radius = 2;
        bo_fill(ctx, &bar, BO_FIELD_LEFT + 2, BO_FIELD_TOP + 4,
                BO_FIELD_LEFT + 2 + w, BO_FIELD_TOP + 8);
    }

    lv_draw_rect_dsc_init(&life);
    life.bg_opa = LV_OPA_COVER;
    life.radius = LV_RADIUS_CIRCLE;
    for (i = 0; i < BO_LIVES; i++) {
        life.bg_color = (i < bo_lives) ? lv_color_hex(0xFFFFFF)
                                       : lv_color_hex(0x3A3A3A);
        bo_fill(ctx, &life, 128 + i * 16, 330, 128 + i * 16 + 8, 338);
    }
}
