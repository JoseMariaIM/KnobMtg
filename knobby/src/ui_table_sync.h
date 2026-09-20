#ifndef _UI_TABLE_SYNC_H
#define _UI_TABLE_SYNC_H

#include "knob.h"

/* The Table Sync screen: start or re-invite, join, leave, and a status
 * tile that tracks pairing while the screen is up.
 *
 * A view over net_sync, reached from Settings. Keeping it in
 * settings.c was what made settings.c include net_sync.h. */

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t *screen_table_sync;

void build_table_sync_screen(void);
void open_table_sync_screen(void);
void refresh_table_sync_ui(void);

#ifdef __cplusplus
}
#endif

#endif // _UI_TABLE_SYNC_H
