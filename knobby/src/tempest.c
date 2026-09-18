#include "tempest.h"
#include "minigame.h"
#include "esp_random.h"
#include <math.h>
#include <string.h>

/* A tube shooter in the Tempest mould, and the game the round panel
 * flatters most: the well recedes to a vanishing point at the centre,
 * you run around its rim, and everything climbs toward you.
 *
 * The knob moves the claw from lane to lane - one detent, one lane, so
 * the control is discrete and you always know where you will land,
 * unlike the continuous sweep Pong and Breakout use. A tap fires down
 * whichever lane you are standing in.
 *
 * Perspective comes from one simple rule: a thing at depth d (1.0 at
 * the centre, 0.0 at the rim) sits at radius TEM_R_RIM - d * span and
 * is drawn proportionally smaller. That single mapping makes the well
 * read as a tube without any 3D machinery. */

#define TEM_CX 180
#define TEM_CY 180
#define TEM_R_RIM   164       /* the lip of the well, where the claw runs */
#define TEM_R_THROAT 26       /* the far end - enemies are born here */

#define TEM_TICK_MS   26
#define TEM_LANES     12      /* 30 degrees each */
#define TEM_LANE_DEG  (360 / TEM_LANES)

#define TEM_SHOT_MAX      6
#define TEM_SHOT_SPEED 0.075f /* depth units per tick: rim to throat in ~13 */

#define TEM_ENEMY_MAX     10
#define TEM_ENEMY_BASE  0.0075f  /* climb per tick at level 1 */
#define TEM_ENEMY_LEVEL 0.0016f  /* added per level */
#define TEM_SPAWN_TICKS_MIN 26
#define TEM_SPAWN_TICKS_VAR 40
/* An enemy that reaches the rim takes a life; one that gets close
   enough to matter is worth seeing early, which is what the widening
   sprite does for free. */
#define TEM_ENEMY_POINTS  25
#define TEM_LEVEL_BONUS  150
#define TEM_LEVEL_KILLS   12  /* kills needed to advance */

#define TEM_LIVES          3
#define TEM_HIT_FLASH     10

#define TEM_DEG2RAD 0.017453292f

typedef struct {
    bool  active;
    int   lane;
    float depth;      /* 1.0 at the throat, 0.0 at the rim */
    int   wiggle;     /* drawn limb animation */
} tem_enemy_t;

typedef struct {
    bool  active;
    int   lane;
    float depth;
} tem_shot_t;

lv_obj_t *screen_tempest = NULL;

static int  tem_lane;          /* the claw's lane */
static tem_enemy_t tem_enemies[TEM_ENEMY_MAX];
static tem_shot_t  tem_shots[TEM_SHOT_MAX];
static int  tem_spawn_countdown;
static int  tem_lives;
static int  tem_level;
static int  tem_kills;
static int  tem_hit_flash;
static int  tem_anim;

static void tempest_reset(minigame_t *g);
static void tempest_tick(minigame_t *g);
static void tempest_draw(lv_event_t *e);

static minigame_t tempest_game = {
    .screen   = &screen_tempest,
    .score_id = GAME_SCORE_TEMPEST,
    .hint     = STR_TEMPEST_HINT,
    .tick_ms  = TEM_TICK_MS,
    .pausable = true,
    .on_reset = tempest_reset,
    .on_tick  = tempest_tick,
    .on_draw  = tempest_draw,
    .on_tap   = tempest_handle_tap,
};

/* Depth 1.0 (throat) .. 0.0 (rim) -> screen radius. */
static float tem_radius_at(float depth)
{
    if (depth < 0.0f) depth = 0.0f;
    if (depth > 1.0f) depth = 1.0f;
    return (float)TEM_R_RIM - depth * (float)(TEM_R_RIM - TEM_R_THROAT);
}

static int tem_lane_centre_deg(int lane)
{
    return lane * TEM_LANE_DEG + TEM_LANE_DEG / 2;
}

static void tem_point(int lane_deg, float depth, float *x, float *y)
{
    float r = tem_radius_at(depth);
    float a = (float)lane_deg * TEM_DEG2RAD;
    *x = (float)TEM_CX + cosf(a) * r;
    *y = (float)TEM_CY + sinf(a) * r;
}

static float tem_climb_rate(void)
{
    return TEM_ENEMY_BASE + (float)(tem_level - 1) * TEM_ENEMY_LEVEL;
}

static void tem_spawn_enemy(void)
{
    int i;
    for (i = 0; i < TEM_ENEMY_MAX; i++) {
        if (tem_enemies[i].active) continue;
        tem_enemies[i].active = true;
        tem_enemies[i].lane = (int)(esp_random() % TEM_LANES);
        tem_enemies[i].depth = 1.0f;
        tem_enemies[i].wiggle = (int)(esp_random() % 100);
        tem_spawn_countdown = TEM_SPAWN_TICKS_MIN +
                              (int)(esp_random() % TEM_SPAWN_TICKS_VAR);
        return;
    }
}

static void tempest_reset(minigame_t *g)
{
    (void)g;
    tem_lane = 0;
    memset(tem_enemies, 0, sizeof(tem_enemies));
    memset(tem_shots, 0, sizeof(tem_shots));
    tem_spawn_countdown = TEM_SPAWN_TICKS_MIN;
    tem_lives = TEM_LIVES;
    tem_level = 1;
    tem_kills = 0;
    tem_hit_flash = 0;
    tem_anim = 0;
}

static void tem_lose_life(minigame_t *g)
{
    tem_hit_flash = TEM_HIT_FLASH;
    /* Clear the well rather than leave the player pinned the instant
       they recover - an enemy at the rim would take the next life too. */
    memset(tem_enemies, 0, sizeof(tem_enemies));
    memset(tem_shots, 0, sizeof(tem_shots));
    tem_spawn_countdown = TEM_SPAWN_TICKS_MIN;
    if (--tem_lives <= 0) minigame_over(g);
}

static void tempest_tick(minigame_t *g)
{
    float climb = tem_climb_rate();
    int i, j;

    tem_anim++;
    if (tem_hit_flash > 0) tem_hit_flash--;

    if (--tem_spawn_countdown <= 0) tem_spawn_enemy();

    /* ---- shots travel away from you, down the well ---- */
    for (i = 0; i < TEM_SHOT_MAX; i++) {
        if (!tem_shots[i].active) continue;
        tem_shots[i].depth += TEM_SHOT_SPEED;
        if (tem_shots[i].depth > 1.0f) {
            tem_shots[i].active = false;
            continue;
        }
        for (j = 0; j < TEM_ENEMY_MAX; j++) {
            if (!tem_enemies[j].active) continue;
            if (tem_enemies[j].lane != tem_shots[i].lane) continue;
            /* Same lane and close in depth: the shot has caught up with
               it. The window is a shade wider than one tick of closing
               speed so a pass-through is not possible. */
            if (fabsf(tem_enemies[j].depth - tem_shots[i].depth) >
                TEM_SHOT_SPEED + climb) continue;
            tem_enemies[j].active = false;
            tem_shots[i].active = false;
            tem_kills++;
            minigame_add_score(g, TEM_ENEMY_POINTS);
            break;
        }
    }

    /* ---- enemies climb toward the rim ---- */
    for (i = 0; i < TEM_ENEMY_MAX; i++) {
        if (!tem_enemies[i].active) continue;
        tem_enemies[i].depth -= climb;
        tem_enemies[i].wiggle++;
        if (tem_enemies[i].depth > 0.0f) continue;

        /* Reached the lip. Only your own lane can hurt you - elsewhere
           it climbs out and is simply gone, which keeps the game about
           choosing which lane to defend. */
        if (tem_enemies[i].lane == tem_lane) {
            tem_lose_life(g);
            return;
        }
        tem_enemies[i].active = false;
    }

    if (tem_kills >= TEM_LEVEL_KILLS) {
        tem_kills = 0;
        tem_level++;
        minigame_add_score(g, TEM_LEVEL_BONUS);
    }
}

void tempest_turn(int dir)
{
    if (minigame_handle_turn_start(&tempest_game)) return;

    /* One detent, one lane, and the rim wraps. */
    tem_lane = (tem_lane + dir + TEM_LANES) % TEM_LANES;
    lv_obj_invalidate(screen_tempest);
}

void tempest_handle_tap(void)
{
    int i;

    if (minigame_handle_tap(&tempest_game)) return;

    for (i = 0; i < TEM_SHOT_MAX; i++) {
        if (tem_shots[i].active) continue;
        tem_shots[i].active = true;
        tem_shots[i].lane = tem_lane;
        tem_shots[i].depth = 0.0f;
        return;
    }
}

void tempest_leave_screen(void) { minigame_leave(&tempest_game); }
void build_tempest_screen(void) { minigame_build(&tempest_game); }
void open_tempest_screen(void)  { minigame_open(&tempest_game); }

// ---------- test accessors ----------
int tempest_test_lane(void)       { return tem_lane; }
int tempest_test_lane_count(void) { return TEM_LANES; }
int tempest_test_lives(void)      { return tem_lives; }
int tempest_test_level(void)      { return tem_level; }

int tempest_test_enemies(void)
{
    int i, n = 0;
    for (i = 0; i < TEM_ENEMY_MAX; i++) if (tem_enemies[i].active) n++;
    return n;
}

int tempest_test_shots(void)
{
    int i, n = 0;
    for (i = 0; i < TEM_SHOT_MAX; i++) if (tem_shots[i].active) n++;
    return n;
}

bool tempest_test_closest_enemy(int *lane, int *climb_pct)
{
    int i, best = -1;
    float best_depth = 2.0f;

    for (i = 0; i < TEM_ENEMY_MAX; i++) {
        if (!tem_enemies[i].active) continue;
        if (tem_enemies[i].depth < best_depth) {
            best_depth = tem_enemies[i].depth;
            best = i;
        }
    }
    if (best < 0) return false;
    if (lane != NULL) *lane = tem_enemies[best].lane;
    if (climb_pct != NULL) *climb_pct = (int)((1.0f - best_depth) * 100.0f);
    return true;
}

// ---------- drawing ----------
static void tem_line(lv_draw_ctx_t *ctx, lv_draw_line_dsc_t *dsc,
                     float x1, float y1, float x2, float y2)
{
    lv_point_t a, b;
    a.x = (lv_coord_t)x1; a.y = (lv_coord_t)y1;
    b.x = (lv_coord_t)x2; b.y = (lv_coord_t)y2;
    lv_draw_line(ctx, dsc, &a, &b);
}

/* The well: a rim polygon, a throat polygon, and a spoke joining them
   at every lane boundary. Drawing the two rings as straight chords
   rather than arcs is deliberate - the faceting is what makes it look
   like a tube built of flat panels instead of a flat bullseye. */
static void tem_draw_well(lv_draw_ctx_t *ctx)
{
    lv_draw_line_dsc_t wire;
    int i;

    lv_draw_line_dsc_init(&wire);
    wire.color = lv_color_hex(0x1E3A5F);
    wire.width = 2;
    wire.opa = LV_OPA_COVER;

    for (i = 0; i < TEM_LANES; i++) {
        float ax, ay, bx, by, cx, cy, dx, dy;
        int a_deg = i * TEM_LANE_DEG;
        int b_deg = (i + 1) * TEM_LANE_DEG;

        tem_point(a_deg, 0.0f, &ax, &ay);
        tem_point(b_deg, 0.0f, &bx, &by);
        tem_point(a_deg, 1.0f, &cx, &cy);
        tem_point(b_deg, 1.0f, &dx, &dy);

        tem_line(ctx, &wire, ax, ay, bx, by);   /* rim edge */
        tem_line(ctx, &wire, cx, cy, dx, dy);   /* throat edge */
        tem_line(ctx, &wire, ax, ay, cx, cy);   /* spoke */
    }
}

/* The claw: a bracket straddling the lane at the rim, with two prongs
   reaching a little way down the well - the shape that says "I am here,
   and this is the lane I am pointing into". */
static void tem_draw_claw(lv_draw_ctx_t *ctx)
{
    lv_draw_line_dsc_t dsc;
    float ax, ay, bx, by, cx, cy, dx, dy, mx, my;
    int a_deg = tem_lane * TEM_LANE_DEG;
    int b_deg = (tem_lane + 1) * TEM_LANE_DEG;

    lv_draw_line_dsc_init(&dsc);
    dsc.color = (tem_hit_flash > 0 && (tem_hit_flash % 2))
                    ? lv_color_hex(0xEF5350) : lv_color_hex(0xFFEB3B);
    dsc.width = 3;
    dsc.opa = LV_OPA_COVER;
    dsc.round_start = 1;
    dsc.round_end = 1;

    tem_point(a_deg, 0.0f, &ax, &ay);
    tem_point(b_deg, 0.0f, &bx, &by);
    tem_point(a_deg, 0.16f, &cx, &cy);
    tem_point(b_deg, 0.16f, &dx, &dy);
    tem_point(tem_lane_centre_deg(tem_lane), 0.26f, &mx, &my);

    tem_line(ctx, &dsc, ax, ay, bx, by);
    tem_line(ctx, &dsc, ax, ay, cx, cy);
    tem_line(ctx, &dsc, bx, by, dx, dy);
    tem_line(ctx, &dsc, cx, cy, mx, my);
    tem_line(ctx, &dsc, dx, dy, mx, my);
}

/* An enemy as a flanged spinner, scaled by how close it is. The size
   doing the work of distance is the whole trick: you read the threat
   before you read the depth. */
static void tem_draw_enemy(lv_draw_ctx_t *ctx, const tem_enemy_t *en)
{
    lv_draw_line_dsc_t dsc;
    float cx, cy;
    float scale = (1.0f - en->depth) * 0.85f + 0.15f;
    float w = 15.0f * scale;
    float h = 11.0f * scale;
    bool flap = ((en->wiggle / 6) % 2) == 0;
    int deg = tem_lane_centre_deg(en->lane);
    float ux, uy;

    tem_point(deg, en->depth, &cx, &cy);
    /* Unit vector pointing outward along the lane, so the sprite is
       oriented to the well rather than to the screen. */
    ux = cosf((float)deg * TEM_DEG2RAD);
    uy = sinf((float)deg * TEM_DEG2RAD);

    lv_draw_line_dsc_init(&dsc);
    dsc.color = lv_color_hex(0xAB47BC);
    dsc.width = 2;
    dsc.opa = LV_OPA_COVER;

    {
        /* Perpendicular, for the cross-bar. */
        float px = -uy, py = ux;
        float span = flap ? w : w * 0.6f;
        float l1x = cx - px * span, l1y = cy - py * span;
        float l2x = cx + px * span, l2y = cy + py * span;
        float fx = cx + ux * h, fy = cy + uy * h;   /* front, toward the rim */
        float bx = cx - ux * h, by = cy - uy * h;

        tem_line(ctx, &dsc, l1x, l1y, l2x, l2y);
        tem_line(ctx, &dsc, l1x, l1y, fx, fy);
        tem_line(ctx, &dsc, l2x, l2y, fx, fy);
        tem_line(ctx, &dsc, l1x, l1y, bx, by);
        tem_line(ctx, &dsc, l2x, l2y, bx, by);
    }
}

static void tempest_draw(lv_event_t *e)
{
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_draw_rect_dsc_t shot, life;
    int i;

    tem_draw_well(ctx);

    /* Enemies far to near, so a close one overlaps a distant one the
       way it should. */
    {
        int pass;
        for (pass = 10; pass >= 0; pass--) {
            for (i = 0; i < TEM_ENEMY_MAX; i++) {
                if (!tem_enemies[i].active) continue;
                if ((int)(tem_enemies[i].depth * 10.0f) != pass) continue;
                tem_draw_enemy(ctx, &tem_enemies[i]);
            }
        }
    }

    lv_draw_rect_dsc_init(&shot);
    shot.bg_color = lv_color_hex(0x80DEEA);
    shot.bg_opa = LV_OPA_COVER;
    shot.radius = LV_RADIUS_CIRCLE;
    for (i = 0; i < TEM_SHOT_MAX; i++) {
        lv_area_t a;
        float x, y;
        int half;
        if (!tem_shots[i].active) continue;
        tem_point(tem_lane_centre_deg(tem_shots[i].lane), tem_shots[i].depth, &x, &y);
        /* Shots shrink as they recede, same rule as everything else. */
        half = 2 + (int)((1.0f - tem_shots[i].depth) * 3.0f);
        a.x1 = (lv_coord_t)(x - half); a.y1 = (lv_coord_t)(y - half);
        a.x2 = (lv_coord_t)(x + half); a.y2 = (lv_coord_t)(y + half);
        lv_draw_rect(ctx, &shot, &a);
    }

    tem_draw_claw(ctx);

    /* Lives, in the throat - the only part of the screen nothing else
       ever reaches. */
    lv_draw_rect_dsc_init(&life);
    life.bg_opa = LV_OPA_COVER;
    life.radius = 2;
    for (i = 0; i < TEM_LIVES; i++) {
        lv_area_t a;
        int x = TEM_CX - 13 + i * 13;
        life.bg_color = (i < tem_lives) ? lv_color_hex(0xFFEB3B)
                                        : lv_color_hex(0x33332A);
        a.x1 = (lv_coord_t)(x - 3); a.y1 = (lv_coord_t)(TEM_CY - 3);
        a.x2 = (lv_coord_t)(x + 3); a.y2 = (lv_coord_t)(TEM_CY + 3);
        lv_draw_rect(ctx, &life, &a);
    }
}
