#ifndef _TEMPEST_H
#define _TEMPEST_H

#include "types.h"

extern lv_obj_t *screen_tempest;

void build_tempest_screen(void);
void open_tempest_screen(void);
void tempest_turn(int dir);      /* knob handler: moves along the rim */
void tempest_handle_tap(void);   /* fires down the lane */
void tempest_leave_screen(void);

/* ---------- read-only accessors (unit tests) ---------- */
int  tempest_test_lane(void);        /* which lane the claw sits in */
int  tempest_test_lane_count(void);
int  tempest_test_enemies(void);
int  tempest_test_lives(void);
int  tempest_test_level(void);
int  tempest_test_shots(void);
/* Lane of the enemy nearest the rim, and how far up the well it has
   climbed as a percentage (100 = at the rim). */
bool tempest_test_closest_enemy(int *lane, int *climb_pct);

#endif // _TEMPEST_H
