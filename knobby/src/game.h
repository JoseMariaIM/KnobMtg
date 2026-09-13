#ifndef _GAME_H
#define _GAME_H

/* Public facade: every other file in the app keeps including "game.h"
 * exactly as before, unchanged. Behind it, the actual rules
 * (life/counters/elimination/Table Sync - everything with no LVGL type
 * in its signature) moved to game_state.h/.c, which compiles without
 * lvgl.h; see the comment at the top of game_state.h for why. What's
 * left here and in game.c is genuinely LVGL-shaped and doesn't belong
 * in a pure domain header: color math returning lv_color_t, and the
 * bridge that owns the 3 lv_timer_t objects game_state.c requests
 * scheduling for through game_hooks.h. */
#include "types.h"
#include "game_state.h"

// ---------- player colors ----------
extern lv_color_hsv_t player_custom_hsv[MAX_DISPLAY_PLAYERS];

lv_color_t get_player_color_vib(int index, int vibrancy);
lv_color_t get_player_base_color(int index);
lv_color_t get_player_active_color(int index);
lv_color_t get_player_text_color(int index);
lv_color_t get_player_preview_color(int index, int delta);
lv_color_t get_custom_color_vib(lv_color_hsv_t hsv, int vibrancy);
lv_color_t get_effective_player_color(int player_i, int color_i, int vibrancy);

/* Wires game_state.c's game_hooks up to the real UI refresh functions
 * and creates the 3 lv_timer_t objects those hooks schedule. Call once
 * at boot, after every screen this refreshes exists - see knob_gui()
 * in knob.c. */
void game_bridge_init(void);

#endif // _GAME_H
