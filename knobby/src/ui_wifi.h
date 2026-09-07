#ifndef _UI_WIFI_H
#define _UI_WIFI_H

#include "types.h"

extern lv_obj_t *screen_wifi_settings;
extern lv_obj_t *screen_wifi_scan_list;
extern lv_obj_t *screen_wifi_text_entry;
extern lv_obj_t *screen_wifi_status;
extern lv_obj_t *screen_ota_update;

void build_wifi_settings_screen(void);
void build_wifi_scan_list_screen(void);
void build_wifi_text_entry_screen(void);
void build_wifi_status_screen(void);
void build_ota_update_screen(void);

void open_wifi_settings_screen(void);
void open_wifi_scan_list_screen(void);
void open_ota_update_screen(void);

void refresh_wifi_settings_ui(void);
void refresh_ota_update_ui(void);

void wifi_text_entry_knob(int dir); /* dir<0 = left, dir>0 = right */

/* Returns true if it handled the back gesture itself (mirrors
   name_screen_handle_back's contract in rename.c). */
bool wifi_text_entry_handle_back(void);
bool wifi_scan_list_handle_back(void);
bool wifi_status_handle_back(void);

#endif // _UI_WIFI_H
