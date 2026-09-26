#ifndef _PREFS_H
#define _PREFS_H

/* Settings that outlive a power cycle, and when they are written.
 *
 * The values themselves are split across prefs_display.h,
 * prefs_table.h, prefs_roster.h, prefs_network.h and prefs_scores.h -
 * one header per thing a module might legitimately care about, so a
 * minigame that wants a high score does not also get handed the WiFi
 * password. They all share one implementation (prefs.c) and one
 * blob in flash; the split is about who can reach what, not about
 * where the bytes live.
 *
 * Writing is nobody's business but prefs.c's. Every setter marks the
 * cache dirty and asks the scheduler for a flush, so a caller can
 * write a preference and stop thinking about it. It used to be the
 * other way round - the setter only marked, and twelve modules were
 * each expected to remember a settings_save() afterwards. Some did it
 * on the way out of a screen, which put the decision in navigation
 * code; ota_notice.c had to call it by hand because nothing else
 * would; and net_sync_apply_names() never did, so a name that arrived
 * from another device at the table showed up on screen and was gone by
 * the next power cycle. */

void prefs_init(void);

/* Commit right now. Only for the moments that are about to lose RAM:
   a reboot, deep sleep, an OTA flash. */
void prefs_flush(void);

/* Installed once at boot by prefs_autosave.c. prefs.c calls arm() on
   every write; the scheduler is expected to call prefs_flush() once
   the writes stop. Nothing is written until one is installed, which
   is what unit tests want - they flush explicitly. */
void prefs_set_scheduler(void (*arm)(void));

#endif // _PREFS_H
