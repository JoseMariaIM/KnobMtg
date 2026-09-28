#ifndef _UI_DEVICE_NAME_H
#define _UI_DEVICE_NAME_H

#include "../../types.h"

/* Naming the device itself (not a seat at the table - that is
 * rename.c). Two ways in:
 *
 * - From Settings, any time, to change the name.
 * - Once, at the end of the first boot after the device is flashed or
 *   factory reset, because a unit with no name is the one state where
 *   asking is better than defaulting silently. intro.c owns that call
 *   and hands over what to run afterwards, so the rest of the boot tail
 *   (life counter, first-player roll, "just updated" toast) happens
 *   after the question is answered instead of racing it.
 *
 * Pressing Enter with the field empty is always a valid answer: on
 * first boot it accepts the default name derived from hw_device_id(),
 * and from Settings it leaves the current name alone. Nobody can end
 * up with a nameless device or be trapped on this screen. */

extern lv_obj_t *screen_device_name;

void build_device_name_screen(void);
void open_device_name_screen(void);

/* First-boot entry. on_done runs once a name is settled, whichever way
   the user settled it. */
void device_name_open_first_boot(void (*on_done)(void));

/* Where to go after a name is saved from Settings (Enter on the
   keyboard has no other way back). Bound once at boot from knob.c, the
   same arrangement as rename_set_return_hook() - this file names no
   settings page of its own. */
void device_name_set_return_hook(void (*fn)(void));

/* Knob moves the text cursor - the keyboard has no arrow keys, same as
   the rename screen (see custom_keyboard.h). */
void device_name_screen_knob(int dir);

/* True when the back gesture was answered here, which only happens
   during the first-boot question: back then means "I do not want to
   type one", so it stores the default name and continues the boot
   instead of navigating anywhere. From Settings this returns false and
   back unwinds to the settings page like every other sub-screen. */
bool device_name_handle_back(void);

/* Test-only: the commit path the keyboard's Enter key takes, without an
   LVGL event. NULL or "" is the empty-field answer. Same precedent as
   rename_test_apply() in rename.h. */
void device_name_test_apply(const char *name);
/* Test-only: the field the keyboard types into, for asserting what the
   screen opened with. NULL when the screen is not built. */
lv_obj_t *device_name_test_textarea(void);

#endif // _UI_DEVICE_NAME_H
