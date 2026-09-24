/* Table Sync: the Lamport-version bookkeeping (per-player version,
 * game epoch) that decides which side of a merge wins, and the
 * wire-format fill/apply pair that turns that into/out of the packets
 * knobby_net.cpp (firmware) and sim_stubs.c (simulator) actually send.
 * Split out of game_state.c (Punto 3 de ARCHITECTURE_PLAN.md) - pure
 * code motion, no behavior change; see the comment at the top of that
 * file and of game_state_internal.h for how the two stay one cohesive
 * module across two files. */
#include "game_state.h"
#include "game_state_internal.h"
#include "damage_log.h"
#include <string.h>
#include <stdio.h>

/* Per-player Lamport versions for Table Sync, scoped by a game epoch.
   Every local commit bumps the touched player's version and broadcasts
   a full state snapshot; adopting a remote block adopts its version.
   The epoch dominates the comparison (see net_sync_apply_state), so
   versions from different games are never compared against each other.
   uint16 wrap is handled with serial arithmetic. */
static uint16_t game_epoch = 0;
static uint16_t player_version[MAX_DISPLAY_PLAYERS] = {0};
static uint16_t names_version = 0;

/* Called from game_state.c every time a local action changes a
   player's state (a life change, a counter edit, an undo...),
   whether or not Table Sync is even active - net_sync_send_state() is
   a no-op broadcast when there is no session (see knobby_net.cpp/
   sim_stubs.c), so callers never need to check first. */
void net_sync_commit_player(int player)
{
    player_version[player]++;
    net_sync_send_state();
}

void net_sync_commit_names(void)
{
    names_version++;
    net_sync_send_names();
}

void net_sync_begin_game(void)
{
    int i;
    game_epoch++;
    for (i = 0; i < MAX_DISPLAY_PLAYERS; i++) player_version[i] = 1;
    /* Names outlive game resets, so a mid-session reset leaves the
       roster version alone. A fresh host must still seed it above a
       joiner's zero, or the joiner's leftover roster could win the
       first tie. */
    if (names_version == 0) names_version = 1;
}

/* ---------- test-only accessors ----------
 * game_epoch/player_version are file-static (see the comment above
 * them): a unit test that wants to probe Table Sync's tie-break and
 * uint16-wrap rules directly, without spinning up two simulator
 * processes and a fake radio, needs to read and force them. Not called
 * from firmware code. */
uint16_t player_version_for_test(int player)
{
    if (player < 0 || player >= MAX_DISPLAY_PLAYERS) return 0;
    return player_version[player];
}

void player_version_set_for_test(int player, uint16_t version)
{
    if (player < 0 || player >= MAX_DISPLAY_PLAYERS) return;
    player_version[player] = version;
}

void net_sync_reset_versions(void)
{
    int i;
    game_epoch = 0;
    memset(player_version, 0, sizeof(player_version));
    names_version = 0;
    /* Joining a table: elimination-undo bookkeeping and the event log
       refer to a game this device is leaving behind; replaying either
       against adopted state would corrupt life and mirror the
       corruption table-wide. */
    for (i = 0; i < MAX_DISPLAY_PLAYERS; i++)
        clear_player_elimination_action(i);
    damage_log_reset();
}

// ---------- table sync (ESP-NOW) ----------
_Static_assert(NET_SYNC_MAX_PLAYERS == MAX_DISPLAY_PLAYERS, "packet layout");
_Static_assert(NET_SYNC_MAX_SOURCES == MAX_GAME_PLAYERS, "packet layout");
_Static_assert(sizeof(((net_sync_player_t *)0)->counters) / sizeof(int16_t)
               == COUNTER_TYPE_COUNT, "packet layout");
_Static_assert(sizeof(((net_sync_names_t *)0)->names) == sizeof(player_names),
               "packet layout");

void net_sync_fill_names(net_sync_names_t *out)
{
    int i;
    /* Copy per-row through snprintf, not one memcpy: bytes past each
       name's NUL are residue from earlier longer names and must not
       go out on the air (also keeps roster comparison canonical). */
    memset(out, 0, sizeof(*out));
    out->version = names_version;
    for (i = 0; i < MAX_GAME_PLAYERS; i++)
        snprintf(out->names[i], NET_SYNC_NAME_LEN, "%s", player_names[i]);
}

/* Adopt a remote roster. Whole-set LWW on the roster version (serial
   arithmetic, MAC tiebreak), like a single state block. Runs on the
   main task, same as net_sync_apply_state. */
void net_sync_apply_names(const net_sync_names_t *in, int wins_ties)
{
    int16_t newer = (int16_t)(in->version - names_version);
    int i;

    if (newer < 0) {
        /* Same self-healing as state: answer a stale roster so the
           sender converges without waiting for a rename or invite. */
        net_sync_send_reply();
        return;
    }
    if (newer == 0 && !wins_ties) return;
    names_version = in->version;
    if (memcmp(player_names, in->names, sizeof(player_names)) == 0) return;
    memcpy(player_names, in->names, sizeof(player_names));
    /* Wire bytes are untrusted: every name must terminate. */
    for (i = 0; i < MAX_GAME_PLAYERS; i++)
        player_names[i][sizeof(player_names[i]) - 1] = '\0';
    /* A name is a statement about who is sitting there, wherever it
       was typed. A local rename persists it (player_names_persist in
       rename.c) and an adopted one did not, so a name that arrived
       from the phone next to you showed up on screen and was gone by
       the next power cycle. */
    player_names_persist();

    /* Same refresh set as a local rename (rename.c). */
    notify_refresh_player_ui();
    notify_refresh_select_ui();
    notify_refresh_damage_ui();
    notify_refresh_rename_ui();
}

void net_sync_fill_state(net_sync_state_t *out)
{
    int p, s, c;

    memset(out, 0, sizeof(*out));
    out->epoch = game_epoch;
    for (p = 0; p < MAX_DISPLAY_PLAYERS; p++) {
        net_sync_player_t *rp = &out->players[p];
        rp->version = player_version[p];
        rp->life = (int16_t)player_life[p];
        for (c = 0; c < COUNTER_TYPE_COUNT; c++)
            rp->counters[c] = (int16_t)player_counters[p][c];
        for (s = 0; s < MAX_GAME_PLAYERS; s++) {
            int v = cmd_damage_totals[s][p];
            rp->cmd_damage[s] = (uint8_t)((v < 0) ? 0 : (v > 255) ? 255 : v);
        }
        if (player_eliminated[p]) rp->eliminated |= NET_SYNC_ELIM;
        if (player_manually_eliminated[p]) rp->eliminated |= NET_SYNC_ELIM_MANUAL;
    }
}

/* Adopt a remote state snapshot. Runs on the main (LVGL) task — packets
   are queued by the radio and drained from loop() — so UI refresh is
   safe here. The game epoch dominates: a newer epoch (new game started
   or reset elsewhere) is adopted wholesale, an older one is rejected
   and answered with our own state (anti-entropy: the stale device
   converges in one exchange instead of waiting out the beacon). Within
   the same epoch, a player's block is adopted iff its version is newer
   (serial arithmetic, so uint16 wrap is fine); equal versions defer to
   the sender with the higher MAC so both devices pick the same winner.

   State is adopted directly — never route remote state through the
   apply/undo helpers: they bump versions and re-broadcast, and they'd
   re-derive elimination that the sender already decided. The damage
   log stays a per-device view of local actions. */
void net_sync_apply_state(const net_sync_state_t *in, int wins_ties)
{
    bool changed = false;
    bool remote_stale = false;
    int16_t epoch_newer = (int16_t)(in->epoch - game_epoch);
    int p, s, c;

    if (epoch_newer < 0) {
        net_sync_send_reply();
        return;
    }
    if (epoch_newer > 0) {
        game_epoch = in->epoch;
        /* A new game from the table: local elimination-undo actions
           and the event log refer to a game that no longer exists
           (see net_sync_reset_versions). */
        for (p = 0; p < MAX_DISPLAY_PLAYERS; p++)
            clear_player_elimination_action(p);
        damage_log_reset();
    }

    for (p = 0; p < MAX_DISPLAY_PLAYERS; p++) {
        const net_sync_player_t *rp = &in->players[p];
        int16_t newer = (int16_t)(rp->version - player_version[p]);
        bool was_eliminated = player_eliminated[p];
        bool now_eliminated = (rp->eliminated & NET_SYNC_ELIM) != 0;
        bool p_changed = false;
        int life = clamp_life(rp->life);

        /* Same epoch: per-player Lamport rule. A newer epoch adopts
           every block regardless of version drift. */
        if (epoch_newer == 0 && (newer < 0 || (newer == 0 && !wins_ties))) {
            if (newer < 0) remote_stale = true;
            continue;
        }

        if (player_life[p] != life) {
            player_life[p] = life;
            /* A pending preview was dialed against a total that no
               longer exists: if this player is in the current
               selection, drop the whole group's proposal (it is one
               shared delta) so a duplicate entry can't silently
               double-apply — the user sees the total snap and can
               re-dial. Changes to unselected players compose as
               usual. The selection itself stays: the grouping is
               still valid intent, only the number was invalidated. */
            if (life_preview_active && player_selected[p]) {
                pending_life_delta = 0;
                life_preview_active = false;
                notify_life_preview_schedule(false);
                notify_select_kick_timer();
            }
            p_changed = true;
        }
        for (c = 0; c < COUNTER_TYPE_COUNT; c++) {
            int v = clamp_counter(rp->counters[c]);
            if (player_counters[p][c] != v) {
                player_counters[p][c] = v;
                p_changed = true;
            }
        }
        for (s = 0; s < MAX_GAME_PLAYERS; s++) {
            if (cmd_damage_totals[s][p] != rp->cmd_damage[s]) {
                cmd_damage_totals[s][p] = rp->cmd_damage[s];
                p_changed = true;
            }
        }
        if (was_eliminated != now_eliminated) {
            player_eliminated[p] = now_eliminated;
            if (!now_eliminated) {
                clear_player_elimination_action(p);
            } else if (player_selected[p]) {
                /* Same rule as check_player_elimination: an eliminated
                   player leaves the selection set. */
                player_selected[p] = false;
                notify_select_kick_timer();
            }
            p_changed = true;
        } else if (was_eliminated && p_changed) {
            /* Still eliminated but the block changed underneath (a
               missed revive+re-kill): the stored undo action belongs
               to the old elimination and would restore the wrong
               amount. Undo then falls back to the revive clamps. */
            clear_player_elimination_action(p);
        }
        player_manually_eliminated[p] =
            now_eliminated && (rp->eliminated & NET_SYNC_ELIM_MANUAL) != 0;
        player_version[p] = rp->version;
        changed = changed || p_changed;
    }

    if (changed) {
        notify_refresh_player_ui();
        notify_refresh_select_ui();
    }
    /* The sender is behind and we adopted nothing: answer immediately
       so its lost-update window is one exchange, not a 5s beacon. */
    if (remote_stale && !changed) {
        net_sync_send_reply();
    }
    /* A fresh joiner can adopt the table's epoch while still awaiting
       the roster (every invite-window names packet lost): announcing
       its version-0 roster makes any peer see it as stale and reply
       with the real one (see net_sync_apply_names). */
    if (epoch_newer > 0 && names_version == 0) {
        net_sync_send_names();
    }
}
