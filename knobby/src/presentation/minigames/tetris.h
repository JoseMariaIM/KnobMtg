#ifndef _TETRIS_H
#define _TETRIS_H

#include "types.h"

extern lv_obj_t *screen_tetris;

void build_tetris_screen(void);
void open_tetris_screen(void);
void tetris_turn(int dir);       /* knob handler: rotates the falling piece */
void tetris_handle_tap(void);    /* screen: left / right / drop by tap zone */
void tetris_leave_screen(void);

/* ---------- read-only accessors (unit tests) ---------- */
int  tetris_test_lines(void);
int  tetris_test_level(void);
int  tetris_test_rotation(void);
int  tetris_test_piece_col(void);
int  tetris_test_filled_cells(void);
int  tetris_test_stack_height(void);
/* Drives a tap zone without a real touch device - the zone the point
   falls in is the only thing tetris_handle_tap() reads. */
void tetris_test_tap_at(int x);

#endif // _TETRIS_H
