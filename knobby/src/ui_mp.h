#ifndef _UI_MP_H
#define _UI_MP_H

#include "types.h"

// ---------- screens ----------
extern lv_obj_t *screen_multiplayer;
extern lv_obj_t *screen_victory;

// ---------- functions ----------
void build_multiplayer_screen(void);
void build_victory_screen(void);
void rebuild_multiplayer_layout(int track);

void refresh_multiplayer_ui(void);

/* True from the moment the victory fade starts until the next reset -
   lets navigation helpers (back_to_main()) avoid loading over it while
   its transition animation is still in flight. See the comment on
   back_to_main() for why that matters. */
bool mp_victory_active(void);

int mp_player_seat_rotation(int player);

void select_kick_timer(void);

#endif // _UI_MP_H
