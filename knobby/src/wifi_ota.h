#ifndef _WIFI_OTA_H
#define _WIFI_OTA_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_STATE_DISCONNECTED = 0,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_FAILED,
} wifi_state_t;

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_CHECKING,
    OTA_STATE_UP_TO_DATE,
    OTA_STATE_AVAILABLE,
    OTA_STATE_UPDATING,
    OTA_STATE_ERROR,
} ota_state_t;

/* Call once at startup (after storage init): loads the saved SSID for
 * display only. Does not attempt to connect - connecting is always an
 * explicit user action (see wifi_connect() below). */
void wifi_ota_init(void);

/* Blocking (a couple of seconds): scans for nearby access points and
 * returns how many were found (capped internally). Query results with
 * the getters below before the next scan/connect call. */
int wifi_scan_start(void);
const char *wifi_scan_get_ssid(int index);
bool wifi_scan_is_open(int index); /* true = no password needed */

/* Saves the credentials and (re)connects. Blocking for a few seconds
 * while it waits to learn whether the connection succeeded - the LVGL
 * loop is single-threaded here, same tradeoff every other blocking call
 * in this codebase (HTTPUpdate, the OTA check below) already makes. */
void wifi_connect(const char *ssid, const char *pass);
void wifi_disconnect(void);
wifi_state_t wifi_get_state(void);
const char *wifi_get_ip(void);       /* "" when not connected */
const char *wifi_get_saved_ssid(void); /* for pre-filling the settings screen */

const char *get_firmware_version(void);

/* Blocking: fetches the manifest hosted alongside the GitHub Pages web
 * installer and compares its version against the running firmware.
 * Requires wifi_get_state() == WIFI_STATE_CONNECTED. */
void ota_check_now(void);
ota_state_t ota_get_state(void);
const char *ota_get_latest_version(void); /* valid when state == OTA_STATE_AVAILABLE */
const char *ota_get_error(void);          /* valid when state == OTA_STATE_ERROR */

/* Blocking: downloads and flashes the new binary to the inactive OTA
 * partition, then reboots on success. Only returns on failure. Refuses
 * to start (OTA_STATE_ERROR) below OTA_MIN_BATTERY_PERCENT - a power
 * loss mid-flash can corrupt the running app partition. */
#define OTA_MIN_BATTERY_PERCENT 40
void ota_apply_update(void);

#ifdef __cplusplus
}
#endif

#endif // _WIFI_OTA_H
