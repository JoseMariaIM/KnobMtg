#ifndef _ATTACK_H
#define _ATTACK_H

#include "types.h"

typedef enum {
    ATTACK_MODE_DAMAGE = 0,
    ATTACK_MODE_HEAL,
    ATTACK_MODE_CMDR,
    ATTACK_MODE_INFECT,
    ATTACK_MODE_COUNT,
} attack_mode_t;

// ---------- screens ----------
extern lv_obj_t *screen_attack;

// ---------- functions ----------
void build_attack_screen(void);
/* Opens the Attack screen for a drag gesture from source's wedge onto
   target's. Both are player indices (0..MAX_DISPLAY_PLAYERS-1). */
void open_attack_screen(int source, int target);
void change_attack_amount(int delta);

#endif // _ATTACK_H
