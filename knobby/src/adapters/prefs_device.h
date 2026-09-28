#ifndef _PREFS_DEVICE_H
#define _PREFS_DEVICE_H

/* What this particular unit is called, as its owner named it. See
 * prefs.h.
 *
 * Distinct from the player names in prefs_roster.h: those are the seats
 * around a table and travel between devices over Table Sync, this one
 * belongs to the hardware and never leaves it. It exists because
 * several of these end up in different people's hands, and "Knobby" on
 * its own stops being an answer to which one is in front of you.
 *
 * Deliberately NOT the identity the update channel targets - that is
 * hw_device_id(), derived from the MAC, precisely because this one is
 * editable by whoever is holding the device. */

#include "prefs.h"
#include <stddef.h>
#include <stdbool.h>

#define DEVICE_NAME_LEN 16
void prefs_get_device_name(char *out, size_t out_len);
void prefs_set_device_name(const char *name);
/* False until a name has been stored, which is what makes the naming
   step run exactly once: on the very first boot, and again after a
   factory reset. An empty string counts as unset. */
bool prefs_has_device_name(void);

#endif // _PREFS_DEVICE_H
