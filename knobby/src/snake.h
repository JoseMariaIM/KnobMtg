#ifndef _SNAKE_H
#define _SNAKE_H

#include "types.h"

// ---------- state ----------
extern lv_obj_t *screen_snake;

// ---------- functions ----------
void build_snake_screen(void);
void open_snake_screen(void);
void snake_turn(int dir);      /* knob handler, screen_snake only: -1 left, +1 right */
void snake_handle_tap(void);   /* tap handler: start / pause / resume / restart */
void snake_leave_screen(void); /* called from back-navigation: freezes the game loop */

#endif // _SNAKE_H
