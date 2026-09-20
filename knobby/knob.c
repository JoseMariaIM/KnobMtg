#include "src/types.h"
#include "src/hw.h"
#include "src/storage.h"
#include "src/game.h"
#include "src/dice.h"
#include "src/intro.h"
#include "src/ui_1p.h"
#include "src/ui_mp.h"
#include "src/ui_player_menu.h"
#include "src/settings.h"
#include "src/nav.h"
#include "src/ota_notice.h"
#include "src/minigames_menu.h"
#include "src/ui_battery.h"
#include "src/ui_partners.h"
#include "src/ui_table_sync.h"
#include "src/ui_language.h"
#include "src/game_mode.h"
#include "src/damage_log.h"
#include "src/rename.h"
#include "src/mana.h"
#include "src/ui_wifi.h"
#include "src/wifi_ota.h"
#include "src/attack.h"
#include "src/snake.h"
#include "src/pong.h"
#include "src/dino.h"
#include "src/tetris.h"
#include "src/breakout.h"
#include "src/flappy.h"
#include "src/eggs.h"
#include "src/invaders.h"
#include "src/rps.h"
#include "src/asteroids.h"

// ---------- swipe state ----------
static lv_obj_t *previous_screen = NULL;
static lv_obj_t *swipe_hint = NULL;
static lv_obj_t *swipe_hint_icon = NULL;
static volatile bool swipe_up_pending = false;
static volatile bool swipe_down_pending = false;
static volatile bool swipe_left_pending = false;
static volatile bool swipe_right_pending = false;

#define SWIPE_HINT_SIZE 52
#define SWIPE_HINT_EDGE_INSET 12
#define SWIPE_HINT_TRAVEL 18
#define SWIPE_HINT_OPACITY_MAX 255
#define SWIPE_HINT_OPACITY_MIN 48
#define SWIPE_HINT_BG_OPACITY_MAX 120

// ---------- knob event queue ----------
static knob_input_event_t knob_event_queue[KNOB_EVENT_QUEUE_SIZE];
static uint8_t knob_event_head = 0;  /* producer (encoder task) owns */
static uint8_t knob_event_tail = 0;  /* consumer (main loop) owns */

// ---------- swipe notifications ----------
void knob_notify_swipe_up(void)
{
    swipe_up_pending = true;
}

void knob_notify_swipe_down(void)
{
    swipe_down_pending = true;
}

void knob_notify_swipe_left(void)
{
    swipe_left_pending = true;
}

void knob_notify_swipe_right(void)
{
    swipe_right_pending = true;
}

static bool is_player_screen(lv_obj_t *screen)
{
    return screen == screen_1p ||
           screen == screen_multiplayer;
}

static int clamp_value(int value, int min_value, int max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static int scale_value(int value, int input_max, int output_max)
{
    if (value < 0 || input_max <= 0 || output_max <= 0) return 0;
    return (int)((((int64_t)value * output_max) + (input_max / 2)) / input_max);
}

static int get_display_width(void)
{
    lv_disp_t *disp = lv_disp_get_default();
    return (disp != NULL) ? (int)lv_disp_get_hor_res(disp) : 360;
}

static int get_display_height(void)
{
    lv_disp_t *disp = lv_disp_get_default();
    return (disp != NULL) ? (int)lv_disp_get_ver_res(disp) : 360;
}

static bool is_axis_dominant(int primary, int secondary, int min_travel)
{
    if (primary < min_travel) return false;
    if (secondary > KNOB_SWIPE_MAX_LATERAL) return false;
    return (primary * KNOB_SWIPE_AXIS_BIAS_DEN) >=
           (secondary * KNOB_SWIPE_AXIS_BIAS_NUM);
}

knob_swipe_direction_t knob_classify_swipe_direction(lv_obj_t *screen,
                                                     int start_x, int start_y,
                                                     int dx, int dy,
                                                     int min_travel)
{
    int width = get_display_width();
    int height = get_display_height();
    int abs_dx = dx >= 0 ? dx : -dx;
    int abs_dy = dy >= 0 ? dy : -dy;

    if (screen == NULL || screen == screen_intro) return KNOB_SWIPE_NONE;

    /* A wedge-to-wedge attack drag can easily cross an edge zone with
       mostly-axis-aligned movement (see attack_drag_source in ui_mp.c) -
       without this it could double-fire as a menu-open swipe too. */
    if (attack_gesture_in_progress()) return KNOB_SWIPE_NONE;

    if (is_player_screen(screen)) {
        if (start_x <= KNOB_SWIPE_LEFT_EDGE_ZONE &&
            dx > 0 &&
            is_axis_dominant(dx, abs_dy, min_travel)) {
            return KNOB_SWIPE_RIGHT;
        }
        if (start_x >= (width - KNOB_SWIPE_RIGHT_EDGE_ZONE) &&
            dx < 0 &&
            is_axis_dominant(-dx, abs_dy, min_travel)) {
            return KNOB_SWIPE_LEFT;
        }
        if (start_y <= KNOB_SWIPE_TOP_EDGE_ZONE &&
            dy > 0 &&
            is_axis_dominant(dy, abs_dx, min_travel)) {
            return KNOB_SWIPE_DOWN;
        }
        if (start_y >= (height - KNOB_SWIPE_BOTTOM_EDGE_ZONE) &&
            dy < 0 &&
            is_axis_dominant(-dy, abs_dx, min_travel)) {
            return KNOB_SWIPE_UP;
        }
    } else {
        if (start_x >= (width - KNOB_SWIPE_RIGHT_EDGE_ZONE) &&
            dx < 0 &&
            is_axis_dominant(-dx, abs_dy, min_travel)) {
            return KNOB_SWIPE_LEFT;
        }
        if (start_y <= KNOB_SWIPE_TOP_EDGE_ZONE &&
            dy > 0 &&
            is_axis_dominant(dy, abs_dx, min_travel)) {
            return KNOB_SWIPE_DOWN;
        }
    }

    return KNOB_SWIPE_NONE;
}

static int get_swipe_hint_distance(knob_swipe_direction_t direction, int dx, int dy)
{
    switch (direction) {
    case KNOB_SWIPE_LEFT:
        return -dx;
    case KNOB_SWIPE_RIGHT:
        return dx;
    case KNOB_SWIPE_UP:
        return -dy;
    case KNOB_SWIPE_DOWN:
        return dy;
    default:
        return 0;
    }
}

static int get_swipe_hint_progress(knob_swipe_direction_t direction, int dx, int dy)
{
    int distance = get_swipe_hint_distance(direction, dx, dy);

    return clamp_value(scale_value(distance - KNOB_SWIPE_HINT_REVEAL_START,
                                   KNOB_SWIPE_THRESHOLD - KNOB_SWIPE_HINT_REVEAL_START,
                                   SWIPE_HINT_OPACITY_MAX),
                       SWIPE_HINT_OPACITY_MIN,
                       SWIPE_HINT_OPACITY_MAX);
}

bool knob_swipe_hint_fully_revealed(lv_obj_t *screen,
                                    int start_x, int start_y,
                                    int cur_x, int cur_y)
{
    knob_swipe_direction_t direction;
    int dx = cur_x - start_x;
    int dy = cur_y - start_y;

    direction = knob_classify_swipe_direction(screen, start_x, start_y,
                                              dx, dy,
                                              KNOB_SWIPE_HINT_REVEAL_START);
    if (direction == KNOB_SWIPE_NONE) return false;

    return get_swipe_hint_progress(direction, dx, dy) >= SWIPE_HINT_OPACITY_MAX;
}

static void ensure_swipe_hint(void)
{
    if (swipe_hint != NULL) return;

    swipe_hint = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(swipe_hint);
    lv_obj_set_size(swipe_hint, SWIPE_HINT_SIZE, SWIPE_HINT_SIZE);
    lv_obj_set_style_radius(swipe_hint, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(swipe_hint, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(swipe_hint, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(swipe_hint, 0, 0);
    lv_obj_set_style_outline_width(swipe_hint, 0, 0);
    lv_obj_set_style_pad_all(swipe_hint, 0, 0);
    lv_obj_clear_flag(swipe_hint, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(swipe_hint, LV_OBJ_FLAG_HIDDEN);

    swipe_hint_icon = lv_label_create(swipe_hint);
    lv_label_set_text(swipe_hint_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(swipe_hint_icon, &lv_font_es_32, 0);
    lv_obj_set_style_text_color(swipe_hint_icon, lv_color_white(), 0);
    lv_obj_set_style_text_opa(swipe_hint_icon, LV_OPA_TRANSP, 0);
    lv_obj_center(swipe_hint_icon);
}

void knob_swipe_hint_clear(void)
{
    if (swipe_hint == NULL) return;
    lv_obj_add_flag(swipe_hint, LV_OBJ_FLAG_HIDDEN);
}

void knob_swipe_hint_update(int start_x, int start_y, int cur_x, int cur_y)
{
    lv_obj_t *screen = lv_scr_act();
    knob_swipe_direction_t direction;
    int dx = cur_x - start_x;
    int dy = cur_y - start_y;
    int distance;
    int width;
    int height;
    int progress;
    int inset;
    int pos_x;
    int pos_y;
    const char *symbol;

    ensure_swipe_hint();
    direction = knob_classify_swipe_direction(screen, start_x, start_y, dx, dy,
                                              KNOB_SWIPE_HINT_REVEAL_START);
    if (direction == KNOB_SWIPE_NONE) {
        knob_swipe_hint_clear();
        return;
    }

    distance = get_swipe_hint_distance(direction, dx, dy);
    if (distance <= KNOB_SWIPE_HINT_REVEAL_START) {
        knob_swipe_hint_clear();
        return;
    }

    width = get_display_width();
    height = get_display_height();
    progress = get_swipe_hint_progress(direction, dx, dy);
    inset = SWIPE_HINT_EDGE_INSET +
            scale_value(distance - KNOB_SWIPE_HINT_REVEAL_START,
                        KNOB_SWIPE_THRESHOLD,
                        SWIPE_HINT_TRAVEL);
    if (inset > (SWIPE_HINT_EDGE_INSET + SWIPE_HINT_TRAVEL)) {
        inset = SWIPE_HINT_EDGE_INSET + SWIPE_HINT_TRAVEL;
    }

    pos_x = clamp_value(start_x - (SWIPE_HINT_SIZE / 2), SWIPE_HINT_EDGE_INSET, width - SWIPE_HINT_SIZE - SWIPE_HINT_EDGE_INSET);
    pos_y = clamp_value(start_y - (SWIPE_HINT_SIZE / 2), SWIPE_HINT_EDGE_INSET, height - SWIPE_HINT_SIZE - SWIPE_HINT_EDGE_INSET);
    symbol = LV_SYMBOL_LEFT;

    switch (direction) {
    case KNOB_SWIPE_RIGHT:
        symbol = LV_SYMBOL_RIGHT;
        pos_x = inset;
        break;
    case KNOB_SWIPE_LEFT:
        symbol = LV_SYMBOL_LEFT;
        pos_x = width - SWIPE_HINT_SIZE - inset;
        break;
    case KNOB_SWIPE_DOWN:
        symbol = LV_SYMBOL_DOWN;
        pos_y = inset;
        break;
    case KNOB_SWIPE_UP:
        symbol = LV_SYMBOL_UP;
        pos_y = height - SWIPE_HINT_SIZE - inset;
        break;
    default:
        break;
    }

    lv_label_set_text(swipe_hint_icon, symbol);
    lv_obj_set_pos(swipe_hint, pos_x, pos_y);
    lv_obj_set_style_bg_opa(swipe_hint,
                            (lv_opa_t)scale_value(progress, SWIPE_HINT_OPACITY_MAX, SWIPE_HINT_BG_OPACITY_MAX),
                            0);
    lv_obj_set_style_text_opa(swipe_hint_icon, (lv_opa_t)progress, 0);
    lv_obj_clear_flag(swipe_hint, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(swipe_hint);
}

static void open_menu_for_screen(lv_obj_t *screen)
{
    if (is_player_screen(screen)) {
        previous_screen = screen;
        open_quad_menu();
    }
}

void knob_remember_return_screen(lv_obj_t *screen)
{
    previous_screen = screen;
}

static void handle_back_navigation(lv_obj_t *screen);

static void handle_swipe_navigation(knob_swipe_direction_t direction, lv_obj_t *screen)
{
    if (screen == NULL) return;

    if (direction == KNOB_SWIPE_NONE) return;

    if (is_player_screen(screen)) {
        open_menu_for_screen(screen);
    } else if (direction == KNOB_SWIPE_LEFT || direction == KNOB_SWIPE_DOWN) {
        handle_back_navigation(screen);
    }
}

// ---------- screen registry ----------
/* Every screen with custom knob and/or back-gesture behavior gets one row
 * here, covering what used to be three separate hand-maintained switches
 * (the knob dispatch below, the back-navigation chain, and
 * menu_facing_hook_screens()) plus knob_gui()'s boot-time build sequence.
 * Adding a screen that needs any of these is now one row instead of
 * hunting down the right spot in four places - a forgotten back handler
 * used to silently strand the user with no way out of that screen.
 *
 * Screens NOT listed here:
 * - Settings pages and their sub-screens (screen_settings, screen_battery,
 *   screen_table_sync, screen_minigames_menu, screen_language_picker,
 *   screen_wifi_settings, screen_ota_update): back is handled generically
 *   by settings_handle_back() in settings.c, checked before this table.
 * - screen_1p / screen_multiplayer: swipe up opens the menu instead of
 *   going through back navigation at all (see is_player_screen() in
 *   handle_swipe_navigation()); their knob handling sits below this
 *   table since it needs that same is_player_screen() gate.
 * - screen_intro and the screen_quad_menu family (screen_quad_menu,
 *   screen_tools_menu, screen_minigames_menu): built together by
 *   build_quad_menus() rather than individually - see knob_gui().
 */
typedef struct {
    lv_obj_t **screen;
    void (*build)(void);      /* NULL: built elsewhere (batch call or lazily on open) */
    void (*on_knob)(int dir); /* NULL: knob falls through to settings_knob_page() */
    void (*on_back)(void);    /* takes priority over back_target when both are set */
    lv_obj_t **back_target;   /* NULL on_back: lv_scr_load(*back_target); both NULL: no-op */
    bool menu_facing_scoped;  /* hook menu_facing_screen_event (menu-facing setting) */
} screen_desc_t;

// --- knob wrappers (bundle the extra side effect a bare setter doesn't have) ---
static void screen_1p_knob(int dir)
{
    selection_set_single(0);
    change_player_life(dir);
}

static void settings_screen_knob(int dir)
{
    change_brightness(dir);
    refresh_settings_ui();
}

static void counter_edit_knob(int dir)
{
    change_counter_edit(dir);
    refresh_counter_edit_ui();
}

static void damage_log_knob(int dir)
{
    if (dir < 0) damage_log_select_prev();
    else         damage_log_select_next();
}

// --- back-navigation wrappers (anything beyond a plain lv_scr_load) ---
static void back_quad_menu(void)
{
    if (previous_screen != NULL) {
        refresh_multiplayer_ui();
        lv_scr_load(previous_screen);
    }
}

static void back_dice(void) { open_dice_menu_screen(); }
static void back_select(void) { back_to_main(); }
static void back_player_menu(void) { back_to_main(); }
static void back_attack(void) { back_to_main(); }
static void back_counter_menu(void) { open_player_menu(menu_player); }
static void back_counter_edit(void) { open_counter_menu(); }
static void back_player_all_damage(void) { open_player_menu(menu_player); }
static void back_player_color_menu(void) { open_player_menu(menu_player); }
static void back_player_color_picker(void) { load_screen_if_needed(screen_player_color_menu); }

static void back_damage(void)
{
    damage_cancel();
    open_select_screen();
}

static void back_custom_life(void)
{
    refresh_game_mode_menu_ui();
    lv_scr_load(screen_game_mode_menu);
}

static void back_player_name(void)
{
    if (!name_screen_handle_back())
        open_player_menu(menu_player);
}

static void back_mana(void)
{
    mana_discard_preview();
    lv_scr_load(screen_tools_menu);
}

static void back_snake(void)
{
    snake_leave_screen();
    open_minigames_menu_at_launch_page();
}

static void back_pong(void)
{
    pong_leave_screen();
    open_minigames_menu_at_launch_page();
}

static void back_dino(void)
{
    dino_leave_screen();
    open_minigames_menu_at_launch_page();
}

/* Every game's back does the same two things - freeze its loop, return
   to the menu - but through its own leave function, because the timer
   being paused belongs to the game, not to the navigation. */
static void back_tetris(void)
{
    tetris_leave_screen();
    open_minigames_menu_at_launch_page();
}

static void back_breakout(void)
{
    breakout_leave_screen();
    open_minigames_menu_at_launch_page();
}

static void back_flappy(void)
{
    flappy_leave_screen();
    open_minigames_menu_at_launch_page();
}

static void back_eggs(void)
{
    eggs_leave_screen();
    open_minigames_menu_at_launch_page();
}

static void back_invaders(void)
{
    invaders_leave_screen();
    open_minigames_menu_at_launch_page();
}

static void back_rps(void)
{
    rps_leave_screen();
    open_minigames_menu_at_launch_page();
}

static void back_asteroids(void)
{
    asteroids_leave_screen();
    open_minigames_menu_at_launch_page();
}

/* wifi_*_handle_back() return bool (mirrors name_screen_handle_back's
   contract); nothing here needs that result. */
static void back_wifi_scan_list(void) { wifi_scan_list_handle_back(); }
static void back_wifi_text_entry(void) { wifi_text_entry_handle_back(); }
static void back_wifi_status(void) { wifi_status_handle_back(); }

static void back_ota_qr(void)
{
    /* Reached via the "just updated" toast (see hw.c): there's no
       Updates screen in that history to return to, so go straight
       to the life counter instead. */
    if (ota_qr_consume_from_toast()) {
        back_to_main();
    } else {
        load_screen_if_needed(screen_ota_update);
    }
}

static const screen_desc_t screen_registry[] = {
    /* screen                      build                              on_knob              on_back                  back_target             menu_facing */
    { &screen_dice_menu,           build_dice_menu_screen,            change_dice_quantity, NULL,                    &screen_tools_menu,     false },
    { &screen_dice,                build_dice_screen,                 NULL,                 back_dice,               NULL,                   false },
    { &screen_coin,                build_coin_screen,                 NULL,                 NULL,                    &screen_tools_menu,     false },
    { &screen_1p,                  build_main_screen,                 NULL,                 NULL,                    NULL,                   false },
    { &screen_multiplayer,         build_multiplayer_screen,          change_player_life,   NULL,                    NULL,                   false },
    { &screen_victory,             build_victory_screen,              NULL,                 NULL,                    NULL,                   false },
    { &screen_attack,              build_attack_screen,               change_attack_amount, back_attack,             NULL,                   false },
    { &screen_player_menu,         build_player_menu_screen,          NULL,                 back_player_menu,        NULL,                   true  },
    { &screen_eliminated_player_menu, build_eliminated_player_menu_screen, NULL,             NULL,                    NULL,                   true  },
    { &screen_player_name,         build_rename_screen,               name_screen_knob,     back_player_name,        NULL,                   true  },
    { &screen_player_all_damage,   build_all_damage_screen,           change_all_damage,    back_player_all_damage,  NULL,                   true  },
    { &screen_counter_menu,        build_counter_menu_screen,         NULL,                 back_counter_menu,       NULL,                   true  },
    { &screen_counter_edit,        build_counter_edit_screen,         counter_edit_knob,    back_counter_edit,       NULL,                   true  },
    { &screen_player_color_menu,   build_player_color_menu_screen,    NULL,                 back_player_color_menu,  NULL,                   true  },
    { &screen_player_color_picker, build_player_color_picker_screen,  change_player_color,  back_player_color_picker, NULL,                  true  },
    { &screen_select,              build_select_screen,               NULL,                 back_select,             NULL,                   true  },
    { &screen_damage,              build_damage_screen,               add_damage_to_selected_enemy, back_damage,     NULL,                   true  },
    { &screen_mana,                build_mana_screen,                 change_mana_value,    back_mana,               NULL,                   false },
    { &screen_settings,            build_settings_screen,             settings_screen_knob, NULL,                    NULL,                   false }, /* back: settings_handle_back() */
    { &screen_battery,             build_battery_screen,              NULL,                 NULL,                    NULL,                   false }, /* back: settings_handle_back() */
    { &screen_table_sync,          build_table_sync_screen,           NULL,                 NULL,                    NULL,                   false }, /* back: settings_handle_back() */
    /* Built on first open (open_partners_screen), not at boot: a quad
       page is ~3.6KB of the 128KB LVGL pool, and most sessions never
       open this one. */
    { &screen_partners,            NULL,                              NULL,                 NULL,                    &screen_game_mode_menu, false },
    { &screen_language_picker,     build_language_picker_screen,      NULL,                 NULL,                    NULL,                   false }, /* back: settings_handle_back() */
    { &screen_damage_log,          build_damage_log_screen,           damage_log_knob,      NULL,                    &screen_tools_menu,     false },
    { &screen_game_mode_menu,      build_game_mode_menu_screen,       change_num_players,   NULL,                    &screen_quad_menu,      false },
    { &screen_custom_life,         build_custom_life_screen,          change_custom_life,   back_custom_life,        NULL,                   false },
    /* screen_quad_menu/screen_tools_menu: built by build_quad_menus(), see knob_gui() */
    { &screen_quad_menu,           NULL,                              NULL,                 back_quad_menu,          NULL,                   false },
    { &screen_tools_menu,          NULL,                              NULL,                 NULL,                    &screen_quad_menu,      false },
    /* WiFi/OTA sub-screens: built lazily by ensure_wifi_ota_screens_built()
       (ui_wifi.c) on first entry into the cluster, not here. */
    { &screen_wifi_scan_list,      NULL,                              NULL,                 back_wifi_scan_list,     NULL,                   false },
    { &screen_wifi_text_entry,     NULL,                              wifi_text_entry_knob, back_wifi_text_entry,    NULL,                   false },
    { &screen_wifi_status,         NULL,                              NULL,                 back_wifi_status,        NULL,                   false },
    { &screen_ota_qr,              NULL,                              NULL,                 back_ota_qr,             NULL,                   false },
    /* Snake/Pong: built lazily on first open_*_screen() call, not here. */
    { &screen_snake,               NULL,                              snake_turn,           back_snake,              NULL,                   false },
    { &screen_pong,                NULL,                              pong_turn,            back_pong,               NULL,                   false },
    { &screen_dino,                NULL,                              dino_turn,            back_dino,               NULL,                   false },
    { &screen_tetris,              NULL,                              tetris_turn,          back_tetris,             NULL,                   false },
    { &screen_breakout,            NULL,                              breakout_turn,        back_breakout,           NULL,                   false },
    { &screen_flappy,              NULL,                              flappy_turn,          back_flappy,             NULL,                   false },
    { &screen_eggs,                NULL,                              eggs_turn,            back_eggs,               NULL,                   false },
    { &screen_invaders,            NULL,                              invaders_turn,        back_invaders,           NULL,                   false },
    { &screen_rps,                 NULL,                              rps_turn,             back_rps,                NULL,                   false },
    { &screen_asteroids,           NULL,                              asteroids_turn,       back_asteroids,          NULL,                   false },
};
#define SCREEN_REGISTRY_COUNT (sizeof(screen_registry) / sizeof(screen_registry[0]))

static const screen_desc_t *find_screen_desc(lv_obj_t *screen)
{
    size_t i;
    for (i = 0; i < SCREEN_REGISTRY_COUNT; i++) {
        if (*screen_registry[i].screen == screen) return &screen_registry[i];
    }
    return NULL;
}

static void handle_back_navigation(lv_obj_t *screen)
{
    const screen_desc_t *desc;

    /* The settings cluster and the minigames menu answer for their own
       screens - neither is in the table below, both being built as
       groups rather than one at a time. Checked first, exactly like
       before this table existed. See nav_handle_back(). */
    if (nav_handle_back(screen)) return;

    desc = find_screen_desc(screen);
    if (desc == NULL) return;

    if (desc->on_back != NULL) {
        desc->on_back();
    } else if (desc->back_target != NULL) {
        lv_scr_load(*desc->back_target);
    }
}

// ---------- reset ----------
void reset_all_values(void)
{
    knob_life_reset();

    brightness_percent = nvs_get_brightness();
    brightness_apply();

    refresh_main_ui();
    refresh_select_ui();
    refresh_damage_ui();
    refresh_settings_ui();
    refresh_multiplayer_ui();

    refresh_rename_ui();
    refresh_all_damage_ui();
    refresh_counter_edit_ui();
    mana_clear_all();

    start_player_selection_animation();
}


/* Recompute the effective display rotation whenever a player-scoped menu
   screen loads or unloads, so menus can face the acting player (menu
   facing setting). The callback is idempotent; hooking both events covers
   every navigation path in and out. */
static void menu_facing_screen_event(lv_event_t *e)
{
    (void)e;
    menu_facing_refresh();
}

static void menu_facing_hook_screens(void)
{
    size_t i;

    for (i = 0; i < SCREEN_REGISTRY_COUNT; i++) {
        if (!screen_registry[i].menu_facing_scoped) continue;
        lv_obj_add_event_cb(*screen_registry[i].screen, menu_facing_screen_event,
                            LV_EVENT_SCREEN_LOADED, NULL);
        lv_obj_add_event_cb(*screen_registry[i].screen, menu_facing_screen_event,
                            LV_EVENT_SCREEN_UNLOADED, NULL);
    }
}

// ---------- init ----------
void knob_gui(void)
{
    size_t i;

    knob_hw_init();
    display_apply_rotation(nvs_get_display_rotation());
    ensure_swipe_hint();

    build_intro_screen();
    lv_scr_load(screen_intro);
    lv_refr_now(NULL);
    scr_display_on();
    brightness_apply();

    /* Every screen with a .build entry in screen_registry (everything
       except: screen_quad_menu/screen_tools_menu, built together by
       build_quad_menus() right below since they don't have their own
       individual build function; and the WiFi/OTA cluster + Snake/Pong,
       built lazily on first entry - see ensure_wifi_ota_screens_built()
       in ui_wifi.c and the guards in snake.c/pong.c. Exact build order
       among these doesn't matter functionally (each is an independent
       lv_obj_t tree; nothing reads another screen's widgets until the
       user can actually interact, by which point everything below is
       built), so one straight pass over the table replaces what used to
       be ~30 individual calls listed by hand. */
    for (i = 0; i < SCREEN_REGISTRY_COUNT; i++) {
        if (screen_registry[i].build != NULL) screen_registry[i].build();
    }
    /* Settings rows that open somebody else.s screen are pointed at it
       here rather than named in settings_items[]. That table used to
       spell out open_wifi_settings_screen and &screen_ota_update and
       five more like them, which made the settings subsystem depend on
       every feature reachable from its menu - and on this device the
       WiFi screens depend on navigation, which depends on settings.
       Binding from the composition root breaks that ring: settings now
       names no screen it does not own. Must run before
       build_quad_menus() below, which builds the pages and draws an
       unbound row dimmed. */
    settings_bind_screen("brightness", open_settings_screen,        &screen_settings);
    settings_bind_screen("battery",    open_battery_screen,         &screen_battery);
    settings_bind_screen("table-sync", open_table_sync_screen,      &screen_table_sync);
    settings_bind_screen("minigames",  open_minigames_menu,         &screen_minigames_menu);
    settings_bind_screen("language",   open_language_picker_screen, &screen_language_picker);
    settings_bind_screen("wifi",       open_wifi_settings_screen,   &screen_wifi_settings);
    settings_bind_screen("updates",    open_ota_update_screen,      &screen_ota_update);

    build_quad_menus();

    menu_facing_hook_screens();

    refresh_main_ui();
    refresh_multiplayer_ui();

    refresh_rename_ui();
    refresh_select_ui();
    refresh_damage_ui();
    refresh_all_damage_ui();
    refresh_counter_edit_ui();
    refresh_settings_ui();
    refresh_wifi_settings_ui();
    refresh_ota_update_ui();

    wifi_ota_init(); /* non-blocking: begins connecting if credentials are saved */

    /* Wires game_state.c's game_hooks to the real UI refresh functions
       and creates the 3 lv_timer_t objects those hooks schedule - must
       run before any input can reach game_state.c (knob_life_init()
       right below only resets pure state, it no longer touches LVGL -
       see the comment at its call site in game_state.c). */
    game_bridge_init();

    /* hw.c owns a timer whose period the low-battery indicator already
       varies; the OTA check rides it rather than waking the CPU on a
       second one. Which check that is belongs here, not in hw.c. */
    hw_set_idle_poll_hook(ota_notice_poll);

    knob_life_init();
    knob_intro_init();
}

// ---------- knob event handler ----------
static void handle_knob_event(knob_event_t k)
{
    lv_obj_t *screen;
    const screen_desc_t *desc;
    int dir;

    activity_kick();
    if (in_undim_grace()) return;
    if (k != KNOB_LEFT && k != KNOB_RIGHT) return; /* every screen below only ever acted on these two */
    dir = (k == KNOB_RIGHT) ? +1 : -1;

    screen = lv_scr_act();
    if (screen == screen_intro) return;

    /* screen_1p needs the same is_player_screen() single-selection setup
       as its swipe handling below, so it can't be a plain screen_registry
       row like screen_multiplayer's identical change_player_life(). */
    if (screen == screen_1p) {
        screen_1p_knob(dir);
        return;
    }

    desc = find_screen_desc(screen);
    if (desc != NULL && desc->on_knob != NULL) {
        desc->on_knob(dir);
        return;
    }

    /* Paged menus: the knob flips between pages (no-op elsewhere). */
    if (minigames_knob_page(dir)) return;
    if (partners_knob_page(dir)) return;
    settings_knob_page(dir);
}

/* Producer: runs in the encoder esp_timer task, possibly on the other
   core. Single-producer/single-consumer ring — only this side writes head,
   only the consumer writes tail. On overflow the new detent is dropped
   rather than advancing the consumer's tail (which would race it). */
void knob_change(knob_event_t k)
{
    uint8_t head = __atomic_load_n(&knob_event_head, __ATOMIC_RELAXED);
    uint8_t tail = __atomic_load_n(&knob_event_tail, __ATOMIC_ACQUIRE);
    uint8_t next_head = (uint8_t)((head + 1U) % KNOB_EVENT_QUEUE_SIZE);

    if (next_head == tail) {
        return;  /* queue full: drop the newest detent */
    }

    knob_event_queue[head].event = k;
    /* Release so the consumer's acquire-load of head sees the slot write. */
    __atomic_store_n(&knob_event_head, next_head, __ATOMIC_RELEASE);
}

void knob_process_pending(void)
{
    uint8_t processed = 0;
    lv_obj_t *cur = lv_scr_act();

    if (swipe_up_pending) {
        swipe_up_pending = false;
        handle_swipe_navigation(KNOB_SWIPE_UP, cur);
    }

    if (swipe_down_pending) {
        swipe_down_pending = false;
        handle_swipe_navigation(KNOB_SWIPE_DOWN, cur);
    }

    if (swipe_left_pending) {
        swipe_left_pending = false;
        handle_swipe_navigation(KNOB_SWIPE_LEFT, cur);
    }

    if (swipe_right_pending) {
        swipe_right_pending = false;
        handle_swipe_navigation(KNOB_SWIPE_RIGHT, cur);
    }

    uint8_t head = __atomic_load_n(&knob_event_head, __ATOMIC_ACQUIRE);
    uint8_t tail = __atomic_load_n(&knob_event_tail, __ATOMIC_RELAXED);
    while (tail != head && processed < 8U) {
        knob_event_t event = knob_event_queue[tail].event;
        tail = (uint8_t)((tail + 1U) % KNOB_EVENT_QUEUE_SIZE);
        __atomic_store_n(&knob_event_tail, tail, __ATOMIC_RELEASE);
        handle_knob_event(event);
        processed++;
    }
}
