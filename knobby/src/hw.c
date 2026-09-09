#include "hw.h"
#include "storage.h"
#include "net_sync.h"
#include "lang.h"
#include "wifi_ota.h"
#include "ui_wifi.h"
#include "../knob.h"
#include "driver/ledc.h"
#include "esp_sleep.h"
#include <stdio.h>
#ifndef SIMULATOR
#include "esp32-hal-cpu.h"
#endif

// ---------- private constants ----------
#include "pincfg.h"
#define BACKLIGHT_PIN TFT_BLK
#define BACKLIGHT_LEDC_MODE LEDC_LOW_SPEED_MODE
#define BACKLIGHT_LEDC_TIMER LEDC_TIMER_0
#define BACKLIGHT_LEDC_CHANNEL LEDC_CHANNEL_0
#define BACKLIGHT_LEDC_FREQ 5000
#define BACKLIGHT_LEDC_RES LEDC_TIMER_10_BIT
#define BACKLIGHT_DUTY_MAX 1023

// Tunable power/timing constants are defined in knob_hw.h

// ---------- state ----------
int brightness_percent = DEFAULT_BRIGHTNESS_PERCENT;
bool dimmed = false;
float battery_voltage = 0.0f;
int battery_percent = -1;

static void check_low_battery_cutoff(void);
static uint32_t battery_sample_tick = 0;
static bool battery_sample_valid = false;
static int low_battery_consecutive = 0;
static uint32_t last_activity_tick = 0;
static uint32_t undim_tick = 0;
static lv_timer_t *auto_dim_timer = NULL;
#ifndef SIMULATOR
static bool cpu_boosted = false;
static lv_timer_t *cpu_boost_timer = NULL;
#endif

#define BATTERY_ICON_MAX 8
static lv_obj_t *battery_icons[BATTERY_ICON_MAX];
static int battery_icon_count = 0;

static bool battery_icon_blink_visible = true;
static lv_timer_t *battery_icon_timer = NULL;

// ---------- battery curve ----------
static const float battery_curve_voltages[] = {
    3.35f, 3.55f, 3.68f, 3.74f, 3.80f, 3.88f, 3.96f, 4.06f, 4.18f
};
static const int battery_curve_percentages[] = {
    0, 5, 12, 22, 34, 48, 64, 82, 100
};

// ---------- private helpers ----------
static int clamp_percent(int value)
{
    if (value < 0) return 0;
    if (value > 100) return 100;
    return value;
}

static int battery_percent_from_voltage(float voltage)
{
    size_t i;

    if (voltage <= battery_curve_voltages[0]) return 0;
    for (i = 1; i < (sizeof(battery_curve_voltages) / sizeof(battery_curve_voltages[0])); i++) {
        if (voltage <= battery_curve_voltages[i]) {
            float low_v = battery_curve_voltages[i - 1];
            float high_v = battery_curve_voltages[i];
            int low_p = battery_curve_percentages[i - 1];
            int high_p = battery_curve_percentages[i];
            float ratio = (voltage - low_v) / (high_v - low_v);
            return clamp_percent((int)(low_p + ((high_p - low_p) * ratio) + 0.5f));
        }
    }

    return 100;
}

// ---------- battery ----------
void update_battery_measurement(bool force)
{
    if (!force && battery_sample_valid && (lv_tick_elaps(battery_sample_tick) < BATTERY_SAMPLE_INTERVAL_MS)) {
        return;
    }

    battery_voltage = knob_read_battery_voltage();
    battery_sample_tick = lv_tick_get();
    battery_sample_valid = (battery_voltage > 0.0f);

    check_low_battery_cutoff();
}

static void check_low_battery_cutoff(void)
{
    if (!battery_sample_valid) return;

    if (battery_voltage <= LOW_BATTERY_VOLTAGE) {
        low_battery_consecutive++;
        if (low_battery_consecutive >= LOW_BATTERY_COUNT) {
            knob_enter_deep_sleep();
        }
    } else {
        low_battery_consecutive = 0;
    }
}

void knob_enter_deep_sleep(void)
{
    /* Deep sleep with the WiFi driver live violates the IDF sleep
       contract (and this path is reachable while Table Sync is on).
       The wake is a full reboot, so the game is left for good — a
       recharged device rejoins via the in-game Invite. */
    net_sync_leave_game();

    // Turn off backlight
    ledc_set_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL, 0);
    ledc_update_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL);

    // Configure 15s timer wakeup to re-check voltage
    esp_sleep_enable_timer_wakeup(LOW_BATTERY_WAKE_US);
    esp_deep_sleep_start();
}

int read_battery_percent(void)
{
    update_battery_measurement(false);
    if (!battery_sample_valid) return -1;
    return battery_percent_from_voltage(battery_voltage);
}

// ---------- low-battery indicator ----------
void battery_icon_register(lv_obj_t *icon)
{
    if (icon == NULL || battery_icon_count >= BATTERY_ICON_MAX) return;
    battery_icons[battery_icon_count++] = icon;
    lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
}

void battery_icon_unregister(lv_obj_t *icon)
{
    int i;
    if (icon == NULL) return;
    for (i = 0; i < battery_icon_count; i++) {
        if (battery_icons[i] == icon) {
            battery_icons[i] = battery_icons[--battery_icon_count];
            return;
        }
    }
}

static void battery_icon_apply(bool visible)
{
    int i;
    for (i = 0; i < battery_icon_count; i++) {
        if (visible) lv_obj_clear_flag(battery_icons[i], LV_OBJ_FLAG_HIDDEN);
        else         lv_obj_add_flag(battery_icons[i], LV_OBJ_FLAG_HIDDEN);
    }
}

// ---------- update-available notice ----------
#define UPDATE_TOAST_MS 5000

static lv_obj_t *s_toast = NULL;
static lv_timer_t *s_toast_timer = NULL;

static void dismiss_update_toast(void)
{
    if (s_toast_timer != NULL) {
        lv_timer_del(s_toast_timer);
        s_toast_timer = NULL;
    }
    if (s_toast != NULL) {
        lv_obj_del(s_toast);
        s_toast = NULL;
    }
}

static void update_toast_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    s_toast_timer = NULL; /* one-shot timer deletes itself on return */
    dismiss_update_toast();
}

static void update_toast_click_cb(lv_event_t *e)
{
    (void)e;
    dismiss_update_toast();
    /* The OTA screen's back gesture returns to whatever screen sent it
       there (see settings_handle_back()'s chain back to screen_quad_menu,
       then knob.c's previous_screen check) - normally that's only ever
       reached via the settings menu, which sets this itself. Tapping the
       toast is a shortcut around that whole chain, so it has to set the
       breadcrumb too, or back leaves the user stranded in settings with
       no way to reach the life counter again. */
    knob_remember_return_screen(lv_scr_act());
    open_ota_update_screen();
}

/* Rather than a persistent icon (too easy to miss on a screen this
   size), this drops a self-dismissing banner on lv_layer_top() (renders
   above whatever screen is active, independent of which one that is)
   spelling out a message in words; tapping it runs click_cb, which owns
   navigating wherever that message points to. Shared by the "update
   available" and "just updated" toasts below. */
static void show_toast(const char *msg, lv_event_cb_t click_cb)
{
    dismiss_update_toast(); /* replace any still-showing toast rather than stack them */

    s_toast = lv_label_create(lv_layer_top());
    lv_label_set_text(s_toast, msg);
    lv_obj_set_style_text_font(s_toast, &lv_font_es_16, 0);
    lv_obj_set_style_text_align(s_toast, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_toast, lv_color_white(), 0);
    lv_obj_set_style_bg_color(s_toast, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_80, 0);
    lv_obj_set_style_radius(s_toast, 12, 0);
    lv_obj_set_style_pad_all(s_toast, 10, 0);
    lv_obj_set_width(s_toast, 220);
    lv_obj_align(s_toast, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_toast, click_cb, LV_EVENT_CLICKED, NULL);

    s_toast_timer = lv_timer_create(update_toast_timer_cb, UPDATE_TOAST_MS, NULL);
    lv_timer_set_repeat_count(s_toast_timer, 1);
}

/* The moment an update is first seen this shows the update-available
   toast; tapping it jumps straight to the update screen. */
static void show_update_toast(void)
{
    char msg[64];
    snprintf(msg, sizeof(msg), t(STR_OTA_AVAILABLE_FMT), ota_get_latest_version());
    show_toast(msg, update_toast_click_cb);
}

static void update_applied_toast_click_cb(lv_event_t *e)
{
    (void)e;
    dismiss_update_toast();
    /* Straight to the QR (release notes), skipping the Updates screen -
       open_ota_qr_screen_from_toast() marks the visit so knob.c's back
       handler sends back to the life counter instead of Updates, which
       this toast never went through. */
    open_ota_qr_screen_from_toast();
}

/* Shown once, right after boot, when the running firmware version
   differs from the one stored at the previous boot (see
   check_firmware_update_toast) - i.e. an OTA update just landed.
   Tapping it shows the QR code to the release notes. */
static void show_update_applied_toast(const char *version)
{
    char msg[64];
    snprintf(msg, sizeof(msg), t(STR_OTA_UPDATED_FMT), version);
    show_toast(msg, update_applied_toast_click_cb);
}

void check_firmware_update_toast(void)
{
    char last[FW_VERSION_LEN];
    const char *current = get_firmware_version();

    nvs_get_last_fw_version(last, sizeof(last));
    if (last[0] != '\0' && strcmp(last, current) != 0) {
        show_update_applied_toast(current);
    }
    if (strcmp(last, current) != 0) {
        nvs_set_last_fw_version(current);
    }
}

static void update_notice_check(void)
{
    static bool s_toast_shown = false;
    bool available = (ota_get_state() == OTA_STATE_AVAILABLE);

    if (available && !s_toast_shown) {
        s_toast_shown = true;
        show_update_toast();
    } else if (!available) {
        s_toast_shown = false;
    }
}

static void battery_icon_timer_cb(lv_timer_t *timer)
{
    int pct = read_battery_percent();

    update_notice_check();
    if (pct < 0 || pct >= LOW_BATTERY_INDICATOR_PCT) {
        /* Icon stays hidden essentially the entire time the device is
           used - falling back to a slow check instead of waking every
           500ms to redundantly re-hide an already-hidden icon is a big
           chunk of this device's idle light-sleep wake frequency. */
        lv_timer_set_period(timer, BATTERY_CHECK_IDLE_PERIOD_MS);
        battery_icon_blink_visible = true;
        battery_icon_apply(false);
        return;
    }
    lv_timer_set_period(timer, BATTERY_BLINK_PERIOD_MS);
    if (pct >= LOW_BATTERY_BLINK_PCT) {
        battery_icon_blink_visible = true;
        battery_icon_apply(true);
        return;
    }
    battery_icon_apply(battery_icon_blink_visible);
    battery_icon_blink_visible = !battery_icon_blink_visible;
}

// ---------- brightness ----------
static void brightness_init(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode = BACKLIGHT_LEDC_MODE,
        .duty_resolution = BACKLIGHT_LEDC_RES,
        .timer_num = BACKLIGHT_LEDC_TIMER,
        .freq_hz = BACKLIGHT_LEDC_FREQ,
        .clk_cfg = LEDC_USE_RTC8M_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .gpio_num = BACKLIGHT_PIN,
        .speed_mode = BACKLIGHT_LEDC_MODE,
        .channel = BACKLIGHT_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BACKLIGHT_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&ledc_channel);
}

void brightness_apply(void)
{
    uint32_t duty = (uint32_t)((brightness_percent * BACKLIGHT_DUTY_MAX) / 100);
    ledc_set_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL, duty);
    ledc_update_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL);
}

void change_brightness(int delta)
{
    brightness_percent = clamp_brightness(brightness_percent + delta);
    nvs_set_brightness(brightness_percent);
    brightness_apply();
}

// ---------- CPU boost ----------
#ifndef SIMULATOR
static void cpu_boost_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (cpu_boosted && lv_tick_elaps(last_activity_tick) >= CPU_BOOST_IDLE_MS) {
        setCpuFrequencyMhz(CPU_FREQ_ACTIVE);
        cpu_boosted = false;
        if (cpu_boost_timer != NULL) {
            lv_timer_pause(cpu_boost_timer);
        }
    }
}
#endif

// ---------- auto-dim ----------
bool activity_kick(void)
{
    bool was_dimmed = dimmed;
    last_activity_tick = lv_tick_get();
#ifndef SIMULATOR
    if (!cpu_boosted) {
        setCpuFrequencyMhz(CPU_FREQ_BOOST);
        cpu_boosted = true;
    }
    if (cpu_boost_timer != NULL) {
        lv_timer_resume(cpu_boost_timer);
    }
#endif
    if (dimmed) {
        if (auto_dim_timer != NULL) {
            lv_timer_resume(auto_dim_timer);
        }
        dimmed = false;
        undim_tick = last_activity_tick;
        brightness_apply();
    }
    return was_dimmed;
}

bool in_undim_grace(void)
{
    return undim_tick != 0 && lv_tick_elaps(undim_tick) < UNDIM_GRACE_MS;
}

static void auto_dim_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    // Piggyback: poll battery and check low-voltage cutoff regardless of
    // dim state.  update_battery_measurement() has its own 60s throttle.
    update_battery_measurement(false);

    int dim_setting = nvs_get_auto_dim();
    if (dim_setting == AUTO_DIM_OFF || dimmed) return;
    uint32_t timeout = auto_dim_ms[dim_setting];
    if (lv_tick_elaps(last_activity_tick) >= timeout) {
        dimmed = true;
        uint32_t duty = (uint32_t)((AUTO_DIM_BRIGHTNESS * BACKLIGHT_DUTY_MAX) / 100);
        ledc_set_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL, duty);
        ledc_update_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL);
        if (auto_dim_timer != NULL) {
            lv_timer_pause(auto_dim_timer);
        }
    }
}

// ---------- init ----------
void knob_hw_init(void)
{
    knob_nvs_init();
    lang_init(); /* must run before any build_*_screen() call below */
    brightness_init();
    brightness_percent = nvs_get_brightness();
    last_activity_tick = lv_tick_get();
    auto_dim_timer = lv_timer_create(auto_dim_timer_cb, AUTO_DIM_CHECK_PERIOD_MS, NULL);
    battery_icon_timer = lv_timer_create(battery_icon_timer_cb, BATTERY_BLINK_PERIOD_MS, NULL);
#ifndef SIMULATOR
    cpu_boost_timer = lv_timer_create(cpu_boost_timer_cb, 100, NULL);
    lv_timer_pause(cpu_boost_timer);
#endif
}
