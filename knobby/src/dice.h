#ifndef _DICE_H
#define _DICE_H

#include "types.h"

// ---------- state ----------
extern lv_obj_t *screen_dice_menu;
extern lv_obj_t *screen_dice;

// ---------- functions ----------
void build_dice_menu_screen(void);
void build_dice_screen(void);
void refresh_dice_ui(void);
void open_dice_menu_screen(void);
void open_dice_screen(void);

// event callback used in menu builder
void event_tool_dice(lv_event_t *e);

#endif // _DICE_H
