#ifndef _EGGS_H
#define _EGGS_H

#include "../../types.h"

extern lv_obj_t *screen_eggs;

void build_eggs_screen(void);
void open_eggs_screen(void);
void eggs_turn(int dir);        /* knob handler: slides the basket */
void eggs_handle_tap(void);
void eggs_leave_screen(void);

/* ---------- read-only accessors (unit tests) ---------- */
int  eggs_test_basket_x(void);  /* basket centre */
int  eggs_test_lives(void);
bool eggs_test_lowest_egg(int *x, int *y);

#endif // _EGGS_H
