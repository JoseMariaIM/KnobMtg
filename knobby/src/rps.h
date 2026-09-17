#ifndef _RPS_H
#define _RPS_H

#include "types.h"

extern lv_obj_t *screen_rps;

void build_rps_screen(void);
void open_rps_screen(void);
void rps_turn(int dir);        /* knob handler: cycles the throw */
void rps_handle_tap(void);     /* throws */
void rps_leave_screen(void);

/* ---------- read-only accessors (unit tests) ---------- */
typedef enum { RPS_ROCK = 0, RPS_PAPER, RPS_SCISSORS, RPS_THROW_COUNT } rps_throw_t;

rps_throw_t rps_test_choice(void);
bool        rps_test_revealing(void);
rps_throw_t rps_test_opponent(void);
/* -1 loss, 0 draw, +1 win for the throw currently on screen. */
int         rps_test_verdict(void);
/* Pure rule, exposed so the win table can be checked exhaustively. */
int         rps_beats(rps_throw_t mine, rps_throw_t theirs);

#endif // _RPS_H
