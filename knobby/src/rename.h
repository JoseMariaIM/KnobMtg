#ifndef _RENAME_H
#define _RENAME_H

#include "types.h"

extern lv_obj_t *screen_player_name;

void build_rename_screen(void);
void refresh_rename_ui(void);
void open_rename_screen(void);
void open_rename_all_screen(void);

void mru_select_next(void);
void mru_select_prev(void);
void name_screen_knob(int dir); /* dir<0 = left, dir>0 = right */
bool name_screen_handle_back(void);

/* ---------- test accessor ---------- */
/* The rename screen's own commit path, as taking a name from the list
   or the keyboard would reach it. Exists so a test can prove the
   persistence hangs off THIS path and not off some other write. */
void rename_test_apply(const char *name);

#endif // _RENAME_H
