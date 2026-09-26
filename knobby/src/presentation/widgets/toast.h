#ifndef _TOAST_H
#define _TOAST_H

#include "knob.h"

/* A self-dismissing banner on lv_layer_top().
 *
 * Renders above whatever screen is active, independent of which one
 * that is, which is why it is not owned by any screen. Rather than a
 * persistent icon - too easy to miss on a display this size - it
 * spells the message out in words; tapping it runs click_cb, which
 * owns navigating wherever that message points to.
 *
 * It lived in hw.c, whose job is the backlight, the battery and the
 * CPU clock. A banner is none of those, and dragging it along meant
 * the hardware layer included lang.h, wifi_ota.h and ui_wifi.h - a
 * driver depending on the UI above it. */

#ifdef __cplusplus
extern "C" {
#endif

/* Replaces any still-showing toast rather than stacking. */
void toast_show(const char *msg, lv_event_cb_t click_cb);
void toast_dismiss(void);

#ifdef __cplusplus
}
#endif

#endif // _TOAST_H
