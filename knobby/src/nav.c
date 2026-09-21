#include "nav.h"
#include "home.h"
#include "quad_screen.h"
#include "types.h"
#include "settings.h"
#include "minigames_menu.h"
#include "storage.h"
#include "hw.h"
#include "lang.h"
#include "dice.h"
#include "mana.h"
#include "damage_log.h"
#include "game_mode.h"
#include "game.h"
#include "ui_1p.h"
#include "ui_cmd_damage.h"
#include "ui_mp.h"
#include "ui_player_menu.h"
#include "rename.h"
#include "mp_victory.h"

/* See nav.h. */

// Forward declaration for knob.c's reset, which has no header of its own
extern void reset_all_values(void);

lv_obj_t *screen_quad_menu = NULL;
lv_obj_t *screen_tools_menu = NULL;

// ---------- back ----------
bool nav_handle_back(lv_obj_t *screen)
{
    int page;
    int i;

    /* Minigame menu pages past the first are not settings screens in
       their own right. Back from any of them leaves the menu entirely
       rather than stepping back a page (the knob flips pages), which
       is exactly what page 0 - screen_minigames_menu, a settings
       sub-screen - already does. */
    for (i = 1; i < minigames_page_count; i++) {
        if (screen == minigames_pages[i]) {
            screen = screen_minigames_menu;
            break;
        }
    }

    switch (settings_classify_screen(screen, &page)) {
    case SETTINGS_SCREEN_CHILD:
        settings_show_page(page);
        return true;
    case SETTINGS_SCREEN_PAGE:
        /* Back from any settings page exits to the quad menu - back
           means "leave settings", not "previous page". */
        lv_scr_load(screen_quad_menu);
        return true;
    default:
        return false;
    }
}

// ---------- menu facing ----------
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

// ---------- the two menus ----------
void open_quad_menu(void)
{
    load_screen_if_needed(screen_quad_menu);
}

static void event_quad_screen_settings(lv_event_t *e)
{
    (void)e;
    settings_show_page(0);
}

static void event_quad_tools(lv_event_t *e)
{
    (void)e;
    lv_scr_load(screen_tools_menu);
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

void build_quad_menus(void)
{
    quad_item_t main_items[4] = {
        {t(STR_MENU_SETTINGS), event_quad_screen_settings, true, LV_EVENT_CLICKED},
        {t(STR_MENU_GAME_MODE), event_general_game_mode, true, LV_EVENT_CLICKED},
        {t(STR_MENU_TOOLS),             event_quad_tools, true, LV_EVENT_CLICKED},
        {t(STR_MENU_RESET_HOLD), event_general_reset, true, LV_EVENT_LONG_PRESSED},
    };
    build_quad_screen(&screen_quad_menu, main_items);

    quad_item_t tools_items[4] = {
        {t(STR_TOOL_DICE),        event_tool_dice, true, LV_EVENT_CLICKED},
        {t(STR_TOOL_COIN),        event_tool_coin, true, LV_EVENT_CLICKED},
        {t(STR_TOOL_EVENT_LOG),  event_open_damage_log, true, LV_EVENT_CLICKED},
        {t(STR_TOOL_MANA_POOL),  event_tool_mana, true, LV_EVENT_CLICKED},
    };
    build_quad_screen(&screen_tools_menu, tools_items);

    build_settings_pages();
    /* The minigames menu is NOT built here: three quad pages of tiles
       for a screen many sessions never open is ~3.5KB of the LVGL pool
       held permanently. open_minigames_menu() builds it on first
       entry, same as the games themselves. */
}

// ---------- navigation ----------
/* Both registered with home_bind() at boot - see home.h for why the
   two halves are apart. */
void nav_refresh_player_ui(void)
{
    if (nvs_get_players_to_track() == 1)
        refresh_main_ui();
    else
        refresh_multiplayer_ui();
}

void nav_go_home(void)
{
    int track = nvs_get_players_to_track();
    cmd_damage_target = -1;

    /* A batch life/counter change (All Damage, a counter edit) can
       eliminate the second-to-last player and the caller's own final
       apply_life_delta() call in the same breath - check_player_elimination()
       notices synchronously and starts the victory screen's animated
       fade right there, mid-call. Every one of those callers then turns
       around and calls back_to_main() to return to the game screen; doing
       that with lv_scr_load() while the fade's transition is still live
       corrupts LVGL's transition state and crashes the device. Once the
       victory screen owns the display, only a reset should navigate
       away from it. */
    if (mp_victory_active()) return;

    if (track > 1) {
        refresh_multiplayer_ui();
        load_screen_if_needed(screen_multiplayer);
    } else {
        refresh_main_ui();
        load_screen_if_needed(screen_1p);
    }
}
