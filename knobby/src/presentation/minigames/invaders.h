#ifndef _INVADERS_H
#define _INVADERS_H

#include "../../types.h"

extern lv_obj_t *screen_invaders;

void build_invaders_screen(void);
void open_invaders_screen(void);
void invaders_turn(int dir);     /* knob handler: slides the ship */
void invaders_handle_tap(void);  /* fires */
void invaders_leave_screen(void);

/* ---------- read-only accessors (unit tests) ---------- */
int  invaders_test_ship_x(void);
int  invaders_test_aliens_left(void);
int  invaders_test_lives(void);
int  invaders_test_wave(void);
int  invaders_test_shots_in_flight(void);
/* True while a slot is free - the question a player actually asks. */
bool invaders_test_can_fire(void);
/* Centre of the lowest surviving alien - what a test aims at. */
bool invaders_test_lowest_alien(int *x, int *y);

#endif // _INVADERS_H
