#include "minigame.h"
#include <math.h>
#include "game.h"
#include <string.h>

/* See minigame.h for what this layer is for. */

/* Every game that has been built, so a test can reach one by its screen
   pointer (see the accessors at the bottom). Games register on build
   rather than at boot, which is also when their screens first exist. */
#define MINIGAME_REGISTRY_MAX 12
static minigame_t *minigame_registry[MINIGAME_REGISTRY_MAX];
static int minigame_registry_count = 0;

/* "The selected player" per the life-counter's own selection state,
   resolved once when the screen opens rather than per frame: a run
   belongs to whoever started it, even if the selection changes while
   they play. */
static int minigame_resolve_player(void)
{
    int i;
    for (i = 0; i < MAX_DISPLAY_PLAYERS; i++) {
        if (is_player_selected(i)) return i;
    }
    return -1;
}

static void minigame_refresh_score(minigame_t *g)
{
    char buf[32];
    if (g->score_lbl == NULL) return;
    snprintf(buf, sizeof(buf), t(STR_GAME_SCORE_FMT), g->score);
    lv_label_set_text(g->score_lbl, buf);
}

static size_t minigame_append_best(minigame_t *g, char *buf, size_t len, size_t pos)
{
    char best[24];
    if (g->player < 0) return pos;
    snprintf(best, sizeof(best), t(STR_GAME_BEST_FMT),
             nvs_get_game_high_score(g->score_id, g->player));
    return pos + (size_t)snprintf(buf + pos, len - pos, "\n%s", best);
}

static void minigame_refresh_message(minigame_t *g)
{
    char buf[128];
    size_t pos;

    if (g->msg_lbl == NULL) return;

    switch (g->state) {
    case MINIGAME_READY:
        pos = (size_t)snprintf(buf, sizeof(buf), "%s\n%s",
                               t(STR_GAME_TAP_START), t(g->hint));
        minigame_append_best(g, buf, sizeof(buf), pos);
        break;
    case MINIGAME_PAUSED:
        snprintf(buf, sizeof(buf), "%s", t(STR_GAME_PAUSED));
        break;
    case MINIGAME_OVER:
        pos = (size_t)snprintf(buf, sizeof(buf), t(STR_GAME_OVER_FMT), g->score);
        if (g->new_best) {
            snprintf(buf + pos, sizeof(buf) - pos, "\n%s", t(STR_GAME_NEW_BEST));
        } else {
            minigame_append_best(g, buf, sizeof(buf), pos);
        }
        break;
    case MINIGAME_PLAYING:
    default:
        lv_obj_add_flag(g->msg_lbl, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_label_set_text(g->msg_lbl, buf);
    lv_obj_clear_flag(g->msg_lbl, LV_OBJ_FLAG_HIDDEN);
}

static void minigame_refresh(minigame_t *g)
{
    minigame_refresh_score(g);
    minigame_refresh_message(g);
}

/* One shared tick callback for every game. The screen check is what
   stops a run continuing in the background after the user navigates
   away - without it a paused-by-nothing timer keeps waking the CPU and,
   worse, the run is already over by the time they come back. */
static void minigame_tick_cb(lv_timer_t *timer)
{
    minigame_t *g = timer->user_data;

    if (lv_scr_act() != *g->screen) {
        lv_timer_pause(timer);
        return;
    }
    if (g->state != MINIGAME_PLAYING) return;

    g->on_tick(g);
    if (!g->partial_redraw) lv_obj_invalidate(*g->screen);
}

void minigame_invalidate_rect(lv_obj_t *screen, int x1, int y1, int x2, int y2)
{
    lv_area_t a;

    if (screen == NULL) return;
    if (x1 > x2 || y1 > y2) return;
    a.x1 = (lv_coord_t)((x1 < 0) ? 0 : x1);
    a.y1 = (lv_coord_t)((y1 < 0) ? 0 : y1);
    a.x2 = (lv_coord_t)((x2 > 359) ? 359 : x2);
    a.y2 = (lv_coord_t)((y2 > 359) ? 359 : y2);
    if (a.x1 > a.x2 || a.y1 > a.y2) return;
    lv_obj_invalidate_area(screen, &a);
}

void minigame_invalidate_box(lv_obj_t *screen, int cx, int cy, int half)
{
    minigame_invalidate_rect(screen, cx - half, cy - half, cx + half, cy + half);
}

void minigame_invalidate_arc(lv_obj_t *screen, int cx, int cy,
                             int r_in, int r_out, int start_deg, int end_deg)
{
    int span = end_deg - start_deg;
    int steps, i;
    int min_x = 10000, min_y = 10000, max_x = -10000, max_y = -10000;

    while (span < 0) span += 360;
    steps = span / 4 + 2;

    for (i = 0; i <= steps; i++) {
        float a = (float)(start_deg + span * i / steps) * 0.017453292f;
        float ca = cosf(a), sa = sinf(a);
        int k;
        for (k = 0; k < 2; k++) {
            int r = k ? r_out : r_in;
            int x = cx + (int)(ca * (float)r);
            int y = cy + (int)(sa * (float)r);
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
            if (y < min_y) min_y = y;
            if (y > max_y) max_y = y;
        }
    }
    minigame_invalidate_rect(screen, min_x - 3, min_y - 3, max_x + 3, max_y + 3);
}

static void event_minigame_draw(lv_event_t *e)
{
    minigame_t *g = lv_event_get_user_data(e);
    g->on_draw(e);
}

static void event_minigame_tap(lv_event_t *e)
{
    minigame_t *g = lv_event_get_user_data(e);
    if (g->on_tap != NULL) g->on_tap();
}

void minigame_build(minigame_t *g)
{
    lv_obj_t *scr = lv_obj_create(NULL);

    *g->screen = scr;
    lv_obj_set_size(scr, 360, 360);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, event_minigame_draw, LV_EVENT_DRAW_MAIN, g);
    lv_obj_add_event_cb(scr, event_minigame_tap,
                        g->tap_on_press ? LV_EVENT_PRESSED : LV_EVENT_CLICKED, g);

    g->score_lbl = lv_label_create(scr);
    lv_label_set_text(g->score_lbl, "");
    lv_obj_set_style_text_color(g->score_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(g->score_lbl, &lv_font_es_16, 0);
    lv_obj_align(g->score_lbl, LV_ALIGN_TOP_MID, 0, 40);

    g->msg_lbl = lv_label_create(scr);
    lv_label_set_text(g->msg_lbl, "");
    lv_obj_set_style_text_color(g->msg_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(g->msg_lbl, &lv_font_es_22, 0);
    lv_obj_set_style_text_align(g->msg_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(g->msg_lbl, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g->msg_lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g->msg_lbl, 8, 0);
    lv_obj_set_style_pad_all(g->msg_lbl, 12, 0);
    lv_obj_set_width(g->msg_lbl, 260);
    lv_obj_align(g->msg_lbl, LV_ALIGN_CENTER, 0, -20);

    g->timer = lv_timer_create(minigame_tick_cb, g->tick_ms, g);
    lv_timer_pause(g->timer);

    /* The game's own widgets, if it has any, before the first reset -
       reset callbacks routinely write to them. */
    if (g->on_build != NULL) g->on_build(g);

    /* Registered once, not once per rebuild: a game evicted and reopened
       comes back through here with the same minigame_t. */
    {
        int i;
        bool known = false;
        for (i = 0; i < minigame_registry_count; i++) {
            if (minigame_registry[i] == g) { known = true; break; }
        }
        if (!known && minigame_registry_count < MINIGAME_REGISTRY_MAX) {
            minigame_registry[minigame_registry_count++] = g;
        }
    }

    minigame_reset(g);
}

void minigame_reset(minigame_t *g)
{
    g->score = 0;
    g->new_best = false;
    g->state = MINIGAME_READY;
    g->on_reset(g);
    minigame_refresh(g);
}

/* Deletes every other game's screen and timer. Called AFTER the
   incoming game's screen has been loaded, which is what makes it safe:
   at that point no game screen but ours can be the active one, so there
   is nothing to delete out from under the display. (Doing it before the
   load looks tidier and silently does nothing - the screen you want to
   free is still lv_scr_act() and gets skipped by the guard below.)
   lv_scr_load() is instant, with no transition animation still holding
   a reference to the outgoing screen. */
static void minigame_evict_others(const minigame_t *keep)
{
    int i;

    for (i = 0; i < minigame_registry_count; i++) {
        minigame_t *other = minigame_registry[i];

        if (other == keep) continue;
        if (*other->screen == NULL) continue;
        if (*other->screen == lv_scr_act()) continue;

        if (other->on_free != NULL) other->on_free(other);
        if (other->timer != NULL) {
            lv_timer_del(other->timer);
            other->timer = NULL;
        }
        lv_obj_del(*other->screen);
        *other->screen = NULL;
        other->score_lbl = NULL;
        other->msg_lbl = NULL;
    }
}

void minigame_open(minigame_t *g)
{
    /* Built on first entry rather than at boot, and rebuilt after an
       eviction - see the note on minigame_open() in minigame.h. */
    if (*g->screen == NULL) minigame_build(g);
    g->player = minigame_resolve_player();
    minigame_reset(g);
    load_screen_if_needed(*g->screen);
    minigame_evict_others(g);
    lv_obj_invalidate(*g->screen);
}

void minigame_leave(minigame_t *g)
{
    if (g->timer != NULL) lv_timer_pause(g->timer);
}

void minigame_start(minigame_t *g)
{
    g->state = MINIGAME_PLAYING;
    minigame_refresh_message(g);
    lv_timer_set_period(g->timer, g->tick_ms);
    lv_timer_resume(g->timer);
}

void minigame_over(minigame_t *g)
{
    g->state = MINIGAME_OVER;
    lv_timer_pause(g->timer);
    g->new_best = (g->player >= 0 && g->score > 0 &&
                   g->score > nvs_get_game_high_score(g->score_id, g->player));
    if (g->new_best) {
        nvs_set_game_high_score(g->score_id, g->player, g->score);
        settings_save();
    }
    minigame_refresh_message(g);
}

void minigame_toggle_pause(minigame_t *g)
{
    if (!g->pausable) return;
    if (g->state == MINIGAME_PLAYING) {
        g->state = MINIGAME_PAUSED;
        lv_timer_pause(g->timer);
    } else if (g->state == MINIGAME_PAUSED) {
        g->state = MINIGAME_PLAYING;
        lv_timer_resume(g->timer);
    }
    minigame_refresh_message(g);
}

void minigame_set_score(minigame_t *g, int score)
{
    if (g->score == score) return;
    g->score = score;
    minigame_refresh_score(g);
}

void minigame_add_score(minigame_t *g, int delta)
{
    minigame_set_score(g, g->score + delta);
}

bool minigame_handle_tap(minigame_t *g)
{
    switch (g->state) {
    case MINIGAME_READY:
        minigame_start(g);
        return true;
    case MINIGAME_OVER:
        minigame_reset(g);
        lv_obj_invalidate(*g->screen);
        return true;
    case MINIGAME_PAUSED:
        minigame_toggle_pause(g);
        return true;
    case MINIGAME_PLAYING:
    default:
        return false;
    }
}

bool minigame_handle_turn_start(minigame_t *g)
{
    if (g->state == MINIGAME_READY) {
        minigame_start(g);
        return true;
    }
    return g->state != MINIGAME_PLAYING;
}

bool minigame_touch_point(lv_point_t *out)
{
    lv_indev_t *indev = lv_indev_get_act();
    if (indev == NULL) return false;
    lv_indev_get_point(indev, out);
    return true;
}

// ---------- test accessors ----------
static minigame_t *minigame_by_screen(lv_obj_t *screen)
{
    int i;
    for (i = 0; i < minigame_registry_count; i++) {
        if (*minigame_registry[i]->screen == screen) return minigame_registry[i];
    }
    return NULL;
}

minigame_state_t minigame_test_state(lv_obj_t *screen)
{
    minigame_t *g = minigame_by_screen(screen);
    return (g != NULL) ? g->state : MINIGAME_READY;
}

int minigame_test_score(lv_obj_t *screen)
{
    minigame_t *g = minigame_by_screen(screen);
    return (g != NULL) ? g->score : 0;
}
