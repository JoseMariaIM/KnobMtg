#ifndef _TYPES_H
#define _TYPES_H

#include "knob.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Constants, plain structs and inline helpers shared with game_state.c
 * live in game_types.h, which doesn't include LVGL - see the comment at
 * its top. Re-included here so every existing #include "types.h" site
 * keeps seeing exactly the same symbols it always has. */
#include "game_types.h"

// ---------- types ----------
typedef struct {
    knob_event_t event;
} knob_input_event_t;

typedef struct {
    const char *label;
    lv_event_cb_t cb;
    bool enabled;
    lv_event_code_t event;
    const char *icon;
    const lv_font_t *icon_font;
    void *user_data;            /* passed to cb via lv_event_get_user_data */
} quad_item_t;

// ---------- life colors (lv_color_t - see game_types.h for the pure tier math) ----------
static inline lv_color_t get_life_color(int value, int max_life)
{
    return lv_color_hex(life_color_table[get_life_tier(value, max_life)][LIFE_VIB_MID]);
}

static inline lv_color_t get_life_color_vib(int tier, int vibrancy)
{
    return lv_color_hex(life_color_table[tier][vibrancy]);
}

static inline bool color_is_light(lv_color_t c)
{
    uint32_t c32 = lv_color_to32(c);
    int r = (c32 >> 16) & 0xFF;
    int g = (c32 >> 8) & 0xFF;
    int b = c32 & 0xFF;
    int lum = r * 299 + g * 587 + b * 114;
    return lum > 128000;
}

// ---------- LVGL widget helpers ----------
static inline lv_obj_t *make_button(lv_obj_t *parent, const char *txt,
                                     lv_coord_t w, lv_coord_t h,
                                     lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, txt);
    lv_obj_center(label);
    return btn;
}

static inline lv_obj_t *make_plain_box(lv_obj_t *parent,
                                        lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, w, h);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static inline void load_screen_if_needed(lv_obj_t *screen)
{
    if (screen != NULL && lv_scr_act() != screen) {
        lv_scr_load(screen);
    }
}

#endif // _TYPES_H
