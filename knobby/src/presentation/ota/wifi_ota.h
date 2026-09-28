#ifndef _WIFI_OTA_H
#define _WIFI_OTA_H

#include "../../types.h"

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

/* Call once at startup (after prefs_init()): if a network was saved,
 * kicks off a non-blocking auto-connect attempt (boot stays instant
 * either way) so the device is online without the user having to
 * revisit WiFi settings every time. Also usable for an explicit
 * (re)connect - see wifi_connect() below, which blocks instead. */
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
/* Drops a stale WIFI_STATE_FAILED back to WIFI_STATE_DISCONNECTED (a
 * no-op otherwise) - lets the UI show a failure message briefly and
 * then revert the "Connect" tile to its normal, obviously-tappable
 * label instead of looking permanently stuck on "Failed". */
void wifi_clear_failed_state(void);
const char *wifi_get_ip(void);       /* "" when not connected */
const char *wifi_get_saved_ssid(void); /* for pre-filling the settings screen */

const char *get_firmware_version(void);

/* Shown as a QR code on the update screen so a phone can jump straight
 * to the release notes without typing a URL on the device itself. */
#define KNOBBY_RELEASES_URL "https://github.com/JoseMariaIM/KnobMtg/releases"

/* Blocking: fetches the routing file hosted alongside the GitHub Pages
 * web installer and compares the version for THIS device's channel
 * against the running firmware.
 * Requires wifi_get_state() == WIFI_STATE_CONNECTED. */
void ota_check_now(void);
ota_state_t ota_get_state(void);

/* ---------- update channels ----------
 *
 * ota.json (published by .github/workflows/release.yml, see OTA_BASE_URL
 * in wifi_ota_esp32.cpp) routes each device to a channel:
 *
 *   { "version": "v1.4.0",          "bin": "knobby.ino.bin",
 *     "test_version": "v1.5.0-rc1", "test_bin": "test/knobby.ino.bin",
 *     "test_devices": "A1B2C3,DE45F6" }
 *
 * A device takes the test build when its hw_device_id() is listed and
 * the stable one otherwise. That list is the ONLY way onto a test
 * build: there is deliberately no switch on the device, because the
 * point of this is that whoever publishes decides who receives, not
 * whoever happens to be holding a unit. Which also means enrolling or
 * removing a tester is an edit to one line of one file - no rebuild -
 * and that a device dropped from the list is handed the stable build on
 * its next check, i.e. rolled back.
 *
 * Flat keys, not nested objects, so the substring parser below is
 * enough: one HTTPS GET, no JSON library, and no GitHub API (whose
 * unauthenticated rate limit this device would share with everyone
 * behind the same address).
 *
 * Both parsers are pure and live in wifi_ota.c so the simulator's
 * unit tests can exercise them without a network - see
 * sim/tests/test_ota_channel.c. */

/* Reads one "key": "value" pair out of the small fixed-shape JSON the
 * site publishes. False when the key is absent or the value is not a
 * plain string. */
bool ota_json_string_field(const char *json, const char *key, char *out, size_t out_len);

/* True when device_id appears in a comma-separated list, matching whole
 * entries only (so "A1B2C3" is not found inside "A1B2C3D") and ignoring
 * case and surrounding spaces. */
bool ota_device_in_list(const char *list, const char *device_id);

/* The whole channel decision for one device, against one routing file:
 * true means this device is enrolled on the test channel. A file with no
 * tester list, or a device not in it, is stable - a malformed or missing
 * list must never promote anybody. */
bool ota_channel_is_test(const char *json, const char *device_id);

/* Which channel the last check resolved to, for the Updates screen.
 * False until a check has run - this device cannot know it is a tester
 * until it reads the list. */
bool ota_on_test_channel(void);

/* Fully powers down the radio (not just a disconnect). Connected WiFi is
 * by far this device's largest power draw: the station keeps modem sleep
 * disabled - see the comment on the setSleep() calls - and, worse, the
 * main loop refuses to light-sleep the CPU at all while the radio is up,
 * so an idle connection pins both the radio and the CPU on indefinitely.
 * Nothing here needs a standing connection, so the radio is only raised
 * to check/apply an update and parked again right after. */
void wifi_radio_off(void);

/* True once the post-boot update check has run to completion, whatever
 * its outcome - lets the UI tell "no update" apart from "not looked
 * yet" without exposing the auto-check state machine. */
bool ota_auto_check_done(void);
const char *ota_get_latest_version(void); /* valid when state == OTA_STATE_AVAILABLE */
const char *ota_get_error(void);          /* valid when state == OTA_STATE_ERROR */

/* Blocking: downloads and flashes the new binary to the inactive OTA
 * partition, then reboots on success. Only returns on failure. Refuses
 * to start (OTA_STATE_ERROR) below OTA_MIN_BATTERY_PERCENT - a power
 * loss mid-flash can corrupt the running app partition. */
#define OTA_MIN_BATTERY_PERCENT 40
void ota_apply_update(void);

/* ota_apply_update() blocks for the whole download+flash, so progress
 * has to be pushed out from inside it rather than polled from the main
 * loop. Called with 0..100 as bytes arrive; the UI is the only thing
 * that needs to know, so this stays a plain function pointer instead
 * of adding a UI dependency to this file. */
typedef void (*ota_progress_cb_t)(int percent);
void ota_set_progress_cb(ota_progress_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif // _WIFI_OTA_H
