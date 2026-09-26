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
 * - Split balls are drawn differently from the served one so the
 *   player can see which ball carries the iron, but they cost nothing
 *   to lose: a life is spent only when the LAST ball is gone. Extra
 *   balls are extra chances, which is the only thing a multiball can
 *   be worth on a board where the entire circumference is the gutter -
 *   charging a life per dropped copy made the power-up a liability.
 * - Balls and iron time both carry across a cleared wall. Finishing a
 *   screen on a multiball, or with the iron ball still burning, starts
 *   the next one that way.
 * - The iron brick itself is passed straight through, keeping the
 *   ball's trajectory. It is the one brick that never deflects you.
 * - Iron is a modifier on the SERVED ball, not on the table: split
 *   balls never burn. Losing the served ball loses the iron with it;
 *   if copies are still up, one of them is promoted to be your ball
 *   (and turns white), otherwise the run re-serves.
 * - Balls collide with each other, so a crowded table plays like a
 *   table rather than several independent games sharing a screen. */

#define BO_CX 180
#define BO_CY 180
#define BO_ARENA_R      168   /* the rim: past this the ball is lost */
#define BO_PADDLE_THICK   8
/* The paddle's INNER face, which is the surface the ball actually
   bounces off. Testing contact at the rim instead let the ball sink
   the full thickness of the paddle before turning round, which looked
   like it was passing through it.
 *
 * The paddle is described by its two edges - face inward, rim outward -
 * and drawn from the same two numbers, so what the player sees is
 * exactly what the ball hits. */
#define BO_PADDLE_FACE  (BO_ARENA_R - BO_PADDLE_THICK)
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
#define BO_BALL_R         6

/* The ball is moved in hops no longer than this before collisions are
   re-tested.
 *
 * Without it, a whole tick of travel happens in one go and the ball is
 * found already buried in whatever it hit - up to 40px deep on a
 * glancing hit against a brick's side, since a sector is 30 degrees
 * wide and the arc length of that grows with radius. Lifting it back
 * out then moved it further in one frame than four frames of ordinary
 * travel, which on the device read as the ball accelerating out of
 * every collision. Sub-stepping keeps penetration to a pixel or two,
 * so the correction is invisible, and it also removes any chance of
 * passing clean through a brick at speed. */
#define BO_MAX_SUBSTEP  2.0f

/* Pacier than the flat version, which played sluggishly once the wall
   thinned out: the opening serve is roughly where that one's third
   level was, and the ceiling is high enough that a long run genuinely
   gets hard to track. */
/* 2.70 px/tick at 50fps is 135 px/s, crossing the 336px arena in about
   two and a half seconds.
 *
 * This was 4.10 - chosen when the device could not actually render the
 * game at 50fps, so the ball crawled on screen however fast the physics
 * said it was going. Once the frame got cheap enough to hit the full
 * rate, the same numbers were suddenly far too quick to track. */
#define BO_SPEED_START  2.70f
#define BO_SPEED_LEVEL  0.35f  /* added per cleared wall */
#define BO_SPEED_MAX    5.00f

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

/* Lives start at three and the life brick can push past that. The
   extras are drawn as shields rather than more hearts: a player
   glancing down needs to see at once that they are running on borrowed
   margin, and hearts four to six would just read as "the bar got
   longer". They are spent first for the same reason - the shield goes,
   then your hearts start going. */
#define BO_LIVES_START     3
#define BO_LIVES_MAX       6
#define BO_LIFE_BRICK_GAIN 1

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
    BO_BRICK_LIFE,
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
static void bo_invalidate_moving_parts(void);
static void bo_invalidate_brick(int idx);
static void bo_invalidate_paddle(int angle);
static void bo_invalidate_hud(void);

static minigame_t breakout_game = {
    .screen   = &screen_breakout,
    .score_id = GAME_SCORE_BREAKOUT,
    .hint     = STR_BREAKOUT_HINT,
    .tick_ms  = BO_TICK_MS,
    .pausable = true,
    .partial_redraw = true,   /* see bo_invalidate_moving_parts() */
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
            /* Weighted, not uniform: a life is worth more than either of
               the others, so it turns up about half as often. */
            {
                uint32_t roll = esp_random() % 100U;
                if (roll < 40U)      bo_bricks[idx] = BO_BRICK_MULTI;
                else if (roll < 80U) bo_bricks[idx] = BO_BRICK_IRON;
                else                 bo_bricks[idx] = BO_BRICK_LIFE;
            }
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
    float r = (float)(BO_PADDLE_FACE - BO_BALL_R - 1);
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
    /* A serve moves the ball across the board and clears the iron
       state, so repaint the lot rather than track what changed. */
    if (screen_breakout != NULL) lv_obj_invalidate(screen_breakout);
}

static void breakout_reset(minigame_t *g)
{
    (void)g;
    bo_paddle_angle = 90;   /* bottom of the circle, as Pong opens */
    bo_speed = BO_SPEED_START;
    bo_level = 1;
    bo_lives = BO_LIVES_START;
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
    switch (kind) {
    case BO_BRICK_MULTI:
        bo_split_balls();
        break;
    case BO_BRICK_LIFE:
        /* Capped rather than unbounded: past six the HUD stops being
           readable at a glance, and a stack that deep stops the game
           being about survival at all. Over the cap it is still worth
           breaking for the points. */
        if (bo_lives < BO_LIVES_MAX) bo_lives += BO_LIFE_BRICK_GAIN;
        if (bo_lives > BO_LIVES_MAX) bo_lives = BO_LIVES_MAX;
        break;
    case BO_BRICK_IRON:
    default:
        /* Re-triggering refreshes the full duration rather than adding
           to it - otherwise a lucky run of iron bricks could bank most
           of a level's worth of invulnerability. */
        bo_iron_ticks = BO_IRON_TICKS;
        break;
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

    bo_invalidate_brick(idx);
    bo_bricks[idx] = BO_BRICK_EMPTY;
    bo_bricks_left--;
    minigame_add_score(g, BO_BRICK_POINTS);
    if (kind != BO_BRICK_NORMAL) {
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
   (tangential) AND lifts it back out of the brick along that same
   axis.
 *
 * The push-out is not cosmetic. Collision is tested after the move, so
 * the ball is always some way inside the brick by the time it is
 * noticed; reflecting the velocity alone leaves it sitting in there for
 * the frame, which on the device looked exactly like the ball passing
 * through a brick and bouncing afterwards. Worse, at 8px per tick it
 * could still be inside on the next tick and reflect a second time,
 * cancelling the first. */
static void bo_reflect(bo_ball_t *b, bool radial, float penetration)
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

    /* Back out along the axis we bounced off, in the direction we are
       now travelling, by however deep we had sunk plus a pixel. */
    if (penetration > 0.0f) {
        float out = penetration + 1.0f;
        float sign = (dot > 0.0f) ? -1.0f : 1.0f;
        b->x += nx * out * sign;
        b->y += ny * out * sign;
    }
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

        bo_reflect(b, pen_r < pen_t, (pen_r < pen_t) ? pen_r : pen_t);
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

    if (dist + (float)BO_BALL_R < (float)BO_PADDLE_FACE) return true;
    if (dist < 0.0001f) return true;

    contact_deg = (int)(atan2f(dy, dx) * BO_RAD2DEG);
    diff = bo_angle_diff(contact_deg, bo_paddle_angle);
    if (diff < -BO_PADDLE_HALF_DEG || diff > BO_PADDLE_HALF_DEG) {
        /* Past the paddle's face but not behind the paddle: it is
           flying through the gap either side of it. Only the rim
           itself is fatal. */
        return dist + (float)BO_BALL_R < (float)BO_ARENA_R;
    }

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

    /* Sit it against the paddle's face, clear of it, so the next tick
       cannot re-trigger the same bounce. */
    b->x = (float)BO_CX + nx * (float)(BO_PADDLE_FACE - BO_BALL_R - 1);
    b->y = (float)BO_CY + ny * (float)(BO_PADDLE_FACE - BO_BALL_R - 1);
    return true;
}

// ---------- tick ----------
static void breakout_tick(minigame_t *g)
{
    bool lost_served = false;
    bool iron_was_on = bo_iron_ticks > 0;
    int lives_before = bo_lives;
    int level_before = bo_level;
    int i;

    /* Where everything is now, before it moves. */
    bo_invalidate_moving_parts();
    if (bo_flash > 0) bo_invalidate_paddle(bo_paddle_angle);

    if (bo_flash > 0) bo_flash--;
    if (bo_iron_ticks > 0) bo_iron_ticks--;
    if (iron_was_on) bo_invalidate_hud();   /* the countdown bar drains */

    if (bo_ball_stuck) {
        /* Riding the paddle: the player aims, then any tap releases. */
        float rad = (float)bo_paddle_angle * BO_DEG2RAD;
        float r = (float)(BO_PADDLE_FACE - BO_BALL_R - 1);
        bo_balls[0].x = (float)BO_CX + cosf(rad) * r;
        bo_balls[0].y = (float)BO_CY + sinf(rad) * r;
        bo_invalidate_moving_parts();
        return;
    }

    /* One tick of travel, taken in hops of at most BO_MAX_SUBSTEP with
       everything re-tested after each - see that constant for why.
       Every ball advances one hop before any of them advance the next,
       so ball-on-ball collisions do not depend on array order: resolving
       a pair mid-sweep would have the second ball collide against the
       first's already-updated position and the third against a
       mixture. */
    {
        int sub = (int)(bo_speed / BO_MAX_SUBSTEP) + 1;
        int k;

        for (k = 0; k < sub; k++) {
            for (i = 0; i < BO_BALL_MAX; i++) {
                bo_ball_t *b = &bo_balls[i];

                if (!b->active) continue;

                b->x += b->vx / (float)sub;
                b->y += b->vy / (float)sub;

                bo_hit_bricks(g, b);

                if (!bo_hit_rim(b)) {
                    b->active = false;
                    if (!b->spawned) lost_served = true;
                }
            }
            if (bo_active_balls() > 1) bo_collide_balls();
        }
    }

    /* Losing the served ball takes the iron with it. If copies are
       still in play one of them is promoted - it becomes your ball and
       is drawn white from here on - so a multiball really does buy
       extra chances rather than just extra things to drop. */
    if (lost_served) {
        bo_iron_ticks = 0;
        for (i = 0; i < BO_BALL_MAX; i++) {
            if (bo_balls[i].active) {
                bo_balls[i].spawned = false;
                break;
            }
        }
    }

    /* A life is spent only when the LAST ball goes. */
    if (bo_active_balls() == 0) {
        if (--bo_lives <= 0) {
            minigame_over(g);
            return;
        }
        bo_serve();
        return;
    }

    if (bo_bricks_left == 0) {
        /* Cleared: a fresh, faster wall rather than a win screen, so one
           good run keeps scoring.
           Deliberately NOT a re-serve - the balls in play and whatever
           iron time is left both carry into the new wall. */
        bo_level++;
        bo_speed += BO_SPEED_LEVEL;
        if (bo_speed > BO_SPEED_MAX) bo_speed = BO_SPEED_MAX;
        /* Bring the balls already in play up to the new speed. Since
           they now carry across a cleared wall, leaving their old
           magnitudes alone meant the per-level ramp only ever reached
           the NEXT serve - measured across four levels, the ball in
           play never changed speed at all. */
        for (i = 0; i < BO_BALL_MAX; i++) {
            float mag;
            if (!bo_balls[i].active) continue;
            mag = sqrtf(bo_balls[i].vx * bo_balls[i].vx +
                        bo_balls[i].vy * bo_balls[i].vy);
            if (mag < 0.0001f) continue;
            bo_balls[i].vx = bo_balls[i].vx / mag * bo_speed;
            bo_balls[i].vy = bo_balls[i].vy / mag * bo_speed;
        }
        bo_fill_wall();
    }

    /* Where everything got to. */
    bo_invalidate_moving_parts();
    if (bo_lives != lives_before) bo_invalidate_hud();
    if (bo_flash > 0) bo_invalidate_paddle(bo_paddle_angle);
    /* A fresh wall and the rim changing colour are both whole-screen
       events, but rare ones - a cleared level, or iron starting or
       ending - so paying for a full repaint there is fine. */
    if (bo_level != level_before || iron_was_on != (bo_iron_ticks > 0)) {
        lv_obj_invalidate(screen_breakout);
    }
}

// ---------- input ----------
void breakout_turn(int dir)
{
    if (minigame_handle_turn_start(&breakout_game)) return;

    bo_invalidate_paddle(bo_paddle_angle);
    bo_paddle_angle = bo_norm_angle(bo_paddle_angle + dir * BO_PADDLE_STEP_DEG);
    bo_invalidate_paddle(bo_paddle_angle);
    /* The parked serve rides the paddle, so it moved too. */
    if (bo_ball_stuck) bo_invalidate_moving_parts();
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
        if (bo_bricks[i] > BO_BRICK_NORMAL) n++;
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

// ---------- partial redraw ----------
/* Bounding box of an arc band, padded for anti-aliasing. Spans here are
   small (a brick is 26 degrees, the paddle 48), so sampling every few
   degrees is both accurate and cheap - far cheaper than the alternative
   of working out which axis extremes the span happens to cross. */
static void bo_arc_bbox(int start_deg, int end_deg, int r_in, int r_out,
                        lv_area_t *out)
{
    int span = end_deg - start_deg;
    int steps, i;
    int min_x = 10000, min_y = 10000, max_x = -10000, max_y = -10000;

    while (span < 0) span += 360;
    steps = span / 4 + 2;

    for (i = 0; i <= steps; i++) {
        float a = (float)(start_deg + span * i / steps) * BO_DEG2RAD;
        float ca = cosf(a), sa = sinf(a);
        int k;
        for (k = 0; k < 2; k++) {
            int r = k ? r_out : r_in;
            int x = BO_CX + (int)(ca * (float)r);
            int y = BO_CY + (int)(sa * (float)r);
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
            if (y < min_y) min_y = y;
            if (y > max_y) max_y = y;
        }
    }

    out->x1 = (lv_coord_t)(min_x - 3);
    out->y1 = (lv_coord_t)(min_y - 3);
    out->x2 = (lv_coord_t)(max_x + 3);
    out->y2 = (lv_coord_t)(max_y + 3);
}

static void bo_invalidate_area(const lv_area_t *a)
{
    if (screen_breakout != NULL) lv_obj_invalidate_area(screen_breakout, a);
}

static void bo_invalidate_box(int cx, int cy, int half)
{
    lv_area_t a;
    a.x1 = (lv_coord_t)(cx - half);
    a.y1 = (lv_coord_t)(cy - half);
    a.x2 = (lv_coord_t)(cx + half);
    a.y2 = (lv_coord_t)(cy + half);
    bo_invalidate_area(&a);
}

static void bo_invalidate_brick(int idx)
{
    int inner, outer, centre;
    lv_area_t a;

    bo_brick_bounds(idx, &inner, &outer, &centre);
    bo_arc_bbox(centre - BO_SECTOR_DEG / 2, centre + BO_SECTOR_DEG / 2,
                inner, outer, &a);
    bo_invalidate_area(&a);
}

static void bo_invalidate_paddle(int angle)
{
    lv_area_t a;
    bo_arc_bbox(angle - BO_PADDLE_HALF_DEG, angle + BO_PADDLE_HALF_DEG,
                BO_PADDLE_FACE - BO_PADDLE_THICK / 2,
                BO_ARENA_R + BO_PADDLE_THICK / 2, &a);
    bo_invalidate_area(&a);
}

/* The centre hole: lives and the iron countdown. */
static void bo_invalidate_hud(void)
{
    bo_invalidate_box(BO_CX, BO_CY, 40);
}

/* Everything that moved, invalidated at its CURRENT position. Called
   once before the balls move and once after, which covers where they
   were and where they got to without having to remember either.
 *
 * This replaced invalidating the whole screen every tick. With 48
 * bricks drawn as anti-aliased arcs - and lv_draw_arc building its
 * masks whether or not the arc is inside the clip area - a full redraw
 * at 50fps was far more than the device could do, and the ball visibly
 * stuttered. Now a tick repaints a few dozen pixels' worth of boxes,
 * and breakout_draw() skips every brick outside them. */
static void bo_invalidate_moving_parts(void)
{
    int i;

    for (i = 0; i < BO_BALL_MAX; i++) {
        if (!bo_balls[i].active) continue;
        /* Generous: the iron halo is 3px outside the ball. */
        bo_invalidate_box((int)bo_balls[i].x, (int)bo_balls[i].y, BO_BALL_R + 5);
    }
}

// ---------- drawing ----------
/* Draws the band between two radii.
 *
 * Takes inner and outer rather than LVGL's (radius, width), because
 * lv_draw_arc()'s radius is the band's OUTER edge and the width runs
 * INWARD from it - which is easy to read as "centre and thickness",
 * and was: the bricks and the paddle were each drawn half a thickness
 * inside where their collision maths put them, so the ball sank
 * visibly into the paddle before turning round. Naming the two edges
 * makes that impossible to get backwards. */
static void bo_draw_band(lv_draw_ctx_t *ctx, uint32_t color, lv_opa_t opa,
                         int inner, int outer, int start_deg, int end_deg)
{
    lv_draw_arc_dsc_t dsc;
    lv_point_t c = { BO_CX, BO_CY };

    lv_draw_arc_dsc_init(&dsc);
    dsc.color = lv_color_hex(color);
    dsc.width = (lv_coord_t)(outer - inner);
    dsc.opa = opa;
    lv_draw_arc(ctx, &dsc, &c, (uint16_t)outer,
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

/* ---------- 8-bit icons ----------
 * Sprites as text, one character per pixel: '#' the body colour, 'o'
 * an accent, '.' transparent. Drawn by merging each row into runs of
 * one colour, so a 7x7 sprite costs a handful of filled rects rather
 * than 49 of them - the lives HUD is redrawn on every one of the 50
 * frames a second this game runs at.
 *
 * Deliberately chunky and low-resolution: at this size a smooth heart
 * reads as a blob, while a blocky one reads as a heart. */
#define BO_ICON_W 7

/* Two lobes, a body, and a point, with the shine in the top-left lobe -
   the shape every 8-bit heart has had since Zelda. */
#define BO_HEART_H 7
static const char *const bo_icon_heart[BO_HEART_H] = {
    ".##.##.",
    "#oo####",
    "#o#####",
    "#######",
    ".#####.",
    "..###..",
    "...#...",
};

/* A heater shield: flat top - the one thing that instantly tells it
   apart from the heart's notch - straight sides, then narrowing to a
   point, with a light band across it for a device.
 *
   The first attempt drew a dark outline around a light interior, which
   is how you'd shade a sprite on a light background. On black the dark
   outline is simply invisible and the shape came out as a pale blob
   with a spike. What defines an icon against black is the bright fill,
   so the body is solid and the accent is a highlight ON it - the same
   way the heart's shine works. */
#define BO_SHIELD_H 8
static const char *const bo_icon_shield[BO_SHIELD_H] = {
    "#######",
    "#######",
    "ooooooo",
    "#######",
    ".#####.",
    ".#####.",
    "..###..",
    "...#...",
};

static void bo_draw_icon(lv_draw_ctx_t *ctx, const char *const *rows, int nrows,
                         int cx, int cy, int scale,
                         uint32_t body, uint32_t accent)
{
    lv_draw_rect_dsc_t dsc;
    int left = cx - BO_ICON_W * scale / 2;
    int top  = cy - nrows * scale / 2;
    int row;

    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_COVER;

    for (row = 0; row < nrows; row++) {
        const char *line = rows[row];
        int col = 0;

        while (col < BO_ICON_W) {
            char c = line[col];
            int run = 1;

            if (c == '.') { col++; continue; }
            while (col + run < BO_ICON_W && line[col + run] == c) run++;

            dsc.bg_color = lv_color_hex((c == 'o') ? accent : body);
            bo_fill(ctx, &dsc,
                    left + col * scale,          top + row * scale,
                    left + (col + run) * scale - 1, top + (row + 1) * scale - 1);
            col += run;
        }
    }
}

/* Power-up bricks have to be identifiable at a glance from across a
   table, so each carries a mark rather than relying on colour alone:
   two pips for the ball that splits in two, a bar for iron, and for
   the life brick the same heart the HUD uses - the clearest possible
   statement of what breaking it gives you. */
static void bo_draw_brick_mark(lv_draw_ctx_t *ctx, bo_brick_t kind, int idx)
{
    int inner, outer, centre;
    int mid;

    bo_brick_bounds(idx, &inner, &outer, &centre);
    mid = (inner + outer) / 2;

    if (kind == BO_BRICK_LIFE) {
        float rad = (float)centre * BO_DEG2RAD;
        int mx = BO_CX + (int)(cosf(rad) * (float)mid);
        int my = BO_CY + (int)(sinf(rad) * (float)mid);
        bo_draw_icon(ctx, bo_icon_heart, BO_HEART_H, mx, my, 2, 0xB71C1C, 0xE57373);
    } else if (kind == BO_BRICK_MULTI) {
        bo_draw_band(ctx, 0x00303A, LV_OPA_COVER, mid - 3, mid + 3,
                     centre - 9, centre - 4);
        bo_draw_band(ctx, 0x00303A, LV_OPA_COVER, mid - 3, mid + 3,
                     centre + 4, centre + 9);
    } else {
        bo_draw_band(ctx, 0x263238, LV_OPA_COVER, mid - 3, mid + 3,
                     centre - 7, centre + 7);
    }
}

/* Hearts on the top row, shields below.
 *
 * Three heart slots always show, dark when spent, so the player can see
 * what they have left AND what they started with. Shields only appear
 * when earned - empty shield slots would suggest something is missing
 * rather than that there is a bonus to win. */
static void bo_draw_lives(lv_draw_ctx_t *ctx)
{
    const int scale = 2;
    const int pitch = BO_ICON_W * scale + 6;
    int shields = bo_lives > BO_LIVES_START ? bo_lives - BO_LIVES_START : 0;
    int hearts  = bo_lives < BO_LIVES_START ? bo_lives : BO_LIVES_START;
    int i;

    for (i = 0; i < BO_LIVES_START; i++) {
        int x = BO_CX + (i - 1) * pitch;
        int y = BO_CY - (shields > 0 ? 11 : 0);
        if (i < hearts) bo_draw_icon(ctx, bo_icon_heart, BO_HEART_H, x, y, scale, 0xE53935, 0xFF8A80);
        else            bo_draw_icon(ctx, bo_icon_heart, BO_HEART_H, x, y, scale, 0x33191B, 0x3E1F21);
    }

    /* Centred row, whatever the count: offset each icon from the
       midpoint by half a pitch per step. */
    for (i = 0; i < shields; i++) {
        int x = BO_CX + (2 * i - (shields - 1)) * pitch / 2;
        bo_draw_icon(ctx, bo_icon_shield, BO_SHIELD_H, x, BO_CY + 12, scale, 0x2196F3, 0xBBDEFB);
    }
}

static void breakout_draw(lv_event_t *e)
{
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    /* Ring colour doubles as depth cue: the innermost ring is the one
       that takes work to reach, so it gets the hottest colour. */
    static const uint32_t ring_color[BO_RINGS] = {
        0xEF5350, 0xFFA726, 0x66BB6A, 0x42A5F5,
    };
    bool iron = bo_iron_ticks > 0;
    int i;

    /* The rim, so the gutter the player is defending is visible at all.
       It turns orange while the iron ball burns. */
    bo_draw_band(ctx, iron ? 0xFF7043 : 0x1E2A30, LV_OPA_COVER,
                 BO_ARENA_R - 2, BO_ARENA_R, 0, 359);



    for (i = 0; i < BO_BRICK_COUNT; i++) {
        int inner, outer, centre;
        bo_brick_t kind = (bo_brick_t)bo_bricks[i];
        uint32_t color;
        lv_area_t box, hit;

        if (kind == BO_BRICK_EMPTY) continue;
        bo_brick_bounds(i, &inner, &outer, &centre);

        /* Skip bricks outside the region being repainted. This has to
           be done here rather than left to LVGL: lv_draw_arc builds its
           angle and radius masks before it tests the clip area, so an
           arc that contributes nothing still costs nearly full price.
           With 48 of them that was the difference between a smooth ball
           and a stuttering one. */
        bo_arc_bbox(centre - BO_SECTOR_DEG / 2, centre + BO_SECTOR_DEG / 2,
                    inner, outer, &box);
        if (!_lv_area_intersect(&hit, &box, ctx->clip_area)) continue;
        switch (kind) {
        case BO_BRICK_MULTI: color = 0x00E5FF; break;
        case BO_BRICK_IRON:  color = 0xB0BEC5; break;
        case BO_BRICK_LIFE:  color = 0xF48FB1; break;
        default:             color = ring_color[i / BO_SECTORS]; break;
        }
        bo_draw_band(ctx, color, LV_OPA_COVER, inner, outer,
                     centre - BO_SECTOR_DEG / 2 + BO_BRICK_GAP_DEG,
                     centre + BO_SECTOR_DEG / 2 - BO_BRICK_GAP_DEG);
        if (kind != BO_BRICK_NORMAL) bo_draw_brick_mark(ctx, kind, i);
    }

    /* Paddle */
    bo_draw_band(ctx,
                 (bo_flash > 0 && (bo_flash % 2)) ? 0x00E5FF : 0xE0E0E0,
                 LV_OPA_COVER, BO_PADDLE_FACE, BO_ARENA_R,
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
    bo_draw_lives(ctx);

    /* Iron countdown, as a bar under the lives.
       This was a ring unwinding just inside the paddle track, which read
       beautifully and cost far too much: it changes every single tick,
       so it forced a whole-screen repaint every frame and defeated the
       partial redraw entirely. In the centre hole it is a 50px box. */
    if (iron) {
        lv_draw_rect_dsc_t bar;
        int w = 48 * bo_iron_ticks / BO_IRON_TICKS;

        lv_draw_rect_dsc_init(&bar);
        bar.bg_opa = LV_OPA_COVER;
        bar.radius = 2;
        bar.bg_color = lv_color_hex(0x3E2D18);
        bo_fill(ctx, &bar, BO_CX - 24, BO_CY + 28, BO_CX + 24, BO_CY + 32);
        if (w > 0) {
            bar.bg_color = lv_color_hex(0xFFEB3B);
            bo_fill(ctx, &bar, BO_CX - 24, BO_CY + 28, BO_CX - 24 + w, BO_CY + 32);
        }
    }
}

void breakout_test_arena_radii(int *cx, int *cy, int *rim_r, int *paddle_face_r)
{
    if (cx != NULL)             *cx = BO_CX;
    if (cy != NULL)             *cy = BO_CY;
    if (rim_r != NULL)          *rim_r = BO_ARENA_R;
    if (paddle_face_r != NULL)  *paddle_face_r = BO_PADDLE_FACE;
}

bool breakout_test_brick_bounds(int idx, int *inner, int *outer, int *centre_deg)
{
    int in, out, centre;

    if (idx < 0 || idx >= BO_BRICK_COUNT) return false;
    if (bo_bricks[idx] == BO_BRICK_EMPTY) return false;
    bo_brick_bounds(idx, &in, &out, &centre);
    if (inner != NULL)      *inner = in;
    if (outer != NULL)      *outer = out;
    if (centre_deg != NULL) *centre_deg = centre;
    return true;
}

int breakout_test_brick_count(void) { return BO_BRICK_COUNT; }
