#ifndef _FLAPPY_H
#define _FLAPPY_H

#include "types.h"

extern lv_obj_t *screen_flappy;

void build_flappy_screen(void);
void open_flappy_screen(void);
void flappy_turn(int dir);      /* knob handler: either direction flaps */
void flappy_handle_tap(void);
void flappy_leave_screen(void);

/* ---------- read-only accessors (unit tests) ----------
 * See sim/tests/test_minigames.c: a test flies the bird by reading the
 * gap the way a player reads the screen. */
int  flappy_test_bird_y(void);
bool flappy_test_next_gap(int *dx, int *gap_center);

#endif // _FLAPPY_H
