#include "tetris.h"
#include "minigame.h"
#include "esp_random.h"
#include <string.h>

/* Tetris with the controls the user asked for: the knob rotates, the
 * screen does everything else. Tapping left of the well moves left,
 * right of it moves right, and anywhere down the middle drops.
 *
 * Splitting the taps by zone rather than adding buttons keeps the whole
 * 360px circle usable as the control surface - buttons big enough to
 * hit reliably would have eaten the well itself. The zones are wider
 * than the well on both sides so a thumb landing near the rim still
 * registers as the move the player meant.
 *
 * Well geometry: 10x18 cells of 14px is 140x252, which fits inside the
 * visible circle with room to spare at the corners (at the well's top
 * and bottom rows the chord is ~257px wide, see round_safe.h). */

#define TET_COLS        10
#define TET_ROWS        18
#define TET_CELL        13
#define TET_WELL_X     115      /* (360 - TET_COLS * TET_CELL) / 2 */
/* Low enough to clear the score label at y=40, high enough that the
   next-piece preview under the well still lands inside the glass. */
#define TET_WELL_Y      64
#define TET_WELL_W     (TET_COLS * TET_CELL)
#define TET_WELL_H     (TET_ROWS * TET_CELL)

/* Tap zones, in screen x. Deliberately overlapping the well's edges:
   see the file comment. */
#define TET_ZONE_LEFT  120
#define TET_ZONE_RIGHT 240

#define TET_TICK_MS     30
/* Gravity is counted in ticks so the timer period can stay fixed while
   the fall speed changes with the level. */
#define TET_FALL_TICKS_START 22
#define TET_FALL_TICKS_MIN    3
#define TET_LINES_PER_LEVEL  10

#define TET_PIECE_COUNT  7
#define TET_ROT_COUNT    4

/* Each rotation is a 4x4 bitmap packed into 16 bits, row-major, bit 15
   = (row 0, col 0). Same encoding every Tetris implementation reaches
   for: collision and drawing both become one loop over 16 bits. */
static const uint16_t tet_shapes[TET_PIECE_COUNT][TET_ROT_COUNT] = {
    { 0x0F00, 0x2222, 0x00F0, 0x4444 },  /* I */
    { 0x8E00, 0x6440, 0x0E20, 0x44C0 },  /* J */
    { 0x2E00, 0x4460, 0x0E80, 0xC440 },  /* L */
    { 0x6600, 0x6600, 0x6600, 0x6600 },  /* O */
    { 0x6C00, 0x4620, 0x06C0, 0x8C40 },  /* S */
    { 0x4E00, 0x4640, 0x0E40, 0x4C40 },  /* T */
    { 0xC600, 0x2640, 0x0C60, 0x4C80 },  /* Z */
};

static const uint32_t tet_colors[TET_PIECE_COUNT] = {
    0x26C6DA, 0x5C6BC0, 0xFFA726, 0xFFEE58, 0x66BB6A, 0xAB47BC, 0xEF5350,
};

lv_obj_t *screen_tetris = NULL;

/* 0 = empty, otherwise piece index + 1 (so the colour survives the
   landing and the well isn't a uniform grey slab). */
static uint8_t tet_well[TET_ROWS][TET_COLS];
static int tet_piece, tet_rot, tet_col, tet_row;
static int tet_next_piece;
static int tet_fall_countdown;
static int tet_lines;
static int tet_level;
static int tet_clear_flash;      /* ticks of line-clear highlight left */
static uint16_t tet_clear_mask;  /* rows being flashed */

static void tetris_reset(minigame_t *g);
static void tetris_tick(minigame_t *g);
static void tetris_draw(lv_event_t *e);

static minigame_t tetris_game = {
    .screen   = &screen_tetris,
    .score_id = GAME_SCORE_TETRIS,
    .hint     = STR_TETRIS_HINT,
    .tick_ms  = TET_TICK_MS,
    .pausable = true,
    .on_reset = tetris_reset,
    .on_tick  = tetris_tick,
    .on_draw  = tetris_draw,
    .on_tap   = tetris_handle_tap,
};

static bool tet_cell_set(int piece, int rot, int r, int c)
{
    return (tet_shapes[piece][rot] & (0x8000u >> (r * 4 + c))) != 0;
}

/* True when the piece would overlap a wall, the floor or a settled
   block at this position - the single predicate every move, rotation
   and gravity step is validated against. */
static bool tet_collides(int piece, int rot, int col, int row)
{
    int r, c;
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            int wr, wc;
            if (!tet_cell_set(piece, rot, r, c)) continue;
            wc = col + c;
            wr = row + r;
            if (wc < 0 || wc >= TET_COLS) return true;
            if (wr >= TET_ROWS) return true;
            /* Above the ceiling is legal while spawning: a piece enters
               partly off-screen and only its in-well cells can clash. */
            if (wr < 0) continue;
            if (tet_well[wr][wc] != 0) return true;
        }
    }
    return false;
}

static void tet_spawn(minigame_t *g)
{
    tet_piece = tet_next_piece;
    tet_next_piece = (int)(esp_random() % TET_PIECE_COUNT);
    tet_rot = 0;
    tet_col = (TET_COLS - 4) / 2;
    tet_row = -1;
    tet_fall_countdown = 0;

    /* No room for the new piece means the stack has reached the top. */
    if (tet_collides(tet_piece, tet_rot, tet_col, tet_row)) {
        minigame_over(g);
    }
}

static void tetris_reset(minigame_t *g)
{
    memset(tet_well, 0, sizeof(tet_well));
    tet_lines = 0;
    tet_level = 1;
    tet_clear_flash = 0;
    tet_clear_mask = 0;
    tet_next_piece = (int)(esp_random() % TET_PIECE_COUNT);
    tet_spawn(g);
    /* tet_spawn can declare game over on a full well; on a reset the
       well is empty, so the state it just set stands. */
    g->state = MINIGAME_READY;
}

static int tet_fall_ticks(void)
{
    int ticks = TET_FALL_TICKS_START - (tet_level - 1) * 2;
    return ticks < TET_FALL_TICKS_MIN ? TET_FALL_TICKS_MIN : ticks;
}

static void tet_lock_piece(void)
{
    int r, c;
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            int wr = tet_row + r;
            int wc = tet_col + c;
            if (!tet_cell_set(tet_piece, tet_rot, r, c)) continue;
            if (wr < 0 || wr >= TET_ROWS || wc < 0 || wc >= TET_COLS) continue;
            tet_well[wr][wc] = (uint8_t)(tet_piece + 1);
        }
    }
}

static int tet_clear_lines(void)
{
    int r, c;
    int cleared = 0;

    tet_clear_mask = 0;
    for (r = TET_ROWS - 1; r >= 0; r--) {
        bool full = true;
        for (c = 0; c < TET_COLS; c++) {
            if (tet_well[r][c] == 0) { full = false; break; }
        }
        if (!full) continue;

        tet_clear_mask |= (uint16_t)(1u << r);
        cleared++;
        /* Collapse everything above down one row, then re-test this same
           row - the row that just fell into it may also be full. */
        for (c = r; c > 0; c--) {
            memcpy(tet_well[c], tet_well[c - 1], sizeof(tet_well[0]));
        }
        memset(tet_well[0], 0, sizeof(tet_well[0]));
        r++;
    }
    return cleared;
}

static void tet_settle(minigame_t *g)
{
    /* Classic scoring curve: clearing four at once is worth far more
       than four singles, which is the whole reason to build a well. */
    static const int line_score[5] = { 0, 100, 300, 500, 800 };
    int cleared;

    tet_lock_piece();
    cleared = tet_clear_lines();
    if (cleared > 0) {
        minigame_add_score(g, line_score[cleared] * tet_level);
        tet_lines += cleared;
        tet_level = 1 + tet_lines / TET_LINES_PER_LEVEL;
        tet_clear_flash = 4;
    }
    tet_spawn(g);
}

static void tet_step_down(minigame_t *g)
{
    if (!tet_collides(tet_piece, tet_rot, tet_col, tet_row + 1)) {
        tet_row++;
        return;
    }
    tet_settle(g);
}

static void tetris_tick(minigame_t *g)
{
    if (tet_clear_flash > 0) tet_clear_flash--;

    if (--tet_fall_countdown > 0) return;
    tet_fall_countdown = tet_fall_ticks();
    tet_step_down(g);
}

static void tet_move(int dir)
{
    if (tet_collides(tet_piece, tet_rot, tet_col + dir, tet_row)) return;
    tet_col += dir;
}

static void tet_hard_drop(minigame_t *g)
{
    while (!tet_collides(tet_piece, tet_rot, tet_col, tet_row + 1)) tet_row++;
    tet_settle(g);
}

void tetris_turn(int dir)
{
    int next_rot;
    static const int kicks[5] = { 0, -1, 1, -2, 2 };
    int i;

    if (minigame_handle_turn_start(&tetris_game)) return;

    /* Knob direction maps straight onto rotation direction, so turning
       back undoes an over-rotation instead of cycling all the way
       round. */
    next_rot = (tet_rot + (dir >= 0 ? 1 : TET_ROT_COUNT - 1)) % TET_ROT_COUNT;

    /* Wall kicks: a rotation that clips a wall or the stack is retried a
       column or two across before being refused. Without this, an I
       piece flat against either wall simply cannot be stood up, which
       reads as the knob being broken. */
    for (i = 0; i < 5; i++) {
        if (!tet_collides(tet_piece, next_rot, tet_col + kicks[i], tet_row)) {
            tet_col += kicks[i];
            tet_rot = next_rot;
            lv_obj_invalidate(screen_tetris);
            return;
        }
    }
}

/* The tap zone a screen x falls in: -1 move left, +1 move right,
   0 drop. Kept separate from the event plumbing so a test can exercise
   the mapping without a touch device. */
static void tet_tap_zone(int x)
{
    if (x < TET_ZONE_LEFT) {
        tet_move(-1);
    } else if (x > TET_ZONE_RIGHT) {
        tet_move(1);
    } else {
        tet_hard_drop(&tetris_game);
    }
    lv_obj_invalidate(screen_tetris);
}

void tetris_handle_tap(void)
{
    lv_point_t p;

    if (minigame_handle_tap(&tetris_game)) return;
    if (!minigame_touch_point(&p)) return;
    tet_tap_zone((int)p.x);
}

void tetris_leave_screen(void) { minigame_leave(&tetris_game); }
void build_tetris_screen(void) { minigame_build(&tetris_game); }
void open_tetris_screen(void)  { minigame_open(&tetris_game); }

// ---------- test accessors ----------
int tetris_test_lines(void)     { return tet_lines; }
int tetris_test_level(void)     { return tet_level; }
int tetris_test_rotation(void)  { return tet_rot; }
int tetris_test_piece_col(void) { return tet_col; }

int tetris_test_filled_cells(void)
{
    int r, c, n = 0;
    for (r = 0; r < TET_ROWS; r++)
        for (c = 0; c < TET_COLS; c++)
            if (tet_well[r][c] != 0) n++;
    return n;
}

int tetris_test_stack_height(void)
{
    int r, c;
    for (r = 0; r < TET_ROWS; r++)
        for (c = 0; c < TET_COLS; c++)
            if (tet_well[r][c] != 0) return TET_ROWS - r;
    return 0;
}

void tetris_test_tap_at(int x)
{
    if (minigame_handle_tap(&tetris_game)) return;
    tet_tap_zone(x);
}

// ---------- drawing ----------
static void tet_fill(lv_draw_ctx_t *ctx, lv_draw_rect_dsc_t *dsc,
                     int x1, int y1, int x2, int y2)
{
    lv_area_t a;
    a.x1 = (lv_coord_t)x1; a.y1 = (lv_coord_t)y1;
    a.x2 = (lv_coord_t)x2; a.y2 = (lv_coord_t)y2;
    lv_draw_rect(ctx, dsc, &a);
}

static void tet_draw_block(lv_draw_ctx_t *ctx, lv_draw_rect_dsc_t *dsc,
                           int col, int row, uint32_t color)
{
    int x = TET_WELL_X + col * TET_CELL;
    int y = TET_WELL_Y + row * TET_CELL;
    if (row < 0) return;   /* still above the ceiling */
    dsc->bg_color = lv_color_hex(color);
    tet_fill(ctx, dsc, x + 1, y + 1, x + TET_CELL - 1, y + TET_CELL - 1);
}

static void tetris_draw(lv_event_t *e)
{
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_draw_rect_dsc_t frame, block, ghost;
    int r, c;
    int ghost_row;

    /* Well frame: the only thing telling the player where the playable
       columns are on a screen with no edges of its own. */
    lv_draw_rect_dsc_init(&frame);
    frame.bg_color = lv_color_hex(0x0C0C14);
    frame.bg_opa = LV_OPA_COVER;
    frame.border_color = lv_color_hex(0x37474F);
    frame.border_width = 2;
    frame.border_opa = LV_OPA_COVER;
    frame.radius = 3;
    tet_fill(ctx, &frame, TET_WELL_X - 3, TET_WELL_Y - 3,
             TET_WELL_X + TET_WELL_W + 3, TET_WELL_Y + TET_WELL_H + 3);

    lv_draw_rect_dsc_init(&block);
    block.bg_opa = LV_OPA_COVER;
    block.radius = 2;

    for (r = 0; r < TET_ROWS; r++) {
        bool flashing = tet_clear_flash > 0 && (tet_clear_mask & (1u << r));
        for (c = 0; c < TET_COLS; c++) {
            if (tet_well[r][c] == 0) continue;
            tet_draw_block(ctx, &block, c, r,
                           flashing ? 0xFFFFFF : tet_colors[tet_well[r][c] - 1]);
        }
    }

    /* Landing preview. A hard drop is one tap and instantly final, so
       the player needs to see where it lands before committing. */
    ghost_row = tet_row;
    while (!tet_collides(tet_piece, tet_rot, tet_col, ghost_row + 1)) ghost_row++;

    lv_draw_rect_dsc_init(&ghost);
    ghost.bg_opa = LV_OPA_TRANSP;
    ghost.border_color = lv_color_hex(tet_colors[tet_piece]);
    ghost.border_width = 1;
    ghost.border_opa = LV_OPA_50;
    ghost.radius = 2;
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            int gr = ghost_row + r;
            if (!tet_cell_set(tet_piece, tet_rot, r, c)) continue;
            if (gr < 0 || gr >= TET_ROWS) continue;
            if (gr == tet_row + r) continue;  /* the piece itself covers it */
            tet_fill(ctx, &ghost,
                     TET_WELL_X + (tet_col + c) * TET_CELL + 1,
                     TET_WELL_Y + gr * TET_CELL + 1,
                     TET_WELL_X + (tet_col + c + 1) * TET_CELL - 1,
                     TET_WELL_Y + (gr + 1) * TET_CELL - 1);
        }
    }

    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            if (!tet_cell_set(tet_piece, tet_rot, r, c)) continue;
            tet_draw_block(ctx, &block, tet_col + c, tet_row + r,
                           tet_colors[tet_piece]);
        }
    }

    /* Next piece, small, tucked below the well where nothing else sits. */
    {
        int nx = TET_WELL_X + TET_WELL_W / 2 - 22;
        int ny = TET_WELL_Y + TET_WELL_H + 10;
        lv_draw_rect_dsc_t next;
        lv_draw_rect_dsc_init(&next);
        next.bg_color = lv_color_hex(tet_colors[tet_next_piece]);
        next.bg_opa = LV_OPA_COVER;
        next.radius = 1;
        for (r = 0; r < 4; r++) {
            for (c = 0; c < 4; c++) {
                if (!tet_cell_set(tet_next_piece, 0, r, c)) continue;
                tet_fill(ctx, &next, nx + c * 9 + 1, ny + r * 9 + 1,
                         nx + (c + 1) * 9 - 1, ny + (r + 1) * 9 - 1);
            }
        }
    }
}
