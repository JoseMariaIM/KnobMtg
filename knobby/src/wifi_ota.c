#include "wifi_ota.h"
#include "storage.h"
#include "version.h"
#include "hw.h"

#include <string.h>
#include <stdio.h>

/* Shared state, updated either by the SIMULATOR block below or by
 * wifi_ota_esp32.cpp on real firmware (which declares these `extern`
 * rather than duplicating them, so both build targets read/write one
 * source of truth). Not static for that reason. */
wifi_state_t g_wifi_state = WIFI_STATE_DISCONNECTED;
char g_wifi_ip[16] = "";
char g_saved_ssid[WIFI_SSID_LEN] = "";
ota_state_t g_ota_state = OTA_STATE_IDLE;
char g_latest_version[32] = "";
char g_ota_error[64] = "";

const char *get_firmware_version(void) { return FIRMWARE_VERSION; }
wifi_state_t wifi_get_state(void) { return g_wifi_state; }
const char *wifi_get_ip(void) { return g_wifi_ip; }
const char *wifi_get_saved_ssid(void) { return g_saved_ssid; }
ota_state_t ota_get_state(void) { return g_ota_state; }
const char *ota_get_latest_version(void) { return g_latest_version; }
const char *ota_get_error(void) { return g_ota_error; }

#ifdef SIMULATOR

/* The simulator has no real network stack; these stand in so the WiFi
 * and Update screens can still be built, navigated, and screenshotted.
 * "Connecting" always succeeds instantly with a fake IP, and a check
 * always reports a fake update available so both OTA screen states are
 * reachable without a real device/network in the loop. Real firmware
 * uses wifi_ota_esp32.cpp instead (excluded from the sim build). */

#define WIFI_SCAN_MAX 12
static char s_scan_ssids[WIFI_SCAN_MAX][WIFI_SSID_LEN];
static bool s_scan_open[WIFI_SCAN_MAX];
static int s_scan_count = 0;

const char *wifi_scan_get_ssid(int index)
{
    if (index < 0 || index >= s_scan_count) return "";
    return s_scan_ssids[index];
}

bool wifi_scan_is_open(int index)
{
    if (index < 0 || index >= s_scan_count) return false;
    return s_scan_open[index];
}

/* A battery reading of -1 means "no calibrated reading" (see
 * refresh_battery_ui in settings.c), which on this board is what a
 * unit with no battery installed - running on stable USB power -
 * reports. Only a *known* low reading blocks the update. */
static bool battery_ok_for_update(void)
{
    int pct = read_battery_percent();
    return pct < 0 || pct >= OTA_MIN_BATTERY_PERCENT;
}

void wifi_ota_init(void)
{
    /* Mirrors the real firmware: just remembers the saved SSID for
       display, doesn't auto-connect. Connecting is always an explicit
       user action (see wifi_connect() below / wifi_ota_esp32.cpp). */
    nvs_get_wifi_ssid(g_saved_ssid, sizeof(g_saved_ssid));
}

int wifi_scan_start(void)
{
    s_scan_count = 3;
    snprintf(s_scan_ssids[0], WIFI_SSID_LEN, "%s", "SimuladaCasa_5G");
    s_scan_open[0] = false;
    snprintf(s_scan_ssids[1], WIFI_SSID_LEN, "%s", "CafeteriaSim_Guest");
    s_scan_open[1] = true;
    snprintf(s_scan_ssids[2], WIFI_SSID_LEN, "%s", "VecinoSim");
    s_scan_open[2] = false;
    return s_scan_count;
}

void wifi_connect(const char *ssid, const char *pass)
{
    nvs_set_wifi_ssid(ssid);
    nvs_set_wifi_pass(pass);
    snprintf(g_saved_ssid, sizeof(g_saved_ssid), "%s", ssid);
    g_wifi_state = WIFI_STATE_CONNECTED;
    snprintf(g_wifi_ip, sizeof(g_wifi_ip), "127.0.0.1");
}

void ota_check_now(void)
{
    if (g_wifi_state != WIFI_STATE_CONNECTED) {
        g_ota_state = OTA_STATE_ERROR;
        snprintf(g_ota_error, sizeof(g_ota_error), "Wifi not connected");
        return;
    }
    g_ota_state = OTA_STATE_AVAILABLE;
    snprintf(g_latest_version, sizeof(g_latest_version), "v99.0.0-sim");
}

void wifi_disconnect(void)
{
    g_wifi_state = WIFI_STATE_DISCONNECTED;
    g_wifi_ip[0] = '\0';
}

void ota_apply_update(void)
{
    if (!battery_ok_for_update()) {
        g_ota_state = OTA_STATE_ERROR;
        snprintf(g_ota_error, sizeof(g_ota_error), "Battery below %d%%", OTA_MIN_BATTERY_PERCENT);
        return;
    }
    g_ota_state = OTA_STATE_ERROR;
    snprintf(g_ota_error, sizeof(g_ota_error), "OTA not available in simulator");
}

#endif
