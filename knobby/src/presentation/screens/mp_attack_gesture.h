#ifndef _MP_ATTACK_GESTURE_H
#define _MP_ATTACK_GESTURE_H

#include "../../types.h"

/* The Attack drag gesture on screen_multiplayer's panels: drag from one
 * player's wedge onto another's to open the Attack screen for that
 * pair, with a live comet-trail following the finger and a glow on
 * whichever panel is currently under it. Split out of ui_mp.c - see
 * ui_mp_internal.h for how rebuild_multiplayer_layout() (ui_mp.c) wires
 * this in per panel/overlay without owning any of its state itself. */

/* Used by knob.c's swipe classifier so a drag that crosses an edge zone
   can't also be read as the back/menu swipe gesture. */
bool attack_gesture_in_progress(void);

/* Used by ui_mp.c's tap/long-press handlers (event_multiplayer_select/
 * event_multiplayer_open_menu) so a drag's tail-end CLICKED isn't read
 * as a real tap, and a long hold mid-drag doesn't also open a menu.
 * mp_attack_drag_suppress_click() consumes the flag (one-shot, like the
 * old attack_drag_suppress_click read-then-clear in event_multiplayer_select). */
bool mp_attack_drag_suppress_click(void);
bool mp_attack_drag_active(void);

#endif // _MP_ATTACK_GESTURE_H
