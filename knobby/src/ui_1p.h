#ifndef _UI_1P_H
#define _UI_1P_H

#include "types.h"

// ---------- screens ----------
extern lv_obj_t *screen_1p;
extern lv_obj_t *screen_select;
extern lv_obj_t *screen_damage;

// ---------- functions ----------
void build_main_screen(void);
void build_select_screen(void);
void build_damage_screen(void);

void refresh_main_ui(void);
void refresh_player_ui(void);
void refresh_select_ui(void);
/* ---------- read-only accessors (unit tests) ---------- */
/* Whether the Commander/Partner slot tabs are on screen. They appear
   only when one of the listed opponents fields a partner. */
bool select_test_slot_tabs_visible(void);
void refresh_damage_ui(void);

void open_select_screen(void);

#endif // _UI_1P_H
