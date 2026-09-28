#ifndef _SETTINGS_H
#define _SETTINGS_H

#include "../../types.h"
#include "../../adapters/lang.h"

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
    /* A row with no .get is a navigation row: what it opens, and which
       screen it opens, are bound at boot by settings_bind_screen()
       rather than named here. */
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
void settings_show_page(int page);
/* Assigns every row to a page and builds the first one. The rest are
   built the first time they are asked for - see settings_page_screen(). */
void build_settings_pages(void);
/* The screen for one settings page, built on first request (NULL for an
   out-of-range page). Anything that wants to load or render a page
   itself, rather than navigate to it, has to come through here or it
   will find the pointer still NULL. */
lv_obj_t *settings_page_screen(int page);
/* Free every built settings page except the one on screen. Called on the
   way out of the menu (see nav.c) and by settings_show_page() itself, so
   only the page being looked at is resident - see the comment at the
   definition. */
void settings_release_pages(void);

/* Point a navigation row at the screen it opens. Called once at boot,
   from knob.c, for every row with no .get - see the comment on
   item_open[] in settings.c for why the binding is not in the table.
   An unknown id is ignored. */
void settings_bind_screen(const char *id, void (*open)(void), lv_obj_t **screen);
bool settings_knob_page(int dir);
int settings_item_page(const char *id);

/* ---------- read-only accessors (unit tests) ---------- */
/* The id of the first navigation row nobody bound, or NULL when every
   one is wired. See settings_bind_screen(). */
const char *settings_test_unbound_item(void);

void open_settings_screen(void);

#endif // _SETTINGS_H
