#include "settings.h"
#include "quad_screen.h"
#include "../../adapters/hw.h"
#include "../../adapters/prefs_display.h"
#include "../../adapters/prefs_table.h"
#include <string.h>
#include "../../adapters/lang.h"
#include "../../usecases/game.h"

// ---------- screens ----------
lv_obj_t *screen_settings = NULL;

// ---------- widgets ----------
static lv_obj_t *arc_brightness = NULL;
static lv_obj_t *label_settings_value = NULL;
static lv_obj_t *label_settings_hint = NULL;

// ---------- refresh ----------
static void refresh_brightness_ring(void)
{
    lv_arc_set_value(arc_brightness, brightness_percent);

    lv_obj_set_style_arc_color(arc_brightness, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_brightness, 18, LV_PART_MAIN);

    lv_obj_set_style_arc_color(arc_brightness, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_brightness, 18, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc_brightness, true, LV_PART_INDICATOR);
}

void refresh_settings_ui(void)
{
    char buf[32];
    snprintf(buf, sizeof(buf), t(STR_BRIGHTNESS_FMT), brightness_percent);
    lv_label_set_text(label_settings_value, buf);
    refresh_brightness_ring();
}

void open_settings_screen(void)
{
    // No forced battery read here – the brightness settings page does not display
    // battery info. The battery screen entry point forces its own sample.
    refresh_settings_ui();
    load_screen_if_needed(screen_settings);
}

// ---------- events ----------
static uint32_t autodim_color(int index)
{
    static const uint32_t colors[AUTO_DIM_COUNT] = {
        0x37474F, 0x1B5E20, 0x0D47A1, 0x4A148C  /* grey, green, blue, purple */
    };
    return (index >= 0 && index < AUTO_DIM_COUNT) ? colors[index] : 0x37474F;
}
static uint32_t orientation_color(int mode)
{
    switch (mode) {
        case ORIENTATION_MODE_CENTRIC: return TOGGLE_ON;
        case ORIENTATION_MODE_TABLETOP:  return 0x0D47A1;
        default:                   return TOGGLE_OFF;
    }
}

static uint32_t color_mode_color(int mode)
{
    return (mode == COLOR_MODE_LIFE) ? 0x4A148C : 0x0D47A1; /* purple / blue */
}

static uint32_t deselect_color(int index)
{
    static const uint32_t colors[DESELECT_COUNT] = {
        0x37474F, 0x1B5E20, 0x0D47A1, 0x4A148C  /* grey, green, blue, purple */
    };
    return (index >= 0 && index < DESELECT_COUNT) ? colors[index] : 0x1A1A2E;
}

static const char *autodim_label(int index)
{
    switch (index) {
        case AUTO_DIM_15S: return t(STR_SETTING_AUTODIM_15S);
        case AUTO_DIM_30S: return t(STR_SETTING_AUTODIM_30S);
        case AUTO_DIM_60S: return t(STR_SETTING_AUTODIM_60S);
        default:           return t(STR_SETTING_AUTODIM_OFF);
    }
}

static const char *color_mode_label(int mode)
{
    switch (mode) {
        case COLOR_MODE_LIFE:   return t(STR_SETTING_COLORS_LIFE);
        default:                return t(STR_SETTING_COLORS_PLAYER);
    }
}

static const char *deselect_label(int index)
{
    switch (index) {
        case DESELECT_5S:    return t(STR_SETTING_DESELECT_5S);
        case DESELECT_15S:   return t(STR_SETTING_DESELECT_15S);
        case DESELECT_30S:   return t(STR_SETTING_DESELECT_30S);
        default:             return t(STR_SETTING_DESELECT_NEVER);
    }
}

static const char *orientation_mode_label(int mode)
{
    switch (mode) {
        case ORIENTATION_MODE_CENTRIC:  return t(STR_SETTING_ORIENTATION_CENTRIC);
        case ORIENTATION_MODE_TABLETOP: return t(STR_SETTING_ORIENTATION_TABLETOP);
        default:                        return t(STR_SETTING_ORIENTATION_ABSOLUTE);
    }
}

static const char *auto_eliminate_label(int val)
{
    return val ? t(STR_SETTING_AUTO_ELIM_ON) : t(STR_SETTING_AUTO_ELIM_OFF);
}

// ---------- declarative settings ----------
/* Every user setting lives in this one table. Pages, "More" chaining,
   back-navigation, and sim navigation are all derived from it: to add,
   remove, or reorder a setting, edit only this table (plus its label,
   color, and NVS functions). Label fns must return strings that
   lv_label_set_text may copy — never switch the refresh to
   lv_label_set_text_static. */

static int autodim_get(void) { return prefs_get_auto_dim(); }
static void autodim_set(int v)
{
    prefs_set_auto_dim(v);
    if (v == AUTO_DIM_OFF && dimmed) {
        dimmed = false;
        brightness_apply();
    }
}

static uint32_t toggle_color(int val)
{
    return val ? TOGGLE_ON : TOGGLE_OFF;
}
static const char *random_first_label(int val)
{
    return val ? t(STR_SETTING_RANDOM_FIRST_ON) : t(STR_SETTING_RANDOM_FIRST_OFF);
}

static const char *menu_facing_label(int val)
{
    return val ? t(STR_SETTING_MENU_FACE_PLAYER) : t(STR_SETTING_MENU_FIXED);
}

static const char *multi_select_label(int val)
{
    return val ? t(STR_SETTING_MULTI_SELECT_ON) : t(STR_SETTING_MULTI_SELECT_OFF);
}

static void multi_select_set(int v)
{
    prefs_set_multi_select(v);
    if (v == 0) {
        /* Turning multi-select off: drop any lingering multi-selection so the
           single-select rules apply cleanly on return to the life screen. */
        selection_clear();
    }
}


static const setting_item_t settings_items[] = {
    { .id = "brightness",     .fixed_label_id = STR_SETTING_BRIGHTNESS },
    { .id = "autodim",        .label = autodim_label,          .color = autodim_color,     .get = autodim_get,              .set = autodim_set,              .count = AUTO_DIM_COUNT },
    { .id = "battery",        .fixed_label_id = STR_SETTING_BATTERY },
    { .id = "color-mode",     .label = color_mode_label,       .color = color_mode_color,  .get = prefs_get_color_mode,       .set = prefs_set_color_mode,       .count = COLOR_MODE_COUNT },
    { .id = "deselect",       .label = deselect_label,         .color = deselect_color,    .get = prefs_get_deselect_timeout, .set = prefs_set_deselect_timeout, .count = DESELECT_COUNT },
    { .id = "orientation",    .label = orientation_mode_label, .color = orientation_color, .get = prefs_get_orientation,      .set = prefs_set_orientation,      .count = ORIENTATION_MODE_COUNT },
    { .id = "auto-eliminate", .label = auto_eliminate_label,   .color = toggle_color,      .get = prefs_get_auto_eliminate,   .set = prefs_set_auto_eliminate,   .count = 2 },
    { .id = "random-first",   .label = random_first_label,     .color = toggle_color,      .get = prefs_get_random_first,     .set = prefs_set_random_first,     .count = 2 },
    { .id = "multi-select",   .label = multi_select_label,     .color = toggle_color,      .get = prefs_get_multi_select,     .set = multi_select_set,         .count = 2 },
    { .id = "table-sync",     .fixed_label_id = STR_SETTING_TABLE_SYNC },
    { .id = "minigames",      .fixed_label_id = STR_SETTING_MINIGAMES },
    { .id = "menu-facing",    .label = menu_facing_label,      .color = toggle_color,      .get = prefs_get_menu_facing,      .set = prefs_set_menu_facing,      .count = 2 },
    { .id = "language",       .fixed_label_id = STR_SETTING_LANGUAGE },
    { .id = "device-name",    .fixed_label_id = STR_SETTING_DEVICE_NAME },
    { .id = "wifi",           .fixed_label_id = STR_SETTING_WIFI },
    { .id = "updates",        .fixed_label_id = STR_SETTING_UPDATES },
    /* Last on purpose: the one row that destroys data sits at the end
       of the last page, as far as possible from anything tapped often. */
    { .id = "factory-reset",  .fixed_label_id = STR_SETTING_FACTORY_RESET },
};
#define SETTINGS_ITEM_COUNT ((int)(sizeof(settings_items) / sizeof(settings_items[0])))
#define MAX_SETTINGS_PAGES  ((SETTINGS_ITEM_COUNT + 2) / 3)

lv_obj_t *settings_pages[MAX_SETTINGS_PAGES];
int settings_page_count = 0;
/* The other half of a navigation row, filled at boot by
   settings_bind_screen(). Kept beside the table rather than in it so
   settings_items[] stays const - and so this file does not have to
   name a single screen it does not own. A row nobody binds draws
   dimmed and does nothing, which is what a missing binding should look
   like. */
static void (*item_open[SETTINGS_ITEM_COUNT])(void);
static lv_obj_t **item_screen[SETTINGS_ITEM_COUNT];

void settings_bind_screen(const char *id, void (*open)(void), lv_obj_t **screen)
{
    int i;
    for (i = 0; i < SETTINGS_ITEM_COUNT; i++) {
        if (strcmp(settings_items[i].id, id) == 0) {
            item_open[i] = open;
            item_screen[i] = screen;
            return;
        }
    }
}

static lv_obj_t *setting_btns[SETTINGS_ITEM_COUNT];
static lv_obj_t *setting_lbls[SETTINGS_ITEM_COUNT];
static int setting_page_of[SETTINGS_ITEM_COUNT];

void refresh_settings_pages_ui(void)
{
    int i;
    for (i = 0; i < SETTINGS_ITEM_COUNT; i++) {
        const setting_item_t *it = &settings_items[i];
        int v;
        if (setting_btns[i] == NULL || it->get == NULL) continue;
        v = it->get();
        lv_label_set_text(setting_lbls[i], it->label(v));
        set_btn_color(setting_btns[i], it->color ? it->color(v) : 0x1A1A2E);
    }
}

static void event_setting_item(lv_event_t *e)
{
    /* user_data is the row index plus one, so that row 0 is
       distinguishable from an event that carries nothing. */
    int i = (int)(intptr_t)lv_event_get_user_data(e) - 1;
    const setting_item_t *it;

    if (i < 0 || i >= SETTINGS_ITEM_COUNT) return;
    it = &settings_items[i];

    if (it->get == NULL) {
        if (item_open[i] != NULL) item_open[i]();
        return;
    }
    it->set((it->get() + 1) % it->count);
    refresh_settings_pages_ui();
}

static void event_setting_more(lv_event_t *e)
{
    int page = (int)(intptr_t)lv_event_get_user_data(e);
    settings_show_page(page);
}

/* Chunk the flat item list into quad pages: 3 items + "More" per page.
   "More" always advances and wraps from the last page to the first, so
   tap navigation cycles just like the knob. */
static void build_settings_page(int page)
{
    int first = page * 3;
    int remaining = SETTINGS_ITEM_COUNT - first;
    int on_page = (remaining < 3) ? remaining : 3;
    int s;
    quad_item_t q[4];

    memset(q, 0, sizeof(q));
    for (s = 0; s < 4; s++) q[s].label = "";
    for (s = 0; s < on_page; s++) {
        int idx = first + s;
        const setting_item_t *it = &settings_items[idx];
        q[s].label = (it->label != NULL) ? it->label(it->get()) : t(it->fixed_label_id);
        q[s].cb = event_setting_item;
        q[s].enabled = (it->get != NULL) || (item_open[idx] != NULL);
        q[s].event = (it->event != 0) ? it->event : LV_EVENT_CLICKED;
        q[s].user_data = (void *)(intptr_t)(idx + 1);
    }
    q[3].label = t(STR_SETTINGS_MORE);
    q[3].cb = event_setting_more;
    q[3].enabled = true;
    q[3].event = LV_EVENT_CLICKED;
    q[3].user_data = (void *)(intptr_t)((page + 1) % settings_page_count);
    build_quad_screen(&settings_pages[page], q);
    for (s = 0; s < on_page; s++) {
        setting_btns[first + s] = lv_obj_get_child(settings_pages[page], s);
        setting_lbls[first + s] = lv_obj_get_child(setting_btns[first + s], 0);
    }
    /* Labels came from the table above, but not the tile colours a
       toggle's current value decides - see refresh_settings_pages_ui(),
       which skips the pages that are not built yet. */
    refresh_settings_pages_ui();
}

/* Page 0 is built at boot because that is what the Settings tile opens;
   the rest wait until somebody actually pages that far. Six quad pages
   held from boot is ~20KB of LVGL's fixed 128KB pool (see
   sim/tests/membudget) for tiles most sessions never scroll to - the
   same reasoning that already keeps the minigames menu out of it, see
   build_quad_menus() in nav.c. Which page each row sits on is decided
   here for every row regardless, so settings_item_page() and the
   back-navigation classification never depend on what has been built. */
lv_obj_t *settings_page_screen(int page)
{
    if (page < 0 || page >= settings_page_count) return NULL;
    if (settings_pages[page] == NULL) build_settings_page(page);
    return settings_pages[page];
}

void build_settings_pages(void)
{
    int i;

    settings_page_count = MAX_SETTINGS_PAGES;
    for (i = 0; i < SETTINGS_ITEM_COUNT; i++) setting_page_of[i] = i / 3;
    for (i = 0; i < MAX_SETTINGS_PAGES; i++) settings_pages[i] = NULL;
    (void)settings_page_screen(0);
}

/* Classification only - what a screen IS to settings, not where
   back should send it. nav.c owns that decision; keeping it here is
   what used to make settings.c name screen_quad_menu and every module
   that wanted to leave a settings sub-screen call into settings. */
settings_screen_kind_t settings_classify_screen(lv_obj_t *screen, int *page)
{
    int i;

    for (i = 0; i < SETTINGS_ITEM_COUNT; i++) {
        if (item_screen[i] != NULL && screen == *item_screen[i]) {
            if (page != NULL) *page = setting_page_of[i];
            return SETTINGS_SCREEN_CHILD;
        }
    }
    for (i = 0; i < settings_page_count; i++) {
        if (settings_pages[i] != NULL && screen == settings_pages[i]) {
            if (page != NULL) *page = i;
            return SETTINGS_SCREEN_PAGE;
        }
    }
    return SETTINGS_SCREEN_NONE;
}

/* Drop every built page except the one currently on screen. Settings is
   a place you pass through: paging to the next one, or leaving the menu
   altogether, means the pages behind you are holding ~1.8KB each of a
   128KB pool for tiles nobody is looking at - and the peak that matters
   is not the page count, it is having room for whatever transient screen
   comes next (a keyboard, the colour wheel, the QR code). Rebuilding one
   page is four tiles; the minigames menu already builds three pages'
   worth on every open.

   Asynchronous deletion because this runs from inside the navigation
   that just left those pages - an lv_obj_del() there would free objects
   LVGL is still walking. The row pointers into them have to be cleared
   in the same breath, or the next refresh_settings_pages_ui() writes
   into freed labels. */
void settings_release_pages(void)
{
    int page;

    for (page = 0; page < settings_page_count; page++) {
        int first = page * 3;
        int i;

        if (settings_pages[page] == NULL) continue;
        if (settings_pages[page] == lv_scr_act()) continue;

        for (i = first; i < first + 3 && i < SETTINGS_ITEM_COUNT; i++) {
            setting_btns[i] = NULL;
            setting_lbls[i] = NULL;
        }
        lv_obj_del_async(settings_pages[page]);
        settings_pages[page] = NULL;
    }
}

void settings_show_page(int page)
{
    lv_obj_t *screen = settings_page_screen(page);
    if (screen == NULL) return;
    lv_scr_load(screen);
    settings_release_pages();
}

/* Knob left/right flips between settings pages, with wraparound.
   Returns false when the active screen is not a settings page. */
bool settings_knob_page(int dir)
{
    int i;

    for (i = 0; i < settings_page_count; i++) {
        if (settings_pages[i] != NULL && lv_scr_act() == settings_pages[i]) {
            settings_show_page((i + dir + settings_page_count) % settings_page_count);
            return true;
        }
    }
    return false;
}

/* Test-only: the id of the first navigation row nobody bound, or NULL
   when every one of them is wired. An unbound row draws dimmed and does
   nothing, which is easy to miss in a screenshot and impossible to miss
   here. */
const char *settings_test_unbound_item(void)
{
    int i;
    for (i = 0; i < SETTINGS_ITEM_COUNT; i++) {
        if (settings_items[i].get == NULL && item_open[i] == NULL)
            return settings_items[i].id;
    }
    return NULL;
}

int settings_item_page(const char *id)
{
    int i;
    for (i = 0; i < SETTINGS_ITEM_COUNT; i++) {
        if (strcmp(settings_items[i].id, id) == 0)
            return setting_page_of[i];
    }
    return -1;
}

void build_settings_screen(void)
{
    screen_settings = lv_obj_create(NULL);
    lv_obj_set_size(screen_settings, 360, 360);
    lv_obj_set_style_bg_color(screen_settings, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_settings, 0, 0);
    lv_obj_set_scrollbar_mode(screen_settings, LV_SCROLLBAR_MODE_OFF);

    arc_brightness = lv_arc_create(screen_settings);
    lv_obj_set_size(arc_brightness, 280, 280);
    lv_obj_align(arc_brightness, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_rotation(arc_brightness, 90);
    lv_arc_set_bg_angles(arc_brightness, 0, 360);
    lv_arc_set_range(arc_brightness, 0, 100);
    lv_arc_set_value(arc_brightness, brightness_percent);
    lv_obj_remove_style(arc_brightness, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc_brightness, LV_OBJ_FLAG_CLICKABLE);

    label_settings_value = lv_label_create(screen_settings);
    lv_label_set_text(label_settings_value, "Brightness: 80%"); /* placeholder, overwritten by refresh_settings_ui() */
    lv_obj_set_style_text_color(label_settings_value, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_settings_value, &lv_font_es_32, 0);
    lv_obj_align(label_settings_value, LV_ALIGN_CENTER, 0, -14);

    label_settings_hint = lv_label_create(screen_settings);
    lv_label_set_text(label_settings_hint, t(STR_BRIGHTNESS_HINT));
    lv_obj_set_style_text_color(label_settings_hint, lv_color_hex(0x6A6A6A), 0);
    lv_obj_set_style_text_font(label_settings_hint, &lv_font_es_14, 0);
    lv_obj_align(label_settings_hint, LV_ALIGN_CENTER, 0, 24);
}
