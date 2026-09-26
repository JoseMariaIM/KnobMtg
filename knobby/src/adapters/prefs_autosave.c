#include "prefs_autosave.h"
#include "prefs.h"
#include "../types.h"

/* See prefs_autosave.h. */

static lv_timer_t *autosave_timer = NULL;

static void autosave_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    autosave_timer = NULL; /* one-shot: LVGL deletes it after this call */
    prefs_flush();
}

/* Called by prefs.c on every write. Restarting the timer rather than
   letting it run means a knob held down through twenty brightness
   steps still costs one flash write, at the end. */
static void autosave_arm(void)
{
    if (autosave_timer != NULL) {
        lv_timer_reset(autosave_timer);
        return;
    }
    autosave_timer = lv_timer_create(autosave_timer_cb, PREFS_AUTOSAVE_MS, NULL);
    if (autosave_timer != NULL) lv_timer_set_repeat_count(autosave_timer, 1);
}

void prefs_autosave_init(void)
{
    prefs_set_scheduler(autosave_arm);
}
