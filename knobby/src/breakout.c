#include "breakout.h"
#include "minigame.h"
#include "esp_random.h"
#include <math.h>
#include <string.h>

/* Breakout in the round.
 *
 * The first version of this game was a rectangle inscribed in the
 * circle, which wasted the corners and, worse, was bounded by the
 * NARROWEST chord it had to span - the playfield ended up 224px wide
 * on a 360px display. Rebuilt in polar coordinates it uses the whole
 * glass: bricks sit in concentric rings around the centre and the
 * paddle arcs along the rim exactly as Pong's does, so the two games
 * on this device now share one control idiom instead of two.
 *
 * Losing the ball means letting it reach the rim anywhere the paddle
 * is not - the whole circumference is the gutter, which is what makes
 * a round Breakout harder than a flat one and why the paddle is
 * generously wide.
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
 *   the next one that way.
 * - The iron brick itself is passed straight through, keeping the
 *   ball's trajectory. It is the one brick that never deflects you.
 * - Iron is a modifier on the SERVED ball, not on the table: split
 *   balls never burn. Losing the served ball loses the iron with it
 *   and re-serves.
 * - Balls collide with each other, so a crowded table plays like a
 *   table rather than several independent games sharing a screen. */

#define BO_CX 180
#define BO_CY 180
#define BO_ARENA_R      168   /* the rim: returned here, or lost here */
#define BO_PADDLE_THICK   8
#define BO_PADDLE_R     (BO_ARENA_R - BO_PADDLE_THICK / 2)
#define BO_PADDLE_HALF_DEG 24 /* generous: the whole rim is the gutter */
#define BO_PADDLE_STEP_DEG  6 /* per knob detent */
/* How far off straight-inward an edge-of-paddle hit sends the ball.
 *
 * The paddle returns the ball at an angle set purely by WHERE it
 * landed, exactly as a flat Breakout paddle does - the incoming
 * direction is discarded. That is the model players already have, and
 * on a circle it is also the only one that works: a specular bounce
 * off the rim sends a shallow arrival back out just as shallowly, and
 * a shallow trajectory on a circle is a stable orbit that never
 * reaches the rings at all.
 *
 * 50 degrees is set by the ring radii. A ball leaving the rim (168) at
 * angle t off the radius gets no closer to the centre than 168*sin(t):
 * dead centre (t=0) drives to the core and can dig out the innermost
 * ring, while the paddle's very edge (t=50) bottoms out at ~129, just
 * grazing the outer ring at 132. So the whole width of the paddle maps
 * onto a useful range of depths, and how deep you dig is a choice. */
#define BO_MAX_STEER_DEG 50.0f

#define BO_TICK_MS       20
#define BO_BALL_R         5

/* Pacier than the flat version, which played sluggishly once the wall
   thinned out: the opening serve is roughly where that one's third
   level was, and the ceiling is high enough that a long run genuinely
   gets hard to track. */
#define BO_SPEED_START  4.10f
#define BO_SPEED_LEVEL  0.55f  /* added per cleared wall */
#define BO_SPEED_MAX    8.00f

/* Four rings of twelve, spanning radius 60..132 of a 168 arena.
 *
 * The radii are set by a geometric constraint, not by taste. A ball
 * leaving the rim at radius R with an inward radial fraction f gets no
 * closer to the centre than R*sqrt(1-f^2). With the rings tucked in
 * near the middle, every return that was not nearly perpendicular
 * missed them entirely and the ball just orbited the rim - a tracked
 * paddle cleared 22 of 48 bricks and then nothing at all for the next
 * 30 minutes of simulated play. Pushing the rings out to 132 means any
 * return with f >= 0.7 (see BO_MIN_INWARD, enforced in bo_hit_rim)
 * reaches the outer ring, while digging to the innermost one still
 * needs a genuinely steep return - which is the skill the round layout
 * adds over the flat one.
 *
 * The hole left in the middle is wide enough for the ball to fly clean
 * through, which is the other thing that makes this read differently
 * from a flat wall. */
#define BO_RINGS           4
#define BO_SECTORS        12
#define BO_BRICK_COUNT    (BO_RINGS * BO_SECTORS)
#define BO_RING0_INNER    60
#define BO_RING_PITCH     19
#define BO_RING_THICK     15
#define BO_SECTOR_DEG     (360 / BO_SECTORS)
#define BO_BRICK_GAP_DEG   2   /* drawn gap between neighbours, visual only */

#define BO_BRICK_POINTS   10
/* Power-up bricks are worth more: they sit wherever the wall put them,
   so reaching one is usually a deliberate detour. */
#define BO_SPECIAL_POINTS 25

#define BO_LIVES           3

/* Up to this many power-up bricks per wall, placed at random. Three in
   forty-eight is often enough to shape how a wall gets attacked
   without making the specials the only thing anyone aims at. */
#define BO_SPECIAL_MAX     3

#define BO_BALL_MAX        6
#define BO_IRON_MS     10000
#define BO_IRON_TICKS  (BO_IRON_MS / BO_TICK_MS)

#define BO_DEG2RAD 0.017453292f
#define BO_RAD2DEG 57.29577951f

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

static int   bo_paddle_angle;  /* degrees, 0 = 3 o'clock, clockwise */
static bo_ball_t bo_balls[BO_BALL_MAX];
static float bo_speed;
static uint8_t bo_bricks[BO_BRICK_COUNT];
static int   bo_bricks_left;
static int   bo_lives;
static int   bo_level;
static bool  bo_ball_stuck;    /* the serve rides the paddle until released */
static int   bo_iron_ticks;
static int   bo_ball_bounces;  /* ball-on-ball collisions this run (tests) */
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

// ---------- angles ----------
static int bo_norm_angle(int a)
{
    a %= 360;
    if (a < 0) a += 360;
    return a;
}

/* Signed shortest difference a - b, in (-180, 180]. */
static int bo_angle_diff(int a, int b)
{
    int d = bo_norm_angle(a) - bo_norm_angle(b);
    if (d > 180) d -= 360;
    if (d < -180) d += 360;
    return d;
}

static int bo_active_balls(void)
{
    int i, n = 0;
    for (i = 0; i < BO_BALL_MAX; i++) if (bo_balls[i].active) n++;
    return n;
}

// ---------- bricks ----------
static void bo_brick_bounds(int idx, int *inner, int *outer, int *centre_deg)
{
    int ring = idx / BO_SECTORS;
    int sector = idx % BO_SECTORS;
    if (inner != NULL)      *inner = BO_RING0_INNER + ring * BO_RING_PITCH;
    if (outer != NULL)      *outer = BO_RING0_INNER + ring * BO_RING_PITCH + BO_RING_THICK;
    if (centre_deg != NULL) *centre_deg = sector * BO_SECTOR_DEG + BO_SECTOR_DEG / 2;
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
        /* Retry rather than reject-and-give-up: with 48 slots and at
           most 3 specials a free slot is always immediately available,
           and a bounded loop can't hang if that ever stops being true. */
        for (tries = 0; tries < 20; tries++) {
            int idx = (int)(esp_random() % BO_BRICK_COUNT);
            if (bo_bricks[idx] != BO_BRICK_NORMAL) continue;
            bo_bricks[idx] = (esp_random() % 2U) ? BO_BRICK_MULTI : BO_BRICK_IRON;
            break;
        }
    }
}

// ---------- serving ----------
/* Parks one ball against the paddle, aimed inward at a slight angle.
   The tangential sign is random so a player can't memorise the opening
   trajectory. */
static void bo_serve(void)
{
    float rad = (float)bo_paddle_angle * BO_DEG2RAD;
    float nx = cosf(rad), ny = sinf(rad);     /* outward radial */
    float tx = -ny, ty = nx;                  /* tangential */
    float lean = (esp_random() % 2U) ? 0.55f : -0.55f;
    float r = (float)(BO_PADDLE_R - BO_PADDLE_THICK / 2 - BO_BALL_R - 1);
    float mag;

    memset(bo_balls, 0, sizeof(bo_balls));
    bo_balls[0].active = true;
    bo_balls[0].spawned = false;
    bo_balls[0].x = (float)BO_CX + nx * r;
    bo_balls[0].y = (float)BO_CY + ny * r;

    bo_balls[0].vx = -nx + tx * lean;
    bo_balls[0].vy = -ny + ty * lean;
    mag = sqrtf(bo_balls[0].vx * bo_balls[0].vx + bo_balls[0].vy * bo_balls[0].vy);
    bo_balls[0].vx = bo_balls[0].vx / mag * bo_speed;
    bo_balls[0].vy = bo_balls[0].vy / mag * bo_speed;

    bo_ball_stuck = true;
    bo_iron_ticks = 0;
}

static void breakout_reset(minigame_t *g)
{
    (void)g;
    bo_paddle_angle = 90;   /* bottom of the circle, as Pong opens */
    bo_speed = BO_SPEED_START;
    bo_level = 1;
    bo_lives = BO_LIVES;
    bo_flash = 0;
    bo_ball_bounces = 0;
    bo_fill_wall();
    bo_serve();
}

// ---------- multiball ----------
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
            float vx = bo_balls[i].vx, vy = bo_balls[i].vy;
            float mag = sqrtf(vx * vx + vy * vy);
            float ca, sa;

            if (bo_balls[slot].active) continue;

            /* The pair has to visibly fan apart, and in a circular
               arena "mirror the horizontal component" is meaningless -
               there is no privileged axis. Rotating the two apart by a
               fixed angle separates them the same way wherever on the
               board they happen to be. */
            if (mag < 0.0001f) { mag = bo_speed; vx = 0.0f; vy = -bo_speed; }
            ca = cosf(22.0f * BO_DEG2RAD);
            sa = sinf(22.0f * BO_DEG2RAD);

            bo_balls[slot] = bo_balls[i];
            bo_balls[slot].spawned = true;
            bo_balls[i].vx    = vx * ca - vy * sa;
            bo_balls[i].vy    = vx * sa + vy * ca;
            bo_balls[slot].vx = vx * ca + vy * sa;
            bo_balls[slot].vy = -vx * sa + vy * ca;
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

/* Iron rides the served ball and nothing else. It is a modifier on
   your ball, not a property of the table: a split ball burning through
   the wall too would multiply the effect by however many copies happen
   to be out, and the copies are already the cheap half of a multiball. */
static bool bo_ball_is_iron(const bo_ball_t *b)
{
    return bo_iron_ticks > 0 && !b->spawned;
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

/* Overlap test in polar coordinates, which is what the ring layout
   makes natural: a brick is an (inner..outer) x (centre +/- half
   sector) box, and the ball is a disc whose angular half-width grows
   as it approaches the middle. Returns the penetration depth on each
   axis, both as lengths in px, so the caller can pick the shallower
   one to reflect off. */
static bool bo_ball_overlaps_brick(const bo_ball_t *b, int idx,
                                   float *pen_radial, float *pen_tangential)
{
    int inner, outer, centre;
    float dx = b->x - (float)BO_CX;
    float dy = b->y - (float)BO_CY;
    float r = sqrtf(dx * dx + dy * dy);
    float ang;
    float half_span;
    float diff;

    bo_brick_bounds(idx, &inner, &outer, &centre);

    if (r + (float)BO_BALL_R < (float)inner) return false;
    if (r - (float)BO_BALL_R > (float)outer) return false;
    if (r < 0.5f) return false;

    ang = atan2f(dy, dx) * BO_RAD2DEG;
    /* The ball covers more degrees the closer it is to the centre. */
    half_span = (float)(BO_SECTOR_DEG / 2) + (float)BO_BALL_R / r * BO_RAD2DEG;
    diff = (float)bo_angle_diff((int)ang, centre);
    if (diff < 0.0f) diff = -diff;
    if (diff > half_span) return false;

    if (pen_radial != NULL) {
        float outward = (b->vx * dx + b->vy * dy);   /* sign only */
        *pen_radial = (outward > 0.0f) ? (r + (float)BO_BALL_R - (float)inner)
                                       : ((float)outer - (r - (float)BO_BALL_R));
    }
    if (pen_tangential != NULL) {
        *pen_tangential = (half_span - diff) * BO_DEG2RAD * r;
    }
    return true;
}

/* Reflects the ball off a ring face (radial) or a sector side
   (tangential) at its current position. */
static void bo_reflect(bo_ball_t *b, bool radial)
{
    float dx = b->x - (float)BO_CX;
    float dy = b->y - (float)BO_CY;
    float r = sqrtf(dx * dx + dy * dy);
    float nx, ny, dot;

    if (r < 0.0001f) { b->vx = -b->vx; b->vy = -b->vy; return; }
    nx = dx / r; ny = dy / r;

    if (!radial) { float t = nx; nx = -ny; ny = t; }  /* tangential axis */

    dot = b->vx * nx + b->vy * ny;
    b->vx -= 2.0f * dot * nx;
    b->vy -= 2.0f * dot * ny;
}

/* Iron ball: burn through every brick touched this tick and keep going
   straight. Normal ball: take the first brick and bounce, choosing the
   axis by which way the ball has penetrated less far - that is what
   stops a corner clip reversing the wrong way and appearing to pass
   through the wall. */
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
        float pen_r, pen_t;

        if (bo_bricks[i] == BO_BRICK_EMPTY) continue;
        if (!bo_ball_overlaps_brick(b, i, &pen_r, &pen_t)) continue;

        /* The iron brick is the one brick that never deflects the ball:
           hitting it hands you the iron ball and you carry straight on
           through the hole you just made, on the trajectory you had. */
        if (bo_bricks[i] == BO_BRICK_IRON) {
            bo_break_brick(g, i);
            return;
        }

        bo_reflect(b, pen_r < pen_t);
        bo_break_brick(g, i);
        return;
    }
}

// ---------- ball-on-ball ----------
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
    float nx, ny, rel, mag_a, mag_b, mag, overlap;

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

static void bo_clamp_to_arena(bo_ball_t *b)
{
    float dx = b->x - (float)BO_CX;
    float dy = b->y - (float)BO_CY;
    float r = sqrtf(dx * dx + dy * dy);
    float limit = (float)(BO_ARENA_R - BO_BALL_R);

    if (r > limit && r > 0.0001f) {
        b->x = (float)BO_CX + dx / r * limit;
        b->y = (float)BO_CY + dy / r * limit;
    }
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

    /* The separation above can shove a ball past the rim; putting it
       back is cheaper and steadier than solving both constraints
       together. */
    for (i = 0; i < BO_BALL_MAX; i++) {
        if (bo_balls[i].active) bo_clamp_to_arena(&bo_balls[i]);
    }
}

// ---------- the rim ----------
/* Returns false when the ball got past the paddle and is lost.
   Specular reflection off the circle's normal at the contact point plus
   a bit of "english" from how far off paddle-centre it landed - the
   same return Pong uses, so the two games steer identically. */
static bool bo_hit_rim(bo_ball_t *b)
{
    float dx = b->x - (float)BO_CX;
    float dy = b->y - (float)BO_CY;
    float dist = sqrtf(dx * dx + dy * dy);
    int contact_deg;
    int diff;
    float nx, ny, speed, ratio, off, ix, iy;

    if (dist + (float)BO_BALL_R < (float)BO_ARENA_R) return true;
    if (dist < 0.0001f) return true;

    contact_deg = (int)(atan2f(dy, dx) * BO_RAD2DEG);
    diff = bo_angle_diff(contact_deg, bo_paddle_angle);
    if (diff < -BO_PADDLE_HALF_DEG || diff > BO_PADDLE_HALF_DEG) return false;

    speed = sqrtf(b->vx * b->vx + b->vy * b->vy);
    nx = dx / dist; ny = dy / dist;

    /* Outgoing direction from the hit position alone - see
       BO_MAX_STEER_DEG. Straight inward, rotated by how far off the
       middle of the paddle the ball landed. */
    ratio = (float)diff / (float)BO_PADDLE_HALF_DEG;
    if (ratio < -1.0f) ratio = -1.0f;
    if (ratio >  1.0f) ratio =  1.0f;
    off = ratio * BO_MAX_STEER_DEG * BO_DEG2RAD;

    ix = -nx; iy = -ny;
    b->vx = (ix * cosf(off) - iy * sinf(off)) * speed;
    b->vy = (ix * sinf(off) + iy * cosf(off)) * speed;

    /* Put it just inside the rim so the next tick can't re-trigger. */
    b->x = (float)BO_CX + nx * (float)(BO_ARENA_R - BO_BALL_R - 1);
    b->y = (float)BO_CY + ny * (float)(BO_ARENA_R - BO_BALL_R - 1);
    return true;
}

// ---------- tick ----------
static void breakout_tick(minigame_t *g)
{
    bool lost = false;
    int i;

    if (bo_flash > 0) bo_flash--;
    if (bo_iron_ticks > 0) bo_iron_ticks--;

    if (bo_ball_stuck) {
        /* Riding the paddle: the player aims, then any tap releases. */
        float rad = (float)bo_paddle_angle * BO_DEG2RAD;
        float r = (float)(BO_PADDLE_R - BO_PADDLE_THICK / 2 - BO_BALL_R - 1);
        bo_balls[0].x = (float)BO_CX + cosf(rad) * r;
        bo_balls[0].y = (float)BO_CY + sinf(rad) * r;
        return;
    }

    for (i = 0; i < BO_BALL_MAX; i++) {
        bo_ball_t *b = &bo_balls[i];

        if (!b->active) continue;

        b->x += b->vx;
        b->y += b->vy;

        bo_hit_bricks(g, b);

        if (!bo_hit_rim(b)) {
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
       One life per tick however many balls were lost together - two
       crossing the rim in the same frame is one mistake, not two. */
    if (lost) {
        for (i = 0; i < BO_BALL_MAX; i++) {
            if (bo_balls[i].spawned) bo_balls[i].active = false;
        }
        if (--bo_lives <= 0) {
            minigame_over(g);
            return;
        }
        /* Only re-serve when nothing is left: if the served ball is
           still up there, play simply continues without the copies. */
        if (bo_active_balls() == 0) {
            bo_serve();
            return;
        }
    }

    if (bo_bricks_left == 0) {
        /* Cleared: a fresh, faster wall rather than a win screen, so one
           good run keeps scoring.
           Deliberately NOT a re-serve - the balls in play and whatever
           iron time is left both carry into the new wall. */
        bo_level++;
        if (bo_speed < BO_SPEED_MAX) bo_speed += BO_SPEED_LEVEL;
        bo_fill_wall();
    }
}

// ---------- input ----------
void breakout_turn(int dir)
{
    if (minigame_handle_turn_start(&breakout_game)) return;

    bo_paddle_angle = bo_norm_angle(bo_paddle_angle + dir * BO_PADDLE_STEP_DEG);
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
int breakout_test_paddle_angle(void) { return bo_paddle_angle; }
int breakout_test_lives(void)        { return bo_lives; }
int breakout_test_bricks_left(void)  { return bo_bricks_left; }
int breakout_test_level(void)        { return bo_level; }
int breakout_test_ball_count(void)   { return bo_active_balls(); }
bool breakout_test_ball_parked(void) { return bo_ball_stuck; }
int breakout_test_iron_ms(void)      { return bo_iron_ticks * BO_TICK_MS; }
int breakout_test_ball_bounces(void) { return bo_ball_bounces; }

int breakout_test_iron_ball_count(void)
{
    int i, n = 0;
    for (i = 0; i < BO_BALL_MAX; i++) {
        if (bo_balls[i].active && bo_ball_is_iron(&bo_balls[i])) n++;
    }
    return n;
}

int breakout_test_special_bricks_left(void)
{
    int i, n = 0;
    for (i = 0; i < BO_BRICK_COUNT; i++) {
        if (bo_bricks[i] == BO_BRICK_MULTI || bo_bricks[i] == BO_BRICK_IRON) n++;
    }
    return n;
}

int breakout_test_spawned_count(void)
{
    int i, n = 0;
    for (i = 0; i < BO_BALL_MAX; i++) {
        if (bo_balls[i].active && bo_balls[i].spawned) n++;
    }
    return n;
}

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

int breakout_test_ball_spread(void)
{
    int i, j, widest = 0;

    for (i = 0; i < BO_BALL_MAX; i++) {
        if (!bo_balls[i].active) continue;
        for (j = i + 1; j < BO_BALL_MAX; j++) {
            int ai, aj, d;
            if (!bo_balls[j].active) continue;
            ai = (int)(atan2f(bo_balls[i].y - BO_CY, bo_balls[i].x - BO_CX) * BO_RAD2DEG);
            aj = (int)(atan2f(bo_balls[j].y - BO_CY, bo_balls[j].x - BO_CX) * BO_RAD2DEG);
            d = bo_angle_diff(ai, aj);
            if (d < 0) d = -d;
            if (d > widest) widest = d;
        }
    }
    return widest;
}

bool breakout_test_outermost_ball_motion(float *x, float *y,
                                         float *vx, float *vy)
{
    int i, best_i = -1;
    float best = -1.0f;

    for (i = 0; i < BO_BALL_MAX; i++) {
        float dx, dy, r;
        if (!bo_balls[i].active) continue;
        dx = bo_balls[i].x - (float)BO_CX;
        dy = bo_balls[i].y - (float)BO_CY;
        r = sqrtf(dx * dx + dy * dy);
        if (r > best) { best = r; best_i = i; }
    }
    if (best_i < 0) return false;
    if (x != NULL)  *x = bo_balls[best_i].x;
    if (y != NULL)  *y = bo_balls[best_i].y;
    if (vx != NULL) *vx = bo_balls[best_i].vx;
    if (vy != NULL) *vy = bo_balls[best_i].vy;
    return true;
}

bool breakout_test_outermost_ball(int *angle_deg, int *radius)
{
    int i;
    float best = -1.0f;
    int best_ang = 0;

    for (i = 0; i < BO_BALL_MAX; i++) {
        float dx, dy, r;
        if (!bo_balls[i].active) continue;
        dx = bo_balls[i].x - (float)BO_CX;
        dy = bo_balls[i].y - (float)BO_CY;
        r = sqrtf(dx * dx + dy * dy);
        if (r > best) {
            best = r;
            best_ang = bo_norm_angle((int)(atan2f(dy, dx) * BO_RAD2DEG));
        }
    }
    if (best < 0.0f) return false;
    if (angle_deg != NULL) *angle_deg = best_ang;
    if (radius != NULL)    *radius = (int)best;
    return true;
}

// ---------- drawing ----------
static void bo_draw_ring_arc(lv_draw_ctx_t *ctx, uint32_t color, lv_opa_t opa,
                             int radius, int width, int start_deg, int end_deg)
{
    lv_draw_arc_dsc_t dsc;
    lv_point_t c = { BO_CX, BO_CY };

    lv_draw_arc_dsc_init(&dsc);
    dsc.color = lv_color_hex(color);
    dsc.width = (lv_coord_t)width;
    dsc.opa = opa;
    lv_draw_arc(ctx, &dsc, &c, (uint16_t)radius,
                (uint16_t)bo_norm_angle(start_deg), (uint16_t)bo_norm_angle(end_deg));
}

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
   two pips for the ball that splits in two, a bar for iron. */
static void bo_draw_brick_mark(lv_draw_ctx_t *ctx, bo_brick_t kind, int idx)
{
    int inner, outer, centre;
    int mid;

    bo_brick_bounds(idx, &inner, &outer, &centre);
    mid = (inner + outer) / 2;

    if (kind == BO_BRICK_MULTI) {
        bo_draw_ring_arc(ctx, 0x00303A, LV_OPA_COVER, mid, 6,
                         centre - 9, centre - 4);
        bo_draw_ring_arc(ctx, 0x00303A, LV_OPA_COVER, mid, 6,
                         centre + 4, centre + 9);
    } else {
        bo_draw_ring_arc(ctx, 0x263238, LV_OPA_COVER, mid, 6,
                         centre - 7, centre + 7);
    }
}

static void breakout_draw(lv_event_t *e)
{
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_draw_rect_dsc_t life;
    /* Ring colour doubles as depth cue: the innermost ring is the one
       that takes work to reach, so it gets the hottest colour. */
    static const uint32_t ring_color[BO_RINGS] = {
        0xEF5350, 0xFFA726, 0x66BB6A, 0x42A5F5,
    };
    bool iron = bo_iron_ticks > 0;
    int i;

    /* The rim, so the gutter the player is defending is visible at all.
       It turns orange while the iron ball burns. */
    bo_draw_ring_arc(ctx, iron ? 0xFF7043 : 0x1E2A30, LV_OPA_COVER,
                     BO_ARENA_R, 2, 0, 359);

    /* Iron timer: a ring that unwinds just inside the paddle track, so
       the countdown is readable without looking away from the ball. */
    if (iron) {
        int sweep = 359 * bo_iron_ticks / BO_IRON_TICKS;
        if (sweep > 0) {
            bo_draw_ring_arc(ctx, 0xFFEB3B, LV_OPA_COVER, BO_ARENA_R - 12, 4,
                             270, 270 + sweep);
        }
    }

    for (i = 0; i < BO_BRICK_COUNT; i++) {
        int inner, outer, centre;
        bo_brick_t kind = (bo_brick_t)bo_bricks[i];
        uint32_t color;

        if (kind == BO_BRICK_EMPTY) continue;
        bo_brick_bounds(i, &inner, &outer, &centre);
        switch (kind) {
        case BO_BRICK_MULTI: color = 0x00E5FF; break;
        case BO_BRICK_IRON:  color = 0xB0BEC5; break;
        default:             color = ring_color[i / BO_SECTORS]; break;
        }
        bo_draw_ring_arc(ctx, color, LV_OPA_COVER, (inner + outer) / 2, BO_RING_THICK,
                         centre - BO_SECTOR_DEG / 2 + BO_BRICK_GAP_DEG,
                         centre + BO_SECTOR_DEG / 2 - BO_BRICK_GAP_DEG);
        if (kind != BO_BRICK_NORMAL) bo_draw_brick_mark(ctx, kind, i);
    }

    /* Paddle */
    bo_draw_ring_arc(ctx,
                     (bo_flash > 0 && (bo_flash % 2)) ? 0x00E5FF : 0xE0E0E0,
                     LV_OPA_COVER, BO_PADDLE_R, BO_PADDLE_THICK,
                     bo_paddle_angle - BO_PADDLE_HALF_DEG,
                     bo_paddle_angle + BO_PADDLE_HALF_DEG);

    /* Balls */
    {
        lv_draw_rect_dsc_t ball;
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
                   only the served ball carries iron, so putting the halo
                   on every ball would promise the copies a pass-through
                   they do not have. */
                ball.bg_color = lv_color_hex(0xFF7043);
                bo_fill(ctx, &ball, bx - BO_BALL_R - 3, by - BO_BALL_R - 3,
                        bx + BO_BALL_R + 3, by + BO_BALL_R + 3);
            }
            if (bo_balls[i].spawned) {
                /* Split balls wear the multiball brick's cyan with a
                   white pip: they are the ones a dropped ball wipes
                   out, and losing track of which is which is losing
                   track of the stake. */
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
    }

    /* Lives, in the hole at the centre of the rings - the one part of
       the board no brick or paddle ever occupies. */
    lv_draw_rect_dsc_init(&life);
    life.bg_opa = LV_OPA_COVER;
    life.radius = LV_RADIUS_CIRCLE;
    for (i = 0; i < BO_LIVES; i++) {
        int x = BO_CX - 16 + i * 16;
        life.bg_color = (i < bo_lives) ? lv_color_hex(0xFFFFFF)
                                       : lv_color_hex(0x3A3A3A);
        bo_fill(ctx, &life, x - 4, BO_CY - 4, x + 4, BO_CY + 4);
    }
}
