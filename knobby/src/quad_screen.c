#include "quad_screen.h"
#include "types.h"

/* See quad_screen.h. */

void build_quad_screen(lv_obj_t **screen, quad_item_t items[4])
{
    int i;
    static const lv_coord_t qx[4] = {0,   182, 0,   182};
    static const lv_coord_t qy[4] = {0,   0,   182, 182};
    static const lv_coord_t lx[4] = {10, -10, 10, -10};
    static const lv_coord_t ly[4] = {15,  15, -15, -15};

    *screen = lv_obj_create(NULL);
    lv_obj_set_size(*screen, 360, 360);
    lv_obj_set_style_bg_color(*screen, lv_color_black(), 0);
    lv_obj_set_style_border_width(*screen, 0, 0);
    lv_obj_set_scrollbar_mode(*screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(*screen, 0, 0);

    for (i = 0; i < 4; i++) {
        lv_obj_t *btn = lv_btn_create(*screen);
        lv_obj_remove_style_all(btn);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_set_size(btn, 178, 178);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_pos(btn, qx[i], qy[i]);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);

        if (items[i].cb != NULL && items[i].enabled) {
            lv_obj_add_event_cb(btn, items[i].cb, items[i].event, items[i].user_data);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x1A1A2E), 0);
        } else {
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x111111), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_60, 0);
            lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        }

        if (items[i].icon != NULL && items[i].icon_font != NULL) {
            lv_obj_t *icon_lbl = lv_label_create(btn);
            lv_label_set_text(icon_lbl, items[i].icon);
            lv_obj_set_style_text_font(icon_lbl, items[i].icon_font, 0);
            lv_obj_set_style_text_color(icon_lbl,
                items[i].enabled ? lv_color_white() : lv_color_hex(0x555555), 0);
            lv_obj_align(icon_lbl, LV_ALIGN_CENTER, lx[i], ly[i] - 18);
        }

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, items[i].label);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_es_16, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, lx[i],
            (items[i].icon != NULL) ? ly[i] + 10 : ly[i]);

        if (!items[i].enabled) {
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x555555), 0);
        }
    }
}
