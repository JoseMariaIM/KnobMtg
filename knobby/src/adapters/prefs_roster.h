#ifndef _PREFS_ROSTER_H
#define _PREFS_ROSTER_H

/* Who is sitting at the table, and the pool of names to offer when
 * somebody renames a seat. See prefs.h. */

#include "prefs.h"
#include "types.h"

/* The players' own names, as last set from the rename screen or
 * adopted from another device at the table.
 *
 * Distinct from the name LIST below, which is the pool of remembered
 * names offered when renaming. This is who is actually sitting there,
 * and it survives a power cycle and a game reset - only a rename
 * changes it. */
#define PLAYER_NAME_COUNT MAX_GAME_PLAYERS
#define PLAYER_NAME_LEN   16
void prefs_get_player_names(char (*out)[PLAYER_NAME_LEN]);
void prefs_set_player_names(const char (*names)[PLAYER_NAME_LEN]);
/* True once a rename has been stored, so boot can tell "nobody has
   ever renamed anyone" from "everybody is called P1..P8 on purpose". */
bool prefs_has_player_names(void);

#define NAME_LIST_COUNT 10
#define NAME_LIST_LEN   16
void prefs_get_name_list(char (*out)[NAME_LIST_LEN]);
void prefs_set_name_list(const char (*list)[NAME_LIST_LEN]);

#endif // _PREFS_ROSTER_H
