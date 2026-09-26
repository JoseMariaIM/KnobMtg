#include "rps.h"
#include "minigame.h"
#include "esp_random.h"
#include <string.h>

/* Rock Paper Scissors, played as a streak: every win is a point and the
 * run continues, a draw is replayed, and the first loss ends it. A
 * best-of-N would have no score to carry into the high-score table, and
 * a free-play version would have nothing at stake - the streak gives
 * the same one-more-go shape as the other minigames here.
 *
 * The knob cycles the throw and a tap commits it, so the opponent is
 * revealed only after the player is locked in. */

#define RPS_TICK_MS        60
#define RPS_REVEAL_TICKS   16   /* ~1s to read the result before moving on */

#define RPS_ICON_R         42
#define RPS_MINE_CX       112
#define RPS_THEIRS_CX     248
#define RPS_ICON_CY       160

lv_obj_t *screen_rps = NULL;

static rps_throw_t rps_choice;
static rps_throw_t rps_opponent;
static bool rps_revealing;
static int  rps_reveal_countdown;
static int  rps_verdict;          /* -1 lose, 0 draw, +1 win */

static lv_obj_t *rps_mine_lbl = NULL;
static lv_obj_t *rps_theirs_lbl = NULL;
static lv_obj_t *rps_verdict_lbl = NULL;

static void rps_reset(minigame_t *g);
static void rps_build(minigame_t *g);
static void rps_free(minigame_t *g);
static void rps_tick(minigame_t *g);
static void rps_draw(lv_event_t *e);

static minigame_t rps_game = {
    .screen   = &screen_rps,
    .score_id = GAME_SCORE_RPS,
    .hint     = STR_RPS_HINT,
    .tick_ms  = RPS_TICK_MS,
    .pausable = false,          /* turn-based: there is nothing to freeze */
    .on_reset = rps_reset,
    .on_tick  = rps_tick,
    .on_draw  = rps_draw,
    .on_tap   = rps_handle_tap,
    .on_build = rps_build,
    .on_free  = rps_free,
};

/* Three labels of the game's own on top of the shared screen: the
   framework owns the score and the READY/OVER panel, this owns the
   round. */
static void rps_build(minigame_t *g)
{
    (void)g;

    rps_mine_lbl = lv_label_create(screen_rps);
    lv_obj_set_style_text_color(rps_mine_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(rps_mine_lbl, &lv_font_es_16, 0);
    lv_obj_set_width(rps_mine_lbl, 100);
    lv_obj_set_style_text_align(rps_mine_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(rps_mine_lbl, LV_ALIGN_TOP_LEFT, RPS_MINE_CX - 50, RPS_ICON_CY + 52);

    rps_theirs_lbl = lv_label_create(screen_rps);
    lv_obj_set_style_text_color(rps_theirs_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(rps_theirs_lbl, &lv_font_es_16, 0);
    lv_obj_set_width(rps_theirs_lbl, 100);
    lv_obj_set_style_text_align(rps_theirs_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(rps_theirs_lbl, LV_ALIGN_TOP_LEFT, RPS_THEIRS_CX - 50, RPS_ICON_CY + 52);

    rps_verdict_lbl = lv_label_create(screen_rps);
    lv_obj_set_style_text_color(rps_verdict_lbl, lv_color_hex(0xFFD54F), 0);
    lv_obj_set_style_text_font(rps_verdict_lbl, &lv_font_es_22, 0);
    lv_obj_set_width(rps_verdict_lbl, 300);
    lv_obj_set_style_text_align(rps_verdict_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(rps_verdict_lbl, LV_ALIGN_TOP_MID, 0, 262);
}

/* The three labels are children of the screen, so deleting the screen
   takes them with it - these pointers just have to stop pointing at the
   wreckage before that happens. */
static void rps_free(minigame_t *g)
{
    (void)g;
    rps_mine_lbl = NULL;
    rps_theirs_lbl = NULL;
    rps_verdict_lbl = NULL;
}

/* The only rule in the game, as a pure function: rock(0) beats
   scissors(2), paper(1) beats rock(0), scissors(2) beats paper(1) -
   i.e. x beats (x + 2) % 3. */
int rps_beats(rps_throw_t mine, rps_throw_t theirs)
{
    if (mine == theirs) return 0;
    return ((mine + 2) % RPS_THROW_COUNT == theirs) ? 1 : -1;
}

static string_id_t rps_throw_name(rps_throw_t th)
{
    switch (th) {
    case RPS_PAPER:    return STR_RPS_PAPER;
    case RPS_SCISSORS: return STR_RPS_SCISSORS;
    case RPS_ROCK:
    default:           return STR_RPS_ROCK;
    }
}

static void rps_refresh_labels(void)
{
    if (rps_mine_lbl == NULL) return;

    lv_label_set_text(rps_mine_lbl, t(rps_throw_name(rps_choice)));
    if (rps_revealing) {
        lv_label_set_text(rps_theirs_lbl, t(rps_throw_name(rps_opponent)));
        lv_label_set_text(rps_verdict_lbl,
            rps_verdict > 0 ? t(STR_RPS_WIN) :
            rps_verdict < 0 ? t(STR_RPS_LOSE) : t(STR_RPS_DRAW));
    } else {
        lv_label_set_text(rps_theirs_lbl, "?");
        lv_label_set_text(rps_verdict_lbl, "");
    }
}

static void rps_set_children_hidden(bool hidden)
{
    if (rps_mine_lbl == NULL) return;
    if (hidden) {
        lv_obj_add_flag(rps_mine_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(rps_theirs_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(rps_verdict_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(rps_mine_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(rps_theirs_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(rps_verdict_lbl, LV_OBJ_FLAG_HIDDEN);
    }
}

static void rps_reset(minigame_t *g)
{
    (void)g;
    rps_choice = RPS_ROCK;
    rps_opponent = RPS_ROCK;
    rps_revealing = false;
    rps_reveal_countdown = 0;
    rps_verdict = 0;
    rps_refresh_labels();
    /* The throw icons would sit under the READY / GAME OVER panel, so
       they only appear once a round is actually in progress. */
    rps_set_children_hidden(true);
}

static void rps_tick(minigame_t *g)
{
    if (!rps_revealing) return;
    if (--rps_reveal_countdown > 0) return;

    rps_revealing = false;
    if (rps_verdict < 0) {
        rps_set_children_hidden(true);
        minigame_over(g);
        return;
    }
    /* A win or a draw both continue; the point for a win was already
       banked at reveal time so the score moves with the reveal, not a
       second later. */
    rps_refresh_labels();
}

void rps_turn(int dir)
{
    if (minigame_handle_turn_start(&rps_game)) {
        rps_set_children_hidden(false);
        rps_refresh_labels();
        return;
    }
    if (rps_revealing) return;   /* locked in until the reveal ends */

    rps_choice = (rps_throw_t)((rps_choice + (dir >= 0 ? 1 : RPS_THROW_COUNT - 1))
                               % RPS_THROW_COUNT);
    rps_refresh_labels();
    lv_obj_invalidate(screen_rps);
}

void rps_handle_tap(void)
{
    if (minigame_handle_tap(&rps_game)) {
        rps_set_children_hidden(rps_game.state != MINIGAME_PLAYING);
        rps_refresh_labels();
        return;
    }
    if (rps_revealing) return;

    rps_opponent = (rps_throw_t)(esp_random() % RPS_THROW_COUNT);
    rps_verdict = rps_beats(rps_choice, rps_opponent);
    rps_revealing = true;
    rps_reveal_countdown = RPS_REVEAL_TICKS;
    if (rps_verdict > 0) minigame_add_score(&rps_game, 1);
    rps_refresh_labels();
    lv_obj_invalidate(screen_rps);
}

void rps_leave_screen(void) { minigame_leave(&rps_game); }

void build_rps_screen(void) { minigame_build(&rps_game); }

void open_rps_screen(void)
{
    minigame_open(&rps_game);
    rps_set_children_hidden(true);
}

// ---------- test accessors ----------
rps_throw_t rps_test_choice(void)   { return rps_choice; }
bool        rps_test_revealing(void){ return rps_revealing; }
rps_throw_t rps_test_opponent(void) { return rps_opponent; }
int         rps_test_verdict(void)  { return rps_verdict; }

// ---------- drawing ----------
static void rps_fill(lv_draw_ctx_t *ctx, lv_draw_rect_dsc_t *dsc,
                     int x1, int y1, int x2, int y2)
{
    lv_area_t a;
    a.x1 = (lv_coord_t)x1; a.y1 = (lv_coord_t)y1;
    a.x2 = (lv_coord_t)x2; a.y2 = (lv_coord_t)y2;
    lv_draw_rect(ctx, dsc, &a);
}

/* Icons rather than hands: a hand rendered from rectangles at 84px
   reads as a blob, whereas the three shapes below are unmistakable and
   each one's silhouette differs from the other two at a glance. */
static void rps_draw_throw(lv_draw_ctx_t *ctx, int cx, int cy,
                           rps_throw_t th, bool hidden, uint32_t color)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_line_dsc_t line;
    int r = RPS_ICON_R;

    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.bg_color = lv_color_hex(color);

    if (hidden) {
        /* Face-down marker while the opponent is still concealed. */
        dsc.bg_opa = LV_OPA_TRANSP;
        dsc.border_color = lv_color_hex(0x546E7A);
        dsc.border_width = 3;
        dsc.border_opa = LV_OPA_COVER;
        dsc.radius = LV_RADIUS_CIRCLE;
        rps_fill(ctx, &dsc, cx - r, cy - r, cx + r, cy + r);
        return;
    }

    switch (th) {
    case RPS_ROCK:
        dsc.radius = LV_RADIUS_CIRCLE;
        rps_fill(ctx, &dsc, cx - r, cy - r + 6, cx + r, cy + r - 6);
        break;

    case RPS_PAPER:
        dsc.radius = 4;
        rps_fill(ctx, &dsc, cx - r + 8, cy - r, cx + r - 8, cy + r);
        /* A folded corner, so a plain sheet isn't mistaken for a card. */
        lv_draw_rect_dsc_init(&dsc);
        dsc.bg_opa = LV_OPA_COVER;
        dsc.bg_color = lv_color_black();
        rps_fill(ctx, &dsc, cx + r - 22, cy - r, cx + r - 8, cy - r + 14);
        break;

    case RPS_SCISSORS:
    default:
        lv_draw_line_dsc_init(&line);
        line.color = lv_color_hex(color);
        line.width = 9;
        line.opa = LV_OPA_COVER;
        line.round_start = 1;
        line.round_end = 1;
        {
            lv_point_t a1 = { (lv_coord_t)(cx - r + 6), (lv_coord_t)(cy - r + 4) };
            lv_point_t a2 = { (lv_coord_t)(cx + r - 14), (lv_coord_t)(cy + r - 18) };
            lv_point_t b1 = { (lv_coord_t)(cx + r - 6), (lv_coord_t)(cy - r + 4) };
            lv_point_t b2 = { (lv_coord_t)(cx - r + 14), (lv_coord_t)(cy + r - 18) };
            lv_draw_line(ctx, &line, &a1, &a2);
            lv_draw_line(ctx, &line, &b1, &b2);
        }
        /* Finger holes at the bottom complete the shape. */
        lv_draw_rect_dsc_init(&dsc);
        dsc.bg_opa = LV_OPA_TRANSP;
        dsc.border_color = lv_color_hex(color);
        dsc.border_width = 5;
        dsc.border_opa = LV_OPA_COVER;
        dsc.radius = LV_RADIUS_CIRCLE;
        rps_fill(ctx, &dsc, cx - r + 4, cy + r - 24, cx - r + 26, cy + r - 2);
        rps_fill(ctx, &dsc, cx + r - 26, cy + r - 24, cx + r - 4, cy + r - 2);
        break;
    }
}

static void rps_draw(lv_event_t *e)
{
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);

    if (rps_game.state != MINIGAME_PLAYING) return;

    rps_draw_throw(ctx, RPS_MINE_CX, RPS_ICON_CY, rps_choice, false, 0x4DD0E1);
    rps_draw_throw(ctx, RPS_THEIRS_CX, RPS_ICON_CY, rps_opponent,
                   !rps_revealing, 0xFF8A65);
}
