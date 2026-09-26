#ifndef _QUAD_SCREEN_H
#define _QUAD_SCREEN_H

#include "../../../knob.h"

/* The four-quarter menu screen, which is how nearly every menu on this
 * device is laid out: settings pages, the tools menu, table sync, the
 * partners picker, the minigames launcher, the WiFi menu, game mode.
 *
 * It used to live in settings.c, which meant any module that wanted a
 * quad menu had to include settings.h - and settings.h drags in the
 * whole settings subsystem, closing a dependency cycle for the three
 * modules that only ever wanted this one function. It is a widget
 * builder, not a setting, so it sits on its own down here where the
 * screens that use it can depend on it and nothing depends back. */

#ifdef __cplusplus
extern "C" {
#endif

/* One quarter of the screen. Lives here rather than in types.h because
   this is the only function that has ever taken one. */
typedef struct {
    const char *label;
    lv_event_cb_t cb;
    bool enabled;
    lv_event_code_t event;
    const char *icon;
    const lv_font_t *icon_font;
    void *user_data;            /* passed to cb via lv_event_get_user_data */
} quad_item_t;

/* Tile backgrounds for an on/off state, shared by every screen built
   out of these tiles - settings toggles, the partner picker, the
   current row in the language list. They were settings.c statics, and
   the screens that moved out needed them too. */
#define TOGGLE_ON  0x1B5E20u   /* dark green */
#define TOGGLE_OFF 0x4A1010u   /* dark red */

void set_btn_color(lv_obj_t *btn, uint32_t color);

/* Creates *screen and fills it with four 178x178 tiles. A quarter with
   no cb, or with .enabled false, is drawn dimmed and non-clickable. */
void build_quad_screen(lv_obj_t **screen, quad_item_t items[4]);

#ifdef __cplusplus
}
#endif

#endif // _QUAD_SCREEN_H
