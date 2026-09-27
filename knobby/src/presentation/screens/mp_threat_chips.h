#ifndef _MP_THREAT_CHIPS_H
#define _MP_THREAT_CHIPS_H

#include "../../types.h"

/* Commander damage on screen_multiplayer, one chip per rival commander
 * that has landed some on this player: the number, on a background in
 * that rival's own panel color. A rival fielding a partner gets a second
 * chip in a lighter shade of the same color, because the two commanders
 * never pool - 21 from either one kills on its own, so they are two
 * tallies, not one.
 *
 * ui_mp.c owns where they go - it is the one that knows each layout's
 * shape, and the chips land in a different place on a quadrant than on
 * a pie slice - while this file owns what a chip is and what it says.
 * Split out of ui_mp.c the same way mp_layout/mp_victory/
 * mp_attack_gesture were.
 */

#define MP_THREAT_CHIPS_MAX 3

/* Builds one panel's chips, hidden. Call per panel on layout rebuild. */
void mp_threat_chips_create(lv_obj_t *panel, int panel_index);

/* Drops every panel's chips. Call before the panels are destroyed. */
void mp_threat_chips_reset(void);

/* Fills in the chips this player currently warrants, worst rival first,
   and hides the rest. Returns how many are now visible, for the caller
   to place - positioning is its job, this only decides content. */
int mp_threat_chips_update(int panel_index, int player_index);

/* The i-th currently visible chip of a panel, for the caller to place. */
lv_obj_t *mp_threat_chips_obj(int panel_index, int index);

#endif // _MP_THREAT_CHIPS_H
