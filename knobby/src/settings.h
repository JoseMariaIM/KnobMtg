#ifndef _SETTINGS_H
#define _SETTINGS_H

#include "types.h"
#include "lang.h"

// ---------- screens ----------
extern lv_obj_t *screen_settings;

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

// ---------- functions ----------
void build_settings_screen(void);

void refresh_settings_ui(void);
void refresh_settings_pages_ui(void);

/* What a screen IS to settings - not where back should send it; see
   nav.h. *page receives the settings page index: the page the item
   sits on for a CHILD, the page itself for a PAGE. */
typedef enum {
    SETTINGS_SCREEN_NONE = 0,
    SETTINGS_SCREEN_PAGE,
    SETTINGS_SCREEN_CHILD,
} settings_screen_kind_t;

settings_screen_kind_t settings_classify_screen(lv_obj_t *screen, int *page);
bool settings_screen_needs_save(lv_obj_t *screen);
void settings_show_page(int page);
void build_settings_pages(void);
bool settings_knob_page(int dir);
int settings_item_page(const char *id);

void open_settings_screen(void);

#endif // _SETTINGS_H
