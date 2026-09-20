#ifndef _UI_LANGUAGE_H
#define _UI_LANGUAGE_H

#include "knob.h"

/* The language picker: one row per language, the current one
 * highlighted. Picking one persists it and restarts the device, since
 * every screen's text was built at boot. */

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t *screen_language_picker;

void build_language_picker_screen(void);
void open_language_picker_screen(void);

#ifdef __cplusplus
}
#endif

#endif // _UI_LANGUAGE_H
