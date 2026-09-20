#include "toast.h"
#include "types.h"

/* See toast.h. */

#define TOAST_MS 5000

static lv_obj_t *s_toast = NULL;
static lv_timer_t *s_toast_timer = NULL;

void toast_dismiss(void)
{
    if (s_toast_timer != NULL) {
        lv_timer_del(s_toast_timer);
        s_toast_timer = NULL;
    }
    if (s_toast != NULL) {
        lv_obj_del(s_toast);
        s_toast = NULL;
    }
}

static void toast_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    s_toast_timer = NULL; /* one-shot timer deletes itself on return */
    toast_dismiss();
}

void toast_show(const char *msg, lv_event_cb_t click_cb)
{
    toast_dismiss();

    s_toast = lv_label_create(lv_layer_top());
    lv_label_set_text(s_toast, msg);
    lv_obj_set_style_text_font(s_toast, &lv_font_es_16, 0);
    lv_obj_set_style_text_align(s_toast, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_toast, lv_color_white(), 0);
    lv_obj_set_style_bg_color(s_toast, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_80, 0);
    lv_obj_set_style_radius(s_toast, 12, 0);
    lv_obj_set_style_pad_all(s_toast, 10, 0);
    lv_obj_set_width(s_toast, 220);
    lv_obj_align(s_toast, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_toast, click_cb, LV_EVENT_CLICKED, NULL);

    s_toast_timer = lv_timer_create(toast_timer_cb, TOAST_MS, NULL);
    lv_timer_set_repeat_count(s_toast_timer, 1);
}
