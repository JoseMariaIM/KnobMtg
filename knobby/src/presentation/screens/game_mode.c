#include "game_mode.h"
#include "home.h"
#include "quad_screen.h"
#include "../../adapters/prefs_table.h"
#include "ui_partners.h"
#include "ui_mp.h"
#include "../../adapters/net_sync.h"
#include "../../adapters/lang.h"
#include "../knob.h" /* reset_all_values */

// ---------- screens ----------
lv_obj_t *screen_game_mode_menu = NULL;
lv_obj_t *screen_custom_life = NULL;

// ---------- dynamic labels ----------
static lv_obj_t *label_gm_num_players = NULL;
static lv_obj_t *label_gm_life_total = NULL;

// ---------- custom life widgets ----------
static lv_obj_t *label_custom_life_value = NULL;

/* ---------- temp settings (applied on Apply) ----------
   One player count, not the two separate "how many are playing" /
   "how many fit on screen" settings this screen used to expose: with
   at most MAX_DISPLAY_PLAYERS panels to show, a second number only
   ever meant "track fewer than are playing", which nobody wants
   mid-game. Apply writes the same value to both NVS keys so the rest
   of the app (which still reads them separately - commander-damage
   opponent lists use num_players, the panel layout uses
   players_to_track) needs no changes. */
static int temp_num_players;
static int temp_life_total;

// ---------- refresh ----------
void refresh_game_mode_menu_ui(void)
{
    char buf[32];

    snprintf(buf, sizeof(buf), t(STR_GAME_MODE_PLAYERS_FMT), temp_num_players);
    lv_label_set_text(label_gm_num_players, buf);

    snprintf(buf, sizeof(buf), t(STR_GAME_MODE_LIFE_FMT), temp_life_total);
    lv_label_set_text(label_gm_life_total, buf);
}

void refresh_custom_life_ui(void)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", temp_life_total);
    lv_label_set_text(label_custom_life_value, buf);
}

// ---------- navigation ----------
void open_game_mode_menu(void)
{
    temp_num_players = prefs_get_num_players();
    /* A config saved before the two player settings were merged could
       hold more players than there are panels; normalize it the moment
       the user opens this screen rather than showing a number the
       control can no longer reach. */
    if (temp_num_players > MAX_DISPLAY_PLAYERS) temp_num_players = MAX_DISPLAY_PLAYERS;
    if (temp_num_players < 1) temp_num_players = 1;
    temp_life_total = prefs_get_life_total();
    refresh_game_mode_menu_ui();
    lv_scr_load(screen_game_mode_menu);
}

// ---------- knob input ----------
void change_custom_life(int delta)
{
    temp_life_total += delta;
    if (temp_life_total < 1) temp_life_total = 1;
    if (temp_life_total > LIFE_MAX) temp_life_total = LIFE_MAX;
    refresh_custom_life_ui();
}

/* Knob handler for screen_game_mode_menu: clamps rather than wrapping,
   so spinning past either end parks on 1 / MAX_DISPLAY_PLAYERS instead
   of jumping back around (tapping the tile still wraps - see
   event_gm_num_players). */
void change_num_players(int delta)
{
    temp_num_players += delta;
    if (temp_num_players < 1) temp_num_players = 1;
    if (temp_num_players > MAX_DISPLAY_PLAYERS) temp_num_players = MAX_DISPLAY_PLAYERS;
    refresh_game_mode_menu_ui();
}

// ---------- events ----------
static void event_gm_num_players(lv_event_t *e)
{
    (void)e;

    temp_num_players++;
    if (temp_num_players > MAX_DISPLAY_PLAYERS) temp_num_players = 1;

    refresh_game_mode_menu_ui();
}

static void event_gm_life_cycle(lv_event_t *e)
{
    (void)e;

    if (temp_life_total == 20) temp_life_total = 25;
    else if (temp_life_total == 25) temp_life_total = 30;
    else if (temp_life_total == 30) temp_life_total = 40;
    else temp_life_total = 20;

    refresh_game_mode_menu_ui();
}

static void event_gm_life_custom(lv_event_t *e)
{
    (void)e;
    refresh_custom_life_ui();
    lv_scr_load(screen_custom_life);
    lv_indev_wait_release(lv_indev_get_act());
}

static void event_gm_apply(lv_event_t *e)
{
    (void)e;
    /* Applying game mode redefines the game (players, view, starting
       life), and settings don't sync — so rather than broadcasting a
       reset at THIS device's config over the whole table, leave the
       session before the reset below can reach it. Same-config new
       games use the shared reset; config changes re-pair. (No-op when
       not synced.) */
    net_sync_leave_game();
    /* One control, both keys: see the comment on temp_num_players. */
    prefs_set_num_players(temp_num_players);
    prefs_set_players_to_track(temp_num_players);
    prefs_set_life_total(temp_life_total);
    reset_all_values();
    rebuild_multiplayer_layout(temp_num_players);
    back_to_main();
    lv_indev_wait_release(lv_indev_get_act());
}

// ---------- screen builders ----------
static void event_gm_partners(lv_event_t *e)
{
    (void)e;
    open_partners_screen();
}

void build_game_mode_menu_screen(void)
{
    lv_obj_t *btn;
    /* Placeholder text only: open_game_mode_menu() always calls
       refresh_game_mode_menu_ui() before this screen is ever shown. */
    char buf_players[16], buf_life[16];
    snprintf(buf_players, sizeof(buf_players), t(STR_GAME_MODE_PLAYERS_FMT), 4);
    snprintf(buf_life, sizeof(buf_life), t(STR_GAME_MODE_LIFE_FMT), 40);

    /* Slot 2 was a disabled placeholder left by merging the old "Track"
       tile into Players. Partners lives there now: who fields two
       commanders is the same kind of decision as how many players and
       what life they start on - you make it once while setting the
       table up, and it belongs beside them rather than buried in the
       device's own settings. Apply stays in slot 3, where it has
       always been. */
    quad_item_t items[4] = {
        {buf_players,           event_gm_num_players, true,  LV_EVENT_CLICKED},
        {buf_life,              event_gm_life_cycle,  true,  LV_EVENT_SHORT_CLICKED},
        {t(STR_SETTING_PARTNERS), event_gm_partners,  true,  LV_EVENT_CLICKED},
        {t(STR_GAME_MODE_APPLY_HOLD), event_gm_apply, true,  LV_EVENT_LONG_PRESSED},
    };
    build_quad_screen(&screen_game_mode_menu, items);

    // Store label references for dynamic updates
    btn = lv_obj_get_child(screen_game_mode_menu, 0);
    label_gm_num_players = lv_obj_get_child(btn, 0);
    btn = lv_obj_get_child(screen_game_mode_menu, 1);
    label_gm_life_total = lv_obj_get_child(btn, 0);

    // Add long press on life total for custom value entry
    lv_obj_add_event_cb(btn, event_gm_life_custom, LV_EVENT_LONG_PRESSED, NULL);
}

void build_custom_life_screen(void)
{
    lv_obj_t *title;
    lv_obj_t *hint;

    screen_custom_life = lv_obj_create(NULL);
    lv_obj_set_size(screen_custom_life, 360, 360);
    lv_obj_set_style_bg_color(screen_custom_life, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_custom_life, 0, 0);
    lv_obj_set_scrollbar_mode(screen_custom_life, LV_SCROLLBAR_MODE_OFF);

    title = lv_label_create(screen_custom_life);
    lv_label_set_text(title, t(STR_GAME_MODE_LIFE_TITLE));
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_es_22, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);

    label_custom_life_value = lv_label_create(screen_custom_life);
    lv_label_set_text(label_custom_life_value, "40");
    lv_obj_set_style_text_color(label_custom_life_value, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_custom_life_value, &lv_font_es_32, 0);
    lv_obj_align(label_custom_life_value, LV_ALIGN_CENTER, 0, -10);

    hint = lv_label_create(screen_custom_life);
    lv_label_set_text(hint, t(STR_GAME_MODE_TURN_KNOB_ADJUST));
    lv_obj_set_style_text_color(hint, lv_color_hex(0x6A6A6A), 0);
    lv_obj_set_style_text_font(hint, &lv_font_es_14, 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 24);
}
