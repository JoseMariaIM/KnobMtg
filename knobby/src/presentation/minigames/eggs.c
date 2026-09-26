#include "eggs.h"
#include "presentation/minigames/minigame.h"
#include "esp_random.h"
#include <string.h>

/* Catch the Egg: a basket slides along the bottom, eggs drop from the
 * top, the knob is the only control. Missing one costs a life; three
 * misses end the run.
 *
 * Lives rather than sudden death because the knob is an absolute-ish
 * control with real travel - a player who is simply on the wrong side
 * of the screen when an egg spawns cannot always get there, and ending
 * the whole run on that reads as the device's fault rather than theirs.
 *
 * The basket's track and the spawn band both sit inside the visible
 * circle: at the basket's y the chord is x ~ 46..314 (see round_safe.h
 * for the same math done generically). */

#define EGGS_TICK_MS        26
#define EGGS_BASKET_Y      296
#define EGGS_BASKET_W       58
#define EGGS_BASKET_H       26
#define EGGS_TRACK_LEFT     52
#define EGGS_TRACK_RIGHT   308
#define EGGS_BASKET_STEP    11   /* px per knob detent */

#define EGGS_SPAWN_LEFT     70
#define EGGS_SPAWN_RIGHT   290
#define EGGS_EGG_W          18
#define EGGS_EGG_H          24
#define EGGS_EGG_MAX         5
#define EGGS_START_Y        34

#define EGGS_FALL_START   2.5f
#define EGGS_FALL_MAX     7.4f
#define EGGS_FALL_RAMP  0.0022f
#define EGGS_SPAWN_TICKS_MIN 26
#define EGGS_SPAWN_TICKS_VAR 34

#define EGGS_LIVES           3
/* A golden egg is worth three and shows up about one time in eight -
   just enough to make the player commit to a cross-screen dash now and
   then instead of hovering at the centre. */
#define EGGS_GOLDEN_PERCENT   12
#define EGGS_GOLDEN_POINTS     3

typedef struct {
    bool  active;
    bool  golden;
    float x, y;   /* top-left */
} egg_t;

lv_obj_t *screen_eggs = NULL;

static int   eggs_basket_x;     /* centre */
static egg_t eggs_items[EGGS_EGG_MAX];
static float eggs_fall_speed;
static int   eggs_spawn_countdown;
static int   eggs_lives;
static int   eggs_flash;        /* ticks of "just missed" feedback left */

static void eggs_reset(minigame_t *g);
static void eggs_tick(minigame_t *g);
static void eggs_draw(lv_event_t *e);

static minigame_t eggs_game = {
    .screen   = &screen_eggs,
    .score_id = GAME_SCORE_EGGS,
    .hint     = STR_EGGS_HINT,
    .tick_ms  = EGGS_TICK_MS,
    .pausable = true,
    .partial_redraw = true,   /* see eggs_touch_everything_moving() */
    .on_reset = eggs_reset,
    .on_tick  = eggs_tick,
    .on_draw  = eggs_draw,
    .on_tap   = eggs_handle_tap,
};

/* ---------- partial redraw ----------
 *
 * Five eggs, a basket and three life dots on an otherwise empty
 * screen: about 3500 changing pixels out of 129600. Repainting the
 * lot 25 times a second cost ~259KB of QSPI and a full software render
 * per frame for scenery that never moves.
 *
 * eggs_touch_everything_moving() is called on both sides of the tick -
 * once over where things were, once over where they now are - so each
 * sprite's trail is repainted along with the sprite. Missing the first
 * call leaves a column of egg behind every egg. */
static void eggs_touch_egg(const egg_t *e)
{
    minigame_invalidate_rect(screen_eggs, (int)e->x - 2, (int)e->y - 2,
                             (int)e->x + EGGS_EGG_W + 2,
                             (int)e->y + EGGS_EGG_H + 2);
}

static void eggs_touch_basket(void)
{
    int half = EGGS_BASKET_W / 2;
    minigame_invalidate_rect(screen_eggs,
                             eggs_basket_x - half - 3, EGGS_BASKET_Y - 3,
                             eggs_basket_x + half + 3,
                             EGGS_BASKET_Y + EGGS_BASKET_H + 3);
}

/* The life dots and the miss flash - small, and only repainted on the
   ticks that can change them. */
static void eggs_touch_hud(void)
{
    minigame_invalidate_rect(screen_eggs, 118, 72, 120 + 3 * 16 + 12, 90);
}

static void eggs_touch_flash(void)
{
    minigame_invalidate_rect(screen_eggs, 58, 338, 302, 346);
}

static void eggs_touch_everything_moving(void)
{
    int i;
    for (i = 0; i < EGGS_EGG_MAX; i++) {
        if (eggs_items[i].active) eggs_touch_egg(&eggs_items[i]);
    }
    eggs_touch_basket();
}

static void eggs_spawn(void)
{
    int i;
    for (i = 0; i < EGGS_EGG_MAX; i++) {
        if (eggs_items[i].active) continue;
        eggs_items[i].active = true;
        eggs_items[i].golden = (esp_random() % 100) < EGGS_GOLDEN_PERCENT;
        eggs_items[i].x = (float)(EGGS_SPAWN_LEFT +
            (int)(esp_random() % (uint32_t)(EGGS_SPAWN_RIGHT - EGGS_SPAWN_LEFT + 1)));
        eggs_items[i].y = (float)EGGS_START_Y;
        eggs_spawn_countdown = EGGS_SPAWN_TICKS_MIN +
            (int)(esp_random() % EGGS_SPAWN_TICKS_VAR);
        return;
    }
}

static void eggs_reset(minigame_t *g)
{
    (void)g;
    memset(eggs_items, 0, sizeof(eggs_items));
    eggs_basket_x = (EGGS_TRACK_LEFT + EGGS_TRACK_RIGHT) / 2;
    eggs_fall_speed = EGGS_FALL_START;
    eggs_spawn_countdown = EGGS_SPAWN_TICKS_MIN;
    eggs_lives = EGGS_LIVES;
    eggs_flash = 0;
    /* Everything moved at once; cheaper to say so than to enumerate
       where each thing used to be. */
    if (screen_eggs != NULL) lv_obj_invalidate(screen_eggs);
}

/* Caught when the egg's base reaches the basket's rim while its centre
   is over the opening. The rim band is deliberately a few px deep so a
   fast egg can't tunnel past it between two ticks. */
static bool eggs_caught(const egg_t *e)
{
    int egg_cx = (int)e->x + EGGS_EGG_W / 2;
    int egg_base = (int)e->y + EGGS_EGG_H;
    int half = EGGS_BASKET_W / 2;

    if (egg_base < EGGS_BASKET_Y) return false;
    if (egg_base > EGGS_BASKET_Y + EGGS_BASKET_H) return false;
    return egg_cx >= eggs_basket_x - half && egg_cx <= eggs_basket_x + half;
}

static void eggs_tick(minigame_t *g)
{
    int i;
    int lives_before = eggs_lives;

    eggs_touch_everything_moving();   /* where they were */

    if (eggs_flash > 0) {
        eggs_flash--;
        eggs_touch_flash();
    }
    if (eggs_fall_speed < EGGS_FALL_MAX) eggs_fall_speed += EGGS_FALL_RAMP;

    if (--eggs_spawn_countdown <= 0) eggs_spawn();

    for (i = 0; i < EGGS_EGG_MAX; i++) {
        if (!eggs_items[i].active) continue;
        eggs_items[i].y += eggs_fall_speed;

        if (eggs_caught(&eggs_items[i])) {
            minigame_add_score(g, eggs_items[i].golden ? EGGS_GOLDEN_POINTS : 1);
            eggs_items[i].active = false;
            continue;
        }
        if (eggs_items[i].y > 360.0f) {
            eggs_items[i].active = false;
            eggs_flash = 6;
            eggs_touch_flash();
            if (--eggs_lives <= 0) {
                eggs_touch_hud();
                minigame_over(g);
                return;
            }
        }
    }

    eggs_touch_everything_moving();   /* and where they are now */
    if (eggs_lives != lives_before) eggs_touch_hud();
}

void eggs_turn(int dir)
{
    if (minigame_handle_turn_start(&eggs_game)) return;

    eggs_touch_basket();
    eggs_basket_x += dir * EGGS_BASKET_STEP;
    if (eggs_basket_x < EGGS_TRACK_LEFT)  eggs_basket_x = EGGS_TRACK_LEFT;
    if (eggs_basket_x > EGGS_TRACK_RIGHT) eggs_basket_x = EGGS_TRACK_RIGHT;
    eggs_touch_basket();
}

void eggs_handle_tap(void)
{
    /* Nothing to do mid-run: the knob is the whole game. The framework
       still owns start / restart / resume. */
    if (minigame_handle_tap(&eggs_game)) return;
    minigame_toggle_pause(&eggs_game);
}

void eggs_leave_screen(void) { minigame_leave(&eggs_game); }
void build_eggs_screen(void) { minigame_build(&eggs_game); }
void open_eggs_screen(void)  { minigame_open(&eggs_game); }

// ---------- test accessors ----------
int eggs_test_basket_x(void) { return eggs_basket_x; }
int eggs_test_lives(void)    { return eggs_lives; }

bool eggs_test_lowest_egg(int *x, int *y)
{
    float best = -1.0f;
    int i;
    for (i = 0; i < EGGS_EGG_MAX; i++) {
        if (!eggs_items[i].active) continue;
        if (eggs_items[i].y > best) {
            best = eggs_items[i].y;
            if (x != NULL) *x = (int)eggs_items[i].x + EGGS_EGG_W / 2;
        }
    }
    if (best < 0.0f) return false;
    if (y != NULL) *y = (int)best;
    return true;
}

// ---------- drawing ----------
static void eggs_fill(lv_draw_ctx_t *ctx, lv_draw_rect_dsc_t *dsc,
                      int x1, int y1, int x2, int y2)
{
    lv_area_t a;
    a.x1 = (lv_coord_t)x1; a.y1 = (lv_coord_t)y1;
    a.x2 = (lv_coord_t)x2; a.y2 = (lv_coord_t)y2;
    lv_draw_rect(ctx, dsc, &a);
}

static void eggs_draw_basket(lv_draw_ctx_t *ctx)
{
    lv_draw_rect_dsc_t rim, weave;
    int half = EGGS_BASKET_W / 2;
    int left = eggs_basket_x - half;
    int right = eggs_basket_x + half;
    int i;

    lv_draw_rect_dsc_init(&rim);
    rim.bg_color = lv_color_hex(0x8D6E63);
    rim.bg_opa = LV_OPA_COVER;
    rim.radius = 4;
    /* rim lip, then the body a touch narrower so it reads as a basket
       rather than a block */
    eggs_fill(ctx, &rim, left, EGGS_BASKET_Y, right, EGGS_BASKET_Y + 7);

    rim.bg_color = lv_color_hex(0x6D4C41);
    eggs_fill(ctx, &rim, left + 4, EGGS_BASKET_Y + 7,
              right - 4, EGGS_BASKET_Y + EGGS_BASKET_H);

    lv_draw_rect_dsc_init(&weave);
    weave.bg_color = lv_color_hex(0x4E342E);
    weave.bg_opa = LV_OPA_COVER;
    for (i = 1; i < 4; i++) {
        int wx = left + 6 + i * (EGGS_BASKET_W - 12) / 4;
        eggs_fill(ctx, &weave, wx, EGGS_BASKET_Y + 9,
                  wx + 2, EGGS_BASKET_Y + EGGS_BASKET_H - 2);
    }
}

static void eggs_draw(lv_event_t *e)
{
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_draw_rect_dsc_t egg, life;
    int i;

    /* Remaining lives, as eggs, top-left of the HUD row. */
    lv_draw_rect_dsc_init(&life);
    life.bg_opa = LV_OPA_COVER;
    life.radius = LV_RADIUS_CIRCLE;
    for (i = 0; i < EGGS_LIVES; i++) {
        life.bg_color = (i < eggs_lives) ? lv_color_hex(0xFFF3E0)
                                         : lv_color_hex(0x3A3A3A);
        eggs_fill(ctx, &life, 120 + i * 16, 74, 120 + i * 16 + 10, 88);
    }

    lv_draw_rect_dsc_init(&egg);
    egg.bg_opa = LV_OPA_COVER;
    /* A large radius on a taller-than-wide box is as close to an oval as
       this LVGL build's rect primitive gets. */
    egg.radius = 9;
    for (i = 0; i < EGGS_EGG_MAX; i++) {
        if (!eggs_items[i].active) continue;
        egg.bg_color = eggs_items[i].golden ? lv_color_hex(0xFFC107)
                                            : lv_color_hex(0xFFF8E1);
        eggs_fill(ctx, &egg, (int)eggs_items[i].x, (int)eggs_items[i].y,
                  (int)eggs_items[i].x + EGGS_EGG_W,
                  (int)eggs_items[i].y + EGGS_EGG_H);
    }

    /* A miss flashes the floor line red - without it a life simply
       vanishes from the HUD with no explanation of when or why. */
    if (eggs_flash > 0) {
        lv_draw_rect_dsc_t flash;
        lv_draw_rect_dsc_init(&flash);
        flash.bg_color = lv_color_hex(0xE53935);
        flash.bg_opa = LV_OPA_70;
        eggs_fill(ctx, &flash, 60, 340, 300, 344);
    }

    eggs_draw_basket(ctx);
}
