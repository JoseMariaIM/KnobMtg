#ifndef _UI_FACTORY_RESET_H
#define _UI_FACTORY_RESET_H

#include "../../types.h"

/* The confirmation in front of prefs_factory_reset(). A settings row
 * that wiped the device the moment it was tapped would be one mis-tap
 * away from destroying everything, so the row only opens this screen,
 * which spells out what goes and asks for a long press - the same
 * hold-to-confirm the general reset and "Forget WiFi" already use.
 *
 * Erasing is irreversible and leaves every already-built screen holding
 * values that no longer exist, so this reboots straight afterwards
 * rather than trying to repaint the running UI. */

extern lv_obj_t *screen_factory_reset;

void build_factory_reset_screen(void);
void open_factory_reset_screen(void);

#endif // _UI_FACTORY_RESET_H
