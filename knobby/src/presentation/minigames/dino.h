#ifndef _DINO_H
#define _DINO_H

#include "../../types.h"

// ---------- state ----------
extern lv_obj_t *screen_dino;

// ---------- functions ----------
void build_dino_screen(void);
void open_dino_screen(void);
void dino_turn(int dir);      /* knob handler, screen_dino only: either direction jumps */
void dino_handle_tap(void);   /* tap handler: start / jump / restart */
void dino_leave_screen(void); /* called from back-navigation: freezes the game loop */

/* ---------- read-only accessors (unit tests) ----------
 * The runner's state lives in file statics; a test that wants to play a
 * run (jump over a cactus, duck nothing, pass under a bird) needs to
 * see where the next obstacle is to time its input, the same way a
 * player reads the screen. See sim/tests/test_dino.c. */
typedef enum {
    DINO_TEST_READY = 0,
    DINO_TEST_PLAYING,
    DINO_TEST_GAME_OVER,
} dino_test_state_t;

dino_test_state_t dino_test_state(void);
int dino_test_score(void);
bool dino_test_grounded(void);
/* Current scroll speed in px/tick - a test timing a jump needs it for
   the same reason a player needs to see how fast things are coming. */
float dino_test_speed(void);
/* Nearest obstacle not yet fully behind the runner. Returns false when
   there is none; otherwise fills the horizontal gap in px from the
   runner's front edge to the obstacle's LEADING edge (negative while
   the two overlap) and whether it is a bird (hold) or a cactus (jump). */
bool dino_test_nearest_obstacle(int *gap_px, bool *is_bird);

#endif // _DINO_H
