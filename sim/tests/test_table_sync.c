/* Table Sync's state-merge rules (net_sync_fill_state/apply_state in
 * game.c): the game epoch dominates the per-player Lamport version
 * comparison, and within an epoch, ties are broken by wins_ties (the
 * caller passes "does the sender's MAC outrank ours"). The radio is a
 * no-op in the simulator (see sim_stubs.c), so this drives the merge
 * function directly with hand-built packets - exactly what two real
 * devices exchange over ESP-NOW, without needing two processes. */
#include "test_harness.h"
#include "../../knobby/src/adapters/net_sync.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(void)
{
    net_sync_state_t remote;

    test_harness_init();
    test_harness_reset_4p(); /* also calls net_sync_begin_game(): epoch=1, versions=1 */

    /* ---- a same-epoch, higher-version remote block is adopted ---- */
    net_sync_fill_state(&remote);
    remote.players[0].version += 1; /* newer than ours */
    remote.players[0].life = 7;
    net_sync_apply_state(&remote, /*wins_ties=*/0);
    assert(player_life[0] == 7);
    printf("PASS: a strictly newer per-player version is adopted\n");

    /* ---- a same-epoch, older-version remote block is rejected ---- */
    net_sync_fill_state(&remote);
    remote.players[1].version = 0; /* older than our seeded version 1 */
    remote.players[1].life = 99;
    net_sync_apply_state(&remote, /*wins_ties=*/0);
    assert(player_life[1] != 99);
    printf("PASS: an older per-player version is rejected, not adopted\n");

    /* ---- equal version: tie broken by wins_ties (sender MAC rank) ---- */
    net_sync_fill_state(&remote);
    remote.players[2].version = player_version_for_test(2); /* exactly equal */
    remote.players[2].life = 13;
    net_sync_apply_state(&remote, /*wins_ties=*/0); /* we lose the tie */
    assert(player_life[2] != 13);
    net_sync_apply_state(&remote, /*wins_ties=*/1); /* we win the tie */
    assert(player_life[2] == 13);
    printf("PASS: equal versions are adopted only when wins_ties is set\n");

    /* ---- a newer epoch is adopted wholesale, regardless of the
       per-player version drift it arrives with ---- */
    net_sync_fill_state(&remote);
    remote.epoch += 1;               /* a new game started elsewhere */
    remote.players[3].version = 0;   /* would lose on version alone */
    remote.players[3].life = 21;
    net_sync_apply_state(&remote, /*wins_ties=*/0);
    assert(player_life[3] == 21);
    printf("PASS: a newer epoch overrides version drift entirely\n");

    /* ---- an older epoch is rejected outright, even with a huge
       version number that would otherwise win ---- */
    {
        int stale_life = player_life[0];
        net_sync_fill_state(&remote);
        remote.epoch -= 1; /* older game */
        remote.players[0].version = 60000;
        remote.players[0].life = -500;
        net_sync_apply_state(&remote, /*wins_ties=*/1);
        assert(player_life[0] == stale_life);
    }
    printf("PASS: an older epoch is rejected outright\n");

    /* ---- uint16 version wrap: serial arithmetic must treat a value
       just past the wrap as newer than one just before it ---- */
    test_harness_reset_4p();
    {
        int p;
        /* Fill once, then mutate per-player fields on the same snapshot -
           calling net_sync_fill_state() again inside the loop would
           memset() and re-fill the whole packet from current local
           state each time, wiping out earlier players' edits. */
        net_sync_fill_state(&remote);
        for (p = 0; p < MAX_DISPLAY_PLAYERS; p++) {
            /* Force our version right up against the wrap point, then
               hand back a remote block one tick further - "newer" by
               serial arithmetic despite being numerically smaller. */
            player_version_set_for_test(p, 65535);
            remote.players[p].version = 0; /* 0 - 65535 == +1 in int16 serial arithmetic */
            remote.players[p].life = 55;
        }
        net_sync_apply_state(&remote, /*wins_ties=*/0);
        for (p = 0; p < MAX_DISPLAY_PLAYERS; p++) {
            assert(player_life[p] == 55);
        }
    }
    printf("PASS: version comparison wraps correctly across the uint16 boundary\n");

    printf("\nAll Table Sync merge tests passed.\n");
    return 0;
}
