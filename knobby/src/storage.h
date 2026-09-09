#ifndef _STORAGE_H
#define _STORAGE_H

#include "types.h"

void knob_nvs_init(void);
void settings_save(void);

int nvs_get_brightness(void);
void nvs_set_brightness(int value);
int nvs_get_auto_dim(void);
void nvs_set_auto_dim(int value);

int nvs_get_color_mode(void);
void nvs_set_color_mode(int value);
int nvs_get_deselect_timeout(void);
void nvs_set_deselect_timeout(int value);
int nvs_get_orientation(void);
void nvs_set_orientation(int value);
int nvs_get_display_rotation(void);
void nvs_set_display_rotation(int value);
int nvs_get_menu_facing(void);
void nvs_set_menu_facing(int value);
int nvs_get_language(void);
void nvs_set_language(int value);

int nvs_get_num_players(void);
void nvs_set_num_players(int value);
int nvs_get_players_to_track(void);
void nvs_set_players_to_track(int value);
int nvs_get_life_total(void);
void nvs_set_life_total(int value);

int nvs_get_auto_eliminate(void);
void nvs_set_auto_eliminate(int value);

int nvs_get_random_first(void);
void nvs_set_random_first(int value);

int nvs_get_multi_select(void);
void nvs_set_multi_select(int value);

#define NAME_LIST_COUNT 10
#define NAME_LIST_LEN   16
void nvs_get_name_list(char (*out)[NAME_LIST_LEN]);
void nvs_set_name_list(const char (*list)[NAME_LIST_LEN]);

#define WIFI_SSID_LEN 33 /* 32 chars + NUL, WPA2 max SSID length */
#define WIFI_PASS_LEN 65 /* 64 chars + NUL, WPA2 max PSK length */
void nvs_get_wifi_ssid(char *out, size_t out_len);
void nvs_set_wifi_ssid(const char *ssid);
void nvs_get_wifi_pass(char *out, size_t out_len);
void nvs_set_wifi_pass(const char *pass);

#define FW_VERSION_LEN 24 /* "vX.Y.Z" style tags, generous headroom */
void nvs_get_last_fw_version(char *out, size_t out_len);
void nvs_set_last_fw_version(const char *version);

int nvs_get_snake_high_score(int player);
void nvs_set_snake_high_score(int player, int score);

#endif // _STORAGE_H
