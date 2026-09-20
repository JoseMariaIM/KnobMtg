#include "ui_language.h"
#include "quad_screen.h"
#include "types.h"
#include "lang.h"
#include "round_safe.h"

lv_obj_t *screen_language_picker = NULL;

static lv_obj_t *language_list_container = NULL;

/* Only ever holds LANG_COUNT (currently 2) short rows, so instead of a
 * top-anchored rectangle we can afford to center this small container
 * on the display's vertical middle, where the circle is widest - see
 * round_safe.h. That keeps it essentially full-width with no clipping,
 * unlike a list long enough to need scrolling (compare scan_list_width
 * in ui_wifi.c, which can't be centered the same way). */
#define LANGUAGE_LIST_Y1 125
#define LANGUAGE_LIST_Y2 235
static int language_list_width = 280;

static void event_language_row_click(lv_event_t *e)
{
    lang_t lang = (lang_t)(intptr_t)lv_event_get_user_data(e);
    lang_set(lang); /* persists + restarts the device to relabel every screen */
}

static void add_language_row(lang_t lang)
{
    bool is_current = (lang == lang_get());

    lv_obj_t *row = lv_obj_create(language_list_container);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, language_list_width - 20, 40);
    lv_obj_set_style_radius(row, 4, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(is_current ? TOGGLE_ON : 0x1E1E2E), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, event_language_row_click, LV_EVENT_CLICKED, (void *)(intptr_t)lang);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, t(lang == LANG_ES ? STR_LANGUAGE_ES : STR_LANGUAGE_EN));
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_es_16, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 12, 0);
}

void open_language_picker_screen(void)
{
    int i;
    lv_obj_clean(language_list_container);
    for (i = 0; i < LANG_COUNT; i++) add_language_row((lang_t)i);
    load_screen_if_needed(screen_language_picker);
}

void build_language_picker_screen(void)
{
    screen_language_picker = lv_obj_create(NULL);
    lv_obj_set_size(screen_language_picker, 360, 360);
    lv_obj_set_style_bg_color(screen_language_picker, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_language_picker, 0, 0);
    lv_obj_set_scrollbar_mode(screen_language_picker, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title = lv_label_create(screen_language_picker);
    lv_label_set_text(title, t(STR_SETTING_LANGUAGE));
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_es_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    language_list_width = round_safe_width(LANGUAGE_LIST_Y1, LANGUAGE_LIST_Y2);
    language_list_container = lv_obj_create(screen_language_picker);
    lv_obj_remove_style_all(language_list_container);
    lv_obj_set_size(language_list_container, language_list_width, LANGUAGE_LIST_Y2 - LANGUAGE_LIST_Y1);
    lv_obj_align(language_list_container, LV_ALIGN_TOP_MID, 0, LANGUAGE_LIST_Y1);
    lv_obj_set_flex_flow(language_list_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(language_list_container, 8, 0);
    lv_obj_set_scrollbar_mode(language_list_container, LV_SCROLLBAR_MODE_OFF);
}
