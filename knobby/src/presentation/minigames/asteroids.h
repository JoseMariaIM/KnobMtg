#ifndef _ASTEROIDS_H
#define _ASTEROIDS_H

#include "../../types.h"

extern lv_obj_t *screen_asteroids;

void build_asteroids_screen(void);
void open_asteroids_screen(void);
void asteroids_turn(int dir);     /* knob handler: rotates the ship */
void asteroids_handle_tap(void);  /* fires; hold to thrust */
void asteroids_leave_screen(void);

/* ---------- read-only accessors (unit tests) ---------- */
int  asteroids_test_heading(void);      /* degrees, 0 = 3 o'clock */
int  asteroids_test_rocks(void);
int  asteroids_test_lives(void);
int  asteroids_test_wave(void);
int  asteroids_test_shots(void);
bool asteroids_test_ship(int *x, int *y);
/* Bearing from the ship to the nearest rock, and its distance - what a
   test aims with, the same read a player makes. */
bool asteroids_test_nearest_rock(int *bearing_deg, int *distance);

#endif // _ASTEROIDS_H
