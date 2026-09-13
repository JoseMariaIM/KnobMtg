/* Life rules and the knob's 3-second preview-then-commit flow
 * (change_player_life / apply_life_delta / life_preview_commit_cb in
 * game.c). The preview exists so a fast run of knob turns coalesces
 * into one commit instead of spamming the event log and Table Sync -
 * these tests exercise that timing explicitly via sim_tick_advance()
 * and a manual lv_timer_handler() pump, the same mechanism the real
 * event loop uses. */
#include "test_harness.h"
#include "sim_stubs.h"
#include <stdio.h>
#include <assert.h>

/* Runs LVGL's timer dispatch until at least ms of simulated time has
 * elapsed, in small steps so a 3000ms lv_timer_create() period fires
 * exactly once instead of being skipped over. */
static void pump(uint32_t ms)
{
    uint32_t elapsed = 0;
    while (elapsed < ms) {
        sim_tick_advance(10);
        lv_timer_handler();
        elapsed += 10;
    }
}

int main(void)
{
    test_harness_init();
    test_harness_reset_4p();

    /* ---- clamp_life ---- */
    assert(clamp_life(LIFE_MAX + 50) == LIFE_MAX);
    assert(clamp_life(LIFE_MIN - 50) == LIFE_MIN);
    assert(clamp_life(0) == 0);
    printf("PASS: clamp_life bounds to [%d, %d]\n", LIFE_MIN, LIFE_MAX);

    /* ---- preview accumulates, does not commit immediately ---- */
    selection_set_single(0);
    int before = player_life[0];
    change_player_life(+1);
    change_player_life(+1);
    change_player_life(+1);
    assert(player_life[0] == before); /* still just previewed */
    assert(pending_life_delta == 3);
    assert(life_preview_active);
    printf("PASS: rapid knob turns accumulate into pending_life_delta without committing\n");

    /* ---- commit fires after the preview window elapses ---- */
    pump(3100);
    assert(player_life[0] == before + 3);
    assert(!life_preview_active);
    assert(pending_life_delta == 0);
    printf("PASS: life_preview_commit_cb applies the accumulated delta after ~3s idle\n");

    /* ---- preview clamps to headroom of the selected set, not per-tick ---- */
    selection_set_single(1);
    player_life[1] = LIFE_MAX - 1;
    change_player_life(+5); /* only +1 of headroom available */
    assert(pending_life_delta == 1);
    pump(3100);
    assert(player_life[1] == LIFE_MAX);
    printf("PASS: preview clamps to the selected player's headroom, not just the final value\n");

    /* ---- selecting multiple players previews the same delta to all ---- */
    test_harness_reset_4p();
    selection_toggle(0);
    selection_toggle(2);
    assert(selection_count() == 2);
    int l0 = player_life[0], l2 = player_life[2];
    change_player_life(-4);
    pump(3100);
    assert(player_life[0] == l0 - 4);
    assert(player_life[2] == l2 - 4);
    assert(player_life[1] == DEFAULT_LIFE_TOTAL); /* untouched */
    printf("PASS: a shared delta applies to every selected player, not the rest\n");

    /* ---- eliminated players don't accrue further life changes ---- */
    test_harness_reset_4p();
    apply_life_delta(3, -5000); /* well past LIFE_MIN from the default 40, clamp_life saturates it */
    assert(player_life[3] == LIFE_MIN);
    assert(player_eliminated[3]);
    int life_before_extra = player_life[3];
    apply_life_delta(3, -10);
    assert(player_life[3] == life_before_extra); /* rejected, not clamped-and-applied */
    printf("PASS: apply_life_delta is a no-op once a player is eliminated\n");

    printf("\nAll life/preview tests passed.\n");
    return 0;
}
