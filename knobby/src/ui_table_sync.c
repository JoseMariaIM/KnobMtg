#include "ui_table_sync.h"
#include "quad_screen.h"
#include "types.h"
#include "net_sync.h"
#include "prefs_table.h"
#include "lang.h"
#include <string.h>

lv_obj_t *screen_table_sync = NULL;
static lv_obj_t *table_sync_action_lbl; /* Start <-> Invite quadrant */
static lv_obj_t *table_sync_status_lbl; /* status tile */
static lv_timer_t *table_sync_timer;
static bool table_sync_radio_error;

void refresh_table_sync_ui(void)
{
    static char status_buf[24];
    int status = net_sync_status();
    int code = net_sync_code();

    switch (status) {
        case NET_SYNC_JOINING:
            snprintf(status_buf, sizeof(status_buf), "%s", t(STR_TABLE_SYNC_JOINING));
            break;
        case NET_SYNC_HOSTING:
            snprintf(status_buf, sizeof(status_buf), t(STR_TABLE_SYNC_INVITING), code);
            break;
        case NET_SYNC_IN_GAME:
            snprintf(status_buf, sizeof(status_buf), t(STR_TABLE_SYNC_IN_GAME), code);
            break;
        default:
            /* Sync is mirror-mode: a 1p view can't represent the shared
               game, so pairing refuses below and the tile says why. */
            if (prefs_get_players_to_track() <= 1)
                snprintf(status_buf, sizeof(status_buf), "%s", t(STR_TABLE_SYNC_1P_NO_SYNC));
            else
                snprintf(status_buf, sizeof(status_buf), "%s",
                         table_sync_radio_error ? t(STR_TABLE_SYNC_RADIO_ERROR) : t(STR_TABLE_SYNC_OFF));
            break;
    }
    lv_label_set_text(table_sync_status_lbl, status_buf);
    /* In a game the host action re-opens the invite window for the same
       session (late joiners, rebooted devices) instead of re-keying. */
    lv_label_set_text(table_sync_action_lbl,
        (status == NET_SYNC_HOSTING || status == NET_SYNC_IN_GAME)
            ? t(STR_TABLE_SYNC_HOLD_INVITE) : t(STR_TABLE_SYNC_HOLD_START));
}

static void event_table_sync_start(lv_event_t *e)
{
    (void)e;
    if (prefs_get_players_to_track() <= 1) return;
    table_sync_radio_error = !net_sync_start_game();
    refresh_table_sync_ui();
}

static void event_table_sync_join(lv_event_t *e)
{
    (void)e;
    if (prefs_get_players_to_track() <= 1) return;
    table_sync_radio_error = !net_sync_join_game();
    refresh_table_sync_ui();
}

static void event_table_sync_leave(lv_event_t *e)
{
    (void)e;
    net_sync_leave_game();
    table_sync_radio_error = false;
    refresh_table_sync_ui();
}

/* Pairing runs in the background (invite window, join listening), so the
   status tile has to track it while the screen is up. The timer pauses
   itself when the user navigates away and is resumed on open. */
static void table_sync_timer_cb(lv_timer_t *timer)
{
    if (lv_scr_act() != screen_table_sync) {
        lv_timer_pause(timer);
        return;
    }
    refresh_table_sync_ui();
}

void open_table_sync_screen(void)
{
    refresh_table_sync_ui();
    lv_timer_resume(table_sync_timer);
    lv_scr_load(screen_table_sync);
}

void build_table_sync_screen(void)
{
    quad_item_t items[4];

    /* All three actions are destructive to a live game (Start opens or
       re-invites, Join drops the current session, Leave exits), so they
       require a long press. */
    memset(items, 0, sizeof(items));
    items[0].label = t(STR_TABLE_SYNC_HOLD_START);
    items[0].cb = event_table_sync_start;
    items[0].enabled = true;
    items[0].event = LV_EVENT_LONG_PRESSED;
    items[1].label = t(STR_TABLE_SYNC_HOLD_JOIN);
    items[1].cb = event_table_sync_join;
    items[1].enabled = true;
    items[1].event = LV_EVENT_LONG_PRESSED;
    items[2].label = t(STR_TABLE_SYNC_HOLD_LEAVE);
    items[2].cb = event_table_sync_leave;
    items[2].enabled = true;
    items[2].event = LV_EVENT_LONG_PRESSED;
    items[3].label = t(STR_TABLE_SYNC_OFF); /* status tile, refreshed live */
    items[3].enabled = false;
    items[3].event = LV_EVENT_CLICKED;

    build_quad_screen(&screen_table_sync, items);
    table_sync_action_lbl =
        lv_obj_get_child(lv_obj_get_child(screen_table_sync, 0), 0);
    table_sync_status_lbl =
        lv_obj_get_child(lv_obj_get_child(screen_table_sync, 3), 0);
    table_sync_timer = lv_timer_create(table_sync_timer_cb, 500, NULL);
    lv_timer_pause(table_sync_timer);
}
