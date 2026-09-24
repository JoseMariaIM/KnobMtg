#ifndef _GAME_STATE_INTERNAL_H
#define _GAME_STATE_INTERNAL_H

/* Small seam between game_state.c (the game rules: life, commander
 * damage, counters, selection, elimination) and game_state_sync.c
 * (Table Sync: the wire-format fill/apply and the Lamport-version
 * bookkeeping that decides which side of a merge wins) - not part of
 * the public domain API (that's game_state.h/net_sync.h, unchanged by
 * this split), just what the two need from each other to stay one
 * cohesive module split across two files for size. Same pattern
 * ui_mp.c uses for mp_victory.c/mp_attack_gesture.c - see
 * ui_mp_internal.h. */

#include <stdbool.h>
#include "game_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Manual elimination (concession), tracked apart from
   player_eliminated's auto-computed conditions - see the comment at
   its definition in game_state.c. game_state_sync.c both reads it
   (net_sync_fill_state, to set NET_SYNC_ELIM_MANUAL) and writes it
   (net_sync_apply_state, adopting a remote concession). */
extern bool player_manually_eliminated[MAX_DISPLAY_PLAYERS];

/* Drops the elimination-undo bookkeeping for a player who is no
   longer eliminated, or whose eliminating event no longer applies (a
   remote state adoption superseded it). Defined in game_state.c;
   called from game_state_sync.c wherever an elimination adopted over
   Table Sync changes. */
void clear_player_elimination_action(int player);

/* Bumps this player's Lamport version and broadcasts the resulting
   state. Defined in game_state_sync.c (owns game_epoch/
   player_version); called from game_state.c every time a local action
   changes a player's state, whether or not Table Sync is even
   active - see the comment at its definition. */
void net_sync_commit_player(int player);

/* UI-refresh/timer-scheduling hook wrappers, defined in game_state.c
   (see the "hook call helpers" comment at the top of that file).
   game_state_sync.c calls these directly instead of reaching into
   game_hooks_get() itself, so there is exactly one place that knows
   the hook struct's shape. */
void notify_refresh_player_ui(void);
void notify_refresh_select_ui(void);
void notify_refresh_damage_ui(void);
void notify_refresh_rename_ui(void);
void notify_select_kick_timer(void);
void notify_life_preview_schedule(bool active);

#ifdef __cplusplus
}
#endif

#endif // _GAME_STATE_INTERNAL_H
