#ifndef _UI_PARTNERS_H
#define _UI_PARTNERS_H

#include "../../../knob.h"

/* Which players field a partner commander.
 *
 * A game-setup choice rather than a preference, which is why it is
 * reached from Game Mode beside the player count and the starting
 * life. It lived in settings.c only because that file happened to own
 * build_quad_screen(); the result was that game_mode.c had to include
 * the whole settings subsystem to open one screen, and settings.c
 * included game_mode.h back. */

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t *screen_partners;

void build_partners_screen(void);
void open_partners_screen(void);
/* Knob flips pages, with wraparound - the same gesture that flips
   settings pages. False when the active screen is not this one. */
bool partners_knob_page(int dir);

/* ---------- read-only accessors (unit tests) ---------- */
int partners_test_page_count(void);
int partners_test_page(void);
/* The player a quarter stands for on the page being shown, or -1 for
   the "More" quarter and any past the end of the table. */
int partners_test_tile_player(int slot);
void partners_page_reset_for_test(void);

#ifdef __cplusplus
}
#endif

#endif // _UI_PARTNERS_H
