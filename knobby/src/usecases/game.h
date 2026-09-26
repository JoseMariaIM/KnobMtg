#ifndef _GAME_H
#define _GAME_H

/* Public facade: every other file in the app keeps including "game.h"
 * exactly as before, unchanged. Behind it, the actual rules
 * (life/counters/elimination/Table Sync - everything with no LVGL type
 * in its signature) moved to game_state.h/.c, which compiles without
 * lvgl.h; see the comment at the top of game_state.h for why. What is
 * left here and in game.c is the one thing that is genuinely LVGL-
 * shaped and does not belong in a pure domain header: colour maths
 * returning lv_color_t.
 *
 * The bridge that owns the 3 lv_timer_t objects game_state.c asks to
 * have scheduled used to live here too. It is in game_bridge.c now -
 * it has to name the UI refresh functions, and this header is what
 * every screen includes, so keeping the two together meant game and
 * the UI each depended on the other. */
#include "../types.h"
#include "../entities/game_state.h"

// ---------- player colors ----------
extern lv_color_hsv_t player_custom_hsv[MAX_DISPLAY_PLAYERS];

lv_color_t get_player_color_vib(int index, int vibrancy);
lv_color_t get_player_base_color(int index);
lv_color_t get_player_active_color(int index);
lv_color_t get_player_text_color(int index);
lv_color_t get_player_preview_color(int index, int delta);
lv_color_t get_custom_color_vib(lv_color_hsv_t hsv, int vibrancy);
lv_color_t get_effective_player_color(int player_i, int color_i, int vibrancy);

#endif // _GAME_H
