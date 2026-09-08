#include "ui_wifi.h"
#include "wifi_ota.h"
#include "storage.h"
#include "lang.h"
#include "settings.h"
#include "custom_keyboard.h"
#include "round_safe.h"
#include <string.h>

// ---------- screens ----------
lv_obj_t *screen_wifi_settings = NULL;
lv_obj_t *screen_wifi_scan_list = NULL;
lv_obj_t *screen_wifi_text_entry = NULL;
lv_obj_t *screen_wifi_status = NULL;
lv_obj_t *screen_ota_update = NULL;
lv_obj_t *screen_ota_qr = NULL;

// ---------- wifi settings widgets ----------
static lv_obj_t *wifi_tile_ssid_label = NULL;
static lv_obj_t *wifi_tile_connect_label = NULL;

// ---------- scan list widgets/state ----------
static lv_obj_t *scan_list_container = NULL;
static int scan_pending_index = -1; /* which scanned network the user just tapped, -1 = manual entry */

/* The container itself never moves - only its (scrollable) content
 * does - so sizing it to the round-safe width for its own fixed y-span
 * keeps every row inside the glass no matter how far the list is
 * scrolled. Centering that span closer to the display's vertical
 * middle (instead of pinning it to the top) is what buys back most of
 * the width a naive top-anchored rectangle would otherwise lose. */
#define SCAN_LIST_Y1 68
#define SCAN_LIST_Y2 292
static int scan_list_width = 280;

// ---------- text entry widgets/state ----------
static lv_obj_t *label_entry_title = NULL;
static lv_obj_t *textarea_entry = NULL;
static custom_keyboard_t text_entry_kb;
static bool entry_is_password = false;

// ---------- status widgets ----------
static lv_obj_t *label_wifi_status_body = NULL;

// ---------- ota widgets ----------
static lv_obj_t *label_ota_current = NULL;
static lv_obj_t *label_ota_status = NULL;
static lv_obj_t *btn_ota_apply = NULL;
static lv_obj_t *btn_ota_open_wifi = NULL;
static lv_obj_t *arc_ota_progress = NULL;
static lv_obj_t *btn_ota_qr = NULL;

// ---------- wifi settings ----------
static bool failed_clear_timer_pending = false;

static void event_clear_wifi_failed_cb(lv_timer_t *timer)
{
    lv_timer_del(timer);
    failed_clear_timer_pending = false;
    wifi_clear_failed_state();
    refresh_wifi_settings_ui();
}

void refresh_wifi_settings_ui(void)
{
    char buf[40];
    char ssid[WIFI_SSID_LEN];
    nvs_get_wifi_ssid(ssid, sizeof(ssid));

    if (wifi_tile_ssid_label != NULL) {
        if (ssid[0] == '\0') {
            lv_label_set_text(wifi_tile_ssid_label, t(STR_WIFI_SSID));
        } else {
            snprintf(buf, sizeof(buf), "%s\n%s", t(STR_WIFI_SSID), ssid);
            lv_label_set_text(wifi_tile_ssid_label, buf);
        }
    }

    if (wifi_tile_connect_label != NULL) {
        switch (wifi_get_state()) {
            case WIFI_STATE_CONNECTING:
                lv_label_set_text(wifi_tile_connect_label, t(STR_WIFI_CONNECTING));
                break;
            case WIFI_STATE_CONNECTED:
                snprintf(buf, sizeof(buf), t(STR_WIFI_DISCONNECT_FMT), wifi_get_ip());
                lv_label_set_text(wifi_tile_connect_label, buf);
                break;
            case WIFI_STATE_FAILED:
                lv_label_set_text(wifi_tile_connect_label, t(STR_WIFI_FAILED));
                /* Otherwise this tile looks permanently stuck/broken -
                   it's still tappable to retry immediately, but nothing
                   on screen suggests that. Revert the label back to the
                   normal "Connect" after a couple of seconds. */
                if (!failed_clear_timer_pending) {
                    failed_clear_timer_pending = true;
                    lv_timer_create(event_clear_wifi_failed_cb, 2000, NULL);
                }
                break;
            default:
                lv_label_set_text(wifi_tile_connect_label, t(STR_WIFI_CONNECT));
                break;
        }
    }
}

void open_wifi_settings_screen(void)
{
    refresh_wifi_settings_ui();
    load_screen_if_needed(screen_wifi_settings);
}

static void event_wifi_status_auto_exit_cb(lv_timer_t *timer)
{
    lv_timer_del(timer);
    settings_handle_back(screen_wifi_settings);
}

/* Jumps to the connection-result screen, performs the (blocking)
   connection attempt, and reports success/failure there - used by every
   path that ends with "we now have an SSID+password, try it": the
   settings tile, an open network tapped in the scan list, and finishing
   the password prompt. Also persists the credentials via wifi_connect(). */
static void attempt_connect_and_show_status(const char *ssid, const char *pass)
{
    char buf[80];

    snprintf(buf, sizeof(buf), t(STR_WIFI_CONNECTING_TO_FMT), ssid);
    if (label_wifi_status_body != NULL) lv_label_set_text(label_wifi_status_body, buf);
    load_screen_if_needed(screen_wifi_status);
    lv_refr_now(NULL);

    wifi_connect(ssid, pass);
    settings_save();

    if (wifi_get_state() == WIFI_STATE_CONNECTED) {
        snprintf(buf, sizeof(buf), t(STR_WIFI_CONNECT_OK_FMT), ssid, wifi_get_ip());
        if (label_wifi_status_body != NULL) lv_label_set_text(label_wifi_status_body, buf);
        refresh_wifi_settings_ui();
        /* Nothing more to do once connected - show the confirmation
           briefly, then leave WiFi settings on its own rather than
           making the user tap OK just to get back to what they were
           doing. */
        lv_timer_create(event_wifi_status_auto_exit_cb, 1500, NULL);
    } else {
        snprintf(buf, sizeof(buf), t(STR_WIFI_CONNECT_FAIL_FMT), ssid);
        if (label_wifi_status_body != NULL) lv_label_set_text(label_wifi_status_body, buf);
        refresh_wifi_settings_ui();
    }
}

static lv_obj_t *btn_toggle_pw_visibility = NULL;
static bool pw_visible = false;

static void open_text_entry(bool is_password)
{
    char current[WIFI_PASS_LEN];
    entry_is_password = is_password;
    pw_visible = false;

    if (label_entry_title != NULL) {
        if (is_password) {
            char ssid[WIFI_SSID_LEN];
            char buf[64];
            nvs_get_wifi_ssid(ssid, sizeof(ssid));
            snprintf(buf, sizeof(buf), t(STR_WIFI_ENTER_PASSWORD_FMT), ssid);
            lv_label_set_text(label_entry_title, buf);
        } else {
            lv_label_set_text(label_entry_title, t(STR_WIFI_ENTER_SSID));
        }
    }
    if (textarea_entry != NULL) {
        lv_textarea_set_password_mode(textarea_entry, is_password);
        if (is_password) {
            nvs_get_wifi_pass(current, sizeof(current));
        } else {
            nvs_get_wifi_ssid(current, sizeof(current));
        }
        lv_textarea_set_text(textarea_entry, current);
    }
    if (btn_toggle_pw_visibility != NULL) {
        if (is_password) {
            lv_obj_clear_flag(btn_toggle_pw_visibility, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(btn_toggle_pw_visibility, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_t *lbl = lv_obj_get_child(btn_toggle_pw_visibility, 0);
        if (lbl != NULL) lv_label_set_text(lbl, LV_SYMBOL_EYE_OPEN);
    }
    custom_keyboard_reset(&text_entry_kb); /* required every open - see custom_keyboard.h */
    load_screen_if_needed(screen_wifi_text_entry);
}

static lv_obj_t *label_scan_status = NULL;

static void event_scan_row_click(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx == -2) return; /* "no networks found" placeholder row */
    if (idx == -1) {
        open_text_entry(false);
        return;
    }
    if (wifi_scan_is_open(idx)) {
        attempt_connect_and_show_status(wifi_scan_get_ssid(idx), "");
    } else {
        nvs_set_wifi_ssid(wifi_scan_get_ssid(idx));
        settings_save();
        open_text_entry(true);
    }
}

static void add_scan_row(int idx, const char *text)
{
    lv_obj_t *row = lv_obj_create(scan_list_container);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, scan_list_width - 20, 32);
    lv_obj_set_style_radius(row, 4, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x1E1E2E), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, event_scan_row_click, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_es_16, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);
}

static void populate_scan_list(void)
{
    char buf[48];
    int count = wifi_scan_start(); /* blocking */

    lv_obj_clean(scan_list_container);
    if (count == 0) {
        add_scan_row(-2, t(STR_WIFI_NO_NETWORKS));
    }
    for (int i = 0; i < count; i++) {
        if (wifi_scan_is_open(i)) {
            snprintf(buf, sizeof(buf), "%s%s", wifi_scan_get_ssid(i), t(STR_WIFI_OPEN_SUFFIX));
            add_scan_row(i, buf);
        } else {
            add_scan_row(i, wifi_scan_get_ssid(i));
        }
    }
    add_scan_row(-1, t(STR_WIFI_TYPE_MANUALLY));
    lv_obj_scroll_to_y(scan_list_container, 0, LV_ANIM_OFF);
}

void open_wifi_scan_list_screen(void)
{
    lv_obj_clean(scan_list_container);
    if (label_scan_status != NULL) {
        lv_label_set_text(label_scan_status, t(STR_WIFI_SCANNING));
        lv_obj_clear_flag(label_scan_status, LV_OBJ_FLAG_HIDDEN);
    }
    load_screen_if_needed(screen_wifi_scan_list);
    lv_refr_now(NULL); /* paint "Scanning..." before the blocking scan below */

    populate_scan_list();
    if (label_scan_status != NULL) lv_obj_add_flag(label_scan_status, LV_OBJ_FLAG_HIDDEN);
}

bool wifi_scan_list_handle_back(void)
{
    open_wifi_settings_screen();
    return true;
}

void build_wifi_scan_list_screen(void)
{
    screen_wifi_scan_list = lv_obj_create(NULL);
    lv_obj_set_size(screen_wifi_scan_list, 360, 360);
    lv_obj_set_style_bg_color(screen_wifi_scan_list, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_wifi_scan_list, 0, 0);
    lv_obj_set_scrollbar_mode(screen_wifi_scan_list, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title = lv_label_create(screen_wifi_scan_list);
    lv_label_set_text(title, t(STR_WIFI_SSID));
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_es_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    label_scan_status = lv_label_create(screen_wifi_scan_list);
    lv_label_set_text(label_scan_status, t(STR_WIFI_SCANNING));
    lv_obj_set_style_text_color(label_scan_status, lv_color_hex(0x7A7A7A), 0);
    lv_obj_set_style_text_font(label_scan_status, &lv_font_es_14, 0);
    lv_obj_align(label_scan_status, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_add_flag(label_scan_status, LV_OBJ_FLAG_HIDDEN);

    scan_list_width = round_safe_width(SCAN_LIST_Y1, SCAN_LIST_Y2);
    scan_list_container = lv_obj_create(screen_wifi_scan_list);
    lv_obj_remove_style_all(scan_list_container);
    lv_obj_set_size(scan_list_container, scan_list_width, SCAN_LIST_Y2 - SCAN_LIST_Y1);
    lv_obj_align(scan_list_container, LV_ALIGN_TOP_MID, 0, SCAN_LIST_Y1);
    lv_obj_set_flex_flow(scan_list_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scan_list_container, 4, 0);
    lv_obj_set_scrollbar_mode(scan_list_container, LV_SCROLLBAR_MODE_OFF);
}

static void event_wifi_edit_ssid(lv_event_t *e)
{
    (void)e;
    open_wifi_scan_list_screen();
}

static void event_wifi_edit_password(lv_event_t *e)
{
    (void)e;
    open_text_entry(true);
}

static void event_wifi_connect(lv_event_t *e)
{
    (void)e;
    /* This tile doubles as Connect/Disconnect depending on current
       state, mirroring its label from refresh_wifi_settings_ui(). */
    if (wifi_get_state() == WIFI_STATE_CONNECTED) {
        wifi_disconnect();
        refresh_wifi_settings_ui();
        return;
    }

    char ssid[WIFI_SSID_LEN];
    char pass[WIFI_PASS_LEN];
    nvs_get_wifi_ssid(ssid, sizeof(ssid));
    nvs_get_wifi_pass(pass, sizeof(pass));
    if (ssid[0] == '\0') return;
    attempt_connect_and_show_status(ssid, pass);
}

static void event_wifi_forget(lv_event_t *e)
{
    (void)e;
    nvs_set_wifi_ssid("");
    nvs_set_wifi_pass("");
    settings_save();
    refresh_wifi_settings_ui();
}

void build_wifi_settings_screen(void)
{
    quad_item_t items[4] = {
        {t(STR_WIFI_SSID), event_wifi_edit_ssid, true, LV_EVENT_CLICKED},
        {t(STR_WIFI_PASSWORD), event_wifi_edit_password, true, LV_EVENT_CLICKED},
        {t(STR_WIFI_CONNECT), event_wifi_connect, true, LV_EVENT_CLICKED},
        {t(STR_WIFI_FORGET_HOLD), event_wifi_forget, true, LV_EVENT_LONG_PRESSED},
    };
    build_quad_screen(&screen_wifi_settings, items);

    wifi_tile_ssid_label = lv_obj_get_child(lv_obj_get_child(screen_wifi_settings, 0), 0);
    wifi_tile_connect_label = lv_obj_get_child(lv_obj_get_child(screen_wifi_settings, 2), 0);
}

// ---------- text entry ----------
static void event_entry_save(lv_event_t *e)
{
    (void)e;
    if (textarea_entry == NULL) return;
    const char *text = lv_textarea_get_text(textarea_entry);
    if (entry_is_password) {
        /* The whole point of typing a password is to connect with it -
           do that immediately instead of silently saving and dumping
           the user back on the settings screen with no feedback. */
        char ssid[WIFI_SSID_LEN];
        nvs_get_wifi_ssid(ssid, sizeof(ssid));
        attempt_connect_and_show_status(ssid, text);
    } else {
        nvs_set_wifi_ssid(text);
        settings_save();
        /* A bare SSID still needs a password (or an explicit blank one
           for open networks) before it's useful - go straight there
           instead of a screen that doesn't do anything on its own. */
        open_text_entry(true);
    }
}

static void event_toggle_pw_visibility(lv_event_t *e)
{
    (void)e;
    pw_visible = !pw_visible;
    lv_textarea_set_password_mode(textarea_entry, !pw_visible);
    lv_obj_t *lbl = lv_obj_get_child(btn_toggle_pw_visibility, 0);
    if (lbl != NULL) lv_label_set_text(lbl, pw_visible ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

/* The keyboard no longer has left/right arrow keys (hard to hit and
   unreliable to tap; see custom_keyboard.c history) - the knob is now
   the only way to reposition the cursor while typing. */
void wifi_text_entry_knob(int dir)
{
    if (textarea_entry == NULL) return;
    if (dir < 0) lv_textarea_cursor_left(textarea_entry);
    else if (dir > 0) lv_textarea_cursor_right(textarea_entry);
}

bool wifi_text_entry_handle_back(void)
{
    open_wifi_settings_screen();
    return true;
}

void build_wifi_text_entry_screen(void)
{
    screen_wifi_text_entry = lv_obj_create(NULL);
    lv_obj_set_size(screen_wifi_text_entry, 360, 360);
    lv_obj_set_style_bg_color(screen_wifi_text_entry, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_wifi_text_entry, 0, 0);
    lv_obj_set_scrollbar_mode(screen_wifi_text_entry, LV_SCROLLBAR_MODE_OFF);

    /* Sitting right at the very top of the round glass leaves almost no
       safe width (the circle is narrowest there), which is why this
       title used to get its corners clipped - see round_safe.h. Pulling
       it down closer to the keyboard (which starts at y=130) buys back
       enough width for its two lines. */
    label_entry_title = lv_label_create(screen_wifi_text_entry);
    lv_label_set_text(label_entry_title, t(STR_WIFI_ENTER_SSID));
    lv_obj_set_style_text_color(label_entry_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_entry_title, &lv_font_es_14, 0);
    lv_obj_set_style_text_align(label_entry_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label_entry_title, 220);
    lv_obj_align(label_entry_title, LV_ALIGN_TOP_MID, 0, 44);

    /* Textarea + show/hide toggle as one centered block instead of
       pinned to the left/right edges, so neither one lands in the
       clipped corner area. */
    textarea_entry = lv_textarea_create(screen_wifi_text_entry);
    lv_obj_set_size(textarea_entry, 200, 40);
    lv_obj_align(textarea_entry, LV_ALIGN_TOP_MID, -24, 86);
    lv_textarea_set_max_length(textarea_entry, WIFI_PASS_LEN - 1);
    lv_textarea_set_one_line(textarea_entry, true);
    lv_obj_set_style_text_font(textarea_entry, &lv_font_es_16, 0);

    /* Show/hide toggle for the masked password field - only visible
       when entering a password, see open_text_entry(). */
    btn_toggle_pw_visibility = make_button(screen_wifi_text_entry, LV_SYMBOL_EYE_OPEN, 44, 40, event_toggle_pw_visibility);
    lv_obj_align(btn_toggle_pw_visibility, LV_ALIGN_TOP_MID, 104, 86);
    lv_obj_add_flag(btn_toggle_pw_visibility, LV_OBJ_FLAG_HIDDEN);

    /* Custom round-display-shaped keyboard (see custom_keyboard.h) -
       its own Enter key fires LV_EVENT_READY, so no separate "OK"
       button is needed here. */
    custom_keyboard_build(&text_entry_kb, screen_wifi_text_entry);
    custom_keyboard_set_textarea(&text_entry_kb, textarea_entry);
    custom_keyboard_set_ready_cb(&text_entry_kb, event_entry_save);
}

// ---------- connection status ----------
static void event_wifi_status_ok(lv_event_t *e)
{
    (void)e;
    open_wifi_settings_screen();
}

bool wifi_status_handle_back(void)
{
    open_wifi_settings_screen();
    return true;
}

void build_wifi_status_screen(void)
{
    screen_wifi_status = lv_obj_create(NULL);
    lv_obj_set_size(screen_wifi_status, 360, 360);
    lv_obj_set_style_bg_color(screen_wifi_status, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_wifi_status, 0, 0);
    lv_obj_set_scrollbar_mode(screen_wifi_status, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title = lv_label_create(screen_wifi_status);
    lv_label_set_text(title, t(STR_WIFI_STATUS_TITLE));
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_es_22, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    label_wifi_status_body = lv_label_create(screen_wifi_status);
    lv_obj_set_style_text_color(label_wifi_status_body, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_wifi_status_body, &lv_font_es_16, 0);
    lv_obj_set_style_text_align(label_wifi_status_body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label_wifi_status_body, 320);
    lv_label_set_long_mode(label_wifi_status_body, LV_LABEL_LONG_WRAP);
    lv_obj_align(label_wifi_status_body, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_ok = make_button(screen_wifi_status, t(STR_OK), 140, 46, event_wifi_status_ok);
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_MID, 0, -44);
}

// ---------- ota update ----------
/* Set only when a check was actually attempted without a connection -
   the "Open WiFi" shortcut is a reaction to that specific tap, not a
   standing hint shown just because the screen happens to be open. */
static bool ota_needs_wifi_hint = false;

void refresh_ota_update_ui(void)
{
    char buf[64];

    if (label_ota_current != NULL) {
        snprintf(buf, sizeof(buf), t(STR_OTA_CURRENT_FMT), get_firmware_version());
        lv_label_set_text(label_ota_current, buf);
    }

    if (label_ota_status == NULL) return;

    if (btn_ota_apply != NULL) {
        if (ota_get_state() == OTA_STATE_AVAILABLE) {
            lv_obj_clear_flag(btn_ota_apply, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(btn_ota_apply, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* Quick access to WiFi settings, but only after a check actually
       failed for lack of a connection - same on-screen slot as Apply
       since an update can only be OTA_STATE_AVAILABLE once WiFi is
       connected, so the two are never needed at the same time. */
    if (wifi_get_state() == WIFI_STATE_CONNECTED) ota_needs_wifi_hint = false;
    if (btn_ota_open_wifi != NULL) {
        if (ota_needs_wifi_hint) {
            lv_obj_clear_flag(btn_ota_open_wifi, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(btn_ota_open_wifi, LV_OBJ_FLAG_HIDDEN);
        }
    }

    switch (ota_get_state()) {
        case OTA_STATE_CHECKING:
            lv_label_set_text(label_ota_status, t(STR_OTA_CHECKING));
            break;
        case OTA_STATE_UP_TO_DATE:
            lv_label_set_text(label_ota_status, t(STR_OTA_UP_TO_DATE));
            break;
        case OTA_STATE_AVAILABLE:
            snprintf(buf, sizeof(buf), t(STR_OTA_AVAILABLE_FMT), ota_get_latest_version());
            lv_label_set_text(label_ota_status, buf);
            break;
        case OTA_STATE_UPDATING:
            lv_label_set_text(label_ota_status, t(STR_OTA_UPDATING));
            break;
        case OTA_STATE_ERROR:
            snprintf(buf, sizeof(buf), t(STR_OTA_ERROR_FMT), ota_get_error());
            lv_label_set_text(label_ota_status, buf);
            break;
        default:
            lv_label_set_text(label_ota_status, t(STR_OTA_TAP_TO_CHECK));
            break;
    }
}

void open_ota_update_screen(void)
{
    ota_needs_wifi_hint = false;
    refresh_ota_update_ui();
    load_screen_if_needed(screen_ota_update);
}

static void event_ota_check(lv_event_t *e)
{
    (void)e;
    if (wifi_get_state() != WIFI_STATE_CONNECTED) {
        /* Refresh first (shows the "Open WiFi" shortcut button), then
           override its default label with this specific message. */
        ota_needs_wifi_hint = true;
        refresh_ota_update_ui();
        lv_label_set_text(label_ota_status, t(STR_OTA_NEED_WIFI));
        return;
    }
    lv_label_set_text(label_ota_status, t(STR_OTA_CHECKING));
    lv_refr_now(NULL);
    ota_check_now();
    refresh_ota_update_ui();
}

/* ota_apply_update() blocks for the whole download+flash and calls this
   straight from inside that call (see ota_set_progress_cb()), so the
   redraw has to happen here too - nothing else pumps the display while
   it's running. */
static void ota_progress_update(int percent)
{
    if (arc_ota_progress == NULL) return;
    lv_arc_set_value(arc_ota_progress, percent);
    lv_refr_now(NULL);
}

static void event_ota_apply(lv_event_t *e)
{
    (void)e;
    if (ota_get_state() != OTA_STATE_AVAILABLE) return;
    lv_label_set_text(label_ota_status, t(STR_OTA_UPDATING));
    if (arc_ota_progress != NULL) {
        lv_arc_set_value(arc_ota_progress, 0);
        lv_obj_clear_flag(arc_ota_progress, LV_OBJ_FLAG_HIDDEN);
    }
    lv_refr_now(NULL);
    ota_apply_update(); /* reboots on success and never returns */
    if (arc_ota_progress != NULL) {
        lv_obj_add_flag(arc_ota_progress, LV_OBJ_FLAG_HIDDEN);
    }
    refresh_ota_update_ui();
}

static void event_ota_open_wifi(lv_event_t *e)
{
    (void)e;
    open_wifi_settings_screen();
}

static void event_ota_qr(lv_event_t *e)
{
    (void)e;
    open_ota_qr_screen();
}

void open_ota_qr_screen(void)
{
    load_screen_if_needed(screen_ota_qr);
}

void build_ota_qr_screen(void)
{
    lv_obj_t *qr;
    lv_obj_t *hint;

    screen_ota_qr = lv_obj_create(NULL);
    lv_obj_set_size(screen_ota_qr, 360, 360);
    lv_obj_set_style_bg_color(screen_ota_qr, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_ota_qr, 0, 0);
    lv_obj_set_scrollbar_mode(screen_ota_qr, LV_SCROLLBAR_MODE_OFF);

    /* The code's own light_color already gives it a white quiet zone
       against the app's black background - plenty of contrast for a
       phone camera without needing to flip the whole screen white. */
    qr = lv_qrcode_create(screen_ota_qr, 220, lv_color_black(), lv_color_white());
    lv_qrcode_update(qr, KNOBBY_RELEASES_URL, strlen(KNOBBY_RELEASES_URL));
    lv_obj_center(qr);

    hint = lv_label_create(screen_ota_qr);
    lv_label_set_text(hint, t(STR_OTA_QR_HINT));
    lv_obj_set_style_text_color(hint, lv_color_hex(0x7A7A7A), 0);
    lv_obj_set_style_text_font(hint, &lv_font_es_14, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -34);
}

void build_ota_update_screen(void)
{
    screen_ota_update = lv_obj_create(NULL);
    lv_obj_set_size(screen_ota_update, 360, 360);
    lv_obj_set_style_bg_color(screen_ota_update, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_ota_update, 0, 0);
    lv_obj_set_scrollbar_mode(screen_ota_update, LV_SCROLLBAR_MODE_OFF);

    /* Full-perimeter ring instead of a bar: this display is round, and
       hugging the bezel leaves the center free for the existing status
       text instead of squeezing a straight bar in above/below it. Built
       first so it sits behind everything else drawn on this screen. */
    arc_ota_progress = lv_arc_create(screen_ota_update);
    lv_obj_set_size(arc_ota_progress, 356, 356);
    lv_obj_center(arc_ota_progress);
    lv_arc_set_bg_angles(arc_ota_progress, 0, 360);
    lv_arc_set_rotation(arc_ota_progress, 270); /* 0% starts at 12 o'clock */
    lv_arc_set_range(arc_ota_progress, 0, 100);
    lv_arc_set_value(arc_ota_progress, 0);
    lv_obj_remove_style(arc_ota_progress, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc_ota_progress, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc_ota_progress, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_ota_progress, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_ota_progress, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_ota_progress, lv_color_hex(0x06D6A0), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc_ota_progress, false, LV_PART_INDICATOR);
    lv_obj_add_flag(arc_ota_progress, LV_OBJ_FLAG_HIDDEN); /* shown only while actually updating */
    ota_set_progress_cb(ota_progress_update);

    lv_obj_t *title = lv_label_create(screen_ota_update);
    lv_label_set_text(title, t(STR_OTA_TITLE));
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_es_22, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    label_ota_current = lv_label_create(screen_ota_update);
    lv_obj_set_style_text_color(label_ota_current, lv_color_hex(0x7A7A7A), 0);
    lv_obj_set_style_text_font(label_ota_current, &lv_font_es_14, 0);
    lv_obj_align(label_ota_current, LV_ALIGN_TOP_MID, 0, 76);

    label_ota_status = lv_label_create(screen_ota_update);
    lv_label_set_text(label_ota_status, t(STR_OTA_TAP_TO_CHECK));
    lv_obj_set_style_text_color(label_ota_status, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_ota_status, &lv_font_es_16, 0);
    lv_obj_set_style_text_align(label_ota_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label_ota_status, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_check = make_button(screen_ota_update, t(STR_OTA_CHECK_NOW), 140, 46, event_ota_check);
    lv_obj_align(btn_check, LV_ALIGN_BOTTOM_MID, 0, -100);

    btn_ota_apply = make_button(screen_ota_update, t(STR_OTA_APPLY_HOLD), 140, 46, NULL);
    lv_obj_add_event_cb(btn_ota_apply, event_ota_apply, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_align(btn_ota_apply, LV_ALIGN_BOTTOM_MID, 0, -44);
    lv_obj_add_flag(btn_ota_apply, LV_OBJ_FLAG_HIDDEN); /* shown only once an update is found */

    btn_ota_open_wifi = make_button(screen_ota_update, t(STR_OTA_OPEN_WIFI), 140, 46, event_ota_open_wifi);
    lv_obj_align(btn_ota_open_wifi, LV_ALIGN_BOTTOM_MID, 0, -44);
    lv_obj_add_flag(btn_ota_open_wifi, LV_OBJ_FLAG_HIDDEN); /* shown only while WiFi isn't connected */

    /* Small, always-available shortcut to the QR code screen - not
       tied to check/apply state like the buttons above, so it sits off
       to the side instead of in that stack. */
    btn_ota_qr = lv_btn_create(screen_ota_update);
    lv_obj_set_size(btn_ota_qr, 44, 44);
    lv_obj_set_style_radius(btn_ota_qr, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(btn_ota_qr, LV_ALIGN_TOP_RIGHT, -24, 36);
    lv_obj_add_event_cb(btn_ota_qr, event_ota_qr, LV_EVENT_CLICKED, NULL);
    lv_obj_t *qr_icon = lv_label_create(btn_ota_qr);
    lv_label_set_text(qr_icon, "QR");
    lv_obj_set_style_text_font(qr_icon, &lv_font_es_14, 0);
    lv_obj_center(qr_icon);
}
