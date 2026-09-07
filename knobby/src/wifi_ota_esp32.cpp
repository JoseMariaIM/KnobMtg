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

static void wifi_auto_connect_poll_cb(lv_timer_t *timer)
{
    if (g_wifi_state != WIFI_STATE_CONNECTING) {
        lv_timer_del(timer);
        return;
    }
    if (WiFi.status() == WL_CONNECTED) {
        g_wifi_state = WIFI_STATE_CONNECTED;
        snprintf(g_wifi_ip, sizeof(g_wifi_ip), "%s", WiFi.localIP().toString().c_str());
        lv_timer_del(timer);
    } else if (millis() - s_auto_connect_started_at > WIFI_AUTO_CONNECT_TIMEOUT_MS) {
        g_wifi_state = WIFI_STATE_FAILED;
        lv_timer_del(timer);
    }
}

extern "C" void wifi_ota_init(void)
{
    nvs_get_wifi_ssid(g_saved_ssid, sizeof(g_saved_ssid));
    if (g_saved_ssid[0] == '\0') return;

    char pass[WIFI_PASS_LEN];
    nvs_get_wifi_pass(pass, sizeof(pass));

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); /* modem sleep can stall/drop packets mid-handshake */
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

    WiFiClientSecure client;
    /* No certificate pinning: GitHub Pages' TLS chain can rotate at any
       time, and a stale pinned root would silently brick the update
       path with no way to fix it except reflashing over USB. Accepted
       tradeoff for a personal device - see the project chat history. */
    client.setInsecure();

    HTTPClient http;
    http.setConnectTimeout(OTA_CONNECT_TIMEOUT_MS);
    if (!http.begin(client, OTA_MANIFEST_URL)) {
        g_ota_state = OTA_STATE_ERROR;
        snprintf(g_ota_error, sizeof(g_ota_error), "Bad manifest URL");
        return;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        char tls_err[64] = "";
        client.lastError(tls_err, sizeof(tls_err));
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
        http.end();
        return;
    }

    String body = http.getString();
    http.end();

    char latest[32];
    if (!extract_json_string_field(body.c_str(), "version", latest, sizeof(latest))) {
        g_ota_state = OTA_STATE_ERROR;
        snprintf(g_ota_error, sizeof(g_ota_error), "Bad manifest");
        return;
    }

    if (strcmp(latest, FIRMWARE_VERSION) == 0) {
        g_ota_state = OTA_STATE_UP_TO_DATE;
    } else {
        g_ota_state = OTA_STATE_AVAILABLE;
        snprintf(g_latest_version, sizeof(g_latest_version), "%s", latest);
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
        snprintf(g_ota_error, sizeof(g_ota_error), "Battery below %d%%", OTA_MIN_BATTERY_PERCENT);
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
