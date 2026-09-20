#ifndef _GAME_BRIDGE_H
#define _GAME_BRIDGE_H

/* Wires game_state.c's game_hooks up to the real UI refresh functions,
 * and owns the three lv_timer_t objects those hooks schedule (the life
 * preview's 3s auto-commit, the all-damage flash's auto-clear, the
 * selection roulette's per-step tick).
 *
 * This is the one place in the codebase that is supposed to know both
 * halves at once, which is exactly why it is a file of its own. It used
 * to sit at the bottom of game.c, next to the player-colour maths -
 * and since game.h is what every screen includes to ask for a colour,
 * that put four UI headers inside the file everything depends on, with
 * each of those four including game.h straight back. Four cycles, all
 * of them paid for by one function.
 *
 * Nothing includes this but the composition root. See game_hooks.h for
 * the seam itself. */

#ifdef __cplusplus
extern "C" {
#endif

/* Call once at boot, after every screen this refreshes exists and
   before any input can reach game_state.c - see knob_gui() in knob.c. */
void game_bridge_init(void);

#ifdef __cplusplus
}
#endif

#endif // _GAME_BRIDGE_H
