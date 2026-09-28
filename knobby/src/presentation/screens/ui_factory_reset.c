#include "ui_factory_reset.h"
#include "../../adapters/prefs.h"
#include "../../adapters/hw.h"
#include "../../adapters/lang.h"
#include "../../adapters/net_sync.h"

/* See ui_factory_reset.h. */

lv_obj_t *screen_factory_reset = NULL;

static lv_obj_t *label_factory_reset_body = NULL;

static void event_factory_reset_confirm(lv_event_t *e)
{
    (void)e;

    /* Say so before doing it: the erase plus the NVS commit takes long
       enough on this CPU to look like a dead screen, and the reboot
       right after gives no later chance to draw anything. */
    if (label_factory_reset_body != NULL) {
        lv_label_set_text(label_factory_reset_body, t(STR_FACTORY_RESET_DONE));
        lv_refr_now(NULL);
    }

    /* A session mirroring state to other devices at the table must not
       outlive the values being mirrored (and the radio should not be up
       across the reboot). */
    net_sync_leave_game();

    prefs_factory_reset();
    hw_reboot();
}

/* Built on first open, like the naming screen beside it in
   screen_registry: a confirmation most sessions never ask for has no
   business holding LVGL heap from boot. */
static void ensure_built(void)
{
    if (screen_factory_reset == NULL) build_factory_reset_screen();
}

void open_factory_reset_screen(void)
{
    ensure_built();
    if (label_factory_reset_body != NULL) {
        /* Reopening after a cancel must not still say "Restarting...". */
        lv_label_set_text(label_factory_reset_body, t(STR_FACTORY_RESET_BODY));
    }
    load_screen_if_needed(screen_factory_reset);
}

void build_factory_reset_screen(void)
{
    lv_obj_t *title;
    lv_obj_t *btn_confirm;
    lv_obj_t *label_confirm;

    screen_factory_reset = lv_obj_create(NULL);
    lv_obj_set_size(screen_factory_reset, 360, 360);
    lv_obj_set_style_bg_color(screen_factory_reset, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_factory_reset, 0, 0);
    lv_obj_set_scrollbar_mode(screen_factory_reset, LV_SCROLLBAR_MODE_OFF);

    title = lv_label_create(screen_factory_reset);
    lv_label_set_text(title, t(STR_FACTORY_RESET_TITLE));
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_es_22, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 56);

    label_factory_reset_body = lv_label_create(screen_factory_reset);
    lv_label_set_text(label_factory_reset_body, t(STR_FACTORY_RESET_BODY));
    lv_obj_set_style_text_color(label_factory_reset_body, lv_color_hex(0xBDBDBD), 0);
    lv_obj_set_style_text_font(label_factory_reset_body, &lv_font_es_16, 0);
    lv_obj_set_style_text_align(label_factory_reset_body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label_factory_reset_body, 280);
    lv_label_set_long_mode(label_factory_reset_body, LV_LABEL_LONG_WRAP);
    lv_obj_align(label_factory_reset_body, LV_ALIGN_CENTER, 0, -4);

    /* Long press only - no LV_EVENT_CLICKED handler at all, so a tap
       does nothing rather than doing half of this. Red because it is
       the one button on the device that destroys data. */
    btn_confirm = lv_btn_create(screen_factory_reset);
    lv_obj_set_size(btn_confirm, 150, 52);
    lv_obj_set_style_bg_color(btn_confirm, lv_color_hex(0xB00020), 0);
    lv_obj_add_event_cb(btn_confirm, event_factory_reset_confirm, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_align(btn_confirm, LV_ALIGN_BOTTOM_MID, 0, -54);
    label_confirm = lv_label_create(btn_confirm);
    lv_label_set_text(label_confirm, t(STR_FACTORY_RESET_HOLD));
    lv_obj_set_style_text_align(label_confirm, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label_confirm);

    /* No cancel button: the back gesture already leaves (this is a
       settings sub-screen, see nav.c), and an extra button would only
       add something else to hit by accident next to the red one. */
}
