/* The game's rules: life totals, commander damage, counters, selection,
 * elimination, and Table Sync's state-merge logic. See game_state.h for
 * why this file has no LVGL types in its public interface, and game.c
 * for the bridge that supplies the UI-refresh/timer-scheduling hooks
 * this file calls through (never directly) - game_hooks.h. */
#include "game_state.h"
#include "game_state_internal.h"
#include "../usecases/damage_log.h"
#include "../adapters/prefs_table.h"
#include "../adapters/prefs_roster.h"
#include "esp_random.h"
#include "../adapters/lang.h"
#include <string.h>
#include <stdio.h>

// ---------- hook call helpers ----------
/* Thin wrappers so a call site reads exactly like the direct call it
   replaces (notify_refresh_player_ui() vs. the old refresh_player_ui())
   while staying NULL-safe for anything that hasn't registered (unit
   tests, or a boot sequence in progress before game_bridge_init()
   runs - see knob.c). */
/* Six of these are also called from game_state_sync.c (declared in
   game_state_internal.h), so they can't be static: notify_refresh_
   player_ui/select_ui/damage_ui/rename_ui, notify_select_kick_timer,
   notify_life_preview_schedule. The other three are only ever called
   from this file and stay static. */
void notify_refresh_player_ui(void)
{
    if (game_hooks_get()->refresh_player_ui != NULL) game_hooks_get()->refresh_player_ui();
}
void notify_refresh_select_ui(void)
{
    if (game_hooks_get()->refresh_select_ui != NULL) game_hooks_get()->refresh_select_ui();
}
void notify_refresh_damage_ui(void)
{
    if (game_hooks_get()->refresh_damage_ui != NULL) game_hooks_get()->refresh_damage_ui();
}
static void notify_refresh_all_damage_ui(void)
{
    if (game_hooks_get()->refresh_all_damage_ui != NULL) game_hooks_get()->refresh_all_damage_ui();
}
void notify_refresh_rename_ui(void)
{
    if (game_hooks_get()->refresh_rename_ui != NULL) game_hooks_get()->refresh_rename_ui();
}
void notify_select_kick_timer(void)
{
    if (game_hooks_get()->select_kick_timer != NULL) game_hooks_get()->select_kick_timer();
}
void notify_life_preview_schedule(bool active)
{
    if (game_hooks_get()->life_preview_schedule != NULL) game_hooks_get()->life_preview_schedule(active);
}
static void notify_life_flash_schedule(void)
{
    if (game_hooks_get()->life_flash_schedule != NULL) game_hooks_get()->life_flash_schedule();
}
static void notify_player_select_anim_schedule(bool active)
{
    if (game_hooks_get()->player_select_anim_schedule != NULL) game_hooks_get()->player_select_anim_schedule(active);
}

// ---------- state ----------
int active_enemy_count = 3;

enemy_state_t enemies[MAX_ENEMY_COUNT] = {
    {"P1", 0}, {"P2", 0}, {"P3", 0}, {"P4", 0},
    {"P5", 0}, {"P6", 0}, {"P7", 0}
};

int selected_enemy = -1;
int dice_result = 0;

int player_life[MAX_DISPLAY_PLAYERS] = {40, 40, 40, 40};
bool player_selected[MAX_DISPLAY_PLAYERS] = {false};
char player_names[MAX_GAME_PLAYERS][16] = {
    "P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8"
};
int menu_player = 0;
int cmd_damage_totals[MAX_GAME_PLAYERS][MAX_DISPLAY_PLAYERS] = {{0}};
int partner_cmd_damage_totals[MAX_GAME_PLAYERS][MAX_DISPLAY_PLAYERS] = {{0}};
int all_damage_value = 0;
int cmd_damage_target = -1;
int cmd_damage_slot = 0;
static int damage_start_value = 0;
int pending_life_delta = 0;
bool life_preview_active = false;
int player_counters[MAX_DISPLAY_PLAYERS][COUNTER_TYPE_COUNT] = {{0}};
counter_type_t counter_edit_type = COUNTER_TYPE_COMMANDER_TAX;
int counter_edit_value = 0;
bool player_eliminated[MAX_DISPLAY_PLAYERS] = {false};
/* Conceded players (manual elimination) tracked apart from auto-elimination,
   so an undo that recomputes auto conditions can't revive someone who
   manually conceded while still above 0 life. Not static: read/written
   by game_state_sync.c too (see game_state_internal.h). */
bool player_manually_eliminated[MAX_DISPLAY_PLAYERS] = {false};

bool player_life_color[MAX_DISPLAY_PLAYERS] = {false, false, false, false};
bool player_has_override[MAX_DISPLAY_PLAYERS] = {false, false, false, false};

typedef struct {
    bool valid;
    uint8_t event_type;
    int source;
    int delta;
} elimination_action_t;

static elimination_action_t elimination_action[MAX_DISPLAY_PLAYERS] = {{0}};

/* Table Sync's Lamport versions (game_epoch/player_version/names_version)
   and the net_sync_*() fill/apply/begin/reset functions that use them
   now live in game_state_sync.c - see the comment at its top and at
   game_state_internal.h. net_sync_commit_player() is declared there
   for us; clear_player_elimination_action() below is declared there
   for it. */

static void commit_player_event(int player, int log_delta, uint8_t event_type,
                                 int source, bool is_lethal);

#define MANA_ICON_COMMANDER "\xEE\xA7\x86"
#define MANA_ICON_PARTY     "\xEE\xA6\x87"
#define MANA_ICON_SKULL     "\xEE\x98\x98"
#define MANA_ICON_LEVEL     "\xEE\xA4\x80"

/* menu_label/display_name are resolved through t() at call time (see
   get_counter_definition), so this table stores string IDs there
   instead of literals - a static const array can't call a function in
   its own initializer. badge_text is a compact one-letter glyph, not
   translated (matches the icon fonts' visual style). */
static const struct {
    string_id_t menu_label_id;
    string_id_t display_name_id;
    string_id_t log_name_id;
    const char *badge_text;
    const char *icon_text;
    uint32_t accent_color;
    bool enabled;
} counter_definitions[COUNTER_TYPE_COUNT] = {
    {STR_COUNTER_COMMANDER_TAX_MENU, STR_COUNTER_COMMANDER_TAX, STR_COUNTER_COMMANDER_TAX_LOG, "C", MANA_ICON_COMMANDER, 0xA84300, true},
    {STR_COUNTER_PARTNER_TAX_MENU,   STR_COUNTER_PARTNER_TAX,   STR_COUNTER_PARTNER_TAX_LOG,   "P", MANA_ICON_PARTY,     0x1565C0, true},
    {STR_COUNTER_POISON,             STR_COUNTER_POISON,        STR_COUNTER_POISON,            "!", MANA_ICON_SKULL,     0x2E7D32, true},
    {STR_COUNTER_EXPERIENCE,         STR_COUNTER_EXPERIENCE,    STR_COUNTER_EXPERIENCE,        "E", MANA_ICON_LEVEL,     0x6A1B9A, true},
};

/* Not static: called from game_state_sync.c too (declared in
   game_state_internal.h). */
void clear_player_elimination_action(int player)
{
    if (player < 0 || player >= MAX_DISPLAY_PLAYERS) return;
    elimination_action[player].valid = false;
}

static void set_player_elimination_action(int player, uint8_t event_type, int source, int delta)
{
    if (player < 0 || player >= MAX_DISPLAY_PLAYERS) return;
    elimination_action[player].valid = true;
    elimination_action[player].event_type = event_type;
    elimination_action[player].source = source;
    elimination_action[player].delta = delta;
}

bool elimination_action_available(int player)
{
    if (player < 0 || player >= MAX_DISPLAY_PLAYERS) return false;
    return elimination_action[player].valid;
}

void undo_elimination_action(int player)
{
    if (!elimination_action_available(player)) return;

    elimination_action_t action = elimination_action[player];
    clear_player_elimination_action(player);

    if (action.event_type == LOG_EVT_LIFE) {
        undo_life_change(player, action.delta);
    } else if (action.event_type == LOG_EVT_CMD_DAMAGE) {
        undo_life_change(player, action.delta);
        undo_cmd_damage(action.source, player, action.delta);
    } else if (action.event_type == LOG_EVT_COUNTER) {
        undo_counter_change(player, action.source, action.delta);
    }

    /* Drop the log entry that caused the elimination so the same event can't
       be undone a second time from the Event Log. The eliminating event is
       the newest one for this player (eliminated players accrue no more). */
    damage_log_remove_last_for(player, action.event_type);
}

void check_player_elimination(int player)
{
    if (player < 0 || player >= MAX_DISPLAY_PLAYERS) return;
    bool was_eliminated = player_eliminated[player];
    bool now_eliminated = false;

    /* Elimination is a multiplayer concept: with a single tracked player
       there is no eliminated-menu route in the 1p UI, so eliminating
       player 0 would brick the counter until reset. */
    if (prefs_get_auto_eliminate() && prefs_get_players_to_track() > 1) {
        if (player_life[player] <= 0) {
            now_eliminated = true;
        } else {
            for (int i = 0; i < MAX_GAME_PLAYERS; i++) {
                if (i != player && (cmd_damage_totals[i][player] >= 21 ||
                                     partner_cmd_damage_totals[i][player] >= 21)) {
                    now_eliminated = true;
                    break;
                }
            }
            if (!now_eliminated && player_counters[player][COUNTER_TYPE_POISON] >= 10) {
                now_eliminated = true;
            }
        }
    }

    if (player_manually_eliminated[player]) {
        now_eliminated = true;
    }

    player_eliminated[player] = now_eliminated;
    if (!now_eliminated) {
        clear_player_elimination_action(player);
    } else if (player_selected[player]) {
        /* An eliminated player is no longer a life-change target: drop it
           from the selection so the knob doesn't preview onto a dead panel
           that can't be tapped to deselect. */
        player_selected[player] = false;
        notify_select_kick_timer();
    }

    if (was_eliminated != now_eliminated) {
        notify_refresh_player_ui();
    }
}

void manual_eliminate_player(int player)
{
    if (player < 0 || player >= MAX_DISPLAY_PLAYERS) return;
    if (player_eliminated[player]) return;
    /* Same solo-mode exemption as check_player_elimination. */
    if (prefs_get_players_to_track() <= 1) return;
    player_eliminated[player] = true;
    player_manually_eliminated[player] = true;
    clear_player_elimination_action(player);
    if (player_selected[player]) {
        player_selected[player] = false;
        notify_select_kick_timer();
    }
    net_sync_commit_player(player);
    notify_refresh_player_ui();
}

void manual_uneliminate_player(int player)
{
    int i;
    if (player < 0 || player >= MAX_DISPLAY_PLAYERS) return;
    if (!player_eliminated[player]) return;
    player_eliminated[player] = false;
    player_manually_eliminated[player] = false;
    clear_player_elimination_action(player);
    /* A remotely-caused elimination arrives with no local
       elimination_action to undo, so revival must also clear whatever
       condition would instantly re-kill the player — otherwise they
       come back at e.g. -2 life and re-die on the next touch. Pull
       each lethal condition just below its threshold, but only when
       auto-elimination would actually re-fire (same gate as
       check_player_elimination): with it off, life <= 0 or poison >=
       10 are legitimate alive states that must not be rewritten. */
    if (prefs_get_auto_eliminate() && prefs_get_players_to_track() > 1) {
        if (player_life[player] < 1) player_life[player] = 1;
        if (player_counters[player][COUNTER_TYPE_POISON] > 9)
            player_counters[player][COUNTER_TYPE_POISON] = 9;
        for (i = 0; i < MAX_GAME_PLAYERS; i++) {
            if (cmd_damage_totals[i][player] > 20)
                cmd_damage_totals[i][player] = 20;
            if (partner_cmd_damage_totals[i][player] > 20)
                partner_cmd_damage_totals[i][player] = 20;
        }
    }
    net_sync_commit_player(player);
    notify_refresh_player_ui();
}

int get_cmd_target_player_index(int row)
{
    int skip_player;
    int num = prefs_get_num_players();
    int count = 0;
    int i;

    if (row < 0 || row >= active_enemy_count) return row;

    if (cmd_damage_target >= 0) {
        skip_player = cmd_damage_target;
    } else {
        /* No explicit target — only hidden-screen repaints and the
           sim's direct navigation reach this (every live flow sets
           cmd_damage_target first): map as if the owner, player 0,
           were the target, so the enemy rows show players 1..n-1. */
        skip_player = 0;
    }

    for (i = 0; i < num; i++) {
        if (i == skip_player) continue;
        if (count == row) return i;
        count++;
    }

    return row;
}

const counter_definition_t *get_counter_definition(counter_type_t type)
{
    static counter_definition_t resolved;
    if (type < 0 || type >= COUNTER_TYPE_COUNT) return NULL;
    resolved.menu_label = t(counter_definitions[type].menu_label_id);
    resolved.display_name = t(counter_definitions[type].display_name_id);
    resolved.log_name = t(counter_definitions[type].log_name_id);
    resolved.badge_text = counter_definitions[type].badge_text;
    resolved.icon_text = counter_definitions[type].icon_text;
    resolved.accent_color = counter_definitions[type].accent_color;
    resolved.enabled = counter_definitions[type].enabled;
    return &resolved;
}

bool counter_type_is_enabled(counter_type_t type)
{
    const counter_definition_t *definition = get_counter_definition(type);

    return (definition != NULL) && definition->enabled;
}

int get_counter_value(int player, counter_type_t type)
{
    if (player < 0 || player >= MAX_DISPLAY_PLAYERS) return 0;
    if (type < 0 || type >= COUNTER_TYPE_COUNT) return 0;

    return player_counters[player][type];
}

void begin_counter_edit(int player, counter_type_t type)
{
    if (player < 0 || player >= MAX_DISPLAY_PLAYERS) return;
    if (type < 0 || type >= COUNTER_TYPE_COUNT) return;

    menu_player = player;
    counter_edit_type = type;
    counter_edit_value = player_counters[player][type];
}

void change_counter_edit(int delta)
{
    counter_edit_value = clamp_counter(counter_edit_value + delta);
}

/* Knob turns since the editor opened, i.e. the delta apply_counter_edit()
   will commit against the player's live counter. */
int counter_edit_pending_delta(void)
{
    if (menu_player < 0 || menu_player >= MAX_DISPLAY_PLAYERS) return 0;
    if (counter_edit_type < 0 || counter_edit_type >= COUNTER_TYPE_COUNT) return 0;
    return counter_edit_value - player_counters[menu_player][counter_edit_type];
}

int apply_counter_edit(void)
{
    int player = menu_player;
    int old_value;
    int change_delta;

    if (player < 0 || player >= MAX_DISPLAY_PLAYERS) return 0;
    if (counter_edit_type < 0 || counter_edit_type >= COUNTER_TYPE_COUNT) return 0;
    /* Same remote-elimination race guard as damage_apply. */
    if (player_eliminated[player]) return 0;

    old_value = player_counters[player][counter_edit_type];
    counter_edit_value = clamp_counter(counter_edit_value);
    change_delta = counter_edit_value - old_value;
    player_counters[player][counter_edit_type] = counter_edit_value;

    if (change_delta != 0) {
        /* Only poison has a lethal threshold; every other counter still
           needs to be logged and synced, just never lethal. */
        bool is_lethal = counter_edit_type == COUNTER_TYPE_POISON &&
                          old_value < 10 && counter_edit_value >= 10;
        commit_player_event(player, change_delta, LOG_EVT_COUNTER, counter_edit_type, is_lethal);
    }

    return change_delta;
}

// ---------- player selection set ----------
int selection_count(void)
{
    int i, n = 0;
    for (i = 0; i < MAX_DISPLAY_PLAYERS; i++)
        if (player_selected[i]) n++;
    return n;
}

bool is_player_selected(int player)
{
    if (player < 0 || player >= MAX_DISPLAY_PLAYERS) return false;
    return player_selected[player];
}

void selection_clear(void)
{
    int i;
    for (i = 0; i < MAX_DISPLAY_PLAYERS; i++)
        player_selected[i] = false;
}

void selection_toggle(int player)
{
    if (player < 0 || player >= MAX_DISPLAY_PLAYERS) return;
    if (player_eliminated[player]) return;
    player_selected[player] = !player_selected[player];
}

void selection_set_single(int player)
{
    selection_clear();
    if (player < 0 || player >= MAX_DISPLAY_PLAYERS) return;
    if (player_eliminated[player]) return;
    player_selected[player] = true;
}

/* The shared tail of every "commit a game event" path below: log it,
   flag an elimination-undo action if this is what kills the player,
   run the actual elimination check, and sync the change to the
   table. Every apply_*() function is its own state mutation (life or
   a counter) plus its own lethality test - those differ per event
   type - followed by one call here. log_delta is whatever value the
   event log and the elimination-undo action both need to be able to
   reverse it later: the life delta for LOG_EVT_LIFE/LOG_EVT_CMD_DAMAGE,
   the counter delta for LOG_EVT_COUNTER. */
static void commit_player_event(int player, int log_delta, uint8_t event_type,
                                 int source, bool is_lethal)
{
    damage_log_add(player, log_delta, event_type, source);
    if (is_lethal) {
        set_player_elimination_action(player, event_type, source, log_delta);
    }
    check_player_elimination(player);
    net_sync_commit_player(player);
}

/* The single entry point for committing a life change as a game event.
   Any path that applies life deltas (knob commit, All Damage) must use
   this so the elimination machinery can't be bypassed. */
void apply_life_delta(int player, int delta)
{
    if (player < 0 || player >= MAX_DISPLAY_PLAYERS) return;
    if (player_eliminated[player]) return;
    player_life[player] = clamp_life(player_life[player] + delta);
    commit_player_event(player, delta, LOG_EVT_LIFE, -1, player_life[player] <= 0);
}

// ---------- life preview ----------
/* Body of the bridge's life-preview lv_timer_t callback (see
   game_state.h). Fires ~3s after the last knob turn that left a
   pending delta; the bridge itself owns arming/disarming that timer
   via the life_preview_schedule hook, this just applies (or discards)
   whatever's pending when it's called. */
void game_life_preview_commit(void)
{
    int track = prefs_get_players_to_track();
    int i;

    if (!life_preview_active || selection_count() == 0) {
        pending_life_delta = 0;
        life_preview_active = false;
        notify_life_preview_schedule(false);
        return;
    }

    for (i = 0; i < track && i < MAX_DISPLAY_PLAYERS; i++) {
        if (!player_selected[i]) continue;
        apply_life_delta(i, pending_life_delta);
    }
    pending_life_delta = 0;
    life_preview_active = false;
    notify_life_preview_schedule(false);
    /* Applying a life change ends the operation: in multi-select mode clear
       the selection so the next tap starts a fresh selection. Otherwise
       sequential per-player damage keeps stacking players into the set. */
    if (prefs_get_multi_select()) {
        selection_clear();
        notify_select_kick_timer();
    }
    notify_refresh_player_ui();
}

// ---------- life changes ----------
void damage_enter(void)
{
    if (selected_enemy >= 0 && selected_enemy < active_enemy_count)
        damage_start_value = enemies[selected_enemy].damage;
    else
        damage_start_value = 0;
}

void add_damage_to_selected_enemy(int delta)
{
    if (selected_enemy < 0 || selected_enemy >= active_enemy_count) return;

    enemies[selected_enemy].damage += delta;
    if (enemies[selected_enemy].damage < 0)
        enemies[selected_enemy].damage = 0;

    notify_refresh_damage_ui();
}

/* Knob turns since the editor opened, i.e. the delta damage_apply()
   will commit. The staged value lives in enemies[].damage; this is the
   difference against the snapshot damage_enter() took. */
int damage_pending_delta(void)
{
    if (selected_enemy < 0 || selected_enemy >= active_enemy_count) return 0;
    return enemies[selected_enemy].damage - damage_start_value;
}

void damage_apply(void)
{
    int delta;
    int source;
    int *cell;
    int encoded_source;

    if (selected_enemy < 0 || selected_enemy >= active_enemy_count) return;
    if (cmd_damage_target < 0 || cmd_damage_target >= MAX_DISPLAY_PLAYERS) return;
    /* Unreachable locally (the editor only opens for a live target),
       but a remote elimination can race an open editor: eliminated
       players accrue no more (same rule as apply_life_delta), and
       committing here would overwrite their elimination-undo action. */
    if (player_eliminated[cmd_damage_target]) return;

    delta = enemies[selected_enemy].damage - damage_start_value;
    if (delta == 0) return;

    source = get_cmd_target_player_index(selected_enemy);
    cell = (cmd_damage_slot == 1) ? &partner_cmd_damage_totals[source][cmd_damage_target]
                                  : &cmd_damage_totals[source][cmd_damage_target];
    *cell = enemies[selected_enemy].damage;
    encoded_source = encode_cmd_source(source, cmd_damage_slot);
    player_life[cmd_damage_target] = clamp_life(player_life[cmd_damage_target] - delta);
    commit_player_event(cmd_damage_target, -delta, LOG_EVT_CMD_DAMAGE, encoded_source,
                         *cell >= 21 || player_life[cmd_damage_target] <= 0);

    notify_refresh_select_ui();
}

void damage_cancel(void)
{
    if (selected_enemy >= 0 && selected_enemy < active_enemy_count)
        enemies[selected_enemy].damage = damage_start_value;
}

/* Attack screen's Cmdr mode: source/target are already known (picked via
   the drag gesture), so this applies straight to cmd_damage_totals
   instead of going through the enemies[]-list editor damage_apply()
   uses. Mirrors its log/elimination/sync sequence exactly. */
void apply_attack_cmd_damage(int source, int target, int delta, int slot)
{
    int *cell;
    int encoded_source;

    if (target < 0 || target >= MAX_DISPLAY_PLAYERS) return;
    if (player_eliminated[target]) return;
    if (delta == 0) return;

    /* A partner's 21 is its own 21: the two commanders never pool, so
       a source with no partner declared must never be able to open a
       second tally by accident. */
    if (slot != 0 && !player_has_partner(source)) slot = 0;

    cell = (slot != 0) ? &partner_cmd_damage_totals[source][target]
                       : &cmd_damage_totals[source][target];
    *cell += delta;
    encoded_source = encode_cmd_source(source, slot);
    player_life[target] = clamp_life(player_life[target] - delta);
    commit_player_event(target, -delta, LOG_EVT_CMD_DAMAGE, encoded_source,
                         *cell >= 21 || player_life[target] <= 0);
}

/* Attack screen's Infect mode: same poison-threshold rule as
   apply_counter_edit(), applied as a delta against a known target
   instead of through the counter editor's menu_player global. */
void apply_attack_poison(int target, int delta)
{
    int old_value;

    if (target < 0 || target >= MAX_DISPLAY_PLAYERS) return;
    if (player_eliminated[target]) return;
    if (delta == 0) return;

    old_value = player_counters[target][COUNTER_TYPE_POISON];
    player_counters[target][COUNTER_TYPE_POISON] = clamp_counter(old_value + delta);
    commit_player_event(target, delta, LOG_EVT_COUNTER, COUNTER_TYPE_POISON,
                         old_value < 10 && player_counters[target][COUNTER_TYPE_POISON] >= 10);
}

void change_player_life(int delta)
{
    /* The shared delta applies to every currently-selected player. Clamp it
       to the headroom of the selected set so the previewed totals always
       equal what the commit will store and overshoot detents at the life
       cap are absorbed instead of accumulating. */
    int track = prefs_get_players_to_track();
    int max_up = LIFE_MAX;
    int min_down = LIFE_MIN;
    int i;

    /* The roulette walks the selection every tick, so a delta dialed
       mid-spin would land on whichever player the wheel stops at. */
    if (player_selection_animation_active()) return;

    if (selection_count() == 0) return;

    notify_select_kick_timer();

    for (i = 0; i < track && i < MAX_DISPLAY_PLAYERS; i++) {
        if (!player_selected[i] || player_eliminated[i]) continue;
        if (LIFE_MAX - player_life[i] < max_up) max_up = LIFE_MAX - player_life[i];
        if (LIFE_MIN - player_life[i] > min_down) min_down = LIFE_MIN - player_life[i];
    }

    pending_life_delta += delta;
    if (pending_life_delta > max_up) pending_life_delta = max_up;
    if (pending_life_delta < min_down) pending_life_delta = min_down;
    life_preview_active = (pending_life_delta != 0);

    notify_life_preview_schedule(life_preview_active);

    notify_refresh_player_ui();
}

/* ---------- life flash ---------- */
/* A change that is already committed, shown for a couple of seconds so
   the player can read what just happened: the same "-N / = total"
   widgets the knob's live preview uses.

   All Damage and the attack screen both apply instantly (like any
   other life change) and then flash. A first version reused the knob's
   actual pending_life_delta/player_selected preview state to get that
   rendering for free, but that left the knob live during the flash:
   turning it kept piling more damage onto everyone it had just hit,
   which is exactly the "should apply and return to normal" behavior
   this replaces. This state is deliberately separate from
   player_selected and never touched by change_player_life - the
   numbers are already committed by the time this displays. */
bool life_flash_active = false;
int life_flash_delta[MAX_DISPLAY_PLAYERS];

/* Body of the bridge's life-flash lv_timer_t callback (see
   game_state.h). The bridge pauses its own timer before calling this;
   this just clears the flash state and asks for a repaint. */
void game_life_flash_end(void)
{
    life_flash_active = false;
    memset(life_flash_delta, 0, sizeof(life_flash_delta));
    notify_refresh_player_ui();
}

void start_life_flash(const int *deltas)
{
    memcpy(life_flash_delta, deltas, sizeof(life_flash_delta));
    life_flash_active = true;

    notify_life_flash_schedule();

    notify_refresh_player_ui();
}

void player_names_restore(void)
{
    char stored[PLAYER_NAME_COUNT][PLAYER_NAME_LEN];
    int i;

    if (!prefs_has_player_names()) return;
    prefs_get_player_names(stored);
    for (i = 0; i < MAX_GAME_PLAYERS && i < PLAYER_NAME_COUNT; i++) {
        /* An empty slot means that player was never renamed; leave the
           P1..P8 default rather than blanking their panel. */
        if (stored[i][0] == '\0') continue;
        snprintf(player_names[i], sizeof(player_names[i]), "%s", stored[i]);
    }
}

void player_names_persist(void)
{
    char out[PLAYER_NAME_COUNT][PLAYER_NAME_LEN];
    int i;

    for (i = 0; i < PLAYER_NAME_COUNT; i++) {
        snprintf(out[i], PLAYER_NAME_LEN, "%s",
                 (i < MAX_GAME_PLAYERS) ? player_names[i] : "");
    }
    prefs_set_player_names(out);
}

bool player_has_partner(int player)
{
    if (player < 0 || player >= MAX_GAME_PLAYERS) return false;
    return (prefs_get_partner_mask() & (1 << player)) != 0;
}

void set_player_has_partner(int player, bool has)
{
    int mask;

    if (player < 0 || player >= MAX_GAME_PLAYERS) return;
    mask = prefs_get_partner_mask();
    if (has) mask |= (1 << player);
    else     mask &= ~(1 << player);
    prefs_set_partner_mask(mask);
}

bool any_player_has_partner(void)
{
    int i, num = prefs_get_num_players();

    for (i = 0; i < num; i++) {
        if (player_has_partner(i)) return true;
    }
    return false;
}

void prepare_cmd_damage_for_player(int target)
{
    int i, row = 0;
    int num = prefs_get_num_players();

    cmd_damage_target = target;
    cmd_damage_slot = 0; /* always open on the primary commander */

    for (i = 0; i < num; i++) {
        if (i == target) continue;
        if (row < MAX_ENEMY_COUNT) {
            enemies[row].damage = cmd_damage_totals[i][target];
            row++;
        }
    }
}

/* Re-stages enemies[].damage from whichever matrix cmd_damage_slot now
   points at, without touching cmd_damage_target. Used when the select
   screen's Commander/Partner toggle flips slots. */
void refresh_cmd_damage_slot(void)
{
    int i, row = 0;
    int num = prefs_get_num_players();

    if (cmd_damage_target < 0) return;

    for (i = 0; i < num; i++) {
        if (i == cmd_damage_target) continue;
        if (row < MAX_ENEMY_COUNT) {
            enemies[row].damage = (cmd_damage_slot == 1)
                                       ? partner_cmd_damage_totals[i][cmd_damage_target]
                                       : cmd_damage_totals[i][cmd_damage_target];
            row++;
        }
    }
}

void change_all_damage(int delta)
{
    all_damage_value += delta;
    if (all_damage_value < 0) all_damage_value = 0;
    notify_refresh_all_damage_ui();
}

// ---------- undo ----------
void undo_life_change(int player, int delta)
{
    if (player < 0 || player >= MAX_DISPLAY_PLAYERS) return;

    player_life[player] = clamp_life(player_life[player] - delta);
    check_player_elimination(player);
    net_sync_commit_player(player);
    notify_refresh_player_ui();
    notify_refresh_select_ui();
}

void undo_cmd_damage(int encoded_source, int target, int delta)
{
    int source, slot;
    int *cell;

    decode_cmd_source(encoded_source, &source, &slot);
    if (source < 0 || source >= MAX_GAME_PLAYERS) return;
    if (target < 0 || target >= MAX_DISPLAY_PLAYERS) return;

    cell = (slot == 1) ? &partner_cmd_damage_totals[source][target] : &cmd_damage_totals[source][target];
    *cell += delta;
    if (*cell < 0) *cell = 0;
    check_player_elimination(target);
    net_sync_commit_player(target);
}

void undo_counter_change(int player, int counter_type, int delta)
{
    if (counter_type < 0 || counter_type >= COUNTER_TYPE_COUNT) return;
    if (player < 0 || player >= MAX_DISPLAY_PLAYERS) return;

    player_counters[player][counter_type] = clamp_counter(
        player_counters[player][counter_type] - delta
    );
    if (counter_type == COUNTER_TYPE_POISON) {
        check_player_elimination(player);
    }
    net_sync_commit_player(player);
    notify_refresh_player_ui();
}

// ---------- reset ----------
void knob_life_reset(void)
{
    int starting_life = prefs_get_life_total();
    int num = prefs_get_num_players();
    int i;

    active_enemy_count = num - 1;
    if (active_enemy_count < 0) active_enemy_count = 0;
    if (active_enemy_count > MAX_ENEMY_COUNT) active_enemy_count = MAX_ENEMY_COUNT;

    damage_log_reset();

    pending_life_delta = 0;
    selection_clear();
    life_preview_active = false;
    selected_enemy = -1;
    dice_result = 0;

    for (i = 0; i < MAX_ENEMY_COUNT; i++) {
        enemies[i].damage = 0;
    }

    for (i = 0; i < MAX_DISPLAY_PLAYERS; i++) {
        player_life[i] = starting_life;
    }
    menu_player = 0;
    cmd_damage_target = -1;
    cmd_damage_slot = 0;
    memset(cmd_damage_totals, 0, sizeof(cmd_damage_totals));
    memset(partner_cmd_damage_totals, 0, sizeof(partner_cmd_damage_totals));
    memset(player_counters, 0, sizeof(player_counters));
    memset(player_eliminated, 0, sizeof(player_eliminated));
    memset(player_manually_eliminated, 0, sizeof(player_manually_eliminated));
    for (i = 0; i < MAX_DISPLAY_PLAYERS; i++) clear_player_elimination_action(i);
    all_damage_value = 0;
    counter_edit_type = COUNTER_TYPE_COMMANDER_TAX;
    counter_edit_value = 0;

    /* A reset starts a new game: bump the epoch (which outranks any
       version drift a strayed device accumulated) and broadcast once. */
    net_sync_begin_game();
    net_sync_send_state();

    notify_life_preview_schedule(false);
}

// ---------- init ----------
void knob_life_init(void)
{
    int starting_life = prefs_get_life_total();
    int num = prefs_get_num_players();
    int i;

    active_enemy_count = num - 1;
    if (active_enemy_count < 0) active_enemy_count = 0;
    if (active_enemy_count > MAX_ENEMY_COUNT) active_enemy_count = MAX_ENEMY_COUNT;

    for (i = 0; i < MAX_DISPLAY_PLAYERS; i++) {
        player_life[i] = starting_life;
    }
    memset(player_counters, 0, sizeof(player_counters));
    counter_edit_type = COUNTER_TYPE_COMMANDER_TAX;
    counter_edit_value = 0;

    /* The life-preview/all-damage-flash/selection-roulette lv_timer_t
       objects are created once by the bridge's own init (see
       game_bridge_init() in game.c, called from knob.c alongside this
       function) - this function only resets the pure state they act on. */
}

// ---------- player selection animation ----------
static int player_select_anim_steps = 0;
static int player_select_anim_period = 0;
static int roulette_idx = 0;
static bool player_select_anim_running = false;

/* Body of the bridge's selection-roulette lv_timer_t callback (see
   game_state.h). Returns the ms to reschedule the bridge's timer at, or
   0 when the animation is done (nothing tracked, or steps ran out) and
   the bridge should pause it instead. */
int game_player_select_anim_step(void)
{
    int track = prefs_get_players_to_track();

    if (track <= 1) {
        player_select_anim_running = false;
        return 0;
    }

    // Move to next player (clockwise logic mapping to bottom/left/top/right)
    roulette_idx = (roulette_idx + 1) % track;
    selection_set_single(roulette_idx);
    /* Restart the deselect-timeout countdown like any selection change,
       so a timer left running from before the reset can't fire mid-spin
       and blank the selection for a tick. */
    notify_select_kick_timer();
    notify_refresh_player_ui();

    player_select_anim_steps--;
    if (player_select_anim_steps <= 0) {
        player_select_anim_running = false;
        notify_select_kick_timer();
        return 0;
    }
    // Linear deceleration
    player_select_anim_period += (200 / (player_select_anim_steps + 1));
    if (player_select_anim_period > 600) player_select_anim_period = 600;
    return player_select_anim_period;
}

void start_player_selection_animation(void)
{
    int track = prefs_get_players_to_track();
    int random_stops;

    if (track <= 1) return;
    if (!prefs_get_random_first()) return;

    // Randomize length to ensure random landing
    random_stops = (int)(esp_random() % track) + (track * 3);
    random_stops += esp_random() % (track * 2);

    player_select_anim_steps = random_stops;
    player_select_anim_period = 40; // start fast
    player_select_anim_running = true;

    roulette_idx = 0;
    selection_set_single(0);
    notify_select_kick_timer();

    notify_player_select_anim_schedule(true);
}

void stop_player_selection_animation(void)
{
    player_select_anim_steps = 0;
    player_select_anim_running = false;
    notify_player_select_anim_schedule(false);
}

bool player_selection_animation_active(void)
{
    return player_select_anim_running && player_select_anim_steps > 0;
}

