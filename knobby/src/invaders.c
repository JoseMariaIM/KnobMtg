#include "invaders.h"
#include "minigame.h"
#include "esp_random.h"
#include <string.h>

/* Space Invaders, knob to traverse and tap to fire.
 *
 * One shot in flight at a time, as in the original: it is what turns
 * the game into "pick your moment" rather than "hold the button", and
 * on this hardware it also means a tap is never ambiguous - there is
 * exactly one thing it can do while a run is live.
 *
 * The formation marches within a field inset to stay under the round
 * glass, steps DOWN when either edge is reached, and speeds up as its
 * numbers thin - the original's pacing came from the same mechanic (a
 * fixed per-frame budget shared between fewer sprites). */

#define INV_TICK_MS        26

#define INV_FIELD_LEFT     62
#define INV_FIELD_RIGHT   298
#define INV_FIELD_TOP      66

#define INV_SHIP_Y        292
#define INV_SHIP_W         30
#define INV_SHIP_H         16
#define INV_SHIP_STEP      11   /* px per knob detent */

#define INV_COLS            6
#define INV_ROWS            3
#define INV_ALIEN_COUNT   (INV_COLS * INV_ROWS)
#define INV_ALIEN_W        22
#define INV_ALIEN_H        16
#define INV_COL_PITCH      38
#define INV_ROW_PITCH      32
#define INV_STEP_DOWN      14
/* Reaching this line is an instant loss regardless of lives: the
   formation has landed. */
#define INV_INVASION_Y    (INV_SHIP_Y - INV_ALIEN_H)

#define INV_SHOT_W          3
#define INV_SHOT_H         12
#define INV_SHOT_SPEED      9

#define INV_BOMB_MAX        3
#define INV_BOMB_W          4
#define INV_BOMB_H         11
#define INV_BOMB_SPEED    3.3f
#define INV_BOMB_CHANCE     3   /* percent per tick, per wave scaling below */

#define INV_LIVES           3
#define INV_ALIEN_POINTS   10
#define INV_WAVE_BONUS     50

/* Formation speed in px per move, and how many ticks between moves. The
   march accelerates as aliens die: ticks_per_move falls toward 2. */
#define INV_MARCH_PX        6
#define INV_MARCH_TICKS_MAX 9
#define INV_MARCH_TICKS_MIN 2

typedef struct {
    bool active;
    float x, y;
} inv_bomb_t;

lv_obj_t *screen_invaders = NULL;

static int  inv_ship_x;             /* centre */
static bool inv_alive[INV_ALIEN_COUNT];
static int  inv_aliens_left;
static int  inv_form_x;             /* formation origin (left edge, col 0) */
static int  inv_form_y;
static int  inv_form_dir;           /* +1 right, -1 left */
static int  inv_march_countdown;
static int  inv_wave;
static int  inv_lives;
static int  inv_anim_tick;
static int  inv_hit_flash;

static bool  inv_shot_active;
static float inv_shot_x, inv_shot_y;
static inv_bomb_t inv_bombs[INV_BOMB_MAX];

static void invaders_reset(minigame_t *g);
static void invaders_tick(minigame_t *g);
static void invaders_draw(lv_event_t *e);

static minigame_t invaders_game = {
    .screen   = &screen_invaders,
    .score_id = GAME_SCORE_INVADERS,
    .hint     = STR_INVADERS_HINT,
    .tick_ms  = INV_TICK_MS,
    .pausable = true,
    .on_reset = invaders_reset,
    .on_tick  = invaders_tick,
    .on_draw  = invaders_draw,
    .on_tap   = invaders_handle_tap,
};

static void inv_alien_rect(int idx, int *x1, int *y1, int *x2, int *y2)
{
    int col = idx % INV_COLS;
    int row = idx / INV_COLS;
    *x1 = inv_form_x + col * INV_COL_PITCH;
    *y1 = inv_form_y + row * INV_ROW_PITCH;
    *x2 = *x1 + INV_ALIEN_W;
    *y2 = *y1 + INV_ALIEN_H;
}

static int inv_march_ticks(void)
{
    /* Linear from MAX at a full formation down to MIN at the last alien. */
    int span = INV_MARCH_TICKS_MAX - INV_MARCH_TICKS_MIN;
    int ticks = INV_MARCH_TICKS_MIN +
                (inv_aliens_left * span + INV_ALIEN_COUNT / 2) / INV_ALIEN_COUNT;
    /* Each cleared wave also tightens the whole curve. */
    ticks -= (inv_wave - 1);
    if (ticks < INV_MARCH_TICKS_MIN) ticks = INV_MARCH_TICKS_MIN;
    return ticks;
}

static void inv_start_wave(void)
{
    int i;
    for (i = 0; i < INV_ALIEN_COUNT; i++) inv_alive[i] = true;
    inv_aliens_left = INV_ALIEN_COUNT;
    inv_form_x = INV_FIELD_LEFT + 6;
    inv_form_y = INV_FIELD_TOP;
    inv_form_dir = 1;
    inv_march_countdown = inv_march_ticks();
    inv_shot_active = false;
    memset(inv_bombs, 0, sizeof(inv_bombs));
}

static void invaders_reset(minigame_t *g)
{
    (void)g;
    inv_ship_x = (INV_FIELD_LEFT + INV_FIELD_RIGHT) / 2;
    inv_wave = 1;
    inv_lives = INV_LIVES;
    inv_anim_tick = 0;
    inv_hit_flash = 0;
    inv_start_wave();
}

/* Columns empty out as aliens die, so the formation's real extent - not
   its nominal width - is what decides when it turns. Without this the
   last surviving alien would bounce off an invisible wall well short of
   the field edge. */
static void inv_formation_extent(int *left, int *right, int *bottom)
{
    int i;
    int l = 0, r = 0, b = 0;
    bool any = false;

    for (i = 0; i < INV_ALIEN_COUNT; i++) {
        int x1, y1, x2, y2;
        if (!inv_alive[i]) continue;
        inv_alien_rect(i, &x1, &y1, &x2, &y2);
        if (!any) { l = x1; r = x2; b = y2; any = true; }
        if (x1 < l) l = x1;
        if (x2 > r) r = x2;
        if (y2 > b) b = y2;
    }
    if (left != NULL)   *left = l;
    if (right != NULL)  *right = r;
    if (bottom != NULL) *bottom = b;
}

static void inv_drop_bomb(void)
{
    int i, slot = -1;
    int candidates[INV_COLS];
    int count = 0;
    int col;

    for (i = 0; i < INV_BOMB_MAX; i++) {
        if (!inv_bombs[i].active) { slot = i; break; }
    }
    if (slot < 0) return;

    /* Only the lowest alien in a column may fire, so bombs never appear
       to pass through the aliens in front of them. */
    for (col = 0; col < INV_COLS; col++) {
        int row;
        for (row = INV_ROWS - 1; row >= 0; row--) {
            if (inv_alive[row * INV_COLS + col]) {
                candidates[count++] = row * INV_COLS + col;
                break;
            }
        }
    }
    if (count == 0) return;

    {
        int idx = candidates[esp_random() % (uint32_t)count];
        int x1, y1, x2, y2;
        inv_alien_rect(idx, &x1, &y1, &x2, &y2);
        inv_bombs[slot].active = true;
        inv_bombs[slot].x = (float)((x1 + x2) / 2 - INV_BOMB_W / 2);
        inv_bombs[slot].y = (float)y2;
    }
}

static void inv_lose_life(minigame_t *g)
{
    inv_hit_flash = 8;
    inv_shot_active = false;
    memset(inv_bombs, 0, sizeof(inv_bombs));
    if (--inv_lives <= 0) minigame_over(g);
}

static void invaders_tick(minigame_t *g)
{
    int i;
    int left, right, bottom;

    inv_anim_tick++;
    if (inv_hit_flash > 0) inv_hit_flash--;

    /* ---- formation march ---- */
    if (--inv_march_countdown <= 0) {
        inv_formation_extent(&left, &right, &bottom);
        if ((inv_form_dir > 0 && right + INV_MARCH_PX > INV_FIELD_RIGHT) ||
            (inv_form_dir < 0 && left - INV_MARCH_PX < INV_FIELD_LEFT)) {
            inv_form_dir = -inv_form_dir;
            inv_form_y += INV_STEP_DOWN;
        } else {
            inv_form_x += inv_form_dir * INV_MARCH_PX;
        }
        inv_march_countdown = inv_march_ticks();

        inv_formation_extent(NULL, NULL, &bottom);
        if (bottom >= INV_INVASION_Y) {
            minigame_over(g);
            return;
        }
    }

    /* ---- player shot ---- */
    if (inv_shot_active) {
        inv_shot_y -= INV_SHOT_SPEED;
        if (inv_shot_y < INV_FIELD_TOP - INV_SHOT_H) {
            inv_shot_active = false;
        } else {
            for (i = 0; i < INV_ALIEN_COUNT; i++) {
                int x1, y1, x2, y2;
                if (!inv_alive[i]) continue;
                inv_alien_rect(i, &x1, &y1, &x2, &y2);
                if (inv_shot_x + INV_SHOT_W < x1 || inv_shot_x > x2) continue;
                if (inv_shot_y > y2 || inv_shot_y + INV_SHOT_H < y1) continue;
                inv_alive[i] = false;
                inv_aliens_left--;
                inv_shot_active = false;
                minigame_add_score(g, INV_ALIEN_POINTS);
                break;
            }
        }
    }

    /* ---- alien bombs ---- */
    if ((int)(esp_random() % 100) < INV_BOMB_CHANCE + inv_wave) inv_drop_bomb();

    for (i = 0; i < INV_BOMB_MAX; i++) {
        if (!inv_bombs[i].active) continue;
        inv_bombs[i].y += INV_BOMB_SPEED;
        if (inv_bombs[i].y > 360.0f) {
            inv_bombs[i].active = false;
            continue;
        }
        if (inv_bombs[i].y + INV_BOMB_H >= INV_SHIP_Y &&
            inv_bombs[i].y <= INV_SHIP_Y + INV_SHIP_H &&
            inv_bombs[i].x + INV_BOMB_W >= (float)(inv_ship_x - INV_SHIP_W / 2) &&
            inv_bombs[i].x <= (float)(inv_ship_x + INV_SHIP_W / 2)) {
            inv_lose_life(g);
            return;
        }
    }

    /* ---- wave cleared ---- */
    if (inv_aliens_left == 0) {
        minigame_add_score(g, INV_WAVE_BONUS);
        inv_wave++;
        inv_start_wave();
    }
}

void invaders_turn(int dir)
{
    if (minigame_handle_turn_start(&invaders_game)) return;

    inv_ship_x += dir * INV_SHIP_STEP;
    if (inv_ship_x < INV_FIELD_LEFT + INV_SHIP_W / 2)
        inv_ship_x = INV_FIELD_LEFT + INV_SHIP_W / 2;
    if (inv_ship_x > INV_FIELD_RIGHT - INV_SHIP_W / 2)
        inv_ship_x = INV_FIELD_RIGHT - INV_SHIP_W / 2;
    lv_obj_invalidate(screen_invaders);
}

void invaders_handle_tap(void)
{
    if (minigame_handle_tap(&invaders_game)) return;
    if (inv_shot_active) return;   /* one shot in flight, see the file comment */
    inv_shot_active = true;
    inv_shot_x = (float)(inv_ship_x - INV_SHOT_W / 2);
    inv_shot_y = (float)(INV_SHIP_Y - INV_SHOT_H);
}

void invaders_leave_screen(void) { minigame_leave(&invaders_game); }
void build_invaders_screen(void) { minigame_build(&invaders_game); }
void open_invaders_screen(void)  { minigame_open(&invaders_game); }

// ---------- test accessors ----------
int  invaders_test_ship_x(void)      { return inv_ship_x; }
int  invaders_test_aliens_left(void) { return inv_aliens_left; }
int  invaders_test_lives(void)       { return inv_lives; }
int  invaders_test_wave(void)        { return inv_wave; }
bool invaders_test_shot_in_flight(void) { return inv_shot_active; }

bool invaders_test_lowest_alien(int *x, int *y)
{
    int i;
    int best_y = -1, best_x = 0;

    for (i = 0; i < INV_ALIEN_COUNT; i++) {
        int x1, y1, x2, y2;
        if (!inv_alive[i]) continue;
        inv_alien_rect(i, &x1, &y1, &x2, &y2);
        if (y2 > best_y) { best_y = y2; best_x = (x1 + x2) / 2; }
    }
    if (best_y < 0) return false;
    if (x != NULL) *x = best_x;
    if (y != NULL) *y = best_y;
    return true;
}

// ---------- drawing ----------
static void inv_fill(lv_draw_ctx_t *ctx, lv_draw_rect_dsc_t *dsc,
                     int x1, int y1, int x2, int y2)
{
    lv_area_t a;
    a.x1 = (lv_coord_t)x1; a.y1 = (lv_coord_t)y1;
    a.x2 = (lv_coord_t)x2; a.y2 = (lv_coord_t)y2;
    lv_draw_rect(ctx, dsc, &a);
}

/* The classic squid silhouette, blocked out with rects: body, eyes, and
   legs that swap position every few ticks so the formation animates on
   the march the way the original's did. */
static void inv_draw_alien(lv_draw_ctx_t *ctx, lv_draw_rect_dsc_t *dsc,
                           int x1, int y1)
{
    bool alt = ((inv_anim_tick / 8) % 2) == 0;
    lv_draw_rect_dsc_t eye;

    inv_fill(ctx, dsc, x1 + 4, y1, x1 + INV_ALIEN_W - 4, y1 + 4);
    inv_fill(ctx, dsc, x1, y1 + 4, x1 + INV_ALIEN_W, y1 + 11);

    if (alt) {
        inv_fill(ctx, dsc, x1, y1 + 11, x1 + 5, y1 + INV_ALIEN_H);
        inv_fill(ctx, dsc, x1 + INV_ALIEN_W - 5, y1 + 11, x1 + INV_ALIEN_W, y1 + INV_ALIEN_H);
    } else {
        inv_fill(ctx, dsc, x1 + 3, y1 + 11, x1 + 8, y1 + INV_ALIEN_H);
        inv_fill(ctx, dsc, x1 + INV_ALIEN_W - 8, y1 + 11, x1 + INV_ALIEN_W - 3, y1 + INV_ALIEN_H);
    }

    lv_draw_rect_dsc_init(&eye);
    eye.bg_color = lv_color_black();
    eye.bg_opa = LV_OPA_COVER;
    inv_fill(ctx, &eye, x1 + 5, y1 + 6, x1 + 8, y1 + 9);
    inv_fill(ctx, &eye, x1 + INV_ALIEN_W - 8, y1 + 6, x1 + INV_ALIEN_W - 5, y1 + 9);
}

static void invaders_draw(lv_event_t *e)
{
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_draw_rect_dsc_t alien, ship, shot, bomb, life;
    static const uint32_t row_color[INV_ROWS] = { 0xCE93D8, 0x80DEEA, 0xA5D6A7 };
    int i;

    lv_draw_rect_dsc_init(&alien);
    alien.bg_opa = LV_OPA_COVER;
    alien.radius = 1;
    for (i = 0; i < INV_ALIEN_COUNT; i++) {
        int x1, y1, x2, y2;
        if (!inv_alive[i]) continue;
        inv_alien_rect(i, &x1, &y1, &x2, &y2);
        alien.bg_color = lv_color_hex(row_color[i / INV_COLS]);
        inv_draw_alien(ctx, &alien, x1, y1);
    }

    lv_draw_rect_dsc_init(&shot);
    shot.bg_color = lv_color_hex(0xFFEB3B);
    shot.bg_opa = LV_OPA_COVER;
    if (inv_shot_active) {
        inv_fill(ctx, &shot, (int)inv_shot_x, (int)inv_shot_y,
                 (int)inv_shot_x + INV_SHOT_W, (int)inv_shot_y + INV_SHOT_H);
    }

    lv_draw_rect_dsc_init(&bomb);
    bomb.bg_color = lv_color_hex(0xFF7043);
    bomb.bg_opa = LV_OPA_COVER;
    bomb.radius = 2;
    for (i = 0; i < INV_BOMB_MAX; i++) {
        if (!inv_bombs[i].active) continue;
        inv_fill(ctx, &bomb, (int)inv_bombs[i].x, (int)inv_bombs[i].y,
                 (int)inv_bombs[i].x + INV_BOMB_W, (int)inv_bombs[i].y + INV_BOMB_H);
    }

    /* Ship: a hull with a raised cannon, flashing while a life is lost
       so the hit is legible at 26ms per frame. */
    lv_draw_rect_dsc_init(&ship);
    ship.bg_color = (inv_hit_flash > 0 && (inv_hit_flash % 2))
                        ? lv_color_hex(0xEF5350) : lv_color_hex(0x4DD0E1);
    ship.bg_opa = LV_OPA_COVER;
    ship.radius = 3;
    inv_fill(ctx, &ship, inv_ship_x - INV_SHIP_W / 2, INV_SHIP_Y + 6,
             inv_ship_x + INV_SHIP_W / 2, INV_SHIP_Y + INV_SHIP_H);
    inv_fill(ctx, &ship, inv_ship_x - 9, INV_SHIP_Y + 2, inv_ship_x + 9, INV_SHIP_Y + 8);
    inv_fill(ctx, &ship, inv_ship_x - 2, INV_SHIP_Y - 3, inv_ship_x + 2, INV_SHIP_Y + 4);

    lv_draw_rect_dsc_init(&life);
    life.bg_opa = LV_OPA_COVER;
    life.radius = 2;
    for (i = 0; i < INV_LIVES; i++) {
        life.bg_color = (i < inv_lives) ? lv_color_hex(0x4DD0E1)
                                        : lv_color_hex(0x33393B);
        inv_fill(ctx, &life, 130 + i * 16, 324, 130 + i * 16 + 10, 330);
    }
}
