#ifndef _UI_CMD_DAMAGE_H
#define _UI_CMD_DAMAGE_H

/* Commander damage: the grid of opponents you can take commander
 * damage from, and the dial that records how much.
 *
 * These two screens used to live in ui_1p.c because that is where the
 * single-player life counter reaches them from, but they are not part
 * of the 1p screen: the player menu opens them for any seat at a four
 * player table. Keeping them here lets ui_player_menu depend on this
 * pair without dragging in - and being dragged back into - the main
 * life counter. */

#include "../../types.h"

// ---------- screens ----------
extern lv_obj_t *screen_select;
extern lv_obj_t *screen_damage;

// ---------- functions ----------
void build_select_screen(void);
void build_damage_screen(void);

void refresh_select_ui(void);
void refresh_damage_ui(void);

void open_select_screen(void);

/* ---------- read-only accessors (unit tests) ---------- */
/* Whether the Commander/Partner slot tabs are on screen. They appear
   only when one of the listed opponents fields a partner. */
bool select_test_slot_tabs_visible(void);

#endif // _UI_CMD_DAMAGE_H
