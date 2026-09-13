/* Regression test for independent partner-commander damage tracking:
 * damage from a source player's second (partner) commander is tracked
 * apart from their primary commander in partner_cmd_damage_totals,
 * because 21 damage from either alone is lethal - they must never sum
 * toward one shared total (see the comment above
 * partner_cmd_damage_totals in game.h). Originally sim/test_partner_commander.c;
 * moved here to run under `make test` instead of by hand. */
#include "test_harness.h"
#include <stdio.h>
#include <assert.h>

/* Mirrors one "pick opponent row, dial the knob, Apply" cycle on the
   already-open Commander Damage flow (screen_select -> screen_damage).
   Does NOT call prepare_cmd_damage_for_player: that only runs once, when
   the flow is entered from the player menu, exactly like the real UI. */
static void select_and_apply(int source_row, int amount)
{
    selected_enemy = source_row;
    damage_enter();
    add_damage_to_selected_enemy(amount);
    damage_apply();
}

int main(void)
{
    test_harness_init();

    /* 4-player game: Alice(0), Bob(1), Carol(2), Dave(3). Bob is
       tracking damage from Alice's two commanders independently.
       Auto-elimination only engages in multiplayer (see
       check_player_elimination), so players-to-track must be > 1. */
    test_harness_reset_4p();

    /* Enter the Commander Damage flow for Bob once, exactly as the
       player menu does. Row 0 in the enemy list is Alice
       (get_cmd_target_player_index skips Bob himself). */
    prepare_cmd_damage_for_player(/*target=*/1);

    select_and_apply(/*source_row=*/0, 18);
    assert(cmd_damage_totals[0][1] == 18);
    assert(partner_cmd_damage_totals[0][1] == 0);
    assert(!player_eliminated[1]);
    printf("PASS: primary commander damage recorded independently (18)\n");

    /* Flip to the Partner tab, same as tapping it on screen_select. */
    cmd_damage_slot = 1;
    refresh_cmd_damage_slot();
    select_and_apply(/*source_row=*/0, 15);
    assert(cmd_damage_totals[0][1] == 18);          /* untouched by the partner edit */
    assert(partner_cmd_damage_totals[0][1] == 15);
    assert(!player_eliminated[1]);                  /* neither commander has hit 21 yet */
    printf("PASS: partner commander damage tracked separately (15), primary untouched\n");

    /* Push the partner commander to lethal; the primary commander's 18
       must not contribute to it. */
    select_and_apply(/*source_row=*/0, 21 - 15);
    assert(partner_cmd_damage_totals[0][1] == 21);
    assert(player_eliminated[1]);
    printf("PASS: 21 from the partner commander alone eliminates Bob\n");

    manual_uneliminate_player(1);
    assert(!player_eliminated[1]);
    assert(partner_cmd_damage_totals[0][1] <= 20);
    assert(cmd_damage_totals[0][1] == 18);
    printf("PASS: revive clamps only the lethal slot, primary total untouched\n");

    printf("\nAll partner-commander tests passed.\n");
    return 0;
}
