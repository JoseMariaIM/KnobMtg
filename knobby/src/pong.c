#include "pong.h"
#include "settings.h"
#include "lang.h"
#include "game.h"
#include "storage.h"
#include "esp_random.h"
#include <math.h>

/* A solo "keep it alive" pong: one paddle arcs along part of the round
   display's rim (turn the knob to slide it around the circle) and the
   ball bounces inside - miss the paddle when the ball reaches the wall
   and the run ends. Physics are continuous (float position/velocity),
   unlike Snake's fixed grid, since a paddle/ball game needs to feel
   smooth rather than stepped. */
#define PONG_CX 180
#define PONG_CY 180
#define PONG_ARENA_RADIUS 165  /* a few px inside the physical glass edge */
#define PONG_BALL_RADIUS 6
#define PONG_PADDLE_HALF_WIDTH_DEG 20
#define PONG_PADDLE_THICKNESS 7
#define PONG_PADDLE_STEP_DEG 5  /* paddle slide per knob detent */
#define PONG_TICK_MS 30
#define PONG_BALL_SPEED_START 2.4f
#define PONG_BALL_SPEED_STEP  0.06f /* speedup per successful bounce */
#define PONG_BALL_SPEED_MAX   5.0f
#define PONG_STEER_FACTOR     1.6f  /* how much off-center paddle hits steer the ball */
#define PONG_DEG2RAD 0.017453292f
#define PONG_RAD2DEG 57.29577951f

typedef enum {
    PONG_STATE_READY = 0,
    PONG_STATE_PLAYING,
    PONG_STATE_PAUSED,
    PONG_STATE_GAME_OVER,
} pong_state_t;

lv_obj_t *screen_pong = NULL;
static lv_obj_t *pong_score_lbl = NULL;
static lv_obj_t *pong_message_lbl = NULL;
static lv_timer_t *pong_timer = NULL;

static float pong_ball_x, pong_ball_y;
static float pong_ball_vx, pong_ball_vy;
static float pong_ball_speed;
/* Degrees, 0-359: 0 = 3 o'clock, increasing clockwise - same convention
   as lv_trigo_sin/cos and ui_mp.c's wedge_polar(), so it lines up with
   lv_draw_arc()'s angle parameters with no conversion needed. */
static int pong_paddle_angle;
static int pong_score;
static pong_state_t pong_state = PONG_STATE_READY;
static int pong_player = -1;        /* selected player this run is scored to, -1 = none */
static bool pong_is_new_best = false;

static int pong_norm_angle(int a)
{
    a %= 360;
    if (a < 0) a += 360;
    return a;
}

static int pong_angle_diff(int a, int b)
{
    int d = (a - b) % 360;
    if (d < -180) d += 360;
    if (d >= 180) d -= 360;
    return d;
}

/* "The selected player" per the life-counter's own selection state -
   see snake.c's identical helper for why this is resolved once, when
   the game starts. */
static int pong_resolve_player(void)
{
    int i;
    for (i = 0; i < MAX_DISPLAY_PLAYERS; i++) {
        if (is_player_selected(i)) return i;
    }
    return -1;
}

static void pong_spawn_ball(void)
{
    float rad = (float)(esp_random() % 360) * PONG_DEG2RAD;

    pong_ball_x = PONG_CX;
    pong_ball_y = PONG_CY;
    pong_ball_speed = PONG_BALL_SPEED_START;
    pong_ball_vx = cosf(rad) * pong_ball_speed;
    pong_ball_vy = sinf(rad) * pong_ball_speed;
}

static void pong_reset(void)
{
    pong_paddle_angle = 90; /* bottom of the circle */
    pong_score = 0;
    pong_spawn_ball();
    pong_state = PONG_STATE_READY;
}

static size_t pong_append_best_line(char *buf, size_t buf_len, size_t pos)
{
    char best_buf[24];

    if (pong_player < 0) return pos;
    snprintf(best_buf, sizeof(best_buf), t(STR_PONG_BEST_FMT), nvs_get_pong_high_score(pong_player));
    return pos + (size_t)snprintf(buf + pos, buf_len - pos, "\n%s", best_buf);
}

static void pong_refresh_message(void)
{
    char buf[96];
    size_t pos;

    if (pong_message_lbl == NULL) return;

    switch (pong_state) {
    case PONG_STATE_READY:
        pos = (size_t)snprintf(buf, sizeof(buf), "%s\n%s", t(STR_PONG_TAP_START), t(STR_PONG_HINT));
        pong_append_best_line(buf, sizeof(buf), pos);
        lv_label_set_text(pong_message_lbl, buf);
        lv_obj_clear_flag(pong_message_lbl, LV_OBJ_FLAG_HIDDEN);
        break;
    case PONG_STATE_PAUSED:
        lv_label_set_text(pong_message_lbl, t(STR_PONG_PAUSED));
        lv_obj_clear_flag(pong_message_lbl, LV_OBJ_FLAG_HIDDEN);
        break;
    case PONG_STATE_GAME_OVER:
        pos = (size_t)snprintf(buf, sizeof(buf), t(STR_PONG_GAME_OVER_FMT), pong_score);
        if (pong_is_new_best) {
            pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, "\n%s", t(STR_PONG_NEW_BEST));
        } else {
            pong_append_best_line(buf, sizeof(buf), pos);
        }
        lv_label_set_text(pong_message_lbl, buf);
        lv_obj_clear_flag(pong_message_lbl, LV_OBJ_FLAG_HIDDEN);
        break;
    case PONG_STATE_PLAYING:
    default:
        lv_obj_add_flag(pong_message_lbl, LV_OBJ_FLAG_HIDDEN);
        break;
    }
}

static void pong_refresh_score(void)
{
    char buf[32];
    if (pong_score_lbl == NULL) return;
    snprintf(buf, sizeof(buf), t(STR_PONG_SCORE_FMT), pong_score);
    lv_label_set_text(pong_score_lbl, buf);
}

static void pong_on_game_over(void)
{
    pong_state = PONG_STATE_GAME_OVER;
    lv_timer_pause(pong_timer);
    pong_is_new_best = (pong_player >= 0 && pong_score > 0 &&
                         pong_score > nvs_get_pong_high_score(pong_player));
    if (pong_is_new_best) {
        nvs_set_pong_high_score(pong_player, pong_score);
        settings_save();
    }
    pong_refresh_message();
}

static void pong_tick_cb(lv_timer_t *timer)
{
    float dx, dy, dist, limit;

    if (lv_scr_act() != screen_pong) {
        lv_timer_pause(timer);
        return;
    }
    if (pong_state != PONG_STATE_PLAYING) return;

    pong_ball_x += pong_ball_vx;
    pong_ball_y += pong_ball_vy;

    dx = pong_ball_x - PONG_CX;
    dy = pong_ball_y - PONG_CY;
    dist = sqrtf(dx * dx + dy * dy);
    limit = PONG_ARENA_RADIUS - PONG_BALL_RADIUS;

    if (dist + PONG_BALL_RADIUS >= PONG_ARENA_RADIUS) {
        int contact_angle = pong_norm_angle((int)lroundf(atan2f(dy, dx) * PONG_RAD2DEG));
        int diff = pong_angle_diff(contact_angle, pong_paddle_angle);

        if (diff >= -PONG_PADDLE_HALF_WIDTH_DEG && diff <= PONG_PADDLE_HALF_WIDTH_DEG) {
            /* Specular reflection off the circular wall's normal at the
               contact point, plus a bit of "english" from how far off
               paddle-center the ball landed - classic paddle-hit steering,
               same idea as Breakout/Pong but on a ring instead of a line. */
            float nx = dx / dist, ny = dy / dist;
            float dot = pong_ball_vx * nx + pong_ball_vy * ny;
            float rvx = pong_ball_vx - 2.0f * dot * nx;
            float rvy = pong_ball_vy - 2.0f * dot * ny;
            float tx = -ny, ty = nx;
            float offset_ratio = (float)diff / (float)PONG_PADDLE_HALF_WIDTH_DEG;
            float rmag;

            pong_score++;
            pong_ball_speed = LV_MIN(PONG_BALL_SPEED_MAX,
                                      PONG_BALL_SPEED_START + pong_score * PONG_BALL_SPEED_STEP);

            rvx += tx * offset_ratio * PONG_STEER_FACTOR;
            rvy += ty * offset_ratio * PONG_STEER_FACTOR;
            /* Renormalize to the target speed - otherwise the steering
               kick would let speed quietly compound bounce after bounce
               on top of the deliberate per-score ramp above. */
            rmag = sqrtf(rvx * rvx + rvy * rvy);
            if (rmag > 0.0001f) {
                pong_ball_vx = rvx / rmag * pong_ball_speed;
                pong_ball_vy = rvy / rmag * pong_ball_speed;
            }
            /* Pull the ball back onto the boundary so it can't tunnel
               through on a fast bounce and re-trigger next tick. */
            pong_ball_x = PONG_CX + nx * limit;
            pong_ball_y = PONG_CY + ny * limit;

            pong_refresh_score();
        } else {
            pong_on_game_over();
            return;
        }
    }

    lv_obj_invalidate(screen_pong);
}

static void pong_start(void)
{
    pong_state = PONG_STATE_PLAYING;
    pong_refresh_message();
    lv_timer_set_period(pong_timer, PONG_TICK_MS);
    lv_timer_resume(pong_timer);
}

void pong_turn(int dir)
{
    if (pong_state == PONG_STATE_READY) pong_start();
    if (pong_state != PONG_STATE_PLAYING) return;
    pong_paddle_angle = pong_norm_angle(pong_paddle_angle + (dir > 0 ? PONG_PADDLE_STEP_DEG : -PONG_PADDLE_STEP_DEG));
    /* Immediate redraw: the paddle should visibly slide on every single
       detent, not wait for the next 30ms tick. */
    lv_obj_invalidate(screen_pong);
}

void pong_handle_tap(void)
{
    switch (pong_state) {
    case PONG_STATE_READY:
        pong_start();
        break;
    case PONG_STATE_PLAYING:
        pong_state = PONG_STATE_PAUSED;
        lv_timer_pause(pong_timer);
        pong_refresh_message();
        break;
    case PONG_STATE_PAUSED:
        pong_state = PONG_STATE_PLAYING;
        pong_refresh_message();
        lv_timer_resume(pong_timer);
        break;
    case PONG_STATE_GAME_OVER:
        pong_reset();
        pong_refresh_score();
        pong_refresh_message();
        lv_obj_invalidate(screen_pong);
        break;
    }
}

void pong_leave_screen(void)
{
    if (pong_timer != NULL) lv_timer_pause(pong_timer);
}

void open_pong_screen(void)
{
    pong_player = pong_resolve_player();
    pong_reset();
    pong_refresh_score();
    pong_refresh_message();
    load_screen_if_needed(screen_pong);
    lv_obj_invalidate(screen_pong);
}

// ---------- drawing ----------
static void event_pong_draw(lv_event_t *e)
{
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    lv_draw_rect_dsc_t wall_dsc, ball_dsc;
    lv_draw_arc_dsc_t paddle_dsc;
    lv_area_t area;
    lv_point_t center = {PONG_CX, PONG_CY};
    int start_angle, end_angle;

    lv_draw_rect_dsc_init(&wall_dsc);
    wall_dsc.bg_opa = LV_OPA_TRANSP;
    wall_dsc.border_width = 2;
    wall_dsc.border_color = lv_color_hex(0x333344);
    wall_dsc.border_opa = LV_OPA_COVER;
    wall_dsc.radius = LV_RADIUS_CIRCLE;
    area.x1 = PONG_CX - PONG_ARENA_RADIUS;
    area.y1 = PONG_CY - PONG_ARENA_RADIUS;
    area.x2 = PONG_CX + PONG_ARENA_RADIUS;
    area.y2 = PONG_CY + PONG_ARENA_RADIUS;
    lv_draw_rect(draw_ctx, &wall_dsc, &area);

    start_angle = pong_norm_angle(pong_paddle_angle - PONG_PADDLE_HALF_WIDTH_DEG);
    end_angle = start_angle + PONG_PADDLE_HALF_WIDTH_DEG * 2;
    lv_draw_arc_dsc_init(&paddle_dsc);
    paddle_dsc.color = lv_color_hex(0x2E86FF);
    paddle_dsc.width = PONG_PADDLE_THICKNESS;
    paddle_dsc.rounded = 1;
    paddle_dsc.opa = LV_OPA_COVER;
    lv_draw_arc(draw_ctx, &paddle_dsc, &center, PONG_ARENA_RADIUS,
                (uint16_t)start_angle, (uint16_t)end_angle);

    lv_draw_rect_dsc_init(&ball_dsc);
    ball_dsc.radius = LV_RADIUS_CIRCLE;
    ball_dsc.bg_color = lv_color_white();
    ball_dsc.bg_opa = LV_OPA_COVER;
    area.x1 = (lv_coord_t)lroundf(pong_ball_x) - PONG_BALL_RADIUS;
    area.y1 = (lv_coord_t)lroundf(pong_ball_y) - PONG_BALL_RADIUS;
    area.x2 = area.x1 + PONG_BALL_RADIUS * 2;
    area.y2 = area.y1 + PONG_BALL_RADIUS * 2;
    lv_draw_rect(draw_ctx, &ball_dsc, &area);
}

static void event_pong_tap(lv_event_t *e)
{
    (void)e;
    pong_handle_tap();
}

void build_pong_screen(void)
{
    screen_pong = lv_obj_create(NULL);
    lv_obj_set_size(screen_pong, 360, 360);
    lv_obj_set_style_bg_color(screen_pong, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_pong, 0, 0);
    lv_obj_set_scrollbar_mode(screen_pong, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(screen_pong, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen_pong, event_pong_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(screen_pong, event_pong_tap, LV_EVENT_CLICKED, NULL);

    pong_score_lbl = lv_label_create(screen_pong);
    lv_label_set_text(pong_score_lbl, "Score: 0");
    lv_obj_set_style_text_color(pong_score_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(pong_score_lbl, &lv_font_es_16, 0);
    lv_obj_align(pong_score_lbl, LV_ALIGN_TOP_MID, 0, 34);

    pong_message_lbl = lv_label_create(screen_pong);
    lv_label_set_text(pong_message_lbl, "");
    lv_obj_set_style_text_color(pong_message_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(pong_message_lbl, &lv_font_es_22, 0);
    lv_obj_set_style_text_align(pong_message_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(pong_message_lbl, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(pong_message_lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pong_message_lbl, 8, 0);
    lv_obj_set_style_pad_all(pong_message_lbl, 12, 0);
    lv_obj_set_width(pong_message_lbl, 260);
    lv_obj_align(pong_message_lbl, LV_ALIGN_CENTER, 0, 0);

    pong_timer = lv_timer_create(pong_tick_cb, PONG_TICK_MS, NULL);
    lv_timer_pause(pong_timer);

    pong_reset();
}
