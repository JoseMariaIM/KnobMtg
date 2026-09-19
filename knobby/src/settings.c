#include "settings.h"
#include "hw.h"
#include "storage.h"
#include <string.h>
#include "dice.h"
#include "lang.h"
#include "game_mode.h"
#include "damage_log.h"
#include "rename.h"
#include "game.h"
#include "mana.h"
#include "net_sync.h"
#include "ui_1p.h"
#include "ui_mp.h"
#include "ui_player_menu.h"
#include "ui_wifi.h"
#include "round_safe.h"
#include "ui_list.h"
#include "snake.h"
#include "pong.h"
#include "dino.h"
#include "tetris.h"
#include "breakout.h"
#include "flappy.h"
#include "eggs.h"
#include "invaders.h"
#include "rps.h"
#include "asteroids.h"

// Forward declarations for cross-module calls
extern void reset_all_values(void);
extern void back_to_main(void);

// ---------- screens ----------
lv_obj_t *screen_quad_menu = NULL;
lv_obj_t *screen_tools_menu = NULL;
lv_obj_t *screen_settings = NULL;
lv_obj_t *screen_battery = NULL;
lv_obj_t *screen_minigames_menu = NULL;
/* Page 0 of the minigames menu IS screen_minigames_menu, so the
   settings item that opens it (and settings_handle_back's scan over
   nav_screen pointers) keeps working unchanged. */


// ---------- widgets ----------
static lv_obj_t *arc_brightness = NULL;
static lv_obj_t *label_settings_value = NULL;
static lv_obj_t *label_settings_hint = NULL;
static lv_obj_t *label_settings_battery = NULL;
static lv_obj_t *label_settings_battery_detail = NULL;

// ---------- quadrant menu builder ----------
void build_quad_screen(lv_obj_t **screen, quad_item_t items[4])
{
    int i;
    static const lv_coord_t qx[4] = {0,   182, 0,   182};
    static const lv_coord_t qy[4] = {0,   0,   182, 182};
    static const lv_coord_t lx[4] = {10, -10, 10, -10};
    static const lv_coord_t ly[4] = {15,  15, -15, -15};

    *screen = lv_obj_create(NULL);
    lv_obj_set_size(*screen, 360, 360);
    lv_obj_set_style_bg_color(*screen, lv_color_black(), 0);
    lv_obj_set_style_border_width(*screen, 0, 0);
    lv_obj_set_scrollbar_mode(*screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(*screen, 0, 0);

    for (i = 0; i < 4; i++) {
        lv_obj_t *btn = lv_btn_create(*screen);
        lv_obj_remove_style_all(btn);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_set_size(btn, 178, 178);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_pos(btn, qx[i], qy[i]);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);

        if (items[i].cb != NULL && items[i].enabled) {
            lv_obj_add_event_cb(btn, items[i].cb, items[i].event, items[i].user_data);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x1A1A2E), 0);
        } else {
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x111111), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_60, 0);
            lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        }

        if (items[i].icon != NULL && items[i].icon_font != NULL) {
            lv_obj_t *icon_lbl = lv_label_create(btn);
            lv_label_set_text(icon_lbl, items[i].icon);
            lv_obj_set_style_text_font(icon_lbl, items[i].icon_font, 0);
            lv_obj_set_style_text_color(icon_lbl,
                items[i].enabled ? lv_color_white() : lv_color_hex(0x555555), 0);
            lv_obj_align(icon_lbl, LV_ALIGN_CENTER, lx[i], ly[i] - 18);
        }

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, items[i].label);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_es_16, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, lx[i],
            (items[i].icon != NULL) ? ly[i] + 10 : ly[i]);

        if (!items[i].enabled) {
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x555555), 0);
        }
    }
}

// ---------- refresh ----------
static void refresh_brightness_ring(void)
{
    lv_arc_set_value(arc_brightness, brightness_percent);

    lv_obj_set_style_arc_color(arc_brightness, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_brightness, 18, LV_PART_MAIN);

    lv_obj_set_style_arc_color(arc_brightness, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_brightness, 18, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc_brightness, true, LV_PART_INDICATOR);
}

void refresh_settings_ui(void)
{
    char buf[32];
    snprintf(buf, sizeof(buf), t(STR_BRIGHTNESS_FMT), brightness_percent);
    lv_label_set_text(label_settings_value, buf);
    refresh_brightness_ring();
}

void refresh_battery_ui(void)
{
    char buf[32];
    char detail_buf[48];

    battery_percent = read_battery_percent();
    if (label_settings_battery == NULL) return;

    if (battery_percent < 0) {
        lv_label_set_text(label_settings_battery, t(STR_BATTERY_UNKNOWN));
        if (label_settings_battery_detail != NULL) {
            lv_label_set_text(label_settings_battery_detail, t(STR_BATTERY_NOT_CALIBRATED));
        }
        return;
    }

    snprintf(buf, sizeof(buf), t(STR_BATTERY_FMT), battery_percent);
    lv_label_set_text(label_settings_battery, buf);
    if (label_settings_battery_detail != NULL) {
        snprintf(detail_buf, sizeof(detail_buf), t(STR_BATTERY_CALIBRATED_FMT), battery_voltage);
        lv_label_set_text(label_settings_battery_detail, detail_buf);
    }
}

// ---------- navigation ----------
void open_quad_menu(void)
{
    load_screen_if_needed(screen_quad_menu);
}

void open_settings_screen(void)
{
    // No forced battery read here – the brightness settings page does not display
    // battery info. The battery screen entry point forces its own sample.
    refresh_settings_ui();
    load_screen_if_needed(screen_settings);
}

// ---------- events ----------
// ---------- toggle colors ----------
static const uint32_t TOGGLE_ON  = 0x1B5E20;  /* dark green */
static const uint32_t TOGGLE_OFF = 0x4A1010;  /* dark red */

static void set_btn_color(lv_obj_t *btn, uint32_t color)
{
    if (btn != NULL) lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
}

static uint32_t autodim_color(int index)
{
    static const uint32_t colors[AUTO_DIM_COUNT] = {
        0x37474F, 0x1B5E20, 0x0D47A1, 0x4A148C  /* grey, green, blue, purple */
    };
    return (index >= 0 && index < AUTO_DIM_COUNT) ? colors[index] : 0x37474F;
}
static uint32_t orientation_color(int mode)
{
    switch (mode) {
        case ORIENTATION_MODE_CENTRIC: return TOGGLE_ON;
        case ORIENTATION_MODE_TABLETOP:  return 0x0D47A1;
        default:                   return TOGGLE_OFF;
    }
}

static uint32_t color_mode_color(int mode)
{
    return (mode == COLOR_MODE_LIFE) ? 0x4A148C : 0x0D47A1; /* purple / blue */
}

static uint32_t deselect_color(int index)
{
    static const uint32_t colors[DESELECT_COUNT] = {
        0x37474F, 0x1B5E20, 0x0D47A1, 0x4A148C  /* grey, green, blue, purple */
    };
    return (index >= 0 && index < DESELECT_COUNT) ? colors[index] : 0x1A1A2E;
}

static void event_quad_screen_settings(lv_event_t *e)
{
    (void)e;
    open_settings_list();
}

static const char *autodim_label(int index)
{
    switch (index) {
        case AUTO_DIM_15S: return t(STR_SETTING_AUTODIM_15S);
        case AUTO_DIM_30S: return t(STR_SETTING_AUTODIM_30S);
        case AUTO_DIM_60S: return t(STR_SETTING_AUTODIM_60S);
        default:           return t(STR_SETTING_AUTODIM_OFF);
    }
}

static const char *color_mode_label(int mode)
{
    switch (mode) {
        case COLOR_MODE_LIFE:   return t(STR_SETTING_COLORS_LIFE);
        default:                return t(STR_SETTING_COLORS_PLAYER);
    }
}

static const char *deselect_label(int index)
{
    switch (index) {
        case DESELECT_5S:    return t(STR_SETTING_DESELECT_5S);
        case DESELECT_15S:   return t(STR_SETTING_DESELECT_15S);
        case DESELECT_30S:   return t(STR_SETTING_DESELECT_30S);
        default:             return t(STR_SETTING_DESELECT_NEVER);
    }
}

static const char *orientation_mode_label(int mode)
{
    switch (mode) {
        case ORIENTATION_MODE_CENTRIC:  return t(STR_SETTING_ORIENTATION_CENTRIC);
        case ORIENTATION_MODE_TABLETOP: return t(STR_SETTING_ORIENTATION_TABLETOP);
        default:                        return t(STR_SETTING_ORIENTATION_ABSOLUTE);
    }
}

static const char *auto_eliminate_label(int val)
{
    return val ? t(STR_SETTING_AUTO_ELIM_ON) : t(STR_SETTING_AUTO_ELIM_OFF);
}

// ---------- declarative settings ----------
/* Every user setting lives in this one table. Pages, "More" chaining,
   back-navigation, and sim navigation are all derived from it: to add,
   remove, or reorder a setting, edit only this table (plus its label,
   color, and NVS functions). Label fns must return strings that
   lv_label_set_text may copy — never switch the refresh to
   lv_label_set_text_static. */

static int autodim_get(void) { return nvs_get_auto_dim(); }
static void autodim_set(int v)
{
    nvs_set_auto_dim(v);
    if (v == AUTO_DIM_OFF && dimmed) {
        dimmed = false;
        brightness_apply();
    }
}

static uint32_t toggle_color(int val)
{
    return val ? TOGGLE_ON : TOGGLE_OFF;
}

void open_battery_screen(void)
{
    update_battery_measurement(true);
    refresh_battery_ui();
    lv_scr_load(screen_battery);
}

/* Player-scoped menu screens that should face the acting player when
   menu facing is enabled. screen_select/screen_damage are the commander
   damage picker and editor: player-scoped in multiplayer (opened from the
   player menu, which sets cmd_damage_target), and a no-op in 1-player
   since mp_player_seat_rotation() returns 0 there. */
static bool screen_is_player_menu(lv_obj_t *screen)
{
    return screen == screen_player_menu ||
           screen == screen_eliminated_player_menu ||
           screen == screen_player_all_damage ||
           screen == screen_counter_menu ||
           screen == screen_counter_edit ||
           screen == screen_player_color_menu ||
           screen == screen_player_color_picker ||
           screen == screen_player_name ||
           screen == screen_select ||
           screen == screen_damage;
}

/* Single applier for the effective display rotation: the user's physical
   rotation, plus the acting player's seat on player menus when the
   "Menus: Face Player" toggle is on. Idempotent — recomputes from the
   active screen, so any navigation path can call it safely. */
void menu_facing_refresh(void)
{
    static int applied = -1;
    int target = nvs_get_display_rotation();

    if (nvs_get_menu_facing() && screen_is_player_menu(lv_scr_act()))
        target = (target + mp_player_seat_rotation(menu_player)) & 3;
    if (target == applied) return;
    applied = target;
    display_apply_rotation(target);
    /* Repaint the whole frame immediately: the panel's GRAM still holds the
       old orientation the instant the MADCTL flags change. */
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL);
}

static const char *random_first_label(int val)
{
    return val ? t(STR_SETTING_RANDOM_FIRST_ON) : t(STR_SETTING_RANDOM_FIRST_OFF);
}

static const char *menu_facing_label(int val)
{
    return val ? t(STR_SETTING_MENU_FACE_PLAYER) : t(STR_SETTING_MENU_FIXED);
}

static const char *multi_select_label(int val)
{
    return val ? t(STR_SETTING_MULTI_SELECT_ON) : t(STR_SETTING_MULTI_SELECT_OFF);
}

static void multi_select_set(int v)
{
    nvs_set_multi_select(v);
    if (v == 0) {
        /* Turning multi-select off: drop any lingering multi-selection so the
           single-select rules apply cleanly on return to the life screen. */
        selection_clear();
    }
}

// ---------- table sync screen ----------
lv_obj_t *screen_table_sync = NULL;
lv_obj_t *screen_language_picker = NULL;
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
            if (nvs_get_players_to_track() <= 1)
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
    if (nvs_get_players_to_track() <= 1) return;
    table_sync_radio_error = !net_sync_start_game();
    refresh_table_sync_ui();
}

static void event_table_sync_join(lv_event_t *e)
{
    (void)e;
    if (nvs_get_players_to_track() <= 1) return;
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

// ---------- language picker ----------
static lv_obj_t *language_list_container = NULL;

/* Only ever holds LANG_COUNT (currently 2) short rows, so instead of a
 * top-anchored rectangle we can afford to center this small container
 * on the display's vertical middle, where the circle is widest - see
 * round_safe.h. That keeps it essentially full-width with no clipping,
 * unlike a list long enough to need scrolling (compare scan_list_width
 * in ui_wifi.c, which can't be centered the same way). */
#define LANGUAGE_LIST_Y1 125
#define LANGUAGE_LIST_Y2 235
static int language_list_width = 280;

static void event_language_row_click(lv_event_t *e)
{
    lang_t lang = (lang_t)(intptr_t)lv_event_get_user_data(e);
    lang_set(lang); /* persists + restarts the device to relabel every screen */
}

static void add_language_row(lang_t lang)
{
    bool is_current = (lang == lang_get());

    lv_obj_t *row = lv_obj_create(language_list_container);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, language_list_width - 20, 40);
    lv_obj_set_style_radius(row, 4, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(is_current ? TOGGLE_ON : 0x1E1E2E), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, event_language_row_click, LV_EVENT_CLICKED, (void *)(intptr_t)lang);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, t(lang == LANG_ES ? STR_LANGUAGE_ES : STR_LANGUAGE_EN));
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_es_16, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 12, 0);
}

void open_language_picker_screen(void)
{
    int i;
    lv_obj_clean(language_list_container);
    for (i = 0; i < LANG_COUNT; i++) add_language_row((lang_t)i);
    load_screen_if_needed(screen_language_picker);
}

void build_language_picker_screen(void)
{
    screen_language_picker = lv_obj_create(NULL);
    lv_obj_set_size(screen_language_picker, 360, 360);
    lv_obj_set_style_bg_color(screen_language_picker, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_language_picker, 0, 0);
    lv_obj_set_scrollbar_mode(screen_language_picker, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title = lv_label_create(screen_language_picker);
    lv_label_set_text(title, t(STR_SETTING_LANGUAGE));
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_es_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    language_list_width = round_safe_width(LANGUAGE_LIST_Y1, LANGUAGE_LIST_Y2);
    language_list_container = lv_obj_create(screen_language_picker);
    lv_obj_remove_style_all(language_list_container);
    lv_obj_set_size(language_list_container, language_list_width, LANGUAGE_LIST_Y2 - LANGUAGE_LIST_Y1);
    lv_obj_align(language_list_container, LV_ALIGN_TOP_MID, 0, LANGUAGE_LIST_Y1);
    lv_obj_set_flex_flow(language_list_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(language_list_container, 8, 0);
    lv_obj_set_scrollbar_mode(language_list_container, LV_SCROLLBAR_MODE_OFF);
}

static const setting_item_t settings_items[] = {
    { .id = "brightness",     .fixed_label_id = STR_SETTING_BRIGHTNESS, .navigate = open_settings_screen, .nav_screen = &screen_settings },
    { .id = "autodim",        .label = autodim_label,          .color = autodim_color,     .get = autodim_get,              .set = autodim_set,              .count = AUTO_DIM_COUNT },
    { .id = "battery",        .fixed_label_id = STR_SETTING_BATTERY, .navigate = open_battery_screen, .nav_screen = &screen_battery },
    { .id = "color-mode",     .label = color_mode_label,       .color = color_mode_color,  .get = nvs_get_color_mode,       .set = nvs_set_color_mode,       .count = COLOR_MODE_COUNT },
    { .id = "deselect",       .label = deselect_label,         .color = deselect_color,    .get = nvs_get_deselect_timeout, .set = nvs_set_deselect_timeout, .count = DESELECT_COUNT },
    { .id = "orientation",    .label = orientation_mode_label, .color = orientation_color, .get = nvs_get_orientation,      .set = nvs_set_orientation,      .count = ORIENTATION_MODE_COUNT },
    { .id = "auto-eliminate", .label = auto_eliminate_label,   .color = toggle_color,      .get = nvs_get_auto_eliminate,   .set = nvs_set_auto_eliminate,   .count = 2 },
    { .id = "random-first",   .label = random_first_label,     .color = toggle_color,      .get = nvs_get_random_first,     .set = nvs_set_random_first,     .count = 2 },
    { .id = "multi-select",   .label = multi_select_label,     .color = toggle_color,      .get = nvs_get_multi_select,     .set = multi_select_set,         .count = 2 },
    { .id = "table-sync",     .fixed_label_id = STR_SETTING_TABLE_SYNC, .navigate = open_table_sync_screen, .nav_screen = &screen_table_sync },
    { .id = "menu-facing",    .label = menu_facing_label,      .color = toggle_color,      .get = nvs_get_menu_facing,      .set = nvs_set_menu_facing,      .count = 2 },
    { .id = "language",       .fixed_label_id = STR_SETTING_LANGUAGE, .navigate = open_language_picker_screen, .nav_screen = &screen_language_picker },
    { .id = "wifi",           .fixed_label_id = STR_SETTING_WIFI, .navigate = open_wifi_settings_screen, .nav_screen = &screen_wifi_settings },
    { .id = "updates",        .fixed_label_id = STR_SETTING_UPDATES, .navigate = open_ota_update_screen, .nav_screen = &screen_ota_update },
};
#define SETTINGS_ITEM_COUNT ((int)(sizeof(settings_items) / sizeof(settings_items[0])))

lv_obj_t *screen_settings_list = NULL;

/* Settings used to be five quad pages of three items plus a "More"
   tile - a quarter of every page spent on paging, and no way to see
   where you were in the sequence. It is one scrolling list now; see
   ui_list.h for why a list and not a grid. */
static void settings_row_text(int index, char *name, size_t name_len,
                              char *value, size_t value_len);
static void settings_row_activate(int index);

static const ui_list_model_t settings_list_model = {
    .count = SETTINGS_ITEM_COUNT,
    .text = settings_row_text,
    .activate = settings_row_activate,
};

static ui_list_t settings_list = {
    .screen = &screen_settings_list,
    .title = STR_MENU_SETTINGS,
    .model = &settings_list_model,
};

static void settings_row_text(int index, char *name, size_t name_len,
                              char *value, size_t value_len)
{
    const setting_item_t *it;

    name[0] = '\0';
    value[0] = '\0';
    if (index < 0 || index >= SETTINGS_ITEM_COUNT) return;
    it = &settings_items[index];

    if (it->label != NULL) {
        /* These labels are authored as stacked lines ("Auto-dim\n30s")
           because that is what a square tile wanted. A row wants them
           side by side, so they are flattened and split here rather
           than duplicated into a second set of strings. */
        ui_list_split_label(it->label(it->get()), name, name_len, value, value_len);
    } else {
        ui_list_split_label(t(it->fixed_label_id), name, name_len, value, value_len);
        /* A navigation row has no value to show, and splitting a
           two-word name would wrongly turn its last word into one -
           put the whole thing back as the name. */
        if (value[0] != '\0') {
            snprintf(name, name_len, "%s", t(it->fixed_label_id));
            {
                char *nl;
                while ((nl = strchr(name, '\n')) != NULL) *nl = ' ';
            }
            value[0] = '\0';
        }
    }
}

static void settings_row_activate(int index)
{
    const setting_item_t *it;

    if (index < 0 || index >= SETTINGS_ITEM_COUNT) return;
    it = &settings_items[index];

    if (it->navigate != NULL) {
        it->navigate();
        return;
    }
    it->set((it->get() + 1) % it->count);
}

void refresh_settings_pages_ui(void)
{
    if (screen_settings_list != NULL) ui_list_refresh(&settings_list);
}

static void build_settings_list(void)
{
    ui_list_build(&settings_list);
}

/* Only one menu list is ever resident.
 *
 * Same argument as the minigames' own screen eviction: you can only be
 * looking at one menu, so holding all three costs ~12KB of a 128KB
 * pool permanently for nothing. Rebuilding is a few dozen objects on a
 * menu tap, far below anything noticeable, and it means the next menu
 * added is free rather than another bite out of the margin.
 *
 * Declared here and used by all three openers, which is why the lists
 * are forward-declared above it. */
static ui_list_t tools_list;
static ui_list_t minigames_list;

static void menus_evict_others(ui_list_t *keep)
{
    if (keep != &settings_list)  ui_list_free(&settings_list);
    if (keep != &tools_list)     ui_list_free(&tools_list);
    if (keep != &minigames_list) ui_list_free(&minigames_list);
}

void open_settings_list(void)
{
    ui_list_open(&settings_list);
    menus_evict_others(&settings_list);
}

bool settings_handle_back(lv_obj_t *screen)
{
    int i;

    for (i = 0; i < SETTINGS_ITEM_COUNT; i++) {
        if (settings_items[i].nav_screen != NULL && screen == *settings_items[i].nav_screen) {
            if (screen == screen_settings) settings_save();
            /* Back from a sub-screen returns to the list with that item
               still under the cursor, so you resume where you were
               rather than at the top. */
            if (screen_settings_list == NULL) build_settings_list();
            ui_list_focus(&settings_list, i);
            lv_scr_load(screen_settings_list);
            return true;
        }
    }
    if (screen != NULL && screen == screen_settings_list) {
        settings_save();
        lv_scr_load(screen_quad_menu);
        return true;
    }
    return false;
}

/* Knob moves the cursor down the settings list. Returns false when the
   active screen is not that list. */
bool settings_knob_page(int dir)
{
    if (screen_settings_list == NULL) return false;
    if (lv_scr_act() != screen_settings_list) return false;
    ui_list_knob(&settings_list, dir);
    return true;
}

bool settings_focus_item(const char *id)
{
    int i;
    for (i = 0; i < SETTINGS_ITEM_COUNT; i++) {
        if (strcmp(settings_items[i].id, id) != 0) continue;
        if (screen_settings_list == NULL) build_settings_list();
        return ui_list_focus(&settings_list, i);
    }
    return false;
}

static void event_quad_tools(lv_event_t *e)
{
    (void)e;
    open_tools_menu();
}

static void event_general_game_mode(lv_event_t *e)
{
    (void)e;
    open_game_mode_menu();
}

static void event_open_damage_log(lv_event_t *e)
{
    (void)e;
    open_damage_log_screen();
}

static void event_general_reset(lv_event_t *e)
{
    (void)e;
    reset_all_values();
    back_to_main();
    lv_indev_wait_release(lv_indev_get_act());
}

// ---------- screen builders ----------
/* Tools: the things you DO, as opposed to Settings' things you
   configure. Minigames moved in here from Settings for exactly that
   reason - it sat among Brightness and WiFi, which is not where anyone
   looks for a game. Five entries is one too many for a quad, which is
   the other reason this is a list. */
typedef struct {
    string_id_t   name;
    lv_event_cb_t open;
} tools_entry_t;

static const tools_entry_t tools_entries[] = {
    { STR_TOOL_DICE,      event_tool_dice },
    { STR_TOOL_COIN,      event_tool_coin },
    { STR_TOOL_EVENT_LOG, event_open_damage_log },
    { STR_TOOL_MANA_POOL, event_tool_mana },
    { STR_SETTING_MINIGAMES, NULL },   /* NULL: opened directly below */
};
#define TOOLS_ENTRY_COUNT \
    ((int)(sizeof(tools_entries) / sizeof(tools_entries[0])))

static void tools_row_text(int index, char *name, size_t name_len,
                           char *value, size_t value_len)
{
    name[0] = '\0';
    value[0] = '\0';
    if (index < 0 || index >= TOOLS_ENTRY_COUNT) return;
    snprintf(name, name_len, "%s", t(tools_entries[index].name));
    {
        char *nl;
        while ((nl = strchr(name, '\n')) != NULL) *nl = ' ';
    }
}

static void tools_row_activate(int index)
{
    if (index < 0 || index >= TOOLS_ENTRY_COUNT) return;
    if (tools_entries[index].open != NULL) {
        tools_entries[index].open(NULL);
        return;
    }
    open_minigames_menu();
}

static const ui_list_model_t tools_list_model = {
    .count = TOOLS_ENTRY_COUNT,
    .text = tools_row_text,
    .activate = tools_row_activate,
};

static void build_tools_list(void)
{
    tools_list.screen = &screen_tools_menu;
    tools_list.title = STR_MENU_TOOLS;
    tools_list.model = &tools_list_model;
    ui_list_build(&tools_list);
}

void open_tools_menu(void)
{
    if (screen_tools_menu == NULL) build_tools_list();
    ui_list_open(&tools_list);
    menus_evict_others(&tools_list);
}

bool tools_knob(int dir)
{
    if (screen_tools_menu == NULL) return false;
    if (lv_scr_act() != screen_tools_menu) return false;
    ui_list_knob(&tools_list, dir);
    return true;
}

void build_quad_menus(void)
{
    quad_item_t main_items[4] = {
        {t(STR_MENU_SETTINGS), event_quad_screen_settings, true, LV_EVENT_CLICKED},
        {t(STR_MENU_GAME_MODE), event_general_game_mode, true, LV_EVENT_CLICKED},
        {t(STR_MENU_TOOLS),             event_quad_tools, true, LV_EVENT_CLICKED},
        {t(STR_MENU_RESET_HOLD), event_general_reset, true, LV_EVENT_LONG_PRESSED},
    };
    build_quad_screen(&screen_quad_menu, main_items);

    /* None of the three lists is built here. Each is a screen many
       sessions never open, and a list costs about as much pool as the
       run of quad pages it replaced - so it is held only once someone
       actually goes there. open_tools_menu(), open_settings_list() and
       open_minigames_menu() each build on first entry, the same way
       the games and the WiFi cluster do. */
}

void build_settings_screen(void)
{
    screen_settings = lv_obj_create(NULL);
    lv_obj_set_size(screen_settings, 360, 360);
    lv_obj_set_style_bg_color(screen_settings, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_settings, 0, 0);
    lv_obj_set_scrollbar_mode(screen_settings, LV_SCROLLBAR_MODE_OFF);

    arc_brightness = lv_arc_create(screen_settings);
    lv_obj_set_size(arc_brightness, 280, 280);
    lv_obj_align(arc_brightness, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_rotation(arc_brightness, 90);
    lv_arc_set_bg_angles(arc_brightness, 0, 360);
    lv_arc_set_range(arc_brightness, 0, 100);
    lv_arc_set_value(arc_brightness, brightness_percent);
    lv_obj_remove_style(arc_brightness, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc_brightness, LV_OBJ_FLAG_CLICKABLE);

    label_settings_value = lv_label_create(screen_settings);
    lv_label_set_text(label_settings_value, "Brightness: 80%"); /* placeholder, overwritten by refresh_settings_ui() */
    lv_obj_set_style_text_color(label_settings_value, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_settings_value, &lv_font_es_32, 0);
    lv_obj_align(label_settings_value, LV_ALIGN_CENTER, 0, -14);

    label_settings_hint = lv_label_create(screen_settings);
    lv_label_set_text(label_settings_hint, t(STR_BRIGHTNESS_HINT));
    lv_obj_set_style_text_color(label_settings_hint, lv_color_hex(0x6A6A6A), 0);
    lv_obj_set_style_text_font(label_settings_hint, &lv_font_es_14, 0);
    lv_obj_align(label_settings_hint, LV_ALIGN_CENTER, 0, 24);
}

void build_battery_screen(void)
{
    screen_battery = lv_obj_create(NULL);
    lv_obj_set_size(screen_battery, 360, 360);
    lv_obj_set_style_bg_color(screen_battery, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_battery, 0, 0);
    lv_obj_set_scrollbar_mode(screen_battery, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title = lv_label_create(screen_battery);
    lv_label_set_text(title, t(STR_BATTERY_TITLE));
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_es_22, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);

    label_settings_battery = lv_label_create(screen_battery);
    lv_label_set_text(label_settings_battery, "Battery: --%");
    lv_obj_set_style_text_color(label_settings_battery, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_settings_battery, &lv_font_es_32, 0);
    lv_obj_align(label_settings_battery, LV_ALIGN_CENTER, 0, -10);

    label_settings_battery_detail = lv_label_create(screen_battery);
    lv_label_set_text(label_settings_battery_detail, "No calibrated reading");
    lv_obj_set_style_text_color(label_settings_battery_detail, lv_color_hex(0x7A7A7A), 0);
    lv_obj_set_style_text_font(label_settings_battery_detail, &lv_font_es_16, 0);
    lv_obj_align(label_settings_battery_detail, LV_ALIGN_CENTER, 0, 30);
}

// ---------- minigames menu ----------
/* Every game on the device, in menu order. This table is the only place
   a game has to be listed for it to appear, paginate and open: adding
   one is a row here plus its screen_registry[] row in knob.c (for the
   knob/back dispatch, which is per-screen, not per-menu-entry).

   Ordered oldest-first so the games people already have records in stay
   on the first page where they have always been. */
static const minigame_entry_t minigame_entries[] = {
    { STR_MINIGAME_SNAKE,    open_snake_screen    },
    { STR_MINIGAME_PONG,     open_pong_screen     },
    { STR_MINIGAME_DINO,     open_dino_screen     },
    { STR_MINIGAME_TETRIS,   open_tetris_screen   },
    { STR_MINIGAME_BREAKOUT, open_breakout_screen },
    { STR_MINIGAME_FLAPPY,   open_flappy_screen   },
    { STR_MINIGAME_EGGS,     open_eggs_screen     },
    { STR_MINIGAME_INVADERS, open_invaders_screen },
    { STR_MINIGAME_RPS,      open_rps_screen      },
    { STR_MINIGAME_ASTEROIDS, open_asteroids_screen },
};
#define MINIGAME_ENTRY_COUNT \
    ((int)(sizeof(minigame_entries) / sizeof(minigame_entries[0])))

/* The row the running game was started from, so leaving it puts the
   cursor back on the game you just played rather than at the top. */
static int minigames_launch_row = 0;

static void minigames_row_text(int index, char *name, size_t name_len,
                               char *value, size_t value_len);
static void minigames_row_activate(int index);

static const ui_list_model_t minigames_list_model = {
    .count = MINIGAME_ENTRY_COUNT,
    .text = minigames_row_text,
    .activate = minigames_row_activate,
};


static void minigames_row_text(int index, char *name, size_t name_len,
                               char *value, size_t value_len)
{
    name[0] = '\0';
    value[0] = '\0';
    if (index < 0 || index >= MINIGAME_ENTRY_COUNT) return;
    /* Game names are single words (or deliberately two lines, as Rock
       Paper Scissors is) - flatten, never split: every word is part of
       the name. */
    snprintf(name, name_len, "%s", t(minigame_entries[index].name));
    {
        char *nl;
        while ((nl = strchr(name, '\n')) != NULL) *nl = ' ';
    }
}

static void minigames_row_activate(int index)
{
    if (index < 0 || index >= MINIGAME_ENTRY_COUNT) return;
    minigames_launch_row = index;
    if (minigame_entries[index].open != NULL) minigame_entries[index].open();
}

/* Fresh entry: cursor at the top. */
void open_minigames_menu(void)
{
    if (screen_minigames_menu == NULL) build_minigames_menu_screen();
    ui_list_open(&minigames_list);
    menus_evict_others(&minigames_list);
    minigames_launch_row = 0;
}

/* Leaving a game: back to the row it was started from. */
void open_minigames_menu_at_launch_page(void)
{
    if (screen_minigames_menu == NULL) build_minigames_menu_screen();
    ui_list_focus(&minigames_list, minigames_launch_row);
    load_screen_if_needed(screen_minigames_menu);
    menus_evict_others(&minigames_list);
}

void build_minigames_menu_screen(void)
{
    minigames_list.screen = &screen_minigames_menu;
    minigames_list.title = STR_SETTING_MINIGAMES;
    minigames_list.model = &minigames_list_model;
    ui_list_build(&minigames_list);
}

bool minigames_handle_back(lv_obj_t *screen)
{
    if (screen != NULL && screen == screen_minigames_menu) {
        /* One screen now, so back means "leave the menu". It used to
           defer to settings_handle_back() because the menu lived in
           Settings; it lives in Tools now - and Tools may have been
           evicted while you were in here, so go through its opener. */
        open_tools_menu();
        return true;
    }
    return false;
}

bool minigames_knob_page(int dir)
{
    if (screen_minigames_menu == NULL) return false;
    if (lv_scr_act() != screen_minigames_menu) return false;
    ui_list_knob(&minigames_list, dir);
    return true;
}
