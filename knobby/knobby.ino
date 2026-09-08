#include "esp_wifi.h"
#include "esp_bt.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "soc/usb_serial_jtag_struct.h"
#include <string.h>

#include "board_detect.h"
#include "scr_st77916.h"
#include <lvgl.h>
#include "hal/lv_hal.h"
#include "knob.h"
#include "src/hw.h"
#include "src/wifi_ota.h"
#include "knobby_net.h"

static const float BATTERY_DIVIDER_RATIO = 2.0f;
static const float BATTERY_CALIBRATION_SCALE = 1.0f;
static const float BATTERY_CALIBRATION_OFFSET = 0.0f;
static float battery_voltage_filtered = 0.0f;
static bool battery_voltage_has_value = false;

extern "C" float knob_read_battery_voltage(void)
{
  uint32_t millivolts = 0;
  uint32_t sum = 0;
  uint32_t min_sample = UINT32_MAX;
  uint32_t max_sample = 0;
  const int sample_count = 16;
  float measured_voltage = 0.0f;

  analogSetPinAttenuation(BATTERY_ADC_PIN_NUM, ADC_11db);
  for (int i = 0; i < sample_count; i++) {
    uint32_t sample = analogReadMilliVolts(BATTERY_ADC_PIN_NUM);
    sum += sample;
    if (sample < min_sample) min_sample = sample;
    if (sample > max_sample) max_sample = sample;
    delayMicroseconds(250);
  }

  sum -= min_sample;
  sum -= max_sample;
  millivolts = sum / (sample_count - 2);
  if (millivolts == 0) {
    return 0.0f;
  }

  measured_voltage = (((float)millivolts * BATTERY_DIVIDER_RATIO) / 1000.0f);
  measured_voltage = (measured_voltage * BATTERY_CALIBRATION_SCALE) + BATTERY_CALIBRATION_OFFSET;

  if (!battery_voltage_has_value) {
    battery_voltage_filtered = measured_voltage;
    battery_voltage_has_value = true;
  } else {
    battery_voltage_filtered = (battery_voltage_filtered * 0.7f) + (measured_voltage * 0.3f);
  }

  return battery_voltage_filtered;
}

// ---------- crash diagnostics ----------
// Regular RAM is cleared on every reset, so a crash (panic, task
// watchdog, brownout...) leaves no trace of itself once it reboots -
// which is exactly the "resets by itself, no idea why" report this is
// for. RTC memory survives any reset that doesn't fully cut power, so a
// small ring of (reset reason, how long the previous run lasted) pairs
// kept there kept there is still readable after several such resets,
// even if nobody had a serial monitor open at the exact moment it
// happened - only a full battery pull/power-on resets it back to
// "no history yet". Printed at the very top of setup(), before anything
// that could itself crash, so the read of the PREVIOUS crash always
// makes it out over serial before any new one.
#define RESET_HISTORY_LEN   6
#define RESET_HISTORY_MAGIC 0x4B4E4232UL /* 'KNB2' - bump if this layout changes */
RTC_NOINIT_ATTR static uint32_t rtc_magic;
RTC_NOINIT_ATTR static uint32_t rtc_boot_count;
RTC_NOINIT_ATTR static uint32_t rtc_last_alive_ms;
RTC_NOINIT_ATTR static uint8_t  rtc_reset_reason[RESET_HISTORY_LEN];
RTC_NOINIT_ATTR static uint32_t rtc_alive_ms[RESET_HISTORY_LEN];

static const char *reset_reason_name(esp_reset_reason_t reason)
{
  switch (reason) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT_PIN";
    case ESP_RST_SW:        return "SW_RESET";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WATCHDOG";
    case ESP_RST_TASK_WDT:  return "TASK_WATCHDOG";
    case ESP_RST_WDT:       return "OTHER_WATCHDOG";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP_WAKE";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
  }
}

static void record_and_print_reset_history(void)
{
  esp_reset_reason_t reason = esp_reset_reason();
  int i;

  if (rtc_magic != RESET_HISTORY_MAGIC) {
    rtc_magic = RESET_HISTORY_MAGIC;
    rtc_boot_count = 0;
    rtc_last_alive_ms = 0;
    memset(rtc_reset_reason, 0, sizeof(rtc_reset_reason));
    memset(rtc_alive_ms, 0, sizeof(rtc_alive_ms));
  } else {
    for (i = RESET_HISTORY_LEN - 1; i > 0; i--) {
      rtc_reset_reason[i] = rtc_reset_reason[i - 1];
      rtc_alive_ms[i] = rtc_alive_ms[i - 1];
    }
  }
  rtc_reset_reason[0] = (uint8_t)reason;
  rtc_alive_ms[0] = rtc_last_alive_ms; /* how long the run that just ended lasted */
  rtc_boot_count++;
  rtc_last_alive_ms = 0;

  Serial.begin(115200);
  delay(50); /* give a native-USB host a moment to enumerate before we print */
  Serial.println();
  Serial.println(F("=== Knobby reset history (most recent first) ==="));
  Serial.printf("Boot #%lu - this boot's cause: %s\n", (unsigned long)rtc_boot_count, reset_reason_name(reason));
  for (i = 0; i < RESET_HISTORY_LEN; i++) {
    if (i == 0 && rtc_boot_count == 1) break; /* fresh history, nothing to show yet */
    Serial.printf("  [%d] %-14s ran %lu ms before this reset\n", i,
                  reset_reason_name((esp_reset_reason_t)rtc_reset_reason[i]),
                  (unsigned long)rtc_alive_ms[i]);
  }
  Serial.println(F("=================================================="));
}

void setup()
{
  record_and_print_reset_history();

  // Detect which board we're running on before any pin-dependent init
  board_detect();

  // Early low-battery check: take 3 readings 50ms apart to confirm
  // genuinely low voltage before sleeping.  Catches deep-sleep timer
  // wakes and power-cycles after a safety shutdown, while a single
  // noisy ADC reading on a healthy battery won't prevent boot.
  {
    int low_count = 0;
    for (int i = 0; i < LOW_BATTERY_COUNT; i++) {
      float v = knob_read_battery_voltage();
      if (v > 0.0f && v <= LOW_BATTERY_VOLTAGE) {
        low_count++;
      }
      if (i < LOW_BATTERY_COUNT - 1) delay(50);
    }
    if (low_count >= LOW_BATTERY_COUNT) {
      knob_enter_deep_sleep();
    }
  }

  // Force backlight off immediately — the pin floats high between power-on
  // and LEDC init, briefly showing garbled LCD contents.
  pinMode(TFT_BLK, OUTPUT);
  digitalWrite(TFT_BLK, LOW);

  // Use the configured active CPU frequency for easier tuning.
  setCpuFrequencyMhz(CPU_FREQ_ACTIVE);

  // Disable radios
  esp_wifi_stop();
  esp_wifi_deinit();
  esp_bt_controller_disable();
  esp_bt_controller_deinit();

  delay(200);
  Serial.begin(115200);

  scr_lvgl_init();
  knob_gui();

  // Table Sync sessions are RAM-only: every boot starts with the radio
  // fully deinitialized until the user starts or joins a game.

  // Keep RTC8M clock alive during light sleep so LEDC PWM (backlight) continues
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC8M, ESP_PD_OPTION_ON);
  gpio_sleep_sel_dis((gpio_num_t)TFT_BLK);

  // Configure light sleep wakeup sources
  gpio_wakeup_enable((gpio_num_t)TOUCH_PIN_NUM_INT, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  // Timer wakeup duration is set dynamically in loop() from lv_timer_handler()'s
  // next-deadline value so the CPU only wakes when LVGL actually needs to run.
}

// Minimum idle interval before using light sleep.
// Below this threshold we fall back to vTaskDelay to avoid sleep/wake overhead.
#define ACTIVE_SLEEP_MIN_MS 10U

// Longest idle delay while Table Sync is on, so queued remote packets are
// applied promptly even when LVGL has no timer due for a while.
#define NET_SYNC_IDLE_MAX_MS 20U

// Detect active USB host by checking if the SOF frame counter is advancing.
// USB hosts send Start-of-Frame every 1ms; a changing counter means plugged in.
static bool usb_host_active(void)
{
  static uint32_t prev_sof = 0;
  uint32_t sof = USB_SERIAL_JTAG.fram_num.sof_frame_index;
  bool active = (sof != prev_sof);
  prev_sof = sof;
  return active;
}

// If a GPIO wake source fires immediately instead of waiting for the
// timer - electrical noise on the rotary encoder pins or the touch IRQ
// line misbehaving are the two candidates on this hardware, both far
// more plausible outdoors (cold, static, EMI) than on a bench indoors -
// esp_light_sleep_start() returns in well under a millisecond every
// iteration. That's a tight loop that never lets the FreeRTOS idle task
// run, which eventually trips the task watchdog and resets the device -
// looking exactly like a random crash, with no correlation to what
// screen is showing or whether the battery is fine, since it's the sleep
// mechanism itself spinning. If several sleep attempts in a row wake up
// far earlier than requested, force a real yield to break the loop.
#define FAST_WAKE_STREAK_LIMIT 5U
static uint8_t fast_wake_streak = 0;

/* With a static screen and no input, LVGL's display-refresh and
 * input-read timers are the only thing waking the CPU - measured on
 * hardware at ~24 wakeups/second doing ~80us of real work each, i.e. a
 * 0.15% duty cycle. The work itself is already negligible; what costs
 * energy is performing 24 light-sleep entry/exit transitions per second
 * to do essentially nothing. Both input paths are already GPIO wake
 * sources (the touch controller's IRQ line and the encoder pins), so
 * letting those two periodic timers run late during a longer sleep
 * loses no responsiveness - a touch or knob turn wakes the CPU straight
 * away and they run on the very next iteration. Pausing them for the
 * duration of the sleep is what lets lv_timer_handler() report the
 * app's own next deadline (auto-dim, previews) instead of clamping
 * every sleep to one 40ms refresh period. */
#define IDLE_SLEEP_MAX_MS 1000U

static bool ui_is_idle(void)
{
  lv_disp_t *disp = lv_disp_get_default();

  if (disp == NULL || disp->inv_p != 0) return false;      /* something still to draw */
  if (lv_anim_count_running() != 0) return false;          /* animation in flight */
  if (indev_touchpad != NULL &&
      indev_touchpad->proc.state == LV_INDEV_STATE_PRESSED) return false; /* finger down */
  return true;
}

static void idle_timers_set_paused(bool paused)
{
  lv_disp_t *disp = lv_disp_get_default();
  lv_timer_t *refr = (disp != NULL) ? disp->refr_timer : NULL;
  lv_timer_t *read = (indev_touchpad != NULL) ? indev_touchpad->driver->read_timer : NULL;

  if (refr != NULL) paused ? lv_timer_pause(refr) : lv_timer_resume(refr);
  if (read != NULL) paused ? lv_timer_pause(read) : lv_timer_resume(read);
}

void loop()
{
  uint32_t time_till_next;

  rtc_last_alive_ms = millis(); /* see record_and_print_reset_history() */

  knob_process_pending();
  knobby_net_process();
  time_till_next = lv_timer_handler();

  // Light sleep powers down the modem, which drops ESP-NOW packets (Table
  // Sync) and, worse, corrupts an in-progress WiFi association/handshake -
  // seen on hardware as a reset a second or two into connecting whenever
  // the device wasn't tethered to a real USB host (which already forces
  // the vTaskDelay branch below via usb_host_active()). Both radios keep
  // the CPU on capped vTaskDelay idles instead while active.
  bool wifi_radio_busy = (wifi_get_state() == WIFI_STATE_CONNECTING || wifi_get_state() == WIFI_STATE_CONNECTED);
  if (time_till_next >= ACTIVE_SLEEP_MIN_MS && !usb_host_active() && !knobby_net_active() && !wifi_radio_busy) {
    bool idle_extended = ui_is_idle();
    uint8_t level_a = gpio_get_level((gpio_num_t)ROTARY_ENC_PIN_A);
    uint8_t level_b = gpio_get_level((gpio_num_t)ROTARY_ENC_PIN_B);
    gpio_wakeup_enable((gpio_num_t)ROTARY_ENC_PIN_A, level_a ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
    gpio_wakeup_enable((gpio_num_t)ROTARY_ENC_PIN_B, level_b ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
    if (idle_extended) {
      idle_timers_set_paused(true);
      /* Re-query with the two periodic LVGL timers out of the way, so
         this reflects the app's own next deadline rather than the next
         refresh tick. Clamped: lv_timer_handler() reports "no timer
         ready" as a huge value when everything is paused. */
      time_till_next = lv_timer_handler();
      if (time_till_next > IDLE_SLEEP_MAX_MS) time_till_next = IDLE_SLEEP_MAX_MS;
    }
    esp_sleep_enable_timer_wakeup((uint64_t)time_till_next * 1000ULL);
    uint32_t sleep_start_ms = millis();
    esp_light_sleep_start();
    uint32_t slept_ms = millis() - sleep_start_ms;
    if (idle_extended) idle_timers_set_paused(false);
    gpio_wakeup_disable((gpio_num_t)ROTARY_ENC_PIN_A);
    gpio_wakeup_disable((gpio_num_t)ROTARY_ENC_PIN_B);
    if (slept_ms + 1 < time_till_next) {
      /* Woke up early via a GPIO source, not the requested timer. */
      if (++fast_wake_streak >= FAST_WAKE_STREAK_LIMIT) {
        fast_wake_streak = 0;
        vTaskDelay(1);
      }
    } else {
      fast_wake_streak = 0;
    }
  } else {
    fast_wake_streak = 0;
    gpio_wakeup_disable((gpio_num_t)ROTARY_ENC_PIN_A);
    gpio_wakeup_disable((gpio_num_t)ROTARY_ENC_PIN_B);
    if (knobby_net_active() && time_till_next > NET_SYNC_IDLE_MAX_MS)
      time_till_next = NET_SYNC_IDLE_MAX_MS;
    vTaskDelay(pdMS_TO_TICKS(time_till_next));
  }
}
