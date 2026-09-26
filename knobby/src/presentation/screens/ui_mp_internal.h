#ifndef _UI_MP_INTERNAL_H
#define _UI_MP_INTERNAL_H

/* Small seam between ui_mp.c (owns screen_multiplayer's widget state,
 * mp_state) and its two satellite files, mp_victory.c and
 * mp_attack_gesture.c - not part of the app-wide public API (that's
 * ui_mp.h, unchanged by this split), just what those two need from the
 * core to do their job without owning mp_state themselves. */

#include "mp_layout.h"

/* The layout currently active on screen_multiplayer (NULL before the
   first rebuild_multiplayer_layout() call) - read-only access to what
   ui_mp.c's mp_state.layout points at, for check_for_winner() (mp_victory.c)
   and the attack-drag hover/target detection (mp_attack_gesture.c). */
const mp_layout_spec_t *mp_current_layout(void);

/* Called from rebuild_multiplayer_layout() (ui_mp.c) each time the
   screen's panels are rebuilt, so each satellite can drop any state
   that referred to the panels about to be destroyed. */
void mp_victory_reset(void);
void mp_attack_gesture_reset(void);

/* Called from rebuild_multiplayer_layout() per panel/once for the
   overlay, so mp_attack_gesture.c owns wiring its own event callbacks
   instead of ui_mp.c reaching into it. index is the panel's position in
   the active layout (matches mp_layout.h's panel arrays). */
void mp_attack_gesture_attach_panel_events(lv_obj_t *panel, int index);
void mp_attack_gesture_attach_overlay_events(lv_obj_t *overlay);

#endif // _UI_MP_INTERNAL_H
