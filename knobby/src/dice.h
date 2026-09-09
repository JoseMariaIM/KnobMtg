#ifndef _DICE_H
#define _DICE_H

#include "types.h"

// ---------- state ----------
extern lv_obj_t *screen_dice_menu;
extern lv_obj_t *screen_dice;
extern lv_obj_t *screen_coin;

// ---------- functions ----------
void build_dice_menu_screen(void);
void build_dice_screen(void);
void build_coin_screen(void);
void refresh_dice_ui(void);
void open_dice_menu_screen(void);
void open_dice_screen(void);
void open_coin_screen(void);
void change_dice_quantity(int delta); /* knob handler, screen_dice_menu only */

// event callbacks used in menu builders
void event_tool_dice(lv_event_t *e);
void event_tool_coin(lv_event_t *e);

#endif // _DICE_H
