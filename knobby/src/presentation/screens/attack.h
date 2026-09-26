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

/* Which seat this screen is acting for, asked while it is up. The
   drag that opens it starts on the attacker's own wedge, so the
   attacker is who is holding the device - that is the seat the screen
   faces when menu facing is on. -1 before the first attack. */
int attack_acting_player(void);

/* ---------- read-only accessors (unit tests) ---------- */
attack_mode_t attack_test_mode(void);
int attack_test_amount(void);
/* The label widget for a mode, so a test can check it lands inside the
   sector it names rather than on the black gap next to it. */
lv_obj_t *attack_test_mode_label(int mode);
/* The sector band's two radii and a mode's mid-angle, so a test can
   compare where things are DRAWN with where they are pressed. */
void attack_test_geometry(int *cx, int *cy, int *inner_r, int *outer_r);
int attack_test_mode_mid_deg(int mode);
/* The hub's own widgets - the amount, the Resolve word, the "who hits
   whom" row - so a test can check they stay inside the hub at their
   widest. The hub's radius is set by the amount's 116pt face; nothing
   in here may quietly outgrow it. */
lv_obj_t *attack_test_amount_label(void);
lv_obj_t *attack_test_resolve_label(void);
lv_obj_t *attack_test_source_label(void);

#endif // _ATTACK_H
