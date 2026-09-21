#ifndef _SIM_STUBS_H
#define _SIM_STUBS_H

#include <stdint.h>
#include <stdbool.h>

/* Controllable tick for LVGL — advance with sim_tick_advance() */
uint32_t sim_millis(void);
void sim_tick_advance(uint32_t ms);

/* Fill the event log with 40 random entries (--random-log fixture) */
void sim_populate_random_log(void);

/* Pre-populate the in-memory NVS store before prefs_init() runs.
 * This lets CLI flags control settings that knob_nvs.c reads at init. */
void sim_nvs_preset_i8(const char *key, int8_t value);
void sim_nvs_preset_i16(const char *key, int16_t value);
void sim_nvs_preset_u32(const char *key, uint32_t value);

/* How many times prefs.c has committed to "flash".
 *
 * Preferences are cached in RAM and written as one blob, so "did this
 * setting survive" and "how many erase cycles did it cost" are
 * different questions and both matter. This answers the second. */
unsigned sim_nvs_commit_count(void);

/* Controllable battery voltage for screenshots */
extern float sim_battery_voltage;

/* Physical display rotation (0-3, degrees = value * 90); set via
   display_apply_rotation(), consumed by the sim flush callbacks */
extern int sim_display_rotation;

/* Whether the panel is currently told to drive GRAM (true) or has had
   Display Off sent to it (false) - see scr_display_on()/scr_display_off()
   in hw.c's screen-blank state machine. Real hardware has no way to
   read this back; the simulator tracks it so a test can assert on it. */
extern bool sim_display_panel_on;

/* ESP32 attribute macros — empty on desktop */
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

#endif /* _SIM_STUBS_H */
