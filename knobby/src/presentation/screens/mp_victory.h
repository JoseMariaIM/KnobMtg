#ifndef _MP_VICTORY_H
#define _MP_VICTORY_H

#include "types.h"

/* The victory screen: fades in once only one player is left standing
 * in multiplayer, shows their name/color full-screen, and holding
 * anywhere resets for another game. Split out of ui_mp.c - see
 * ui_mp_internal.h for its one dependency back on the core (reading
 * the active layout to enumerate alive players). */

extern lv_obj_t *screen_victory;

void build_victory_screen(void);

/* True from the moment the victory fade starts until the next reset -
   lets navigation helpers (back_to_main() in ui_1p.c) avoid loading
   over it while its transition animation is still in flight. */
bool mp_victory_active(void);

/* Called from refresh_multiplayer_ui() (ui_mp.c) after every panel
   refresh: fades to the victory screen the moment exactly one player
   remains un-eliminated. No-op once already shown, or with 1 or fewer
   players tracked (no "victory" in solo mode). */
void check_for_winner(void);

#endif // _MP_VICTORY_H
