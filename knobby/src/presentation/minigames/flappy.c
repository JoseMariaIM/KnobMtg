#include "flappy.h"
#include "minigame.h"
#include "esp_random.h"
#include <math.h>
#include <string.h>

/* Flappy Bird on a 360x360 round panel. The bird holds a fixed x and
 * the pipes scroll past it; one input (tap OR knob detent, either
 * direction - the user asked for both) gives it an upward impulse that
 * gravity immediately starts eating.
 *
 * Round-glass note: pipes are drawn from the very top and bottom of the
 * canvas, so their far ends fall outside the visible circle and simply
 * disappear under the bezel - which reads correctly, as pipes running
 * off-screen. What must stay visible is the gap, so FLAPPY_GAP_MIN_Y /
 * FLAPPY_GAP_MAX_Y keep every gap centre well inside the circle. */

#define FLAPPY_TICK_MS      28
#define FLAPPY_BIRD_X      128
#define FLAPPY_BIRD_W       26
#define FLAPPY_BIRD_H       20
#define FLAPPY_GRAVITY    0.62f
#define FLAPPY_FLAP     -7.40f
#define FLAPPY_MAX_FALL  11.0f

/* The boundary is the glass itself - there is no drawn floor and no
   ceiling, just the round edge of the panel. A horizontal brown strip
   was a line the player had to learn; the bezel is a limit they can
   already see, and on a circular display it is the honest one: the
   playable space really is a disc, so pretending its top and bottom
   are straight wasted the middle and lied about the corners.
   Radius is a shade inside the visible circle so the bird is lost at
   the edge rather than after vanishing under it. */
#define FLAPPY_RIM_R       172
#define FLAPPY_BIRD_HALF    10   /* half the bird's height, for the rim test */

#define FLAPPY_PIPE_W       42
#define FLAPPY_PIPE_GAP    104   /* vertical opening, generous: one button */
#define FLAPPY_GAP_MIN_Y   112
#define FLAPPY_GAP_MAX_Y   248
#define FLAPPY_PIPE_COUNT    3
#define FLAPPY_PIPE_SPACING 168  /* px between consecutive pipes */
#define FLAPPY_SPEED       3.4f

typedef struct {
    bool  active;
    bool  scored;
    float x;          /* left edge */
    int   gap_center;
} flappy_pipe_t;

lv_obj_t *screen_flappy = NULL;

static float flappy_y;
static float flappy_vy;
static flappy_pipe_t flappy_pipes[FLAPPY_PIPE_COUNT];
static float flappy_spawn_x;   /* x the next pipe enters at */

static void flappy_reset(minigame_t *g);
static void flappy_tick(minigame_t *g);
static void flappy_draw(lv_event_t *e);

static minigame_t flappy_game = {
    .screen   = &screen_flappy,
    .score_id = GAME_SCORE_FLAPPY,
    .hint     = STR_FLAPPY_HINT,
    .tick_ms  = FLAPPY_TICK_MS,
    .pausable = false,          /* nothing to pause into: one continuous fall */
    .on_reset = flappy_reset,
    .on_tick  = flappy_tick,
    .on_draw  = flappy_draw,
    .on_tap   = flappy_handle_tap,
    .tap_on_press = true,     /* an action game: fire on finger-down */
};

static int flappy_random_gap(void)
{
    return FLAPPY_GAP_MIN_Y +
           (int)(esp_random() % (uint32_t)(FLAPPY_GAP_MAX_Y - FLAPPY_GAP_MIN_Y + 1));
}

static void flappy_spawn_pipe(void)
{
    int i;
    for (i = 0; i < FLAPPY_PIPE_COUNT; i++) {
        if (flappy_pipes[i].active) continue;
        flappy_pipes[i].active = true;
        flappy_pipes[i].scored = false;
        flappy_pipes[i].x = flappy_spawn_x;
        flappy_pipes[i].gap_center = flappy_random_gap();
        flappy_spawn_x += FLAPPY_PIPE_SPACING;
        return;
    }
}

static void flappy_reset(minigame_t *g)
{
    (void)g;
    memset(flappy_pipes, 0, sizeof(flappy_pipes));
    flappy_y = 160.0f;
    flappy_vy = 0.0f;
    /* First pipe starts off-screen right with a full screen of runway,
       so the opening seconds are free flight rather than an immediate
       reaction test. */
    flappy_spawn_x = 400.0f;
    flappy_spawn_pipe();
    flappy_spawn_pipe();
}

/* The bird's box is trimmed a couple of px on each side: at this size a
   pixel-exact hitbox reads as unfair when the beak clips a pipe edge. */
static bool flappy_hits(const flappy_pipe_t *p)
{
    int bx1 = FLAPPY_BIRD_X + 2;
    int bx2 = FLAPPY_BIRD_X + FLAPPY_BIRD_W - 2;
    int by1 = (int)flappy_y + 2;
    int by2 = (int)flappy_y + FLAPPY_BIRD_H - 2;
    int px1 = (int)p->x;
    int px2 = px1 + FLAPPY_PIPE_W;
    int gap_top = p->gap_center - FLAPPY_PIPE_GAP / 2;
    int gap_bottom = p->gap_center + FLAPPY_PIPE_GAP / 2;

    if (bx2 <= px1 || bx1 >= px2) return false;
    return by1 < gap_top || by2 > gap_bottom;
}

static void flappy_tick(minigame_t *g)
{
    int i;

    flappy_vy += FLAPPY_GRAVITY;
    if (flappy_vy > FLAPPY_MAX_FALL) flappy_vy = FLAPPY_MAX_FALL;
    flappy_y += flappy_vy;

    /* Touching the rim anywhere is fatal, top or bottom alike - a bird
       parked against the roof would otherwise be a safe strategy.
       Expressed against the disc rather than as two y limits: the bird
       holds a fixed x today, so this reduces to the same pair of
       numbers, but it says what the rule actually is. */
    {
        float cx = (float)(FLAPPY_BIRD_X + FLAPPY_BIRD_W / 2) - 180.0f;
        float cy = flappy_y + (float)FLAPPY_BIRD_H / 2.0f - 180.0f;
        if (sqrtf(cx * cx + cy * cy) + (float)FLAPPY_BIRD_HALF > (float)FLAPPY_RIM_R) {
            minigame_over(g);
            return;
        }
    }

    flappy_spawn_x -= FLAPPY_SPEED;
    for (i = 0; i < FLAPPY_PIPE_COUNT; i++) {
        if (!flappy_pipes[i].active) continue;
        flappy_pipes[i].x -= FLAPPY_SPEED;

        if (!flappy_pipes[i].scored &&
            flappy_pipes[i].x + FLAPPY_PIPE_W < FLAPPY_BIRD_X) {
            flappy_pipes[i].scored = true;
            minigame_add_score(g, 1);
        }
        if (flappy_hits(&flappy_pipes[i])) {
            minigame_over(g);
            return;
        }
        if (flappy_pipes[i].x + FLAPPY_PIPE_W < -8.0f) {
            flappy_pipes[i].active = false;
        }
    }

    if (flappy_spawn_x < 380.0f) flappy_spawn_pipe();
}

static void flappy_flap(void)
{
    flappy_vy = FLAPPY_FLAP;
}

void flappy_turn(int dir)
{
    (void)dir; /* either direction flaps - one-input game */
    if (minigame_handle_turn_start(&flappy_game)) return;
    flappy_flap();
    lv_obj_invalidate(screen_flappy);
}

void flappy_handle_tap(void)
{
    if (minigame_handle_tap(&flappy_game)) return;
    flappy_flap();
}

void flappy_leave_screen(void) { minigame_leave(&flappy_game); }
void build_flappy_screen(void) { minigame_build(&flappy_game); }

void open_flappy_screen(void)
{
    minigame_open(&flappy_game);
}

// ---------- test accessors ----------
int flappy_test_bird_y(void)
{
    return (int)flappy_y;
}

bool flappy_test_next_gap(int *dx, int *gap_center)
{
    int best = 0;
    bool found = false;
    int i;

    for (i = 0; i < FLAPPY_PIPE_COUNT; i++) {
        int d;
        if (!flappy_pipes[i].active) continue;
        d = (int)flappy_pipes[i].x + FLAPPY_PIPE_W - FLAPPY_BIRD_X;
        if (d < 0) continue;  /* already cleared */
        if (!found || d < best) {
            best = d;
            found = true;
            if (gap_center != NULL) *gap_center = flappy_pipes[i].gap_center;
        }
    }
    if (found && dx != NULL) *dx = best;
    return found;
}

// ---------- drawing ----------
static void flappy_fill(lv_draw_ctx_t *ctx, lv_draw_rect_dsc_t *dsc,
                        int x1, int y1, int x2, int y2)
{
    lv_area_t a;
    a.x1 = (lv_coord_t)x1; a.y1 = (lv_coord_t)y1;
    a.x2 = (lv_coord_t)x2; a.y2 = (lv_coord_t)y2;
    lv_draw_rect(ctx, dsc, &a);
}

static void flappy_draw_pipe(lv_draw_ctx_t *ctx, lv_draw_rect_dsc_t *body,
                             lv_draw_rect_dsc_t *lip, const flappy_pipe_t *p)
{
    int x = (int)p->x;
    int gap_top = p->gap_center - FLAPPY_PIPE_GAP / 2;
    int gap_bottom = p->gap_center + FLAPPY_PIPE_GAP / 2;

    flappy_fill(ctx, body, x, 0, x + FLAPPY_PIPE_W, gap_top);
    flappy_fill(ctx, body, x, gap_bottom, x + FLAPPY_PIPE_W, 360);
    /* Wider lips at the gap edges: the classic silhouette, and they make
       the opening's exact height obvious at a glance. */
    flappy_fill(ctx, lip, x - 4, gap_top - 14, x + FLAPPY_PIPE_W + 4, gap_top);
    flappy_fill(ctx, lip, x - 4, gap_bottom, x + FLAPPY_PIPE_W + 4, gap_bottom + 14);
}

static void flappy_draw_bird(lv_draw_ctx_t *ctx)
{
    lv_draw_rect_dsc_t body, beak, eye;
    int x = FLAPPY_BIRD_X;
    int y = (int)flappy_y;
    /* Wing beats with the bird's own motion rather than a timer: it is
       up while rising and down while falling, which also gives the
       player a second read on their vertical speed. */
    bool rising = flappy_vy < 0.0f;

    lv_draw_rect_dsc_init(&body);
    body.bg_color = lv_color_hex(0xFFD54F);
    body.bg_opa = LV_OPA_COVER;
    body.radius = 6;
    body.border_color = lv_color_hex(0x5D4037);
    body.border_width = 2;
    body.border_opa = LV_OPA_COVER;
    flappy_fill(ctx, &body, x, y, x + FLAPPY_BIRD_W, y + FLAPPY_BIRD_H);

    lv_draw_rect_dsc_init(&body);
    body.bg_color = lv_color_hex(0xF5F5F5);
    body.bg_opa = LV_OPA_COVER;
    body.radius = 3;
    if (rising) {
        flappy_fill(ctx, &body, x + 3, y + 2, x + 15, y + 9);
    } else {
        flappy_fill(ctx, &body, x + 3, y + 11, x + 15, y + 18);
    }

    lv_draw_rect_dsc_init(&beak);
    beak.bg_color = lv_color_hex(0xFF7043);
    beak.bg_opa = LV_OPA_COVER;
    flappy_fill(ctx, &beak, x + FLAPPY_BIRD_W - 2, y + 8, x + FLAPPY_BIRD_W + 7, y + 14);

    lv_draw_rect_dsc_init(&eye);
    eye.bg_color = lv_color_black();
    eye.bg_opa = LV_OPA_COVER;
    eye.radius = 2;
    flappy_fill(ctx, &eye, x + 17, y + 4, x + 22, y + 9);
}

static void flappy_draw(lv_event_t *e)
{
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_draw_rect_dsc_t body, lip;
    int i;

    lv_draw_rect_dsc_init(&body);
    body.bg_color = lv_color_hex(0x43A047);
    body.bg_opa = LV_OPA_COVER;

    lv_draw_rect_dsc_init(&lip);
    lip.bg_color = lv_color_hex(0x66BB6A);
    lip.bg_opa = LV_OPA_COVER;
    lip.radius = 2;

    for (i = 0; i < FLAPPY_PIPE_COUNT; i++) {
        if (!flappy_pipes[i].active) continue;
        flappy_draw_pipe(ctx, &body, &lip, &flappy_pipes[i]);
    }

    flappy_draw_bird(ctx);
}
