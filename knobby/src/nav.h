#ifndef _NAV_H
#define _NAV_H

#include "knob.h"

/* The shell: the two top-level menus, and the one place that decides
 * where "back" goes.
 *
 * Back used to be answered in three files at once - knob.c's
 * screen_registry[], settings_handle_back() over settings_items[], and
 * minigames_handle_back() deferring to it - which meant no single
 * place described how the device unwinds. Worse, it put the quad menu
 * inside settings.c: the screen settings exits TO was owned by
 * settings, so anything that wanted to leave a settings sub-screen had
 * to call into the settings subsystem.
 *
 * Now settings answers questions about its own screens and draws its
 * own pages, and this file decides what that means for navigation.
 * Dependencies point one way: nav -> settings, never back. */

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t *screen_quad_menu;
extern lv_obj_t *screen_tools_menu;

/* Builds the quad and tools menus, and (through settings) the settings
   pages. Called once at boot from knob_gui(). */
void build_quad_menus(void);
void open_quad_menu(void);

/* The body behind back_to_main(): the multiplayer screen or the 1p
   one, whichever the player count calls for. Registered at boot; call
   it through home.h, not from here. */
void nav_go_home(void);

/* Where back goes for the settings cluster and the minigames menu.
   False when the screen is none of theirs, which is knob.c's cue to
   fall through to screen_registry[]. */
bool nav_handle_back(lv_obj_t *screen);

/* Single applier for the effective display rotation: the user's
   physical rotation, plus the acting player's seat on player menus
   when "Menus: Face Player" is on. Idempotent - recomputes from the
   active screen, so any navigation path can call it safely. */
void menu_facing_refresh(void);

#ifdef __cplusplus
}
#endif

#endif // _NAV_H
