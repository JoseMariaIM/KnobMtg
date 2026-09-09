#include "snake.h"
#include "settings.h"
#include "lang.h"
#include "game.h"
#include "storage.h"
#include "esp_random.h"
#include <string.h>

/* Grid fit to the round display: cells whose centers fall outside the
   inscribed circle are simply not part of the arena (walls), so the
   playfield reads as a circular arena rather than a square one dropped
   onto a round face - see is_cell_valid(). SNAKE_ARENA_RADIUS is a few
   pixels inside the physical glass edge (round_safe.h's radius) so the
   wall ring and snake segments never get clipped by the bezel. */
#define SNAKE_CELL_PX      15
#define SNAKE_GRID_DIM     24  /* 24 * 15 = 360 */
#define SNAKE_ARENA_CX     180
#define SNAKE_ARENA_CY     180
#define SNAKE_ARENA_RADIUS 168

#define SNAKE_MAX_LEN      400 /* comfortably above the arena's ~390 valid cells */
#define SNAKE_INITIAL_LEN  3
#define SNAKE_TICK_START_MS 220
#define SNAKE_TICK_MIN_MS   100
#define SNAKE_TICK_STEP_MS  3   /* speedup per food eaten */

typedef struct {
    int8_t x, y;
} snake_pt_t;

typedef enum {
    SNAKE_DIR_UP = 0,
    SNAKE_DIR_RIGHT,
    SNAKE_DIR_DOWN,
    SNAKE_DIR_LEFT,
} snake_dir_t;

typedef enum {
    SNAKE_STATE_READY = 0,
    SNAKE_STATE_PLAYING,
    SNAKE_STATE_PAUSED,
    SNAKE_STATE_GAME_OVER,
} snake_state_t;

lv_obj_t *screen_snake = NULL;
static lv_obj_t *snake_score_lbl = NULL;
static lv_obj_t *snake_message_lbl = NULL;
static lv_timer_t *snake_timer = NULL;

static snake_pt_t snake_body[SNAKE_MAX_LEN];
static int snake_len;
static snake_dir_t snake_dir;
static snake_pt_t snake_food;
static int snake_score;
static snake_state_t snake_state = SNAKE_STATE_READY;
static bool snake_turned_this_tick = false;
static int snake_player = -1;       /* selected player this run is scored to, -1 = none */
static bool snake_is_new_best = false;

/* "The selected player" per the life-counter's own selection state
   (is_player_selected), resolved once when the game starts - that
   selection doesn't change while Snake itself is on screen. */
static int snake_resolve_player(void)
{
    int i;
    for (i = 0; i < MAX_DISPLAY_PLAYERS; i++) {
        if (is_player_selected(i)) return i;
    }
    return -1;
}

static bool is_cell_valid(int gx, int gy)
{
    int cx, cy, dx, dy;

    if (gx < 0 || gx >= SNAKE_GRID_DIM || gy < 0 || gy >= SNAKE_GRID_DIM) return false;

    cx = gx * SNAKE_CELL_PX + SNAKE_CELL_PX / 2;
    cy = gy * SNAKE_CELL_PX + SNAKE_CELL_PX / 2;
    dx = cx - SNAKE_ARENA_CX;
    dy = cy - SNAKE_ARENA_CY;
    return (dx * dx + dy * dy) <= (SNAKE_ARENA_RADIUS * SNAKE_ARENA_RADIUS);
}

static bool snake_occupies(int gx, int gy)
{
    int i;
    for (i = 0; i < snake_len; i++) {
        if (snake_body[i].x == gx && snake_body[i].y == gy) return true;
    }
    return false;
}

/* Food used to be able to spawn on any valid+unoccupied cell, including
   ones right against the wall ring - reachable in principle (the whole
   arena is one 4-connected region), but approaching a cell that hugs
   the boundary leaves almost no margin to turn without clipping the
   wall, which is exactly what it looks like when it happens: the food
   sits right on the ring and dying trying to reach it feels like it
   was never actually reachable. snake_spawn_food() now also requires
   food to be at least SNAKE_FOOD_MARGIN_PX inside the wall (see
   is_cell_food_safe), leaving room to approach it safely.

   Separately, a long snake's own body can temporarily wall off part of
   the arena from the head - flood-filling from the head through valid,
   unoccupied cells and only spawning food inside that reachable set
   keeps food from ever landing somewhere genuinely cut off right now. */
static bool snake_reachable[SNAKE_GRID_DIM][SNAKE_GRID_DIM];

#define SNAKE_FOOD_MARGIN_PX (SNAKE_CELL_PX * 2)
#define SNAKE_FOOD_RADIUS    (SNAKE_ARENA_RADIUS - SNAKE_FOOD_MARGIN_PX)

static bool is_cell_food_safe(int gx, int gy)
{
    int cx, cy, dx, dy;

    if (!is_cell_valid(gx, gy)) return false;
    cx = gx * SNAKE_CELL_PX + SNAKE_CELL_PX / 2;
    cy = gy * SNAKE_CELL_PX + SNAKE_CELL_PX / 2;
    dx = cx - SNAKE_ARENA_CX;
    dy = cy - SNAKE_ARENA_CY;
    return (dx * dx + dy * dy) <= (SNAKE_FOOD_RADIUS * SNAKE_FOOD_RADIUS);
}

static void snake_compute_reachable(void)
{
    static snake_pt_t queue[SNAKE_GRID_DIM * SNAKE_GRID_DIM];
    static const int dxs[4] = {0, 0, -1, 1};
    static const int dys[4] = {-1, 1, 0, 0};
    int qh = 0, qt = 0, d;

    memset(snake_reachable, 0, sizeof(snake_reachable));
    queue[qt++] = snake_body[0];
    snake_reachable[snake_body[0].x][snake_body[0].y] = true;

    while (qh < qt) {
        snake_pt_t p = queue[qh++];
        for (d = 0; d < 4; d++) {
            int gx = p.x + dxs[d];
            int gy = p.y + dys[d];
            if (!is_cell_valid(gx, gy)) continue;
            if (snake_reachable[gx][gy]) continue;
            if (snake_occupies(gx, gy)) continue;
            snake_reachable[gx][gy] = true;
            queue[qt].x = (int8_t)gx;
            queue[qt].y = (int8_t)gy;
            qt++;
        }
    }
}

static void snake_spawn_food(void)
{
    static snake_pt_t candidates[SNAKE_GRID_DIM * SNAKE_GRID_DIM];
    int gx, gy, count = 0;

    snake_compute_reachable();
    for (gy = 0; gy < SNAKE_GRID_DIM; gy++) {
        for (gx = 0; gx < SNAKE_GRID_DIM; gx++) {
            /* The head's own cell is marked reachable as the BFS's
               starting point, so it must be excluded here explicitly -
               every other occupied cell is already excluded above. */
            if (snake_reachable[gx][gy] && !snake_occupies(gx, gy) && is_cell_food_safe(gx, gy)) {
                candidates[count].x = (int8_t)gx;
                candidates[count].y = (int8_t)gy;
                count++;
            }
        }
    }
    if (count == 0) {
        /* Only once the snake is long enough to fill every cell with
           safe clearance - fall back to any reachable cell at all
           rather than getting stuck with no food to place. */
        for (gy = 0; gy < SNAKE_GRID_DIM; gy++) {
            for (gx = 0; gx < SNAKE_GRID_DIM; gx++) {
                if (snake_reachable[gx][gy] && !snake_occupies(gx, gy)) {
                    candidates[count].x = (int8_t)gx;
                    candidates[count].y = (int8_t)gy;
                    count++;
                }
            }
        }
    }
    if (count == 0) return; /* the snake fills the entire reachable arena - a win, effectively */
    snake_food = candidates[esp_random() % (uint32_t)count];
}

static void snake_reset(void)
{
    int i, cy = SNAKE_GRID_DIM / 2, cx = SNAKE_GRID_DIM / 2;

    snake_len = SNAKE_INITIAL_LEN;
    for (i = 0; i < snake_len; i++) {
        snake_body[i].x = (int8_t)(cx - i);
        snake_body[i].y = (int8_t)cy;
    }
    snake_dir = SNAKE_DIR_RIGHT;
    snake_score = 0;
    snake_spawn_food();
    snake_state = SNAKE_STATE_READY;
}

/* Appends a "Best: %d" line when this run is scored to a player - shown
   both before playing (so there's a target) and on Game Over. */
static size_t snake_append_best_line(char *buf, size_t buf_len, size_t pos)
{
    char best_buf[24];

    if (snake_player < 0) return pos;
    snprintf(best_buf, sizeof(best_buf), t(STR_SNAKE_BEST_FMT), nvs_get_snake_high_score(snake_player));
    return pos + (size_t)snprintf(buf + pos, buf_len - pos, "\n%s", best_buf);
}

static void snake_refresh_message(void)
{
    char buf[96];
    size_t pos;

    if (snake_message_lbl == NULL) return;

    switch (snake_state) {
    case SNAKE_STATE_READY:
        pos = (size_t)snprintf(buf, sizeof(buf), "%s\n%s", t(STR_SNAKE_TAP_START), t(STR_SNAKE_HINT));
        snake_append_best_line(buf, sizeof(buf), pos);
        lv_label_set_text(snake_message_lbl, buf);
        lv_obj_clear_flag(snake_message_lbl, LV_OBJ_FLAG_HIDDEN);
        break;
    case SNAKE_STATE_PAUSED:
        lv_label_set_text(snake_message_lbl, t(STR_SNAKE_PAUSED));
        lv_obj_clear_flag(snake_message_lbl, LV_OBJ_FLAG_HIDDEN);
        break;
    case SNAKE_STATE_GAME_OVER:
        pos = (size_t)snprintf(buf, sizeof(buf), t(STR_SNAKE_GAME_OVER_FMT), snake_score);
        if (snake_is_new_best) {
            pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, "\n%s", t(STR_SNAKE_NEW_BEST));
        } else {
            snake_append_best_line(buf, sizeof(buf), pos);
        }
        lv_label_set_text(snake_message_lbl, buf);
        lv_obj_clear_flag(snake_message_lbl, LV_OBJ_FLAG_HIDDEN);
        break;
    case SNAKE_STATE_PLAYING:
    default:
        lv_obj_add_flag(snake_message_lbl, LV_OBJ_FLAG_HIDDEN);
        break;
    }
}

static void snake_refresh_score(void)
{
    char buf[32];
    if (snake_score_lbl == NULL) return;
    snprintf(buf, sizeof(buf), t(STR_SNAKE_SCORE_FMT), snake_score);
    lv_label_set_text(snake_score_lbl, buf);
}

static void snake_on_game_over(void)
{
    snake_state = SNAKE_STATE_GAME_OVER;
    lv_timer_pause(snake_timer);
    snake_is_new_best = (snake_player >= 0 && snake_score > 0 &&
                          snake_score > nvs_get_snake_high_score(snake_player));
    if (snake_is_new_best) {
        nvs_set_snake_high_score(snake_player, snake_score);
        settings_save();
    }
    snake_refresh_message();
}

static void snake_tick_cb(lv_timer_t *timer)
{
    snake_pt_t new_head;
    bool will_grow;
    int check_len, i;

    if (lv_scr_act() != screen_snake) {
        lv_timer_pause(timer);
        return;
    }
    if (snake_state != SNAKE_STATE_PLAYING) return;

    /* Only one knob turn is allowed to land between ticks (see
       snake_turn()) - the window for the next one reopens now, after
       this tick has consumed whichever direction was queued. */
    snake_turned_this_tick = false;

    new_head = snake_body[0];
    switch (snake_dir) {
    case SNAKE_DIR_UP:    new_head.y--; break;
    case SNAKE_DIR_DOWN:  new_head.y++; break;
    case SNAKE_DIR_LEFT:  new_head.x--; break;
    case SNAKE_DIR_RIGHT: new_head.x++; break;
    }

    if (!is_cell_valid(new_head.x, new_head.y)) {
        snake_on_game_over();
        return;
    }

    will_grow = (new_head.x == snake_food.x && new_head.y == snake_food.y);
    /* The tail is about to vacate its cell unless the snake is growing
       this tick, so moving into it isn't a collision - exclude it. */
    check_len = will_grow ? snake_len : snake_len - 1;
    for (i = 0; i < check_len; i++) {
        if (snake_body[i].x == new_head.x && snake_body[i].y == new_head.y) {
            snake_on_game_over();
            return;
        }
    }

    if (will_grow) {
        if (snake_len < SNAKE_MAX_LEN) {
            memmove(&snake_body[1], &snake_body[0], (size_t)snake_len * sizeof(snake_pt_t));
            snake_len++;
        }
        snake_body[0] = new_head;
        snake_score++;
        snake_spawn_food();
        snake_refresh_score();
        lv_timer_set_period(snake_timer,
            LV_MAX(SNAKE_TICK_MIN_MS, SNAKE_TICK_START_MS - snake_score * SNAKE_TICK_STEP_MS));
    } else {
        memmove(&snake_body[1], &snake_body[0], (size_t)(snake_len - 1) * sizeof(snake_pt_t));
        snake_body[0] = new_head;
    }

    lv_obj_invalidate(screen_snake);
}

static void snake_start(void)
{
    snake_state = SNAKE_STATE_PLAYING;
    snake_turned_this_tick = false;
    snake_refresh_message();
    lv_timer_set_period(snake_timer, SNAKE_TICK_START_MS);
    lv_timer_resume(snake_timer);
}

void snake_turn(int dir)
{
    if (snake_state == SNAKE_STATE_READY) snake_start();
    if (snake_state != SNAKE_STATE_PLAYING) return;
    /* At most one turn is allowed to land between ticks - otherwise a
       quick double-turn (e.g. two detents arriving before the next
       tick fires) could add up to a 180-degree flip and kill the snake
       on its own neck, which read as "the knob randomly does nothing"
       since the direction it settled on wasn't the last one dialed in.
       See snake_tick_cb() for where the window reopens. */
    if (snake_turned_this_tick) return;
    /* Relative steering (not absolute up/down/left/right) so a single
       knob detent - one 90-degree step - can never turn the snake
       directly into itself; the classic "no reversing" rule falls out
       for free instead of needing a separate guard. */
    snake_dir = (snake_dir_t)((snake_dir + (dir > 0 ? 1 : 3)) % 4);
    snake_turned_this_tick = true;
    /* Immediate redraw so a turn between ticks is visibly acknowledged
       right away instead of only becoming apparent up to a whole tick
       period later, which was the other half of "feels unresponsive". */
    lv_obj_invalidate(screen_snake);
}

void snake_handle_tap(void)
{
    switch (snake_state) {
    case SNAKE_STATE_READY:
        snake_start();
        break;
    case SNAKE_STATE_PLAYING:
        snake_state = SNAKE_STATE_PAUSED;
        lv_timer_pause(snake_timer);
        snake_refresh_message();
        break;
    case SNAKE_STATE_PAUSED:
        snake_state = SNAKE_STATE_PLAYING;
        snake_refresh_message();
        lv_timer_resume(snake_timer);
        break;
    case SNAKE_STATE_GAME_OVER:
        snake_reset();
        snake_refresh_score();
        snake_refresh_message();
        lv_obj_invalidate(screen_snake);
        break;
    }
}

void snake_leave_screen(void)
{
    if (snake_timer != NULL) lv_timer_pause(snake_timer);
}

void open_snake_screen(void)
{
    snake_player = snake_resolve_player();
    snake_reset();
    snake_refresh_score();
    snake_refresh_message();
    load_screen_if_needed(screen_snake);
    lv_obj_invalidate(screen_snake);
}

// ---------- drawing ----------
static void event_snake_draw(lv_event_t *e)
{
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    lv_draw_rect_dsc_t wall_dsc, food_dsc, seg_dsc;
    lv_area_t area;
    int i;

    lv_draw_rect_dsc_init(&wall_dsc);
    wall_dsc.bg_opa = LV_OPA_TRANSP;
    wall_dsc.border_width = 3;
    wall_dsc.border_color = lv_color_hex(0x333344);
    wall_dsc.border_opa = LV_OPA_COVER;
    wall_dsc.radius = LV_RADIUS_CIRCLE;
    area.x1 = SNAKE_ARENA_CX - SNAKE_ARENA_RADIUS;
    area.y1 = SNAKE_ARENA_CY - SNAKE_ARENA_RADIUS;
    area.x2 = SNAKE_ARENA_CX + SNAKE_ARENA_RADIUS;
    area.y2 = SNAKE_ARENA_CY + SNAKE_ARENA_RADIUS;
    lv_draw_rect(draw_ctx, &wall_dsc, &area);

    lv_draw_rect_dsc_init(&food_dsc);
    food_dsc.radius = LV_RADIUS_CIRCLE;
    food_dsc.bg_color = lv_color_hex(0xE63946);
    food_dsc.bg_opa = LV_OPA_COVER;
    area.x1 = snake_food.x * SNAKE_CELL_PX + 2;
    area.y1 = snake_food.y * SNAKE_CELL_PX + 2;
    area.x2 = area.x1 + SNAKE_CELL_PX - 4;
    area.y2 = area.y1 + SNAKE_CELL_PX - 4;
    lv_draw_rect(draw_ctx, &food_dsc, &area);

    lv_draw_rect_dsc_init(&seg_dsc);
    seg_dsc.radius = 4;
    seg_dsc.bg_opa = LV_OPA_COVER;
    for (i = 0; i < snake_len; i++) {
        bool is_head = (i == 0);
        seg_dsc.bg_color = is_head ? lv_color_hex(0x27AE60) : lv_color_hex(0x2ECC71);
        seg_dsc.border_width = is_head ? 2 : 0;
        seg_dsc.border_color = lv_color_white();
        seg_dsc.border_opa = LV_OPA_COVER;
        area.x1 = snake_body[i].x * SNAKE_CELL_PX + 1;
        area.y1 = snake_body[i].y * SNAKE_CELL_PX + 1;
        area.x2 = area.x1 + SNAKE_CELL_PX - 2;
        area.y2 = area.y1 + SNAKE_CELL_PX - 2;
        lv_draw_rect(draw_ctx, &seg_dsc, &area);
    }

    /* Heading nub: a small bright dot offset from the head in the
       current direction of travel. snake_turn() invalidates the screen
       the instant a turn is dialed in (state, not just the head
       position, changes right away) so this repaints immediately too -
       the player sees their turn register well before the next tick
       actually moves the snake. */
    if (snake_len > 0) {
        int hx = snake_body[0].x * SNAKE_CELL_PX + SNAKE_CELL_PX / 2;
        int hy = snake_body[0].y * SNAKE_CELL_PX + SNAKE_CELL_PX / 2;
        int nx = hx, ny = hy;
        lv_draw_rect_dsc_t nub_dsc;

        switch (snake_dir) {
        case SNAKE_DIR_UP:    ny -= 6; break;
        case SNAKE_DIR_DOWN:  ny += 6; break;
        case SNAKE_DIR_LEFT:  nx -= 6; break;
        case SNAKE_DIR_RIGHT: nx += 6; break;
        }
        lv_draw_rect_dsc_init(&nub_dsc);
        nub_dsc.radius = LV_RADIUS_CIRCLE;
        nub_dsc.bg_color = lv_color_white();
        nub_dsc.bg_opa = LV_OPA_COVER;
        area.x1 = nx - 2;
        area.y1 = ny - 2;
        area.x2 = nx + 2;
        area.y2 = ny + 2;
        lv_draw_rect(draw_ctx, &nub_dsc, &area);
    }
}

static void event_snake_tap(lv_event_t *e)
{
    (void)e;
    snake_handle_tap();
}

void build_snake_screen(void)
{
    screen_snake = lv_obj_create(NULL);
    lv_obj_set_size(screen_snake, 360, 360);
    lv_obj_set_style_bg_color(screen_snake, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_snake, 0, 0);
    lv_obj_set_scrollbar_mode(screen_snake, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(screen_snake, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen_snake, event_snake_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(screen_snake, event_snake_tap, LV_EVENT_CLICKED, NULL);

    snake_score_lbl = lv_label_create(screen_snake);
    lv_label_set_text(snake_score_lbl, "Score: 0");
    lv_obj_set_style_text_color(snake_score_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(snake_score_lbl, &lv_font_es_16, 0);
    lv_obj_align(snake_score_lbl, LV_ALIGN_TOP_MID, 0, 34);

    snake_message_lbl = lv_label_create(screen_snake);
    lv_label_set_text(snake_message_lbl, "");
    lv_obj_set_style_text_color(snake_message_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(snake_message_lbl, &lv_font_es_22, 0);
    lv_obj_set_style_text_align(snake_message_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(snake_message_lbl, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(snake_message_lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(snake_message_lbl, 8, 0);
    lv_obj_set_style_pad_all(snake_message_lbl, 12, 0);
    lv_obj_set_width(snake_message_lbl, 260);
    lv_obj_align(snake_message_lbl, LV_ALIGN_CENTER, 0, 0);

    snake_timer = lv_timer_create(snake_tick_cb, SNAKE_TICK_START_MS, NULL);
    lv_timer_pause(snake_timer);

    snake_reset();
}


