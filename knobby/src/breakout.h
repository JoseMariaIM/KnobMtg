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
void breakout_test_ball(int *x, int *y);
/* True while the ball is riding the paddle waiting to be released - the
   state a player reads before aiming their serve. */
bool breakout_test_ball_parked(void);

#endif // _BREAKOUT_H
