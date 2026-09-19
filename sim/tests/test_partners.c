/* Partner commanders, and the rule that nothing about them shows up
 * until somebody says they have one.
 *
 * Most tables play no partners at all, and the device used to show the
 * second commander-damage tab and the partner-tax counter to everyone
 * regardless - which made it look like it was tracking something that
 * was not on the table. The flag is now per player, set once in
 * Settings, and every partner control on the device is gated on it.
 *
 * What is worth guarding here is the gating, not the toggle: a control
 * that reappears for a player with no partner is a second 21-damage
 * clock nobody declared. */
#include "test_harness.h"
#include "sim_stubs.h"
#include "game_state.h"
#include "storage.h"
#include "settings.h"
#include "ui_1p.h"
#include "ui_player_menu.h"
#include <stdio.h>
#include <assert.h>

static void test_the_flag_is_per_player_and_survives_a_save(void)
{
    int i;

    for (i = 0; i < MAX_GAME_PLAYERS; i++) assert(!player_has_partner(i));
    printf("PASS: nobody has a partner by default\n");

    set_player_has_partner(2, true);
    set_player_has_partner(5, true);
    for (i = 0; i < MAX_GAME_PLAYERS; i++) {
        bool want = (i == 2 || i == 5);
        if (player_has_partner(i) != want) {
            printf("FAIL: player %d reads %d, expected %d\n",
                   i, (int)player_has_partner(i), (int)want);
            assert(0);
        }
    }
    printf("PASS: the flag lands on the players it was set for, and no others\n");

    /* Stored as a bitmask beside the other settings, so the mask a
       save writes has to be the mask the getters describe. */
    assert(nvs_get_partner_mask() == ((1 << 2) | (1 << 5)));
    settings_save();
    assert(nvs_get_partner_mask() == ((1 << 2) | (1 << 5)));
    printf("PASS: the mask survives a settings save\n");

    set_player_has_partner(2, false);
    set_player_has_partner(5, false);
    assert(!any_player_has_partner());
    printf("PASS: clearing the last one leaves nothing set\n");
}

/* The commander-damage screen's slot tabs, which are the control the
   partner flag was added for. Asked of the opponents LISTED for this
   target: a partner elsewhere in the game is not a choice this screen
   can make. */
static void test_the_slot_tabs_follow_the_listed_opponents(void)
{
    /* Bob(1) is the target, so Alice(0), Carol(2) and Dave(3) are the
       rows. Give the target himself a partner and nobody else: his own
       partner is irrelevant here, because he is not a source. */
    set_player_has_partner(1, true);
    prepare_cmd_damage_for_player(1);
    refresh_select_ui();
    assert(!select_test_slot_tabs_visible());
    printf("PASS: the target's own partner does not raise the tabs\n");

    set_player_has_partner(2, true);
    prepare_cmd_damage_for_player(1);
    refresh_select_ui();
    assert(select_test_slot_tabs_visible());
    printf("PASS: one listed opponent with a partner raises the tabs\n");

    /* And taking it away pins the screen back to the primary tally
       rather than leaving it parked on a slot with no tabs to leave. */
    cmd_damage_slot = 1;
    set_player_has_partner(2, false);
    refresh_select_ui();
    assert(!select_test_slot_tabs_visible());
    assert(cmd_damage_slot == 0);
    printf("PASS: losing the last partner hides the tabs and resets the slot\n");

    set_player_has_partner(1, false);
}

/* The partner-tax counter tile, which is per player rather than per
   screen - the menu is built once and visited for whoever was held. */
static void test_the_partner_tax_tile_follows_the_player(void)
{
    set_player_has_partner(0, true);

    open_player_menu(0);
    open_counter_menu();
    assert(counter_menu_test_partner_tile_visible());
    printf("PASS: the partner-tax tile shows for a player who has one\n");

    open_player_menu(1);
    open_counter_menu();
    assert(!counter_menu_test_partner_tile_visible());
    printf("PASS: the same menu hides it for the next player along\n");

    set_player_has_partner(0, false);
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);

    test_harness_init();
    test_harness_settle_intro();
    test_harness_reset_4p();

    test_the_flag_is_per_player_and_survives_a_save();
    test_the_slot_tabs_follow_the_listed_opponents();
    test_the_partner_tax_tile_follows_the_player();

    printf("\nAll partner tests passed.\n");
    return 0;
}
