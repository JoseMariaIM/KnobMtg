#ifndef _SETTINGS_H
#define _SETTINGS_H

#include "types.h"
#include "lang.h"

// ---------- screens ----------
extern lv_obj_t *screen_quad_menu;
extern lv_obj_t *screen_tools_menu;
extern lv_obj_t *screen_settings;
extern lv_obj_t *screen_battery;
extern lv_obj_t *screen_minigames_menu;
extern lv_obj_t *screen_table_sync;
extern lv_obj_t *screen_language_picker;

// ---------- declarative settings ----------
/* One table in settings.c defines every user setting; pages and
   navigation are derived from it. */
typedef struct {
    const char *id;              /* stable id for sim navigation: "autodim" */
    string_id_t fixed_label_id;  /* used when label == NULL (navigation items) */
    const char *(*label)(int v); /* value -> text */
    uint32_t (*color)(int v);    /* value -> bg color; NULL = default */
    int (*get)(void);            /* NULL => navigation item */
    void (*set)(int v);          /* writes NVS + side effects */
    int count;                   /* cycle modulo (2 for ON/OFF toggles) */
    void (*navigate)(void);      /* non-NULL => click opens a sub-screen */
    lv_obj_t **nav_screen;       /* sub-screen global, for generic back-nav */
    lv_event_code_t event;       /* trigger; 0 = LV_EVENT_CLICKED (use
                                    LV_EVENT_LONG_PRESSED for "Hold" items) */
} setting_item_t;

extern lv_obj_t *settings_pages[];
extern int settings_page_count;

/* Minigames menu: one quad page per three games, plus "More". Sized for
   comfortably more games than exist so adding one is only a row in
   minigame_entries[] (settings.c). */
#define MINIGAMES_PAGE_MAX 6
extern lv_obj_t *minigames_pages[MINIGAMES_PAGE_MAX];
extern int minigames_page_count;

/* One game as the menu sees it: a name and the door into it. The game's
   own knob/tap/back behaviour is per-screen and lives in knob.c's
   screen_registry[]. */
typedef struct {
    string_id_t name;
    void (*open)(void);
} minigame_entry_t;

// ---------- functions ----------
void build_quad_screen(lv_obj_t **screen, quad_item_t items[4]);
void build_quad_menus(void);
void build_settings_screen(void);
void build_battery_screen(void);
void build_minigames_menu_screen(void);
void build_table_sync_screen(void);
void build_language_picker_screen(void);

void refresh_settings_ui(void);
void refresh_settings_pages_ui(void);
void refresh_battery_ui(void);
void refresh_table_sync_ui(void);

bool settings_handle_back(lv_obj_t *screen);
bool settings_knob_page(int dir);
bool minigames_handle_back(lv_obj_t *screen);
bool minigames_knob_page(int dir);
int settings_item_page(const char *id);

void open_quad_menu(void);
void open_settings_screen(void);
void open_battery_screen(void);
void open_minigames_menu(void);
void open_minigames_menu_at_launch_page(void);
void menu_facing_refresh(void);
void open_table_sync_screen(void);
void open_language_picker_screen(void);

#endif // _SETTINGS_H
