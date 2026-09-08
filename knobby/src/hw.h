#ifndef _HW_H
#define _HW_H

#ifdef __cplusplus
extern "C" {
#endif

#include "types.h"

// ---------- state ----------
extern int brightness_percent;
extern bool dimmed;
extern float battery_voltage;
extern int battery_percent;

// ---------- tunable power / timing constants (shared) ----------
// Auto-dim timeout is now configurable via NVS (see auto_dim_ms[] in knob_types.h).
// Reducing AUTO_DIM_BRIGHTNESS below 5 further cuts backlight draw while dimmed.
#define AUTO_DIM_BRIGHTNESS     5       /* % brightness while dimmed */
// UNDIM_GRACE_MS suppresses input for this long after wake to avoid accidental presses.
#define UNDIM_GRACE_MS          225     /* ms */
#define CPU_FREQ_ACTIVE         80      /* MHz – APB bus stays 80 MHz at 80/160/240, so
                                            SPI/QSPI display timing is unaffected; only
                                            CPU-bound work (LVGL rendering) takes longer. */
// Turning the knob or dragging a widget (color wheel, etc.) at 80MHz visibly
// lags behind the input - see activity_kick(). Boosted only for the
// duration of actual interaction, then dropped back to CPU_FREQ_ACTIVE
// CPU_BOOST_IDLE_MS after the last activity, so the cost is paid only
// while the backlight/touch/knob are already active anyway.
#define CPU_FREQ_BOOST          160     /* MHz while actively turning/dragging */
#define CPU_BOOST_IDLE_MS       400     /* ms of no input before dropping back down */
// Battery sample throttle: how often a fresh ADC measurement is allowed.
#define BATTERY_SAMPLE_INTERVAL_MS  60000   /* ms between passive battery measurements */
// Auto-dim check period: how often the inactivity timer fires.
#define AUTO_DIM_CHECK_PERIOD_MS    1000    /* ms */
#define LOW_BATTERY_VOLTAGE         3.35f   /* shutdown threshold (matches 0% curve) */
#define LOW_BATTERY_COUNT           3       /* consecutive low readings before cutoff */
#define LOW_BATTERY_WAKE_US         (15ULL * 1000000ULL)  /* deep sleep wake interval */
#define LOW_BATTERY_INDICATOR_PCT   10      /* show solid icon below this percent */
#define LOW_BATTERY_BLINK_PCT       5       /* blink the icon below this percent */
#define BATTERY_BLINK_PERIOD_MS     500     /* low-battery icon blink cadence */
// While battery is comfortably above LOW_BATTERY_INDICATOR_PCT (nearly
// always) the icon timer has nothing to show and nothing to blink - no
// reason to wake the CPU from light sleep 2x/second for that. It only
// needs BATTERY_BLINK_PERIOD_MS cadence once it's actually blinking.
#define BATTERY_CHECK_IDLE_PERIOD_MS 5000   /* ms, while icon is hidden */

// ---------- functions ----------
void knob_hw_init(void);
void brightness_apply(void);
void update_battery_measurement(bool force);
int read_battery_percent(void);
void change_brightness(int delta);
bool in_undim_grace(void);
void knob_enter_deep_sleep(void);
void battery_icon_register(lv_obj_t *icon);
void battery_icon_unregister(lv_obj_t *icon);

#ifdef __cplusplus
}
#endif

#endif // _HW_H
