#ifndef _BREAKOUT_H
#define _BREAKOUT_H

#include "types.h"

extern lv_obj_t *screen_breakout;

void build_breakout_screen(void);
void open_breakout_screen(void);
void breakout_turn(int dir);     /* knob handler: slides the paddle round the rim */
void breakout_handle_tap(void);
void breakout_leave_screen(void);

/* ---------- read-only accessors (unit tests) ----------
 * The arena is circular, so a test aims the way a player does: compare
 * the paddle's angle with the angle of whichever ball is closest to the
 * rim, and turn toward it. */
int  breakout_test_paddle_angle(void);   /* degrees, 0 = 3 o'clock, clockwise */
int  breakout_test_lives(void);
int  breakout_test_bricks_left(void);
int  breakout_test_level(void);
/* The ball nearest the rim - the one the paddle has to be under.
   False when none is in play (between a loss and the next serve). */
bool breakout_test_outermost_ball(int *angle_deg, int *radius);
/* Same ball, as position and velocity, so a test can extrapolate where
   it will reach the rim. Chasing its CURRENT angle is not enough: a
   ball crossing near the centre sweeps through most of a circle in a
   few ticks, and a paddle following that is always in the wrong place
   when it arrives. A player reads the line, not the dot. */
bool breakout_test_outermost_ball_motion(float *x, float *y,
                                         float *vx, float *vy);
int  breakout_test_ball_count(void);
/* Widest angular gap between any two balls in play, in degrees - how
   far a multiball has actually fanned out. */
int  breakout_test_ball_spread(void);
/* Balls in play that came from a split rather than a serve - the ones
   drawn in cyan, and the ones a dropped ball wipes out. */
int  breakout_test_spawned_count(void);
/* Ball-on-ball collisions resolved so far this run. */
int  breakout_test_ball_bounces(void);
/* Balls currently burning through bricks - one at most, by design. */
int  breakout_test_iron_ball_count(void);
/* Distance between the closest two ball centres, -1 with fewer than
   two balls in play. */
int  breakout_test_min_ball_gap(void);
/* True while the ball is riding the paddle waiting to be released. */
bool breakout_test_ball_parked(void);
/* Milliseconds of iron ball left, 0 when not active. */
int  breakout_test_iron_ms(void);
/* How many power-up bricks the current wall still holds. */
int  breakout_test_special_bricks_left(void);
/* The radii the physics uses: the rim past which a ball is lost, and
   the paddle's inner face, which is the surface it bounces off. A test
   reads these to check the paddle is PAINTED across the same band it
   is collided against - the two are computed in different places. */
void breakout_test_arena_radii(int *cx, int *cy, int *rim_r, int *paddle_face_r);
/* Inner and outer radius, and centre angle, of one brick - the same
   bounds the collision test uses. Returns false for a broken brick. */
bool breakout_test_brick_bounds(int idx, int *inner, int *outer, int *centre_deg);
int  breakout_test_brick_count(void);

#endif // _BREAKOUT_H
