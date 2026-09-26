#ifndef _UI_BATTERY_H
#define _UI_BATTERY_H

#include "../../../knob.h"

/* The battery screen: one reading, one calibration line.
 *
 * Reached from Settings, but it is not a setting - nothing here is
 * adjustable. It reads what hw.c measured and draws it, which makes it
 * a view over the power service rather than part of the settings
 * subsystem it used to share a file with. */

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t *screen_battery;

void build_battery_screen(void);
/* Forces a fresh sample before showing: the reading on screen should be
   the one taken when the user asked for it, not whenever the periodic
   sampler last ran. */
void open_battery_screen(void);
void refresh_battery_ui(void);

#ifdef __cplusplus
}
#endif

#endif // _UI_BATTERY_H
