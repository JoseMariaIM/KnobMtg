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

/* Settings, Tools and Minigames are each one knob-driven scrolling list
   now rather than a run of quad pages - see ui_list.h. The main menu
   stays a quad because it holds exactly four things, which is what a
   2x2 grid is good at. */
extern lv_obj_t *screen_settings_list;

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
/* Knob handlers for the three lists: each returns false when the active
   screen is not its own, so knob.c can try them in turn. */
bool settings_knob_page(int dir);
bool minigames_handle_back(lv_obj_t *screen);
bool minigames_knob_page(int dir);
bool tools_knob(int dir);
/* Puts the cursor on a named setting, building the list if needed.
   Used by the simulator to screenshot one setting in place. */
bool settings_focus_item(const char *id);

void open_quad_menu(void);
void open_settings_screen(void);
void open_battery_screen(void);
void open_minigames_menu(void);
void open_settings_list(void);
void open_tools_menu(void);
void open_minigames_menu_at_launch_page(void);
void menu_facing_refresh(void);
void open_table_sync_screen(void);
void open_language_picker_screen(void);

#endif // _SETTINGS_H
