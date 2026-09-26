#ifndef _GAME_STATE_H
#define _GAME_STATE_H

/* The game's actual rules - life totals, commander damage, counters,
 * elimination, Table Sync's state-merge logic - with zero LVGL types
 * anywhere in this header. Deliberately does NOT include types.h (which
 * pulls knob.h -> lvgl.h for its widget helpers and lv_color_t-returning
 * color math) - only game_types.h, the pure subset of constants/structs
 * types.h itself re-exports, and net_sync.h, which is pure too.
 *
 * Two things this header does NOT own, both inherently LVGL-shaped, and
 * both declared in game.h instead (the public facade every other file
 * still includes exactly as before - see the comment at its top):
 *   - The 7 get_*_color()/get_effective_player_color() functions and
 *     player_custom_hsv[]: color math whose return type is lv_color_t.
 *   - The 3 lv_timer_t objects (life preview's 3s auto-commit, the
 *     all-damage flash's auto-clear, the selection roulette's per-step
 *     tick) and their raw LVGL callbacks - game.c's bridge layer owns
 *     creating/scheduling them; this file exposes the plain functions
 *     those callbacks call into (game_life_preview_commit(),
 *     game_life_flash_end(), game_player_select_anim_step()) and
 *     the game_hooks_t scheduling requests (life_preview_schedule etc.,
 *     see game_hooks.h) that ask the bridge to arm/disarm them.
 *
 * A test that only cares about game rules (life clamps, elimination,
 * commander damage, Table Sync merges) can include just this header and
 * link just game_state.c plus game_state_sync.c (Table Sync's own
 * Lamport-version bookkeeping and wire-format fill/apply - split out
 * for size, see the comment at its top), with no lvgl.h on the
 * include path and no lv_init()/knob_gui() boot needed - UI-facing
 * effects (refresh calls, timer scheduling) are just hook calls that
 * no-op if nothing is registered (see game_hooks.h). sim/tests/ still
 * boots the full UI for its tests today (simpler test setup, and the
 * boot cost is already negligible - see test_harness.h), but nothing
 * about this header requires that anymore. */

#include "game_types.h"
#include "game_hooks.h"
#include "../adapters/net_sync.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	COUNTER_TYPE_COMMANDER_TAX = 0,
	COUNTER_TYPE_PARTNER_TAX,
	COUNTER_TYPE_POISON,
	COUNTER_TYPE_EXPERIENCE,
	COUNTER_TYPE_COUNT,
} counter_type_t;

typedef struct {
	const char *menu_label;
	const char *display_name;
	/* Short form for the event log, where a row must stay on one line.
	   Same text as display_name when there is nothing to shorten. */
	const char *log_name;
	const char *badge_text;
	const char *icon_text;
	uint32_t accent_color;
	bool enabled;
} counter_definition_t;

// ---------- state ----------
extern int active_enemy_count;
extern enemy_state_t enemies[MAX_ENEMY_COUNT];
extern int selected_enemy;
extern int player_life[MAX_DISPLAY_PLAYERS];
extern bool player_selected[MAX_DISPLAY_PLAYERS];
extern char player_names[MAX_GAME_PLAYERS][16];
extern int menu_player;
extern int cmd_damage_totals[MAX_GAME_PLAYERS][MAX_DISPLAY_PLAYERS];
/* Damage from each source player's SECOND (partner) commander, tracked
   independently from cmd_damage_totals because 21 damage from either
   commander alone is lethal — they don't sum toward one shared total.
   Not currently mirrored over Table Sync (see net_sync_fill_state). */
extern int partner_cmd_damage_totals[MAX_GAME_PLAYERS][MAX_DISPLAY_PLAYERS];
extern int cmd_damage_target;
extern int cmd_damage_slot; /* 0 = commander, 1 = partner commander */
extern int all_damage_value;
extern int pending_life_delta;
extern bool life_preview_active;
extern int dice_result;
extern int player_counters[MAX_DISPLAY_PLAYERS][COUNTER_TYPE_COUNT];
extern counter_type_t counter_edit_type;
extern int counter_edit_value;
extern bool player_eliminated[MAX_DISPLAY_PLAYERS];

/* Whether this player fields a partner commander.
 *
 * Every partner control on the device is gated on this: the second
 * commander-damage slot, the partner-tax counter, the attack dial's
 * second commander mode. Most games have no partners in them at all,
 * and showing those controls to everyone made the device look like it
 * was tracking something the table was not. */
/* Loads the stored player names over the P1..P8 defaults. Called once
   at boot; a no-op until somebody has actually renamed someone. */
void player_names_restore(void);
/* Stores the current names. Called from the rename screen only - the
   names are the table's, not the game's, so a reset or a power cycle
   must not touch them. */
void player_names_persist(void);

bool player_has_partner(int player);
void set_player_has_partner(int player, bool has);
/* True if ANY player in the current game fields one - the question a
   screen asks before it draws a shared partner control. */
bool any_player_has_partner(void);

// ---------- commander-slot encoding ----------
/* The damage log and elimination-undo bookkeeping both store a single
   "source" int for LOG_EVT_CMD_DAMAGE entries. Rather than widen those
   structs, the partner slot rides along in the same field: sources
   0..MAX_GAME_PLAYERS-1 are the primary commander, +MAX_GAME_PLAYERS
   is the partner commander from that same source player. */
static inline int encode_cmd_source(int source, int slot) {
    return source + (slot ? MAX_GAME_PLAYERS : 0);
}

static inline void decode_cmd_source(int encoded, int *source, int *slot) {
    *slot = (encoded >= MAX_GAME_PLAYERS) ? 1 : 0;
    *source = encoded - (*slot ? MAX_GAME_PLAYERS : 0);
}

// ---------- functions ----------
void knob_life_init(void);
void knob_life_reset(void);
void damage_enter(void);
void add_damage_to_selected_enemy(int delta);
int damage_pending_delta(void);
void damage_apply(void);
void damage_cancel(void);
void change_player_life(int delta);
void change_all_damage(int delta);
void apply_life_delta(int player, int delta);
void apply_attack_cmd_damage(int source, int target, int delta, int slot);
void apply_attack_poison(int target, int delta);

// ---------- life flash (read-only post-commit feedback) ----------
/* What just happened to each player's life, shown for a couple of
 * seconds after the change is already committed.
 *
 * Per-player rather than one delta for a set, because the changes it
 * has to show are not all the same number: All Damage hits everyone
 * for the same amount, but a lifelink attack takes life off the
 * defender and gives it to the attacker in one action. A zero means
 * that player has nothing to show. */
extern bool life_flash_active;
extern int life_flash_delta[MAX_DISPLAY_PLAYERS];
void start_life_flash(const int *deltas);

// ---------- player selection set ----------
int selection_count(void);
bool is_player_selected(int player);
void selection_clear(void);
void selection_toggle(int player);
void selection_set_single(int player);
void undo_life_change(int player, int delta);
void undo_cmd_damage(int encoded_source, int target, int delta); /* see decode_cmd_source */
void undo_counter_change(int player, int counter_type, int delta);
void prepare_cmd_damage_for_player(int target);
void refresh_cmd_damage_slot(void);
/* The life-preview timer's callback body (was life_preview_commit_cb(lv_timer_t*)
   before the bridge split - see the comment at the top of this file).
   Called by game.c's bridge from its real lv_timer_t callback. */
void game_life_preview_commit(void);
void begin_counter_edit(int player, counter_type_t type);
void change_counter_edit(int delta);
int counter_edit_pending_delta(void);
int apply_counter_edit(void);
int get_counter_value(int player, counter_type_t type);
const counter_definition_t *get_counter_definition(counter_type_t type);
bool counter_type_is_enabled(counter_type_t type);
void start_player_selection_animation(void);
void stop_player_selection_animation(void);
bool player_selection_animation_active(void);
/* The selection roulette's per-step tick body (was player_select_anim_cb).
   Returns the delay in ms to reschedule the bridge's timer at, or 0 when
   the animation is done and the bridge should pause it instead. */
int game_player_select_anim_step(void);
/* The life flash's auto-clear body (was all_damage_flash_end_cb).
   Called by the bridge's timer callback after it pauses itself. */
void game_life_flash_end(void);

bool elimination_action_available(int player);
void undo_elimination_action(int player);
void manual_eliminate_player(int player);
void manual_uneliminate_player(int player);

void check_player_elimination(int player);

/* Test-only accessors for Table Sync's file-static per-player Lamport
   versions (see game_state_sync.c). Not for firmware use. */
uint16_t player_version_for_test(int player);
void player_version_set_for_test(int player, uint16_t version);

// ---------- per-player color state (bools only - the lv_color_hsv_t
// value itself, and everything that turns these into a color, is in
// game.h/game.c; see the comment at the top of this file) ----------
extern bool player_life_color[MAX_DISPLAY_PLAYERS];
extern bool player_has_override[MAX_DISPLAY_PLAYERS];

int get_cmd_target_player_index(int row);

/* net_sync_fill_state()/net_sync_apply_state()/net_sync_fill_names()/
   net_sync_apply_names()/net_sync_commit_names()/net_sync_begin_game()/
   net_sync_reset_versions() are declared in net_sync.h (included above)
   and implemented in game_state_sync.c. */

#ifdef __cplusplus
}
#endif

#endif // _GAME_STATE_H
