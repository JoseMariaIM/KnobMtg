#include "ota_notice.h"
#include "presentation/widgets/toast.h"
#include "types.h"
#include "adapters/prefs_network.h"
#include "adapters/lang.h"
#include "presentation/ota/wifi_ota.h"
#include "ui_wifi.h"
#include "../knob.h"
#include <stdio.h>
#include <string.h>

/* See ota_notice.h. */

static void update_toast_click_cb(lv_event_t *e)
{
    (void)e;
    toast_dismiss();
    /* The OTA screen's back gesture returns to whatever screen sent it
       there (see settings_handle_back()'s chain back to screen_quad_menu,
       then knob.c's previous_screen check) - normally that's only ever
       reached via the settings menu, which sets this itself. Tapping the
       toast is a shortcut around that whole chain, so it has to set the
       breadcrumb too, or back leaves the user stranded in settings with
       no way to reach the life counter again. */
    knob_remember_return_screen(lv_scr_act());
    open_ota_update_screen();
}

/* Rather than a persistent icon (too easy to miss on a screen this
   size), this drops a self-dismissing banner on lv_layer_top() (renders
   above whatever screen is active, independent of which one that is)
   spelling out a message in words; tapping it runs click_cb, which owns
   navigating wherever that message points to. Shared by the "update
   available" and "just updated" toasts below. */


/* The moment an update is first seen this shows the update-available
   toast; tapping it jumps straight to the update screen. */
static void show_update_toast(void)
{
    char msg[64];
    snprintf(msg, sizeof(msg), t(STR_OTA_AVAILABLE_FMT), ota_get_latest_version());
    toast_show(msg, update_toast_click_cb);
}

static void update_applied_toast_click_cb(lv_event_t *e)
{
    (void)e;
    toast_dismiss();
    /* Straight to the QR (release notes), skipping the Updates screen -
       open_ota_qr_screen_from_toast() marks the visit so knob.c's back
       handler sends back to the life counter instead of Updates, which
       this toast never went through. */
    open_ota_qr_screen_from_toast();
}

/* Shown once, right after boot, when the running firmware version
   differs from the one stored at the previous boot (see
   check_firmware_update_toast) - i.e. an OTA update just landed.
   Tapping it shows the QR code to the release notes. */
static void show_update_applied_toast(const char *version)
{
    char msg[64];
    snprintf(msg, sizeof(msg), t(STR_OTA_UPDATED_FMT), version);
    toast_show(msg, update_applied_toast_click_cb);
}

void ota_notice_check_after_boot(void)
{
    char last[FW_VERSION_LEN];
    const char *current = get_firmware_version();

    prefs_get_last_fw_version(last, sizeof(last));
    if (last[0] != '\0' && strcmp(last, current) != 0) {
        show_update_applied_toast(current);
    }
    if (strcmp(last, current) != 0) {
        /* Used to need a hand-written settings_save() here: this runs
           once at boot, and if the player never opened Settings
           nothing else would commit, so the stored version never
           advanced and the toast re-fired on every single boot.
           Writing now schedules its own flush. */
        prefs_set_last_fw_version(current);
    }
}

void ota_notice_poll(void)
{
    static bool s_toast_shown = false;
    bool available = (ota_get_state() == OTA_STATE_AVAILABLE);

    if (available && !s_toast_shown) {
        s_toast_shown = true;
        show_update_toast();
    } else if (!available) {
        s_toast_shown = false;
    }
}
