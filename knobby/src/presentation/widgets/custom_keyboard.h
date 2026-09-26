#ifndef _CUSTOM_KEYBOARD_H
#define _CUSTOM_KEYBOARD_H

#include "types.h"

/* A 4-row on-screen keyboard shaped to fit this device's round display:
 * each row is only as wide as the circle actually allows at its height,
 * instead of a rectangular grid whose corners get clipped by the glass
 * (see ui_wifi.c's history for what that looked like). Drop-in
 * replacement for lv_keyboard_create()+lv_keyboard_set_textarea() with
 * the same full character set (letters, digits, punctuation, symbols,
 * backspace, cursor movement, space, enter) plus Spanish n with a
 * tilde when the active language is Spanish.
 *
 * IMPORTANT: LVGL's lv_keyboard mode maps (LV_KEYBOARD_MODE_USER_1..4)
 * are GLOBAL, not per-widget - every custom_keyboard_t instance reuses
 * the same four slots for its four rows. That's safe because only one
 * keyboard is ever visible at a time, but it means every screen that
 * owns one MUST call custom_keyboard_reset() when it opens (not just
 * once at build time), so its own content wins those slots back from
 * whichever keyboard used them last. */
typedef struct {
    lv_obj_t *backdrop; /* face panel filling the circle behind the rows */
    lv_obj_t *row_top;
    lv_obj_t *row_home;
    lv_obj_t *row_bottom;
    /* Standalone buttons - shift/backspace claim the leftover width next
       to row_bottom's safe rectangle, mode-switch/Enter/space do the
       same for row_control's (see custom_keyboard.c's CORNER_*_W
       comment). There's no on-screen cursor-left/right anymore - the
       knob does that job instead (see wifi_text_entry_knob() /
       name_screen_knob()) - those keys were both hard to hit and
       unreliable to tap; see custom_keyboard_build()'s history. */
    lv_obj_t *btn_shift;
    lv_obj_t *btn_backspace;
    lv_obj_t *btn_mode;
    lv_obj_t *btn_enter;
    lv_obj_t *btn_space;
    lv_obj_t *ta; /* target textarea, cached for the standalone buttons' own handlers */
    int mode; /* kb_mode_t, opaque to callers */
} custom_keyboard_t;

/* Creates the four row widgets as children of `parent`, sized and
 * positioned to fit the round display, starting in lowercase-letters
 * mode. Caller still owns layout of everything else on the screen. */
void custom_keyboard_build(custom_keyboard_t *kb, lv_obj_t *parent);

/* Binds every row to the same target textarea - typing on any row
 * inserts into `ta`, exactly like lv_keyboard_set_textarea(). */
void custom_keyboard_set_textarea(custom_keyboard_t *kb, lv_obj_t *ta);

/* Forwards LV_EVENT_READY from the Enter key to `cb`, exactly like
 * adding an LV_EVENT_READY callback to an lv_keyboard would. */
void custom_keyboard_set_ready_cb(custom_keyboard_t *kb, lv_event_cb_t cb);

/* Back to lowercase-letters mode and re-applies this keyboard's own
 * row content - see the global-mode-slots note above for why this must
 * be called every time the owning screen opens, not just once. */
void custom_keyboard_reset(custom_keyboard_t *kb);

/* Shows/hides all four rows together - for screens that toggle a
 * keyboard in and out alongside other content (e.g. a saved-names
 * list vs. free text entry on the same screen). */
void custom_keyboard_set_hidden(custom_keyboard_t *kb, bool hidden);

#endif // _CUSTOM_KEYBOARD_H
