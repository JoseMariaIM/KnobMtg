#ifndef _BREAKOUT_H
#define _BREAKOUT_H

#include "types.h"

extern lv_obj_t *screen_breakout;

void build_breakout_screen(void);
void open_breakout_screen(void);
void breakout_turn(int dir);     /* knob handler: slides the paddle */
void breakout_handle_tap(void);
void breakout_leave_screen(void);

/* ---------- read-only accessors (unit tests) ---------- */
int  breakout_test_paddle_x(void);   /* paddle centre */
int  breakout_test_lives(void);
int  breakout_test_bricks_left(void);
int  breakout_test_level(void);
/* Lowest ball in play - the one a paddle should be chasing. False when
   none is active (between a loss and the next serve). */
bool breakout_test_lowest_ball(int *x, int *y);
int  breakout_test_ball_count(void);
/* Widest horizontal gap between any two balls in play - how far a
   multiball has actually fanned out. */
int  breakout_test_ball_spread(void);
/* Balls in play that came from a split rather than a serve - the ones
   drawn in cyan, and the ones a dropped ball wipes out. */
int  breakout_test_spawned_count(void);
/* True while the ball is riding the paddle waiting to be released - the
   state a player reads before aiming their serve. */
bool breakout_test_ball_parked(void);
/* Milliseconds of iron ball left, 0 when not active. */
int  breakout_test_iron_ms(void);
/* How many power-up bricks the current wall still holds. */
int  breakout_test_special_bricks_left(void);

#endif // _BREAKOUT_H
