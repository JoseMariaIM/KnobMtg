#ifndef _PONG_H
#define _PONG_H

#include "types.h"

// ---------- state ----------
extern lv_obj_t *screen_pong;

// ---------- functions ----------
void build_pong_screen(void);
void open_pong_screen(void);
void pong_turn(int dir);      /* knob handler, screen_pong only: -1 left, +1 right */
void pong_handle_tap(void);   /* tap handler: start / pause / resume / restart */
void pong_leave_screen(void); /* called from back-navigation: freezes the game loop */

#endif // _PONG_H
