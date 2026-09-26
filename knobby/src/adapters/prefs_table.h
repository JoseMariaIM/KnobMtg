#ifndef _PREFS_TABLE_H
#define _PREFS_TABLE_H

/* The shape of the game on the table: how many are playing, how many
 * this device is tracking, what everyone started on, and the two rules
 * that change how a game ends and how it begins. See prefs.h. */

#include "prefs.h"

int  prefs_get_num_players(void);
void prefs_set_num_players(int value);
int  prefs_get_players_to_track(void);
void prefs_set_players_to_track(int value);
int  prefs_get_life_total(void);
void prefs_set_life_total(int value);

int  prefs_get_auto_eliminate(void);
void prefs_set_auto_eliminate(int value);
int  prefs_get_random_first(void);
void prefs_set_random_first(int value);

/* Whether a life change addresses one player or several. Lives with
   the table rather than with the display: it is about how you address
   the seats, not about how they are drawn. */
int  prefs_get_multi_select(void);
void prefs_set_multi_select(int value);

/* One bit per player index: set means that player fields a partner
   commander, which is what makes every partner control on the device
   appear at all. */
int  prefs_get_partner_mask(void);
void prefs_set_partner_mask(int mask);

#endif // _PREFS_TABLE_H
