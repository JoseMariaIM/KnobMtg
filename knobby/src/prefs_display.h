#ifndef _PREFS_DISPLAY_H
#define _PREFS_DISPLAY_H

/* How the device looks and how it answers the knob: the screen, the
 * colours, the language, and how long a selection stays lit. See
 * prefs.h. */

#include "prefs.h"

int  prefs_get_brightness(void);
void prefs_set_brightness(int value);
int  prefs_get_auto_dim(void);
void prefs_set_auto_dim(int value);

int  prefs_get_color_mode(void);
void prefs_set_color_mode(int value);
int  prefs_get_orientation(void);
void prefs_set_orientation(int value);
int  prefs_get_display_rotation(void);
void prefs_set_display_rotation(int value);
int  prefs_get_menu_facing(void);
void prefs_set_menu_facing(int value);
int  prefs_get_language(void);
void prefs_set_language(int value);

int  prefs_get_deselect_timeout(void);
void prefs_set_deselect_timeout(int value);

#endif // _PREFS_DISPLAY_H
