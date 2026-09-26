#include "asteroids.h"
#include "presentation/minigames/minigame.h"
#include "esp_random.h"
#include <math.h>
#include <string.h>

/* Asteroids, which is the game this hardware was waiting for: a rotary
 * control driving a rotation, with nothing in between. Turn the knob
 * and the ship turns, degree for degree. No other game here maps its
 * input that directly.
 *
 * Tap to fire, HOLD to thrust. The first cut had a tap do both, which
 * sounded like a reasonable compromise for a device with one button
 * and turned out to be the thing that made the game unplayable: every
 * shot was also an acceleration, so anyone shooting at a normal rate
 * was pinned at maximum speed and careening. A simulated player with
 * perfect aim survived five seconds and never once cleared the first
 * wave. Separating them costs nothing - a press fires, and only a
 * press held past AST_HOLD_TO_THRUST reads as the engine.
 *
 * The playfield wraps, and on a round panel that is more than a
 * convenience - the visible area is a disc, so a ship leaving at any
 * angle reappears diametrically opposite, which reads naturally rather
 * than as the rectangular world's four-corner oddity.
 *
 * Rocks split when hit, big into medium into small, and only the small
 * ones vanish. Clearing them all starts a larger wave. */

#define AST_CX 180
#define AST_CY 180
#define AST_R  172            /* wrap radius: just outside the glass */

#define AST_TICK_MS      24
#define AST_TURN_DEG     12   /* per knob detent */

#define AST_THRUST     0.42f
/* Consecutive PRESSING events before a held finger counts as thrust.
   LVGL reads the pointer every ~30ms, so this is about 120ms - longer
   than any tap, shorter than any deliberate hold. */
#define AST_HOLD_TO_THRUST 4
#define AST_DRAG       0.988f /* space is not quite frictionless here */
#define AST_MAX_SPEED   4.6f

#define AST_SHIP_R        9   /* collision radius; the hull drawn is longer */

#define AST_SHOT_MAX      4
#define AST_SHOT_SPEED  6.2f
#define AST_SHOT_LIFE    46   /* ticks - a shot crosses the disc and dies */

#define AST_ROCK_MAX     14
#define AST_ROCK_SIZES    3
#define AST_WAVE_START    3   /* big rocks in wave 1 */
#define AST_WAVE_MAX      7

#define AST_LIVES         3
#define AST_RESPAWN_TICKS 40  /* invulnerable while the ship reforms */

#define AST_DEG2RAD 0.017453292f
#define AST_RAD2DEG 57.29577951f

/* Radius and score by size index: 0 small, 1 medium, 2 big. Small rocks
   are worth most, as in the original - they are the hard ones to hit. */
static const int  ast_rock_radius[AST_ROCK_SIZES] = { 8, 14, 22 };
static const int  ast_rock_points[AST_ROCK_SIZES] = { 50, 30, 20 };
static const float ast_rock_speed[AST_ROCK_SIZES] = { 1.55f, 1.15f, 0.80f };

/* Rocks get quicker with each wave, capped. Without it a player who
   simply stops moving and aims well is safe indefinitely - wrapping a
   disc by reflecting through the centre puts every rock on a closed
   periodic path, so a stationary ship that can hit anything it sees
   never has to solve a new problem. The speed ramp is what eventually
   makes standing still untenable. */
#define AST_SPEED_PER_WAVE 0.06f
#define AST_SPEED_MAX_MULT 1.90f

static int ast_wave;   /* declared here: the speed ramp above needs it */

static float ast_wave_speed_mult(void)
{
    float m = 1.0f + (float)(ast_wave - 1) * AST_SPEED_PER_WAVE;
    return (m > AST_SPEED_MAX_MULT) ? AST_SPEED_MAX_MULT : m;
}

typedef struct {
    bool  active;
    int   size;      /* index into the tables above */
    float x, y;
    float vx, vy;
    int   spin;      /* drawn rotation, degrees */
    int   spin_rate;
} ast_rock_t;

typedef struct {
    bool  active;
    float x, y;
    float vx, vy;
    int   life;
} ast_shot_t;

lv_obj_t *screen_asteroids = NULL;

static float ast_ship_x, ast_ship_y;
static float ast_ship_vx, ast_ship_vy;
static int   ast_heading;          /* degrees, 0 = 3 o'clock */
static int   ast_hold;             /* consecutive PRESSING events */
static int   ast_respawn_ticks;    /* invulnerable, and flashing */
static int   ast_lives;
static ast_rock_t ast_rocks[AST_ROCK_MAX];
static ast_shot_t ast_shots[AST_SHOT_MAX];

static void asteroids_reset(minigame_t *g);
static void asteroids_build(minigame_t *g);
static void asteroids_tick(minigame_t *g);
static void asteroids_draw(lv_event_t *e);

static void event_ast_pressing(lv_event_t *e)
{
    (void)e;
    if (ast_hold < AST_HOLD_TO_THRUST) ast_hold++;
}

static void event_ast_released(lv_event_t *e)
{
    (void)e;
    ast_hold = 0;
}

/* The framework wires the tap (on PRESSED, so a shot leaves the moment
   you touch the glass); these two turn a held finger into the engine. */
static void asteroids_build(minigame_t *g)
{
    (void)g;
    lv_obj_add_event_cb(screen_asteroids, event_ast_pressing, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(screen_asteroids, event_ast_released, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(screen_asteroids, event_ast_released, LV_EVENT_PRESS_LOST, NULL);
}

static minigame_t asteroids_game = {
    .screen   = &screen_asteroids,
    .score_id = GAME_SCORE_ASTEROIDS,
    .hint     = STR_ASTEROIDS_HINT,
    .tick_ms  = AST_TICK_MS,
    .pausable = true,
    .on_reset = asteroids_reset,
    .on_tick  = asteroids_tick,
    .on_draw  = asteroids_draw,
    .on_build = asteroids_build,
    .on_tap   = asteroids_handle_tap,
    .tap_on_press = true,     /* an action game: fire on finger-down */
    .partial_redraw = true,   /* see ast_touch_everything() */
};

/* ---------- partial redraw ----------
 *
 * Everything here moves every tick, so this game saves less than the
 * others - but a screenful of black between a dozen rocks is still
 * most of the panel, and repainting all 360x360 at 25fps costs ~259KB
 * over QSPI and a full software render each time.
 *
 * ast_touch_everything() is called on both sides of the tick: once
 * over where things were, once over where they are now, so each
 * sprite's trail is repainted with it.
 *
 * The margins are generous on purpose. A rock's outline reaches 108%
 * of its nominal radius at its longest spike (ast_rock_jag), the ship
 * is drawn longer than its collision radius, and a thing that moves
 * faster than its own margin leaves a trail nothing repaints - the one
 * failure this optimisation can produce, and one no screenshot shows,
 * since a screenshot takes the full repaint. */
#define AST_TOUCH_PAD 8

static void ast_touch_everything(void)
{
    int i;

    minigame_invalidate_box(screen_asteroids, (int)ast_ship_x, (int)ast_ship_y,
                            AST_SHIP_R * 2 + AST_TOUCH_PAD);
    for (i = 0; i < AST_ROCK_MAX; i++) {
        if (!ast_rocks[i].active) continue;
        minigame_invalidate_box(screen_asteroids,
                                (int)ast_rocks[i].x, (int)ast_rocks[i].y,
                                ast_rock_radius[ast_rocks[i].size] * 5 / 4
                                    + AST_TOUCH_PAD);
    }
    for (i = 0; i < AST_SHOT_MAX; i++) {
        if (!ast_shots[i].active) continue;
        minigame_invalidate_box(screen_asteroids,
                                (int)ast_shots[i].x, (int)ast_shots[i].y,
                                (int)AST_SHOT_SPEED + AST_TOUCH_PAD);
    }
}

static int ast_norm_angle(int a)
{
    a %= 360;
    if (a < 0) a += 360;
    return a;
}

/* Wraps a point back into the disc by reflecting it through the centre.
   A rectangular world wraps each axis independently; a circular one has
   only the one edge, so leaving anywhere means arriving diametrically
   opposite. */
static void ast_wrap(float *x, float *y)
{
    float dx = *x - (float)AST_CX;
    float dy = *y - (float)AST_CY;
    float r = sqrtf(dx * dx + dy * dy);

    if (r <= (float)AST_R || r < 0.0001f) return;
    *x = (float)AST_CX - dx / r * (float)AST_R;
    *y = (float)AST_CY - dy / r * (float)AST_R;
}

static float ast_dist(float ax, float ay, float bx, float by)
{
    float dx = ax - bx, dy = ay - by;
    return sqrtf(dx * dx + dy * dy);
}

static void ast_spawn_rock(int size, float x, float y)
{
    int i;
    for (i = 0; i < AST_ROCK_MAX; i++) {
        float ang;
        if (ast_rocks[i].active) continue;

        ang = (float)(esp_random() % 360) * AST_DEG2RAD;
        ast_rocks[i].active = true;
        ast_rocks[i].size = size;
        ast_rocks[i].x = x;
        ast_rocks[i].y = y;
        {
            float sp = ast_rock_speed[size] * ast_wave_speed_mult();
            ast_rocks[i].vx = cosf(ang) * sp;
            ast_rocks[i].vy = sinf(ang) * sp;
        }
        ast_rocks[i].spin = (int)(esp_random() % 360);
        ast_rocks[i].spin_rate = 1 + (int)(esp_random() % 4);
        if (esp_random() % 2U) ast_rocks[i].spin_rate = -ast_rocks[i].spin_rate;
        return;
    }
}

/* A new wave, with the rocks placed out at the rim. Spawning them near
   the centre would drop one on the ship before the player had moved. */
static void ast_start_wave(void)
{
    if (screen_asteroids != NULL) lv_obj_invalidate(screen_asteroids);
    int count = AST_WAVE_START + ast_wave - 1;
    int i;

    if (count > AST_WAVE_MAX) count = AST_WAVE_MAX;
    memset(ast_rocks, 0, sizeof(ast_rocks));
    memset(ast_shots, 0, sizeof(ast_shots));

    for (i = 0; i < count; i++) {
        float a = (float)(i * 360 / count + (int)(esp_random() % 40)) * AST_DEG2RAD;
        float r = (float)(AST_R - 30);
        ast_spawn_rock(AST_ROCK_SIZES - 1,
                       (float)AST_CX + cosf(a) * r,
                       (float)AST_CY + sinf(a) * r);
    }
}

static void ast_place_ship(void)
{
    ast_ship_x = (float)AST_CX;
    ast_ship_y = (float)AST_CY;
    ast_ship_vx = 0.0f;
    ast_ship_vy = 0.0f;
    ast_heading = 270;          /* pointing up */
    ast_hold = 0;
    ast_respawn_ticks = AST_RESPAWN_TICKS;
}

static void asteroids_reset(minigame_t *g)
{
    (void)g;
    ast_lives = AST_LIVES;
    ast_wave = 1;
    ast_start_wave();
    ast_place_ship();
}

static int ast_rocks_left(void)
{
    int i, n = 0;
    for (i = 0; i < AST_ROCK_MAX; i++) if (ast_rocks[i].active) n++;
    return n;
}

/* A hit splits a rock into two of the next size down, thrown apart.
   Smalls just vanish. */
static void ast_break_rock(minigame_t *g, int idx)
{
    int size = ast_rocks[idx].size;
    float x = ast_rocks[idx].x, y = ast_rocks[idx].y;

    minigame_add_score(g, ast_rock_points[size]);
    ast_rocks[idx].active = false;
    if (size == 0) return;

    ast_spawn_rock(size - 1, x, y);
    ast_spawn_rock(size - 1, x, y);
}

static void ast_lose_life(minigame_t *g)
{
    /* The ship jumps to the centre and a life leaves the HUD; too much
       scattered change to enumerate. */
    if (screen_asteroids != NULL) lv_obj_invalidate(screen_asteroids);
    if (--ast_lives <= 0) {
        minigame_over(g);
        return;
    }
    ast_place_ship();
}

static void asteroids_tick(minigame_t *g)
{
    int i, j;

    ast_touch_everything();   /* where everything was */

    if (ast_respawn_ticks > 0) ast_respawn_ticks--;

    /* ---- ship ---- */
    if (ast_hold >= AST_HOLD_TO_THRUST) {
        float rad = (float)ast_heading * AST_DEG2RAD;
        ast_ship_vx += cosf(rad) * AST_THRUST;
        ast_ship_vy += sinf(rad) * AST_THRUST;
    }
    ast_ship_vx *= AST_DRAG;
    ast_ship_vy *= AST_DRAG;
    {
        float sp = sqrtf(ast_ship_vx * ast_ship_vx + ast_ship_vy * ast_ship_vy);
        if (sp > AST_MAX_SPEED) {
            ast_ship_vx = ast_ship_vx / sp * AST_MAX_SPEED;
            ast_ship_vy = ast_ship_vy / sp * AST_MAX_SPEED;
        }
    }
    ast_ship_x += ast_ship_vx;
    ast_ship_y += ast_ship_vy;
    ast_wrap(&ast_ship_x, &ast_ship_y);

    /* ---- shots ---- */
    for (i = 0; i < AST_SHOT_MAX; i++) {
        if (!ast_shots[i].active) continue;
        ast_shots[i].x += ast_shots[i].vx;
        ast_shots[i].y += ast_shots[i].vy;
        ast_wrap(&ast_shots[i].x, &ast_shots[i].y);
        if (--ast_shots[i].life <= 0) {
            ast_shots[i].active = false;
            continue;
        }
        for (j = 0; j < AST_ROCK_MAX; j++) {
            if (!ast_rocks[j].active) continue;
            if (ast_dist(ast_shots[i].x, ast_shots[i].y,
                         ast_rocks[j].x, ast_rocks[j].y)
                > (float)ast_rock_radius[ast_rocks[j].size]) continue;
            ast_shots[i].active = false;
            ast_break_rock(g, j);
            break;
        }
    }

    /* ---- rocks ---- */
    for (i = 0; i < AST_ROCK_MAX; i++) {
        if (!ast_rocks[i].active) continue;
        ast_rocks[i].x += ast_rocks[i].vx;
        ast_rocks[i].y += ast_rocks[i].vy;
        ast_wrap(&ast_rocks[i].x, &ast_rocks[i].y);
        ast_rocks[i].spin = ast_norm_angle(ast_rocks[i].spin + ast_rocks[i].spin_rate);

        if (ast_respawn_ticks > 0) continue;   /* still reforming */
        if (ast_dist(ast_ship_x, ast_ship_y, ast_rocks[i].x, ast_rocks[i].y)
            > (float)(ast_rock_radius[ast_rocks[i].size] + AST_SHIP_R)) continue;

        ast_lose_life(g);
        return;
    }

    ast_touch_everything();   /* and where it is now */

    if (ast_rocks_left() == 0) {
        ast_wave++;
        ast_start_wave();
        /* The ship keeps its position and momentum into the new wave -
           it has earned the breathing room, and a forced respawn in the
           middle would be a punishment for clearing. */
        ast_respawn_ticks = AST_RESPAWN_TICKS;
        if (screen_asteroids != NULL) lv_obj_invalidate(screen_asteroids);
    }
}

void asteroids_turn(int dir)
{
    if (minigame_handle_turn_start(&asteroids_game)) return;

    /* The whole point of the game on this device: knob degrees become
       ship degrees. */
    ast_heading = ast_norm_angle(ast_heading + dir * AST_TURN_DEG);
    minigame_invalidate_box(screen_asteroids, (int)ast_ship_x, (int)ast_ship_y,
                            AST_SHIP_R * 2 + AST_TOUCH_PAD);
}

void asteroids_handle_tap(void)
{
    int i;
    float rad;

    if (minigame_handle_tap(&asteroids_game)) return;

    rad = (float)ast_heading * AST_DEG2RAD;
    for (i = 0; i < AST_SHOT_MAX; i++) {
        if (ast_shots[i].active) continue;
        ast_shots[i].active = true;
        ast_shots[i].life = AST_SHOT_LIFE;
        ast_shots[i].x = ast_ship_x + cosf(rad) * (float)(AST_SHIP_R + 2);
        ast_shots[i].y = ast_ship_y + sinf(rad) * (float)(AST_SHIP_R + 2);
        /* Inherits the ship's momentum, so a shot fired while drifting
           backwards does not hang in front of you. */
        ast_shots[i].vx = ast_ship_vx + cosf(rad) * AST_SHOT_SPEED;
        ast_shots[i].vy = ast_ship_vy + sinf(rad) * AST_SHOT_SPEED;
        /* The shot exists between ticks; nothing else asks for those
           pixels until it has already moved on. */
        minigame_invalidate_box(screen_asteroids,
                                (int)ast_shots[i].x, (int)ast_shots[i].y,
                                (int)AST_SHOT_SPEED + AST_TOUCH_PAD);
        return;
    }
}

void asteroids_leave_screen(void) { minigame_leave(&asteroids_game); }
void build_asteroids_screen(void) { minigame_build(&asteroids_game); }
void open_asteroids_screen(void)  { minigame_open(&asteroids_game); }

// ---------- test accessors ----------
int asteroids_test_heading(void) { return ast_heading; }
int asteroids_test_rocks(void)   { return ast_rocks_left(); }
int asteroids_test_lives(void)   { return ast_lives; }
int asteroids_test_wave(void)    { return ast_wave; }

int asteroids_test_shots(void)
{
    int i, n = 0;
    for (i = 0; i < AST_SHOT_MAX; i++) if (ast_shots[i].active) n++;
    return n;
}

bool asteroids_test_ship(int *x, int *y)
{
    if (x != NULL) *x = (int)ast_ship_x;
    if (y != NULL) *y = (int)ast_ship_y;
    return true;
}

bool asteroids_test_nearest_rock(int *bearing_deg, int *distance)
{
    int i, best = -1;
    float best_d = 0.0f;

    for (i = 0; i < AST_ROCK_MAX; i++) {
        float d;
        if (!ast_rocks[i].active) continue;
        d = ast_dist(ast_ship_x, ast_ship_y, ast_rocks[i].x, ast_rocks[i].y);
        if (best < 0 || d < best_d) { best_d = d; best = i; }
    }
    if (best < 0) return false;
    if (bearing_deg != NULL) {
        *bearing_deg = ast_norm_angle((int)(atan2f(ast_rocks[best].y - ast_ship_y,
                                                   ast_rocks[best].x - ast_ship_x)
                                            * AST_RAD2DEG));
    }
    if (distance != NULL) *distance = (int)best_d;
    return true;
}

// ---------- drawing ----------
static void ast_line(lv_draw_ctx_t *ctx, lv_draw_line_dsc_t *dsc,
                     float x1, float y1, float x2, float y2)
{
    lv_point_t a, b;
    a.x = (lv_coord_t)x1; a.y = (lv_coord_t)y1;
    b.x = (lv_coord_t)x2; b.y = (lv_coord_t)y2;
    lv_draw_line(ctx, dsc, &a, &b);
}

/* Rocks as irregular polygons, the way the original drew them - a
   circle would read as a ball and invite you to expect it to bounce.
   The radii are a fixed jagged pattern rotated by the rock's own spin,
   so every rock looks hand-cut but costs nothing to generate. */
#define AST_ROCK_VERTS 9
static const int ast_rock_jag[AST_ROCK_VERTS] = {
    100, 78, 96, 70, 104, 84, 92, 108, 74,
};

static void ast_draw_rock(lv_draw_ctx_t *ctx, lv_draw_line_dsc_t *dsc,
                          const ast_rock_t *r)
{
    float base = (float)ast_rock_radius[r->size];
    float px = 0.0f, py = 0.0f;
    int v;

    for (v = 0; v <= AST_ROCK_VERTS; v++) {
        int k = v % AST_ROCK_VERTS;
        float a = (float)(r->spin + k * 360 / AST_ROCK_VERTS) * AST_DEG2RAD;
        float rad = base * (float)ast_rock_jag[k] / 100.0f;
        float x = r->x + cosf(a) * rad;
        float y = r->y + sinf(a) * rad;
        if (v > 0) ast_line(ctx, dsc, px, py, x, y);
        px = x; py = y;
    }
}

/* The ship: a long isosceles dart with a notched tail, and a flame
   behind it while the engine burns. */
static void ast_draw_ship(lv_draw_ctx_t *ctx)
{
    lv_draw_line_dsc_t hull, flame;
    float rad = (float)ast_heading * AST_DEG2RAD;
    float ca = cosf(rad), sa = sinf(rad);
    float nose_x, nose_y, lx, ly, rx, ry, tx, ty;

    /* Flash while reforming, so the player can see they are safe - and
       that it is about to stop being true. */
    if (ast_respawn_ticks > 0 && ((ast_respawn_ticks / 4) % 2)) return;

    lv_draw_line_dsc_init(&hull);
    hull.color = lv_color_hex(0xE8F1FF);
    hull.width = 2;
    hull.opa = LV_OPA_COVER;
    hull.round_start = 1;
    hull.round_end = 1;

    /* Points in ship space (nose, two wings, tail notch), rotated. */
#define AST_PT(px, py, ox, oy) do { \
        (ox) = ast_ship_x + (px) * ca - (py) * sa; \
        (oy) = ast_ship_y + (px) * sa + (py) * ca; \
    } while (0)
    AST_PT( 14.0f,  0.0f, nose_x, nose_y);
    AST_PT(-10.0f, -8.0f, lx, ly);
    AST_PT(-10.0f,  8.0f, rx, ry);
    AST_PT( -5.0f,  0.0f, tx, ty);

    ast_line(ctx, &hull, nose_x, nose_y, lx, ly);
    ast_line(ctx, &hull, nose_x, nose_y, rx, ry);
    ast_line(ctx, &hull, lx, ly, tx, ty);
    ast_line(ctx, &hull, rx, ry, tx, ty);

    if (ast_hold >= AST_HOLD_TO_THRUST) {
        float fx, fy, f1x, f1y, f2x, f2y;
        lv_draw_line_dsc_init(&flame);
        flame.color = lv_color_hex(0xFF7043);
        flame.width = 2;
        flame.opa = LV_OPA_COVER;
        /* Length flickers with the burn so it reads as fire, not a rod. */
        AST_PT(-6.0f - (float)(lv_tick_get() / 60 % 3) * 3.0f, 0.0f, fx, fy);
        AST_PT(-5.0f, -4.0f, f1x, f1y);
        AST_PT(-5.0f,  4.0f, f2x, f2y);
        ast_line(ctx, &flame, f1x, f1y, fx, fy);
        ast_line(ctx, &flame, f2x, f2y, fx, fy);
    }
#undef AST_PT
}

static void asteroids_draw(lv_event_t *e)
{
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_draw_line_dsc_t rock;
    lv_draw_rect_dsc_t shot;
    int i;

    lv_draw_line_dsc_init(&rock);
    rock.color = lv_color_hex(0x9E9E9E);
    rock.width = 2;
    rock.opa = LV_OPA_COVER;
    for (i = 0; i < AST_ROCK_MAX; i++) {
        if (!ast_rocks[i].active) continue;
        ast_draw_rock(ctx, &rock, &ast_rocks[i]);
    }

    lv_draw_rect_dsc_init(&shot);
    shot.bg_color = lv_color_hex(0xFFEB3B);
    shot.bg_opa = LV_OPA_COVER;
    shot.radius = LV_RADIUS_CIRCLE;
    for (i = 0; i < AST_SHOT_MAX; i++) {
        lv_area_t a;
        if (!ast_shots[i].active) continue;
        a.x1 = (lv_coord_t)(ast_shots[i].x - 2);
        a.y1 = (lv_coord_t)(ast_shots[i].y - 2);
        a.x2 = (lv_coord_t)(ast_shots[i].x + 2);
        a.y2 = (lv_coord_t)(ast_shots[i].y + 2);
        lv_draw_rect(ctx, &shot, &a);
    }

    ast_draw_ship(ctx);

    /* Lives, as little ships along the bottom of the disc. */
    {
        lv_draw_line_dsc_t mark;
        lv_draw_line_dsc_init(&mark);
        mark.color = lv_color_hex(0x80CBC4);
        mark.width = 2;
        mark.opa = LV_OPA_COVER;
        for (i = 0; i < ast_lives; i++) {
            float cx = (float)(AST_CX - 18 + i * 18);
            float cy = 322.0f;
            ast_line(ctx, &mark, cx, cy - 7, cx - 5, cy + 6);
            ast_line(ctx, &mark, cx, cy - 7, cx + 5, cy + 6);
            ast_line(ctx, &mark, cx - 5, cy + 6, cx + 5, cy + 6);
        }
    }
}
