#include "breakout.h"
#include "minigame.h"
#include "esp_random.h"
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
 * ending the run, so a good player's score keeps climbing. */

#define BO_TICK_MS        20

#define BO_FIELD_LEFT     68
#define BO_FIELD_RIGHT   292
#define BO_FIELD_TOP      64
#define BO_FIELD_BOTTOM  318

#define BO_PADDLE_Y      300
#define BO_PADDLE_W       52
#define BO_PADDLE_H        9
#define BO_PADDLE_STEP    10   /* px per knob detent */

#define BO_BALL_R          5
#define BO_SPEED_START  3.05f
#define BO_SPEED_LEVEL  0.42f  /* added per cleared wall */
#define BO_SPEED_MAX    6.20f

#define BO_COLS            7
#define BO_ROWS            4
#define BO_BRICK_W        32   /* (BO_FIELD_RIGHT - BO_FIELD_LEFT) / BO_COLS */
#define BO_BRICK_H        15
#define BO_BRICK_TOP      76
#define BO_BRICK_GAP       2
#define BO_BRICK_COUNT   (BO_COLS * BO_ROWS)
#define BO_BRICK_POINTS   10

#define BO_LIVES           3

lv_obj_t *screen_breakout = NULL;

static int   bo_paddle_x;      /* centre */
static float bo_ball_x, bo_ball_y;
static float bo_ball_vx, bo_ball_vy;
static float bo_speed;
static bool  bo_bricks[BO_BRICK_COUNT];
static int   bo_bricks_left;
static int   bo_lives;
static int   bo_level;
static bool  bo_ball_stuck;    /* riding the paddle until the run resumes */

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
};

static void bo_fill_wall(void)
{
    int i;
    for (i = 0; i < BO_BRICK_COUNT; i++) bo_bricks[i] = true;
    bo_bricks_left = BO_BRICK_COUNT;
}

/* Parks the ball on the paddle and aims it up at a slight angle. The
   sign is random so a player can't memorise the opening trajectory. */
static void bo_serve(void)
{
    bo_ball_stuck = true;
    bo_ball_x = (float)bo_paddle_x;
    bo_ball_y = (float)(BO_PADDLE_Y - BO_BALL_R - 1);
    bo_ball_vx = ((esp_random() % 2U) ? 0.62f : -0.62f) * bo_speed;
    bo_ball_vy = -0.78f * bo_speed;
}

static void breakout_reset(minigame_t *g)
{
    (void)g;
    bo_paddle_x = (BO_FIELD_LEFT + BO_FIELD_RIGHT) / 2;
    bo_speed = BO_SPEED_START;
    bo_level = 1;
    bo_lives = BO_LIVES;
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

/* Returns true if the ball hit something, having already flipped the
   appropriate velocity component. The axis is chosen by comparing how
   far the ball has penetrated horizontally vs vertically, which is what
   keeps a ball clipping a brick's corner from reversing the wrong way
   and appearing to pass straight through the wall. */
static bool bo_hit_bricks(minigame_t *g)
{
    int i;
    int bx = (int)bo_ball_x;
    int by = (int)bo_ball_y;

    for (i = 0; i < BO_BRICK_COUNT; i++) {
        int x1, y1, x2, y2;
        int overlap_x, overlap_y;

        if (!bo_bricks[i]) continue;
        bo_brick_rect(i, &x1, &y1, &x2, &y2);
        if (bx + BO_BALL_R < x1 || bx - BO_BALL_R > x2) continue;
        if (by + BO_BALL_R < y1 || by - BO_BALL_R > y2) continue;

        overlap_x = (bo_ball_vx > 0.0f) ? (bx + BO_BALL_R - x1) : (x2 - bx + BO_BALL_R);
        overlap_y = (bo_ball_vy > 0.0f) ? (by + BO_BALL_R - y1) : (y2 - by + BO_BALL_R);
        if (overlap_x < overlap_y) bo_ball_vx = -bo_ball_vx;
        else                       bo_ball_vy = -bo_ball_vy;

        bo_bricks[i] = false;
        bo_bricks_left--;
        minigame_add_score(g, BO_BRICK_POINTS);
        return true;
    }
    return false;
}

static void bo_bounce_off_paddle(void)
{
    int half = BO_PADDLE_W / 2;
    float offset = (bo_ball_x - (float)bo_paddle_x) / (float)half; /* -1..1 */
    float speed = bo_speed;

    if (offset < -1.0f) offset = -1.0f;
    if (offset >  1.0f) offset =  1.0f;

    /* Where the ball lands on the paddle sets the outgoing angle - the
       control that makes Breakout a game of aim rather than reflexes.
       vy keeps a floor so a near-edge hit can't come off so flat that
       the ball skims sideways forever. */
    bo_ball_vx = offset * 0.86f * speed;
    bo_ball_vy = -speed;
    if (bo_ball_vy > -0.55f * speed) bo_ball_vy = -0.55f * speed;
    bo_ball_y = (float)(BO_PADDLE_Y - BO_BALL_R - 1);
}

static void breakout_tick(minigame_t *g)
{
    int half = BO_PADDLE_W / 2;

    if (bo_ball_stuck) {
        /* Riding the paddle: the player aims, then any tap releases. */
        bo_ball_x = (float)bo_paddle_x;
        return;
    }

    bo_ball_x += bo_ball_vx;
    bo_ball_y += bo_ball_vy;

    if (bo_ball_x - BO_BALL_R < BO_FIELD_LEFT) {
        bo_ball_x = (float)(BO_FIELD_LEFT + BO_BALL_R);
        bo_ball_vx = -bo_ball_vx;
    } else if (bo_ball_x + BO_BALL_R > BO_FIELD_RIGHT) {
        bo_ball_x = (float)(BO_FIELD_RIGHT - BO_BALL_R);
        bo_ball_vx = -bo_ball_vx;
    }
    if (bo_ball_y - BO_BALL_R < BO_FIELD_TOP) {
        bo_ball_y = (float)(BO_FIELD_TOP + BO_BALL_R);
        bo_ball_vy = -bo_ball_vy;
    }

    bo_hit_bricks(g);

    if (bo_ball_vy > 0.0f &&
        bo_ball_y + BO_BALL_R >= BO_PADDLE_Y &&
        bo_ball_y - BO_BALL_R <= BO_PADDLE_Y + BO_PADDLE_H &&
        bo_ball_x >= (float)(bo_paddle_x - half) &&
        bo_ball_x <= (float)(bo_paddle_x + half)) {
        bo_bounce_off_paddle();
    }

    if (bo_ball_y - BO_BALL_R > BO_FIELD_BOTTOM) {
        if (--bo_lives <= 0) {
            minigame_over(g);
            return;
        }
        bo_serve();
        return;
    }

    if (bo_bricks_left == 0) {
        /* Cleared: a fresh, faster wall rather than a win screen, so one
           good run keeps scoring. */
        bo_level++;
        if (bo_speed < BO_SPEED_MAX) bo_speed += BO_SPEED_LEVEL;
        bo_fill_wall();
        bo_serve();
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

bool breakout_test_ball_parked(void) { return bo_ball_stuck; }

void breakout_test_ball(int *x, int *y)
{
    if (x != NULL) *x = (int)bo_ball_x;
    if (y != NULL) *y = (int)bo_ball_y;
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

static void breakout_draw(lv_event_t *e)
{
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_draw_rect_dsc_t wall, brick, paddle, ball, life;
    /* Row colour doubles as depth cue: the back row is the one worth
       reaching, so it gets the hottest colour. */
    static const uint32_t row_color[BO_ROWS] = {
        0xEF5350, 0xFFA726, 0x66BB6A, 0x42A5F5,
    };
    int i;

    lv_draw_rect_dsc_init(&wall);
    wall.bg_opa = LV_OPA_TRANSP;
    wall.border_color = lv_color_hex(0x263238);
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
        if (!bo_bricks[i]) continue;
        bo_brick_rect(i, &x1, &y1, &x2, &y2);
        brick.bg_color = lv_color_hex(row_color[i / BO_COLS]);
        bo_fill(ctx, &brick, x1, y1, x2, y2);
    }

    lv_draw_rect_dsc_init(&paddle);
    paddle.bg_color = lv_color_hex(0xE0E0E0);
    paddle.bg_opa = LV_OPA_COVER;
    paddle.radius = 4;
    bo_fill(ctx, &paddle, bo_paddle_x - BO_PADDLE_W / 2, BO_PADDLE_Y,
            bo_paddle_x + BO_PADDLE_W / 2, BO_PADDLE_Y + BO_PADDLE_H);

    lv_draw_rect_dsc_init(&ball);
    ball.bg_color = lv_color_hex(0xFFFFFF);
    ball.bg_opa = LV_OPA_COVER;
    ball.radius = LV_RADIUS_CIRCLE;
    bo_fill(ctx, &ball, (int)bo_ball_x - BO_BALL_R, (int)bo_ball_y - BO_BALL_R,
            (int)bo_ball_x + BO_BALL_R, (int)bo_ball_y + BO_BALL_R);

    lv_draw_rect_dsc_init(&life);
    life.bg_opa = LV_OPA_COVER;
    life.radius = LV_RADIUS_CIRCLE;
    for (i = 0; i < BO_LIVES; i++) {
        life.bg_color = (i < bo_lives) ? lv_color_hex(0xFFFFFF)
                                       : lv_color_hex(0x3A3A3A);
        bo_fill(ctx, &life, 128 + i * 16, 330, 128 + i * 16 + 8, 338);
    }
}
