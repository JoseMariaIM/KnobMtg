/* Auto-elimination (life <= 0, 21+ commander damage, 10+ poison),
 * manual concede/revive, and undo_elimination_action's job of putting
 * a player back exactly where they were before the killing blow -
 * across all three trigger types, since each stores a different
 * source/delta shape in elimination_action_t (game.c). */
#include "test_harness.h"
#include <stdio.h>
#include <assert.h>

int main(void)
{
    test_harness_init();

    /* ---- auto-elimination requires >1 tracked player (see
       check_player_elimination's solo-mode exemption comment) ---- */
    prefs_set_num_players(4);
    prefs_set_players_to_track(4);
    prefs_set_auto_eliminate(1);
    knob_life_reset();

    /* ---- life-based elimination + undo ---- */
    apply_life_delta(0, -DEFAULT_LIFE_TOTAL);
    assert(player_life[0] == 0);
    assert(player_eliminated[0]);
    assert(elimination_action_available(0));
    undo_elimination_action(0);
    assert(!player_eliminated[0]);
    assert(player_life[0] == DEFAULT_LIFE_TOTAL);
    assert(!elimination_action_available(0));
    printf("PASS: life-based elimination undoes back to the exact pre-kill life total\n");

    /* ---- commander damage elimination (21 lethal) + undo restores
       both the life total and the commander damage total ---- */
    test_harness_reset_4p();
    prepare_cmd_damage_for_player(/*target=*/1);
    selected_enemy = 0; /* row 0 = Alice, per get_cmd_target_player_index */
    damage_enter();
    add_damage_to_selected_enemy(21);
    damage_apply();
    assert(cmd_damage_totals[0][1] == 21);
    assert(player_eliminated[1]);
    assert(elimination_action_available(1));
    undo_elimination_action(1);
    assert(!player_eliminated[1]);
    assert(cmd_damage_totals[0][1] < 21);
    assert(player_life[1] == DEFAULT_LIFE_TOTAL);
    printf("PASS: commander-damage elimination undo clears both life and the 21+ total\n");

    /* ---- poison elimination (10+) + undo ---- */
    test_harness_reset_4p();
    apply_attack_poison(2, 10);
    assert(player_counters[2][COUNTER_TYPE_POISON] == 10);
    assert(player_eliminated[2]);
    undo_elimination_action(2);
    assert(!player_eliminated[2]);
    assert(player_counters[2][COUNTER_TYPE_POISON] < 10);
    printf("PASS: poison elimination undo drops the counter back below the threshold\n");

    /* ---- manual concede is independent of auto-elimination math:
       reviving must not resurrect someone still lethal by the numbers,
       and check_player_elimination() must re-kill them on the next
       poke rather than leaving a walking-dead state ---- */
    test_harness_reset_4p();
    manual_eliminate_player(0);
    assert(player_eliminated[0]);
    assert(player_life[0] == DEFAULT_LIFE_TOTAL); /* concede doesn't touch life */
    assert(!elimination_action_available(0));     /* nothing to "undo" for a manual concede */
    manual_uneliminate_player(0);
    assert(!player_eliminated[0]);
    printf("PASS: manual concede/revive round-trips without corrupting life\n");

    /* ---- reviving a player who's lethal purely by the numbers (e.g. a
       remote elimination with no local elimination_action) must pull
       them just above the threshold, or check_player_elimination()
       would instantly re-kill them ---- */
    test_harness_reset_4p();
    player_life[3] = 0;
    manual_eliminate_player(3); /* simulates "already eliminated by some other path" */
    manual_uneliminate_player(3);
    assert(!player_eliminated[3]);
    assert(player_life[3] >= 1);
    printf("PASS: reviving a numerically-lethal player clears the condition, not just the flag\n");

    /* ---- selecting an eliminated player is impossible, and being
       selected at the moment of elimination drops the selection ---- */
    test_harness_reset_4p();
    selection_set_single(1);
    assert(is_player_selected(1));
    apply_life_delta(1, -DEFAULT_LIFE_TOTAL);
    assert(player_eliminated[1]);
    assert(!is_player_selected(1));
    selection_toggle(1); /* must refuse: eliminated players can't be selected */
    assert(!is_player_selected(1));
    printf("PASS: elimination drops an active selection and blocks new selection\n");

    printf("\nAll elimination tests passed.\n");
    return 0;
}
