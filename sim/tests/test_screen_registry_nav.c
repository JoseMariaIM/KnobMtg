/* End-to-end exercise of screen_registry (knob.c) through its real
 * public entry points - knob_change()/knob_process_pending() for knob
 * turns, knob_notify_swipe_left()/knob_process_pending() for the back
 * gesture - covering one row of each shape the table holds: a plain
 * back_target row, an on_back wrapper row, the special-cased screen_1p
 * knob wrapper, a bare table-driven knob handler (screen_multiplayer),
 * and the settings_handle_back() pre-check that runs before the table
 * is even consulted. A wiring mistake in any registry row (wrong
 * target, wrong handler) would show up here as landing on the wrong
 * screen or the state not changing - not just a compile error, since
 * every field is a plain function/object pointer with no type-level
 * connection to "which screen this row is for". */
#include "test_harness.h"
#include "presentation/minigames/dice.h"
#include "presentation/screens/settings.h"
#include "nav.h"
#include "presentation/screens/home.h"
#include "presentation/screens/ui_table_sync.h"
#include "presentation/screens/ui_language.h"
#include "presentation/screens/ui_battery.h"
#include "presentation/screens/minigames_menu.h"
#include "presentation/screens/ui_1p.h"
#include "presentation/screens/ui_cmd_damage.h"
#include "presentation/screens/ui_mp.h"
#include <stdio.h>
#include <assert.h>

/* Turns the knob and drains the queue, same path a real encoder detent
 * takes (knob_change() is the ISR-side producer). */
static void turn_knob(knob_event_t k)
{
    knob_change(k);
    knob_process_pending();
}

static void swipe_back(void)
{
    knob_notify_swipe_left();
    knob_process_pending();
}

int main(void)
{
    test_harness_init();
    test_harness_reset_4p();

    /* ---- back_target row: screen_dice_menu -> screen_tools_menu, and
       its on_knob (change_dice_quantity) actually runs ---- */
    open_dice_menu_screen();
    assert(lv_scr_act() == screen_dice_menu);
    int before = dice_test_get_quantity();
    turn_knob(KNOB_RIGHT);
    assert(dice_test_get_quantity() == before + 1);
    swipe_back();
    assert(lv_scr_act() == screen_tools_menu);
    printf("PASS: screen_dice_menu's knob handler runs and back_target lands on Tools\n");

    /* ---- on_back wrapper row: screen_damage -> damage_cancel() +
       open_select_screen() (back_damage in knob.c). Reaching
       screen_damage here mirrors open_damage_screen() in ui_1p.c
       (static, so not callable directly) rather than a real tap. */
    prepare_cmd_damage_for_player(/*target=*/1);
    selected_enemy = 0;
    damage_enter();
    refresh_damage_ui();
    load_screen_if_needed(screen_damage);
    int enemy_damage_before = enemies[0].damage;
    add_damage_to_selected_enemy(5);
    assert(enemies[0].damage == enemy_damage_before + 5);
    assert(lv_scr_act() == screen_damage);
    swipe_back();
    assert(lv_scr_act() == screen_select);
    assert(enemies[0].damage == enemy_damage_before); /* the +5 was cancelled, not committed */
    printf("PASS: screen_damage's back wrapper cancels the pending edit and returns to Select\n");

    /* ---- special-cased screen_1p knob wrapper (selection_set_single(0)
       + change_player_life, not a plain table row) ---- */
    test_harness_reset_4p();
    prefs_set_players_to_track(1);
    back_to_main();
    assert(lv_scr_act() == screen_1p);
    turn_knob(KNOB_RIGHT);
    turn_knob(KNOB_RIGHT);
    assert(pending_life_delta == 2); /* previewed, same 3s-commit contract as elsewhere */
    assert(is_player_selected(0));   /* screen_1p_knob's selection_set_single(0) ran */
    printf("PASS: screen_1p's knob wrapper selects player 0 and previews the delta\n");

    /* ---- plain table-driven knob handler: screen_multiplayer's row
       points straight at change_player_life, no wrapper needed ---- */
    test_harness_reset_4p();
    prefs_set_players_to_track(4);
    back_to_main();
    assert(lv_scr_act() == screen_multiplayer);
    selection_toggle(2);
    turn_knob(KNOB_LEFT);
    turn_knob(KNOB_LEFT);
    assert(pending_life_delta == -2);
    printf("PASS: screen_multiplayer's table-driven knob handler previews the delta\n");

    /* ---- settings_handle_back() pre-check: screen_settings's back
       is NOT in screen_registry at all, it's settings.c's own table ---- */
    open_settings_screen();
    assert(lv_scr_act() == screen_settings);
    swipe_back();
    assert(lv_scr_act() != screen_settings); /* landed on a settings page, not stuck */
    printf("PASS: screen_settings's back is handled by settings_handle_back(), not stuck\n");

    printf("\nAll screen registry navigation tests passed.\n");
    return 0;
}
