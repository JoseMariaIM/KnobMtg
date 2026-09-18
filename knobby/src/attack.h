#ifndef _ATTACK_H
#define _ATTACK_H

#include "types.h"

/* What the attack resolves as. These are exclusive - one press, one
 * effect - which is why Lifelink is a mode here rather than the tick
 * box it used to be alongside Damage. It was never combinable with
 * anything else anyway: the old checkbox was hidden outside Damage
 * mode and only ever read in the Damage branch, so promoting it to a
 * mode loses no combination that existed.
 *
 * Heal is gone. The knob on the life screen already adds life to a
 * player directly, which is fewer taps than dragging an arrow and
 * picking a mode to do the same thing. */
typedef enum {
    ATTACK_MODE_DAMAGE = 0,
    ATTACK_MODE_LIFELINK,
    ATTACK_MODE_CMDR,
    ATTACK_MODE_INFECT,
    ATTACK_MODE_COUNT,
} attack_mode_t;

// ---------- screens ----------
extern lv_obj_t *screen_attack;

// ---------- functions ----------
void build_attack_screen(void);
/* Opens the Attack screen for a drag gesture from source's wedge onto
   target's. Both are player indices (0..MAX_DISPLAY_PLAYERS-1); the
   colour indices come with them because they are not the same thing in
   a two-player layout (see mp_panel_spec_t.color_index), and this
   screen paints both players in their own colours. */
void open_attack_screen(int source, int source_color,
                        int target, int target_color);
void change_attack_amount(int delta);

#endif // _ATTACK_H
