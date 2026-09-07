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

#endif // _RENAME_H
