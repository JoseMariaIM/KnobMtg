#ifndef _GAME_HOOKS_H
#define _GAME_HOOKS_H

#include <stdbool.h>

/* game_state.c's dependency inversion seam: it needs to ask for a UI
 * refresh and to arm/disarm a couple of timed follow-ups (the life
 * preview's 3s auto-commit, the all-damage flash's auto-clear, the
 * player-selection roulette's per-step tick), but none of that is game
 * *logic* - it's LVGL plumbing that belongs to the UI/bridge layer.
 * Rather than game_state.c calling functions named after specific UI
 * modules (the old extern void refresh_player_ui(void); forward
 * declarations this replaces - the compiler never checked those
 * matched their real definition, only the linker, and only at link
 * time), it calls through this small pure-C interface instead. Exactly
 * one implementation registers itself, from game.c's bridge init - see
 * game_bridge_init() in game.c and its call site in knob.c.
 *
 * No LVGL types appear here on purpose: game_state.h includes this
 * file (not the other way around), and game_state.h/.c must stay
 * compilable without lvgl.h - see the comment at the top of
 * game_state.h for why that matters. Tests that only care about game
 * rules can leave every hook unset (game_hooks_get() defaults every
 * field to NULL, and each call site here checks before calling), or
 * register no-op stand-ins - either way, that's an application-glue
 * decision, not a domain-logic dependency. */

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*game_hook_fn)(void);
typedef void (*game_hook_bool_fn)(bool active);

typedef struct {
    /* Replace: extern void refresh_player_ui(void); (home.h) */
    game_hook_fn refresh_player_ui;
    /* Replace: extern void refresh_select_ui(void); (ui_cmd_damage.c) */
    game_hook_fn refresh_select_ui;
    /* Replace: extern void refresh_damage_ui(void); (ui_cmd_damage.c) */
    game_hook_fn refresh_damage_ui;
    /* Replace: extern void refresh_all_damage_ui(void); (ui_player_menu.c) */
    game_hook_fn refresh_all_damage_ui;
    /* Replace: extern void refresh_rename_ui(void); (rename.c) */
    game_hook_fn refresh_rename_ui;
    /* Replace: extern void select_kick_timer(void); (ui_mp.c) */
    game_hook_fn select_kick_timer;
    /* Replace the life-preview lv_timer_t's pause/reset/resume dance:
     * true = a delta is pending, (re)arm the 3s auto-commit; false =
     * nothing pending, pause it. */
    game_hook_bool_fn life_preview_schedule;
    /* Replace life_flash_timer's create-or-reset-then-resume: arm
     * (or re-arm) the flash's auto-clear countdown. */
    game_hook_fn life_flash_schedule;
    /* Replace player_select_anim_timer's resume/pause: true = start or
     * keep running the roulette's per-step tick; false = pause it. */
    game_hook_bool_fn player_select_anim_schedule;
} game_hooks_t;

/* Called once at boot (see game_bridge_init() in game.c) before any
 * screen can generate input. */
void game_hooks_register(const game_hooks_t *hooks);

/* Test-only: puts every hook back to NULL (the default), so a test
 * doesn't have to fight state a previous test's game_bridge_init() or
 * a hand-registered stand-in left behind. */
void game_hooks_reset(void);

const game_hooks_t *game_hooks_get(void);

#ifdef __cplusplus
}
#endif

#endif // _GAME_HOOKS_H
