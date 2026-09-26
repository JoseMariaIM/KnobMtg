#include "dino.h"
#include "presentation/minigames/minigame.h"
#include "adapters/lang.h"
#include "usecases/game.h"
#include "adapters/prefs_scores.h"
#include "esp_random.h"
#include <string.h>

/* Chrome's offline dinosaur, on a round 360x360 display: the runner is
 * fixed near the left, the world scrolls right-to-left underneath it,
 * and a single input (tap OR knob detent, either direction) jumps.
 *
 * Only one control, so the obstacle mix is built around jumping alone:
 * cacti sit on the ground and must be cleared, birds fly high enough to
 * pass under by simply NOT jumping. That keeps the "read the obstacle,
 * decide, commit" loop the original has without needing a duck input
 * this hardware has no natural gesture for.
 *
 * Everything is drawn with lv_draw_rect blocks (no filled-polygon
 * primitive in this LVGL build, and no image assets to bloat flash),
 * which suits the original's chunky pixel look anyway. */

/* Playfield. The ground sits low enough to feel like ground but high
   enough that the whole obstacle band stays inside the round glass:
   at y=250 the visible chord is x≈15..345 (see round_safe.h for the
   same circle math used by scrolling lists). */
#define DINO_GROUND_Y     250
#define DINO_FIELD_LEFT    18
#define DINO_FIELD_RIGHT  342
#define DINO_X             76   /* runner's fixed x (its left edge) */

#define DINO_TICK_MS       28
#define DINO_GRAVITY      1.15f
#define DINO_JUMP_IMPULSE 13.6f
#define DINO_SPEED_START   4.2f
#define DINO_SPEED_MAX    10.5f
/* Speed creeps up per tick rather than per score point: score already
   tracks distance, so tying both to the same clock keeps the ramp
   smooth instead of stepping every time a point lands. */
#define DINO_SPEED_RAMP    0.0016f

#define DINO_BODY_W        26
#define DINO_BODY_H        21
#define DINO_HEAD_W        19
#define DINO_HEAD_H        16
#define DINO_TOTAL_H       39   /* feet to top of head, for collision */

#define DINO_CACTUS_W      13
#define DINO_CACTUS_H_MIN  26
#define DINO_CACTUS_H_MAX  42
#define DINO_BIRD_W        30
#define DINO_BIRD_H        15
/* Birds fly with their lowest pixel this far above the ground - just
   over the standing runner's head (DINO_TOTAL_H), so staying grounded
   is always a clean pass and jumping into one is the mistake. */
#define DINO_BIRD_CLEARANCE 8
/* How much faster than the opening pace the run has to get before birds
   start mixing in (about 20s / ~300 points at DINO_SPEED_RAMP) - the
   original holds pterodactyls back to roughly the same point. */
#define DINO_BIRD_MIN_SPEED_OVER_START 1.2f

#define DINO_OBSTACLE_MAX   4
#define DINO_GAP_MIN      118   /* px between consecutive obstacles */
#define DINO_GAP_RANGE     96

typedef enum {
    DINO_STATE_READY = 0,
    DINO_STATE_PLAYING,
    DINO_STATE_GAME_OVER,
} dino_state_t;

typedef struct {
    bool active;
    bool is_bird;
    float x;        /* left edge */
    int w, h;       /* size; for birds h is the wing box height */
    int top;        /* y of the obstacle's top edge */
} dino_obstacle_t;

lv_obj_t *screen_dino = NULL;
static lv_obj_t *dino_score_lbl = NULL;
static lv_obj_t *dino_message_lbl = NULL;
static lv_timer_t *dino_timer = NULL;

static float dino_y;          /* feet offset above the ground, 0 = standing */
static float dino_vy;
static float dino_speed;
static float dino_distance;   /* px travelled, scaled down into the score */
static int dino_score;
static int dino_spawn_countdown; /* px until the next obstacle spawns */
static dino_obstacle_t dino_obstacles[DINO_OBSTACLE_MAX];
static dino_state_t dino_state = DINO_STATE_READY;
static int dino_player = -1;  /* selected player this run is scored to, -1 = none */
static bool dino_is_new_best = false;
static int dino_anim_tick;    /* drives the leg/wing flap */

/* "The selected player" per the life-counter's own selection state -
   see snake.c's identical helper for why this is resolved once, when
   the game starts. */
static int dino_resolve_player(void)
{
    int i;
    for (i = 0; i < MAX_DISPLAY_PLAYERS; i++) {
        if (is_player_selected(i)) return i;
    }
    return -1;
}

static void dino_spawn_obstacle(void)
{
    int i;

    for (i = 0; i < DINO_OBSTACLE_MAX; i++) {
        if (dino_obstacles[i].active) continue;

        dino_obstacles[i].active = true;
        dino_obstacles[i].x = (float)DINO_FIELD_RIGHT;

        /* Birds only once the run has some speed behind it - the
           original holds pterodactyls back early too, and a bird while
           the player is still learning the jump timing just reads as
           unfair. */
        dino_obstacles[i].is_bird = (dino_speed > DINO_SPEED_START + DINO_BIRD_MIN_SPEED_OVER_START) &&
                                    ((esp_random() % 100) < 30);

        if (dino_obstacles[i].is_bird) {
            dino_obstacles[i].w = DINO_BIRD_W;
            dino_obstacles[i].h = DINO_BIRD_H;
            dino_obstacles[i].top = DINO_GROUND_Y - DINO_TOTAL_H - DINO_BIRD_CLEARANCE - DINO_BIRD_H;
        } else {
            int h = DINO_CACTUS_H_MIN +
                    (int)(esp_random() % (uint32_t)(DINO_CACTUS_H_MAX - DINO_CACTUS_H_MIN + 1));
            dino_obstacles[i].w = DINO_CACTUS_W;
            dino_obstacles[i].h = h;
            dino_obstacles[i].top = DINO_GROUND_Y - h;
        }

        dino_spawn_countdown = DINO_GAP_MIN + (int)(esp_random() % DINO_GAP_RANGE);
        return;
    }
}

static void dino_reset(void)
{
    memset(dino_obstacles, 0, sizeof(dino_obstacles));
    dino_y = 0.0f;
    dino_vy = 0.0f;
    dino_speed = DINO_SPEED_START;
    dino_distance = 0.0f;
    dino_score = 0;
    dino_anim_tick = 0;
    /* A little breathing room before the first obstacle so the run
       never opens with an unreactable jump. */
    dino_spawn_countdown = DINO_GAP_MIN;
    dino_state = DINO_STATE_READY;
}

static size_t dino_append_best_line(char *buf, size_t buf_len, size_t pos)
{
    char best_buf[24];

    if (dino_player < 0) return pos;
    snprintf(best_buf, sizeof(best_buf), t(STR_GAME_BEST_FMT), prefs_get_game_high_score(GAME_SCORE_DINO, dino_player));
    return pos + (size_t)snprintf(buf + pos, buf_len - pos, "\n%s", best_buf);
}

static void dino_refresh_message(void)
{
    char buf[96];
    size_t pos;

    if (dino_message_lbl == NULL) return;

    switch (dino_state) {
    case DINO_STATE_READY:
        pos = (size_t)snprintf(buf, sizeof(buf), "%s\n%s", t(STR_GAME_TAP_START), t(STR_DINO_HINT));
        dino_append_best_line(buf, sizeof(buf), pos);
        lv_label_set_text(dino_message_lbl, buf);
        lv_obj_clear_flag(dino_message_lbl, LV_OBJ_FLAG_HIDDEN);
        break;
    case DINO_STATE_GAME_OVER:
        pos = (size_t)snprintf(buf, sizeof(buf), t(STR_GAME_OVER_FMT), dino_score);
        if (dino_is_new_best) {
            pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, "\n%s", t(STR_GAME_NEW_BEST));
        } else {
            dino_append_best_line(buf, sizeof(buf), pos);
        }
        lv_label_set_text(dino_message_lbl, buf);
        lv_obj_clear_flag(dino_message_lbl, LV_OBJ_FLAG_HIDDEN);
        break;
    case DINO_STATE_PLAYING:
    default:
        lv_obj_add_flag(dino_message_lbl, LV_OBJ_FLAG_HIDDEN);
        break;
    }
}

static void dino_refresh_score(void)
{
    char buf[32];
    if (dino_score_lbl == NULL) return;
    snprintf(buf, sizeof(buf), t(STR_GAME_SCORE_FMT), dino_score);
    lv_label_set_text(dino_score_lbl, buf);
}

static void dino_on_game_over(void)
{
    dino_state = DINO_STATE_GAME_OVER;
    lv_timer_pause(dino_timer);
    dino_is_new_best = (dino_player >= 0 && dino_score > 0 &&
                        dino_score > prefs_get_game_high_score(GAME_SCORE_DINO, dino_player));
    if (dino_is_new_best) {
        prefs_set_game_high_score(GAME_SCORE_DINO, dino_player, dino_score);
    }
    dino_refresh_message();
}

/* Axis-aligned overlap between the runner's box and one obstacle's.
   The runner's box is deliberately a little narrower than what's drawn
   (the tail and snout stick out past it) so a near-miss that visually
   grazes still reads as a fair clear - the same forgiveness the
   original's hitbox has. */
static bool dino_hits(const dino_obstacle_t *ob)
{
    int rx1 = DINO_X + 4;
    int rx2 = DINO_X + DINO_BODY_W + 5;
    int ry2 = DINO_GROUND_Y - (int)dino_y;
    int ry1 = ry2 - DINO_TOTAL_H + 2;
    int ox1 = (int)ob->x;
    int ox2 = ox1 + ob->w;
    int oy1 = ob->top;
    int oy2 = ob->top + ob->h;

    return rx1 < ox2 && rx2 > ox1 && ry1 < oy2 && ry2 > oy1;
}

/* ---------- partial redraw ----------
 *
 * A side-scroller repaints more than the other games here - the ground
 * dashes scroll every frame - but the sky above the ground line is
 * two thirds of the panel and never changes at all. Repainting all
 * 360x360 cost ~259KB over QSPI and a full software render 25 times a
 * second for that empty sky.
 *
 * The runner is at a fixed x and only moves vertically, so its box is
 * the jump arc; the obstacles are touched where they were and where
 * they now are, so their trails come with them. */
static void dino_touch_runner(void)
{
    int gy = DINO_GROUND_Y - (int)dino_y;
    minigame_invalidate_rect(screen_dino,
                             DINO_X - 14, gy - DINO_TOTAL_H - 8,
                             DINO_X + DINO_BODY_W + DINO_HEAD_W + 10, gy + 6);
}

static void dino_touch_obstacle(const dino_obstacle_t *ob)
{
    minigame_invalidate_rect(screen_dino,
                             (int)ob->x - 4, ob->top - 4,
                             (int)ob->x + ob->w + 4, DINO_GROUND_Y + 4);
}

/* The scrolling dashes, as one band across the field. They move every
   frame and there are six of them, so a single strip is both cheaper
   to invalidate and cheaper to reason about than six boxes. */
static void dino_touch_ground(void)
{
    minigame_invalidate_rect(screen_dino,
                             DINO_FIELD_LEFT - 2, DINO_GROUND_Y + 3,
                             DINO_FIELD_RIGHT + 2, DINO_GROUND_Y + 8);
}

static void dino_touch_moving_things(void)
{
    int i;
    dino_touch_runner();
    for (i = 0; i < DINO_OBSTACLE_MAX; i++) {
        if (dino_obstacles[i].active) dino_touch_obstacle(&dino_obstacles[i]);
    }
}

static void dino_tick_cb(lv_timer_t *timer)
{
    int i;
    int prev_score;

    if (lv_scr_act() != screen_dino) {
        lv_timer_pause(timer);
        return;
    }
    if (dino_state != DINO_STATE_PLAYING) return;

    dino_touch_moving_things();   /* where they were */
    dino_anim_tick++;

    /* Runner */
    dino_y += dino_vy;
    dino_vy -= DINO_GRAVITY;
    if (dino_y <= 0.0f) {
        dino_y = 0.0f;
        dino_vy = 0.0f;
    }

    /* World */
    if (dino_speed < DINO_SPEED_MAX) dino_speed += DINO_SPEED_RAMP;
    dino_distance += dino_speed;
    prev_score = dino_score;
    dino_score = (int)(dino_distance / 12.0f);
    if (dino_score != prev_score) dino_refresh_score();

    dino_spawn_countdown -= (int)dino_speed;
    if (dino_spawn_countdown <= 0) dino_spawn_obstacle();

    for (i = 0; i < DINO_OBSTACLE_MAX; i++) {
        if (!dino_obstacles[i].active) continue;
        dino_obstacles[i].x -= dino_speed;
        if (dino_obstacles[i].x + dino_obstacles[i].w < DINO_FIELD_LEFT) {
            dino_obstacles[i].active = false;
            continue;
        }
        if (dino_hits(&dino_obstacles[i])) {
            dino_on_game_over();
            lv_obj_invalidate(screen_dino);
            return;
        }
    }

    dino_touch_moving_things();   /* and where they are now */
    dino_touch_ground();
}

static void dino_start(void)
{
    dino_state = DINO_STATE_PLAYING;
    dino_refresh_message();
    lv_timer_set_period(dino_timer, DINO_TICK_MS);
    lv_timer_resume(dino_timer);
}

static void dino_jump(void)
{
    /* Grounded-only: no double jump, so a committed jump can't be
       rescued mid-air - that commitment is the whole game. */
    if (dino_y > 0.0f) return;
    dino_vy = DINO_JUMP_IMPULSE;
}

void dino_turn(int dir)
{
    (void)dir; /* either direction jumps - it's a one-button game */
    switch (dino_state) {
    case DINO_STATE_READY:
        dino_start();
        break;
    case DINO_STATE_PLAYING:
        dino_jump();
        lv_obj_invalidate(screen_dino);
        break;
    case DINO_STATE_GAME_OVER:
    default:
        break;
    }
}

void dino_handle_tap(void)
{
    switch (dino_state) {
    case DINO_STATE_READY:
        dino_start();
        break;
    case DINO_STATE_PLAYING:
        dino_jump();
        break;
    case DINO_STATE_GAME_OVER:
        dino_reset();
        dino_refresh_score();
        dino_refresh_message();
        lv_obj_invalidate(screen_dino);
        break;
    }
}

void dino_leave_screen(void)
{
    if (dino_timer != NULL) lv_timer_pause(dino_timer);
}

void open_dino_screen(void)
{
    /* Built on first entry rather than at boot - see the matching
       comment in snake.c's open_snake_screen(). */
    if (screen_dino == NULL) build_dino_screen();
    dino_player = dino_resolve_player();
    dino_reset();
    dino_refresh_score();
    dino_refresh_message();
    load_screen_if_needed(screen_dino);
    lv_obj_invalidate(screen_dino);
}

// ---------- test accessors ----------
/* See dino.h for why these exist. Not called from firmware code. */
dino_test_state_t dino_test_state(void)
{
    switch (dino_state) {
    case DINO_STATE_PLAYING:   return DINO_TEST_PLAYING;
    case DINO_STATE_GAME_OVER: return DINO_TEST_GAME_OVER;
    default:                   return DINO_TEST_READY;
    }
}

int dino_test_score(void)
{
    return dino_score;
}

bool dino_test_grounded(void)
{
    return dino_y <= 0.0f;
}

float dino_test_speed(void)
{
    return dino_speed;
}

bool dino_test_nearest_obstacle(int *gap_px, bool *is_bird)
{
    int runner_front = DINO_X + DINO_BODY_W + 5; /* matches dino_hits()'s rx2 */
    int best_gap = 0;
    bool found = false;
    int i;

    for (i = 0; i < DINO_OBSTACLE_MAX; i++) {
        int gap;
        if (!dino_obstacles[i].active) continue;
        if ((int)dino_obstacles[i].x + dino_obstacles[i].w < runner_front) continue; /* fully behind */
        gap = (int)dino_obstacles[i].x - runner_front; /* leading edge */
        if (!found || gap < best_gap) {
            best_gap = gap;
            found = true;
            if (is_bird != NULL) *is_bird = dino_obstacles[i].is_bird;
        }
    }
    if (found && gap_px != NULL) *gap_px = best_gap;
    return found;
}

// ---------- drawing ----------
static void dino_fill(lv_draw_ctx_t *ctx, lv_draw_rect_dsc_t *dsc,
                      int x1, int y1, int x2, int y2)
{
    lv_area_t a;
    a.x1 = (lv_coord_t)x1;
    a.y1 = (lv_coord_t)y1;
    a.x2 = (lv_coord_t)x2;
    a.y2 = (lv_coord_t)y2;
    lv_draw_rect(ctx, dsc, &a);
}

/* The runner, as the original's blocky silhouette: tail, body, a head
   with a snout and a notched eye, one arm, and two legs that swap
   every few ticks while grounded to read as a run cycle (both tucked
   while airborne, like the original). Coordinates are relative to the
   feet, so the whole thing follows dino_y for free. */
static void dino_draw_runner(lv_draw_ctx_t *ctx)
{
    lv_draw_rect_dsc_t body;
    lv_draw_rect_dsc_t eye;
    int x = DINO_X;
    int gy = DINO_GROUND_Y - (int)dino_y;   /* feet */
    int body_top = gy - 4 - DINO_BODY_H;
    int head_x = x + DINO_BODY_W - 4;
    int head_top = body_top - DINO_HEAD_H + 3;
    bool airborne = dino_y > 0.0f;
    bool step = ((dino_anim_tick / 4) % 2) == 0;

    lv_draw_rect_dsc_init(&body);
    body.bg_color = lv_color_hex(0xE8E8E8);
    body.bg_opa = LV_OPA_COVER;
    body.radius = 1;

    /* tail */
    dino_fill(ctx, &body, x - 10, body_top + 2, x + 1, body_top + 10);
    /* body */
    dino_fill(ctx, &body, x, body_top, x + DINO_BODY_W, gy - 6);
    /* head + snout */
    dino_fill(ctx, &body, head_x, head_top, head_x + DINO_HEAD_W, head_top + DINO_HEAD_H);
    dino_fill(ctx, &body, head_x + DINO_HEAD_W - 2, head_top + 7,
              head_x + DINO_HEAD_W + 6, head_top + DINO_HEAD_H - 2);
    /* arm */
    dino_fill(ctx, &body, x + DINO_BODY_W - 9, body_top + 12, x + DINO_BODY_W - 1, body_top + 16);

    /* legs: tucked together in the air, alternating on the ground */
    if (airborne) {
        dino_fill(ctx, &body, x + 4, gy - 7, x + 11, gy - 2);
        dino_fill(ctx, &body, x + 15, gy - 7, x + 22, gy - 2);
    } else if (step) {
        dino_fill(ctx, &body, x + 4, gy - 7, x + 11, gy);
        dino_fill(ctx, &body, x + 15, gy - 7, x + 22, gy - 3);
    } else {
        dino_fill(ctx, &body, x + 4, gy - 7, x + 11, gy - 3);
        dino_fill(ctx, &body, x + 15, gy - 7, x + 22, gy);
    }

    /* eye: a hole punched in the head, so it reads at this size */
    lv_draw_rect_dsc_init(&eye);
    eye.bg_color = lv_color_black();
    eye.bg_opa = LV_OPA_COVER;
    dino_fill(ctx, &eye, head_x + 11, head_top + 4, head_x + 15, head_top + 8);
}

static void dino_draw_cactus(lv_draw_ctx_t *ctx, lv_draw_rect_dsc_t *dsc,
                             const dino_obstacle_t *ob)
{
    int x = (int)ob->x;
    int top = ob->top;
    int bottom = DINO_GROUND_Y;
    int arm_y = top + ob->h / 3;

    /* trunk */
    dino_fill(ctx, dsc, x + 3, top, x + ob->w - 3, bottom);
    /* two arms, one a little lower than the other */
    dino_fill(ctx, dsc, x - 3, arm_y, x + 3, arm_y + 4);
    dino_fill(ctx, dsc, x - 3, arm_y, x + 1, arm_y + 12);
    dino_fill(ctx, dsc, x + ob->w - 3, arm_y + 6, x + ob->w + 3, arm_y + 10);
    dino_fill(ctx, dsc, x + ob->w - 1, arm_y + 6, x + ob->w + 3, arm_y + 18);
}

static void dino_draw_bird(lv_draw_ctx_t *ctx, lv_draw_rect_dsc_t *dsc,
                           const dino_obstacle_t *ob)
{
    int x = (int)ob->x;
    int mid = ob->top + ob->h / 2;
    bool wings_up = ((dino_anim_tick / 5) % 2) == 0;

    /* body + beak */
    dino_fill(ctx, dsc, x + 6, mid - 2, x + 18, mid + 3);
    dino_fill(ctx, dsc, x, mid - 1, x + 6, mid + 1);
    /* wing, flapping above or below the body */
    if (wings_up) {
        dino_fill(ctx, dsc, x + 9, ob->top, x + 17, mid - 2);
    } else {
        dino_fill(ctx, dsc, x + 9, mid + 3, x + 17, ob->top + ob->h);
    }
}

static void event_dino_draw(lv_event_t *e)
{
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_draw_rect_dsc_t ground_dsc, ob_dsc;
    int i;

    /* Ground: a solid line plus a few dashes that scroll with the world,
       which is what actually sells the sense of speed - the runner and
       obstacles alone read as floating. */
    lv_draw_rect_dsc_init(&ground_dsc);
    ground_dsc.bg_color = lv_color_hex(0x8A8A8A);
    ground_dsc.bg_opa = LV_OPA_COVER;
    dino_fill(ctx, &ground_dsc, DINO_FIELD_LEFT, DINO_GROUND_Y, DINO_FIELD_RIGHT, DINO_GROUND_Y + 2);

    ground_dsc.bg_color = lv_color_hex(0x4A4A4A);
    for (i = 0; i < 6; i++) {
        int span = DINO_FIELD_RIGHT - DINO_FIELD_LEFT;
        int offset = (int)dino_distance % span;
        int dx = DINO_FIELD_LEFT + ((i * span / 6) - offset + span * 2) % span;
        if (dx + 16 > DINO_FIELD_RIGHT) continue;
        dino_fill(ctx, &ground_dsc, dx, DINO_GROUND_Y + 5, dx + 16, DINO_GROUND_Y + 6);
    }

    lv_draw_rect_dsc_init(&ob_dsc);
    ob_dsc.bg_color = lv_color_hex(0x4CAF50);
    ob_dsc.bg_opa = LV_OPA_COVER;
    ob_dsc.radius = 1;

    for (i = 0; i < DINO_OBSTACLE_MAX; i++) {
        if (!dino_obstacles[i].active) continue;
        if (dino_obstacles[i].is_bird) {
            ob_dsc.bg_color = lv_color_hex(0xB0BEC5);
            dino_draw_bird(ctx, &ob_dsc, &dino_obstacles[i]);
        } else {
            ob_dsc.bg_color = lv_color_hex(0x4CAF50);
            dino_draw_cactus(ctx, &ob_dsc, &dino_obstacles[i]);
        }
    }

    dino_draw_runner(ctx);
}

static void event_dino_tap(lv_event_t *e)
{
    (void)e;
    dino_handle_tap();
}

void build_dino_screen(void)
{
    screen_dino = lv_obj_create(NULL);
    lv_obj_set_size(screen_dino, 360, 360);
    lv_obj_set_style_bg_color(screen_dino, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_dino, 0, 0);
    lv_obj_set_scrollbar_mode(screen_dino, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(screen_dino, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen_dino, event_dino_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(screen_dino, event_dino_tap, LV_EVENT_CLICKED, NULL);

    dino_score_lbl = lv_label_create(screen_dino);
    lv_label_set_text(dino_score_lbl, "Score: 0");
    lv_obj_set_style_text_color(dino_score_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(dino_score_lbl, &lv_font_es_16, 0);
    lv_obj_align(dino_score_lbl, LV_ALIGN_TOP_MID, 0, 40);

    dino_message_lbl = lv_label_create(screen_dino);
    lv_label_set_text(dino_message_lbl, "");
    lv_obj_set_style_text_color(dino_message_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(dino_message_lbl, &lv_font_es_22, 0);
    lv_obj_set_style_text_align(dino_message_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(dino_message_lbl, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(dino_message_lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dino_message_lbl, 8, 0);
    lv_obj_set_style_pad_all(dino_message_lbl, 12, 0);
    lv_obj_set_width(dino_message_lbl, 260);
    lv_obj_align(dino_message_lbl, LV_ALIGN_CENTER, 0, -20);

    dino_timer = lv_timer_create(dino_tick_cb, DINO_TICK_MS, NULL);
    lv_timer_pause(dino_timer);

    dino_reset();
}
