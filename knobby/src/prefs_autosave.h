#ifndef _PREFS_AUTOSAVE_H
#define _PREFS_AUTOSAVE_H

/* The clock behind prefs.h's write scheduler.
 *
 * Preferences are cached in RAM and written to flash as one blob, so
 * writing on every setter would burn erase cycles for a knob turn that
 * is still in progress. This waits for the writes to stop, then
 * commits once - the same batching the old manual settings_save() gave
 * us, minus the requirement that twelve callers remember it.
 *
 * A one-shot timer, armed per write rather than a periodic poll: on
 * this device a timer that ticks forever is a wake source forever, and
 * light-sleep wake frequency is what the idle power budget is made of. */

void prefs_autosave_init(void);

/* The quiet period, exposed so a test can reason about it. */
#define PREFS_AUTOSAVE_MS 1500

#endif // _PREFS_AUTOSAVE_H
