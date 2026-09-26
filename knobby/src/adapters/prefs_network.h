#ifndef _PREFS_NETWORK_H
#define _PREFS_NETWORK_H

/* The credentials for the one network this device joins, and the
 * firmware version it last came up as. Kept apart from the rest so a
 * minigame or a settings page has no way to read a password it has no
 * business with. See prefs.h. */

#include "prefs.h"
#include <stddef.h>

#define WIFI_SSID_LEN 33 /* 32 chars + NUL, WPA2 max SSID length */
#define WIFI_PASS_LEN 65 /* 64 chars + NUL, WPA2 max PSK length */
void prefs_get_wifi_ssid(char *out, size_t out_len);
void prefs_set_wifi_ssid(const char *ssid);
void prefs_get_wifi_pass(char *out, size_t out_len);
void prefs_set_wifi_pass(const char *pass);

#define FW_VERSION_LEN 24 /* "vX.Y.Z" style tags, generous headroom */
void prefs_get_last_fw_version(char *out, size_t out_len);
void prefs_set_last_fw_version(const char *version);

#endif // _PREFS_NETWORK_H
