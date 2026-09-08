/* Real ESP32 WiFi + OTA implementation. Not part of the simulator build
 * (sim/Makefile never lists this file) - wifi_ota.c's #ifdef SIMULATOR
 * block covers the sim side, sharing the same state variables declared
 * (non-static) in that file. */
#ifndef SIMULATOR

#include "wifi_ota.h"
#include "version.h"

extern "C" {
#include "storage.h"
#include "net_sync.h"
#include "hw.h"
#include "lang.h"
}

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <string.h>
#include <stdio.h>

/* GitHub Pages URL that release.yml already deploys to: the web
 * installer's manifest.json and the raw app binary both end up hosted
 * there for free, so the device reuses that instead of talking to the
 * GitHub API (which rate-limits unauthenticated requests) or needing
 * its own hosting. Update this if the repo is ever renamed/forked. */
#define OTA_BASE_URL "https://josemariaim.github.io/KnobMtg"
#define OTA_MANIFEST_URL OTA_BASE_URL "/manifest.json"
#define OTA_BIN_URL OTA_BASE_URL "/knobby.ino.bin"

/* HTTPClient's own default connect timeout is only 5000ms, and that
 * same value ends up gating every individual read/write step of the
 * TLS handshake too (not just the initial TCP connect) - see
 * ssl_client.cpp's socket_timeout usage. 5s is routinely too tight for
 * a full handshake with an external CDN from this CPU, which surfaced
 * as a misleading "Manifest HTTP -1" (HTTPC_ERROR_CONNECTION_REFUSED). */
#define OTA_CONNECT_TIMEOUT_MS 15000

/* Default TX power draws current spikes (~300-400mA) that a low or
 * aging battery can't source without the voltage sagging enough to
 * brownout-reset the board - happening consistently ~1-2s into any
 * connection attempt (right as the radio powers up) is the signature
 * of this, not a software bug. Cuts range somewhat, acceptable for a
 * device normally used within a room of its own WiFi router. */
#define WIFI_TX_POWER WIFI_POWER_11dBm

extern "C" {
extern wifi_state_t g_wifi_state;
extern char g_wifi_ip[16];
extern char g_saved_ssid[WIFI_SSID_LEN];
extern ota_state_t g_ota_state;
extern char g_latest_version[32];
extern char g_ota_error[64];
}

/* Auto-connect at boot is non-blocking - boot must stay instant even if
 * the saved network is out of range - so a lightweight LVGL timer polls
 * WiFi.status() until it resolves one way or the other. (A previous
 * version fired WiFi.begin() at boot but never polled afterwards, which
 * left the WiFi settings screen stuck showing "Connecting..." forever;
 * this timer is what was missing.) */
#define WIFI_AUTO_CONNECT_TIMEOUT_MS 15000
static uint32_t s_auto_connect_started_at = 0;
static bool s_auto_check_done = false;

extern "C" bool ota_auto_check_done(void)
{
    return s_auto_check_done;
}

/* An available update keeps the radio on so the user can apply it from
 * the notification, but if they never come back to it that's WiFi left
 * on indefinitely for nothing - worse than the "always connected" state
 * this whole feature was built to avoid. Whichever check last found an
 * update (auto or manual, from the OTA screen) (re)arms this; applying
 * or dismissing before it fires is fine, the callback only acts if the
 * update is still just sitting there ignored. */
#define WIFI_IDLE_OFF_MS (5UL * 60UL * 1000UL)
static lv_timer_t *s_idle_off_timer = NULL;

static void wifi_idle_off_cb(lv_timer_t *timer)
{
    (void)timer;
    s_idle_off_timer = NULL;
    if (g_wifi_state == WIFI_STATE_CONNECTED && g_ota_state == OTA_STATE_AVAILABLE) {
        Serial.println(F("[OTA] update ignored for 5 minutes, powering wifi off"));
        wifi_radio_off();
    }
}

static void arm_wifi_idle_off(void)
{
    if (s_idle_off_timer != NULL) {
        lv_timer_del(s_idle_off_timer);
    }
    s_idle_off_timer = lv_timer_create(wifi_idle_off_cb, WIFI_IDLE_OFF_MS, NULL);
    lv_timer_set_repeat_count(s_idle_off_timer, 1);
}

extern "C" void wifi_radio_off(void)
{
    if (s_idle_off_timer != NULL) {
        lv_timer_del(s_idle_off_timer);
        s_idle_off_timer = NULL;
    }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    g_wifi_state = WIFI_STATE_DISCONNECTED;
    g_wifi_ip[0] = '\0';
}

/* Runs once, shortly after the boot auto-connect succeeds: looks for an
 * update and then decides whether the radio has earned its keep. If one
 * is waiting, WiFi stays up so the user can apply it straight from the
 * notification; otherwise the radio is parked immediately - there's
 * nothing else on this device that wants a network. Deferred behind a
 * one-shot timer rather than run inline from the connect poll because
 * ota_check_now() blocks on HTTPS for a second or two, and this way the
 * intro animation has finished by the time that happens. */
static void ota_auto_check_cb(lv_timer_t *timer)
{
    lv_timer_del(timer);

    if (g_wifi_state != WIFI_STATE_CONNECTED) {
        Serial.println(F("[OTA] auto-check: wifi no longer connected, skipping check"));
        s_auto_check_done = true;
        wifi_radio_off();
        return;
    }

    Serial.println(F("[OTA] auto-check: running ota_check_now()"));
    ota_check_now();
    s_auto_check_done = true;
    Serial.printf("[OTA] auto-check: state=%d latest=%s error=%s\n",
                   (int)g_ota_state, g_latest_version, g_ota_error);

    /* Only an available update justifies keeping the radio powered;
       "up to date" and a failed check both mean nobody needs it. */
    if (g_ota_state != OTA_STATE_AVAILABLE) {
        Serial.println(F("[OTA] auto-check: no update to offer, powering radio off"));
        wifi_radio_off();
    } else {
        Serial.println(F("[OTA] auto-check: update available, keeping wifi on"));
    }
}

static void wifi_auto_connect_poll_cb(lv_timer_t *timer)
{
    if (g_wifi_state != WIFI_STATE_CONNECTING) {
        lv_timer_del(timer);
        return;
    }
    if (WiFi.status() == WL_CONNECTED) {
        g_wifi_state = WIFI_STATE_CONNECTED;
        snprintf(g_wifi_ip, sizeof(g_wifi_ip), "%s", WiFi.localIP().toString().c_str());
        Serial.printf("[OTA] auto-connect: connected, ip=%s\n", g_wifi_ip);
        lv_timer_del(timer);
        lv_timer_create(ota_auto_check_cb, 1500, NULL);
    } else if (millis() - s_auto_connect_started_at > WIFI_AUTO_CONNECT_TIMEOUT_MS) {
        g_wifi_state = WIFI_STATE_FAILED;
        s_auto_check_done = true;
        Serial.printf("[OTA] auto-connect: timed out, WiFi.status()=%d\n", (int)WiFi.status());
        lv_timer_del(timer);
        wifi_radio_off(); /* out of range or wrong password - don't leave the radio hunting */
    }
}

extern "C" void wifi_ota_init(void)
{
    nvs_get_wifi_ssid(g_saved_ssid, sizeof(g_saved_ssid));
    if (g_saved_ssid[0] == '\0') {
        Serial.println(F("[OTA] wifi_ota_init: no saved SSID, staying off"));
        return;
    }

    char pass[WIFI_PASS_LEN];
    nvs_get_wifi_pass(pass, sizeof(pass));

    Serial.printf("[OTA] wifi_ota_init: connecting to \"%s\"\n", g_saved_ssid);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); /* modem sleep can stall/drop packets mid-handshake */
    WiFi.setTxPower(WIFI_TX_POWER); /* see comment on the macro below */
    WiFi.begin(g_saved_ssid, pass);
    g_wifi_state = WIFI_STATE_CONNECTING;
    s_auto_connect_started_at = millis();
    lv_timer_create(wifi_auto_connect_poll_cb, 300, NULL);
}

#define WIFI_SCAN_MAX 12
static String s_scan_ssids[WIFI_SCAN_MAX];
static bool s_scan_open[WIFI_SCAN_MAX];
static int s_scan_count = 0;

extern "C" int wifi_scan_start(void)
{
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_TX_POWER);
    int found = WiFi.scanNetworks();
    s_scan_count = 0;
    for (int i = 0; i < found && s_scan_count < WIFI_SCAN_MAX; i++) {
        /* Collapse duplicate SSIDs seen on multiple channels/APs (mesh
           networks, repeaters) into a single entry. */
        bool duplicate = false;
        for (int j = 0; j < s_scan_count; j++) {
            if (s_scan_ssids[j] == WiFi.SSID(i)) { duplicate = true; break; }
        }
        if (duplicate) continue;
        s_scan_ssids[s_scan_count] = WiFi.SSID(i);
        s_scan_open[s_scan_count] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
        s_scan_count++;
    }
    WiFi.scanDelete();
    return s_scan_count;
}

extern "C" const char *wifi_scan_get_ssid(int index)
{
    if (index < 0 || index >= s_scan_count) return "";
    return s_scan_ssids[index].c_str();
}

extern "C" bool wifi_scan_is_open(int index)
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

extern "C" void wifi_connect(const char *ssid, const char *pass)
{
    nvs_set_wifi_ssid(ssid);
    nvs_set_wifi_pass(pass);
    snprintf(g_saved_ssid, sizeof(g_saved_ssid), "%s", ssid);

    /* Table Sync (ESP-NOW) and a connected WiFi station can conflict
       over radio channel/mode; updating firmware is a deliberate,
       occasional action, so it wins and any active sync session ends. */
    net_sync_leave_game();

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); /* modem sleep can stall/drop packets mid-handshake */
    WiFi.setTxPower(WIFI_TX_POWER);
    WiFi.disconnect();
    WiFi.begin(ssid, pass);
    g_wifi_state = WIFI_STATE_CONNECTING;

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(200);
    }

    if (WiFi.status() == WL_CONNECTED) {
        g_wifi_state = WIFI_STATE_CONNECTED;
        snprintf(g_wifi_ip, sizeof(g_wifi_ip), "%s", WiFi.localIP().toString().c_str());
    } else {
        g_wifi_state = WIFI_STATE_FAILED;
        g_wifi_ip[0] = '\0';
    }
}

extern "C" void wifi_disconnect(void)
{
    WiFi.disconnect(true);
    g_wifi_state = WIFI_STATE_DISCONNECTED;
    g_wifi_ip[0] = '\0';
}

/* Pulls "version" out of the small fixed-shape manifest.json the web
 * installer already publishes (see web/manifest.json) with a plain
 * substring search - not worth adding a JSON library dependency for one
 * field. If the manifest's shape ever changes this needs updating. */
static bool extract_json_string_field(const char *json, const char *key, char *out, size_t out_len)
{
    char needle[32];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return false;
    p = strchr(p + strlen(needle), '"');
    if (!p) return false;
    p++;
    const char *end = strchr(p, '"');
    if (!end) return false;
    size_t len = (size_t)(end - p);
    if (len >= out_len) len = out_len - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

extern "C" void ota_check_now(void)
{
    if (g_wifi_state != WIFI_STATE_CONNECTED) {
        g_ota_state = OTA_STATE_ERROR;
        snprintf(g_ota_error, sizeof(g_ota_error), "Wifi not connected");
        return;
    }

    g_ota_state = OTA_STATE_CHECKING;

    /* One retry: a fresh WiFi connection's very first TLS handshake is
       the flakiest one (still-settling routing/ARP, GitHub's CDN edge
       occasionally slow to respond) - a single retry with a fresh
       client clears most transient failures that a longer timeout
       alone doesn't fix. */
    String body;
    int code = 0;
    char tls_err[64] = "";
    bool ok = false;

    for (int attempt = 0; attempt < 2 && !ok; attempt++) {
        if (attempt > 0) delay(1000);

        WiFiClientSecure client;
        /* No certificate pinning: GitHub Pages' TLS chain can rotate at
           any time, and a stale pinned root would silently brick the
           update path with no way to fix it except reflashing over USB.
           Accepted tradeoff for a personal device - see the project
           chat history. */
        client.setInsecure();

        HTTPClient http;
        http.setConnectTimeout(OTA_CONNECT_TIMEOUT_MS);
        if (!http.begin(client, OTA_MANIFEST_URL)) {
            snprintf(g_ota_error, sizeof(g_ota_error), "Bad manifest URL");
            continue;
        }

        code = http.GET();
        if (code == HTTP_CODE_OK) {
            body = http.getString();
            ok = true;
        } else {
            tls_err[0] = '\0';
            client.lastError(tls_err, sizeof(tls_err));
        }
        http.end();
    }

    if (!ok) {
        g_ota_state = OTA_STATE_ERROR;
        /* Free heap is included because "SSL - Memory allocation failed"
           only means something with a number attached - if it recurs
           this tells us straight from the error screen whether it's a
           borderline shortage or something is leaking. */
        if (tls_err[0] != '\0') {
            snprintf(g_ota_error, sizeof(g_ota_error), "Manifest: %s (heap %u)", tls_err, (unsigned)ESP.getFreeHeap());
        } else {
            snprintf(g_ota_error, sizeof(g_ota_error), "Manifest: %s", HTTPClient::errorToString(code).c_str());
        }
        return;
    }

    char latest[32];
    if (!extract_json_string_field(body.c_str(), "version", latest, sizeof(latest))) {
        g_ota_state = OTA_STATE_ERROR;
        snprintf(g_ota_error, sizeof(g_ota_error), "Bad manifest");
        return;
    }

    Serial.printf("[OTA] manifest: latest=%s running=%s\n", latest, FIRMWARE_VERSION);
    if (strcmp(latest, FIRMWARE_VERSION) == 0) {
        g_ota_state = OTA_STATE_UP_TO_DATE;
    } else {
        g_ota_state = OTA_STATE_AVAILABLE;
        snprintf(g_latest_version, sizeof(g_latest_version), "%s", latest);
        arm_wifi_idle_off();
    }
}

extern "C" void ota_apply_update(void)
{
    if (g_wifi_state != WIFI_STATE_CONNECTED) {
        g_ota_state = OTA_STATE_ERROR;
        snprintf(g_ota_error, sizeof(g_ota_error), "Wifi not connected");
        return;
    }
    if (!battery_ok_for_update()) {
        g_ota_state = OTA_STATE_ERROR;
        snprintf(g_ota_error, sizeof(g_ota_error), t(STR_OTA_LOW_BATTERY_FMT), OTA_MIN_BATTERY_PERCENT);
        return;
    }

    g_ota_state = OTA_STATE_UPDATING;

    WiFiClientSecure client;
    client.setInsecure();

    /* Build our own HTTPClient (instead of the client+url convenience
       overload) so we can raise the connect timeout - see
       OTA_CONNECT_TIMEOUT_MS above. httpUpdate.update(HTTPClient&)
       takes an already-begin()'d client and skips straight to the
       download/flash logic. */
    HTTPClient http;
    http.setConnectTimeout(OTA_CONNECT_TIMEOUT_MS);
    if (!http.begin(client, OTA_BIN_URL)) {
        g_ota_state = OTA_STATE_ERROR;
        snprintf(g_ota_error, sizeof(g_ota_error), "Bad firmware URL");
        return;
    }

    httpUpdate.rebootOnUpdate(false);
    t_httpUpdate_return ret = httpUpdate.update(http);

    switch (ret) {
        case HTTP_UPDATE_OK:
            ESP.restart();
            return; /* unreachable */
        case HTTP_UPDATE_NO_UPDATES:
            g_ota_state = OTA_STATE_UP_TO_DATE;
            break;
        case HTTP_UPDATE_FAILED:
        default:
            g_ota_state = OTA_STATE_ERROR;
            snprintf(g_ota_error, sizeof(g_ota_error), "%s (heap %u)", httpUpdate.getLastErrorString().c_str(), (unsigned)ESP.getFreeHeap());
            break;
    }
}

#endif // !SIMULATOR
