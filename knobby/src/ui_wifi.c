#include "ui_wifi.h"
#include "wifi_ota.h"
#include "storage.h"
#include "lang.h"
#include "settings.h"

// ---------- screens ----------
lv_obj_t *screen_wifi_settings = NULL;
lv_obj_t *screen_wifi_scan_list = NULL;
lv_obj_t *screen_wifi_text_entry = NULL;
lv_obj_t *screen_wifi_status = NULL;
lv_obj_t *screen_ota_update = NULL;

// ---------- wifi settings widgets ----------
static lv_obj_t *wifi_tile_ssid_label = NULL;
static lv_obj_t *wifi_tile_connect_label = NULL;

// ---------- scan list widgets/state ----------
static lv_obj_t *scan_list_container = NULL;
static int scan_pending_index = -1; /* which scanned network the user just tapped, -1 = manual entry */

// ---------- text entry widgets/state ----------
static lv_obj_t *label_entry_title = NULL;
static lv_obj_t *textarea_entry = NULL;
static lv_obj_t *keyboard_entry = NULL;
static bool entry_is_password = false;

// ---------- status widgets ----------
static lv_obj_t *label_wifi_status_body = NULL;

// ---------- ota widgets ----------
static lv_obj_t *label_ota_current = NULL;
static lv_obj_t *label_ota_status = NULL;
static lv_obj_t *btn_ota_apply = NULL;

// ---------- wifi settings ----------
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
    } else {
        snprintf(buf, sizeof(buf), t(STR_WIFI_CONNECT_FAIL_FMT), ssid);
    }
    if (label_wifi_status_body != NULL) lv_label_set_text(label_wifi_status_body, buf);
    refresh_wifi_settings_ui();
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
    lv_obj_set_size(row, 280, 32);
    lv_obj_set_style_radius(row, 4, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x1E1E2E), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, event_scan_row_click, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
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
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    label_scan_status = lv_label_create(screen_wifi_scan_list);
    lv_label_set_text(label_scan_status, t(STR_WIFI_SCANNING));
    lv_obj_set_style_text_color(label_scan_status, lv_color_hex(0x7A7A7A), 0);
    lv_obj_set_style_text_font(label_scan_status, &lv_font_montserrat_14, 0);
    lv_obj_align(label_scan_status, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_add_flag(label_scan_status, LV_OBJ_FLAG_HIDDEN);

    scan_list_container = lv_obj_create(screen_wifi_scan_list);
    lv_obj_remove_style_all(scan_list_container);
    lv_obj_set_size(scan_list_container, 300, 290);
    lv_obj_align(scan_list_container, LV_ALIGN_TOP_MID, 0, 36);
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

bool wifi_text_entry_handle_back(void)
{
    open_wifi_settings_screen();
    return true;
}

void build_wifi_text_entry_screen(void)
{
    /* This is a round display: the LVGL canvas is a 360x360 square but
       only the inscribed circle (radius 180 around its center) is
       actually visible under the glass - anything placed near a corner
       of the square (like a wide keyboard's outer edges, or a button
       pinned to TOP_LEFT/TOP_RIGHT) gets cut off by the bezel even
       though it renders fine in the simulator's flat screenshot. The
       layout below mirrors rename.c's proven-on-hardware keyboard
       screen: a centered, narrower content column and a keyboard no
       taller than 170px so its top corners stay inside the circle. */
    screen_wifi_text_entry = lv_obj_create(NULL);
    lv_obj_set_size(screen_wifi_text_entry, 360, 360);
    lv_obj_set_style_bg_color(screen_wifi_text_entry, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_wifi_text_entry, 0, 0);
    lv_obj_set_scrollbar_mode(screen_wifi_text_entry, LV_SCROLLBAR_MODE_OFF);

    label_entry_title = lv_label_create(screen_wifi_text_entry);
    lv_label_set_text(label_entry_title, t(STR_WIFI_ENTER_SSID));
    lv_obj_set_style_text_color(label_entry_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_entry_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(label_entry_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label_entry_title, 260);
    lv_obj_align(label_entry_title, LV_ALIGN_TOP_MID, 0, 18);

    /* Textarea + show/hide toggle as one centered block instead of
       pinned to the left/right edges, so neither one lands in the
       clipped corner area. */
    textarea_entry = lv_textarea_create(screen_wifi_text_entry);
    lv_obj_set_size(textarea_entry, 200, 40);
    lv_obj_align(textarea_entry, LV_ALIGN_TOP_MID, -24, 60);
    lv_textarea_set_max_length(textarea_entry, WIFI_PASS_LEN - 1);
    lv_textarea_set_one_line(textarea_entry, true);
    lv_obj_set_style_text_font(textarea_entry, &lv_font_montserrat_16, 0);

    /* Show/hide toggle for the masked password field - only visible
       when entering a password, see open_text_entry(). */
    btn_toggle_pw_visibility = make_button(screen_wifi_text_entry, LV_SYMBOL_EYE_OPEN, 44, 40, event_toggle_pw_visibility);
    lv_obj_align(btn_toggle_pw_visibility, LV_ALIGN_TOP_MID, 104, 60);
    lv_obj_add_flag(btn_toggle_pw_visibility, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *btn_save = make_button(screen_wifi_text_entry, t(STR_OK), 100, 38, event_entry_save);
    lv_obj_align(btn_save, LV_ALIGN_TOP_MID, 0, 112);

    keyboard_entry = lv_keyboard_create(screen_wifi_text_entry);
    lv_obj_set_size(keyboard_entry, 360, 170);
    lv_obj_align(keyboard_entry, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(keyboard_entry, textarea_entry);
    lv_obj_add_event_cb(keyboard_entry, event_entry_save, LV_EVENT_READY, NULL);
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
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    label_wifi_status_body = lv_label_create(screen_wifi_status);
    lv_obj_set_style_text_color(label_wifi_status_body, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_wifi_status_body, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(label_wifi_status_body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label_wifi_status_body, 320);
    lv_label_set_long_mode(label_wifi_status_body, LV_LABEL_LONG_WRAP);
    lv_obj_align(label_wifi_status_body, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_ok = make_button(screen_wifi_status, t(STR_OK), 140, 46, event_wifi_status_ok);
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_MID, 0, -44);
}

// ---------- ota update ----------
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
    refresh_ota_update_ui();
    load_screen_if_needed(screen_ota_update);
}

static void event_ota_check(lv_event_t *e)
{
    (void)e;
    if (wifi_get_state() != WIFI_STATE_CONNECTED) {
        lv_label_set_text(label_ota_status, t(STR_OTA_NEED_WIFI));
        return;
    }
    lv_label_set_text(label_ota_status, t(STR_OTA_CHECKING));
    lv_refr_now(NULL);
    ota_check_now();
    refresh_ota_update_ui();
}

static void event_ota_apply(lv_event_t *e)
{
    (void)e;
    if (ota_get_state() != OTA_STATE_AVAILABLE) return;
    lv_label_set_text(label_ota_status, t(STR_OTA_UPDATING));
    lv_refr_now(NULL);
    ota_apply_update(); /* reboots on success and never returns */
    refresh_ota_update_ui();
}

void build_ota_update_screen(void)
{
    screen_ota_update = lv_obj_create(NULL);
    lv_obj_set_size(screen_ota_update, 360, 360);
    lv_obj_set_style_bg_color(screen_ota_update, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_ota_update, 0, 0);
    lv_obj_set_scrollbar_mode(screen_ota_update, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title = lv_label_create(screen_ota_update);
    lv_label_set_text(title, t(STR_OTA_TITLE));
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    label_ota_current = lv_label_create(screen_ota_update);
    lv_obj_set_style_text_color(label_ota_current, lv_color_hex(0x7A7A7A), 0);
    lv_obj_set_style_text_font(label_ota_current, &lv_font_montserrat_14, 0);
    lv_obj_align(label_ota_current, LV_ALIGN_TOP_MID, 0, 76);

    label_ota_status = lv_label_create(screen_ota_update);
    lv_label_set_text(label_ota_status, t(STR_OTA_TAP_TO_CHECK));
    lv_obj_set_style_text_color(label_ota_status, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_ota_status, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(label_ota_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label_ota_status, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_check = make_button(screen_ota_update, t(STR_OTA_CHECK_NOW), 140, 46, event_ota_check);
    lv_obj_align(btn_check, LV_ALIGN_BOTTOM_MID, 0, -100);

    btn_ota_apply = make_button(screen_ota_update, t(STR_OTA_APPLY_HOLD), 140, 46, NULL);
    lv_obj_add_event_cb(btn_ota_apply, event_ota_apply, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_align(btn_ota_apply, LV_ALIGN_BOTTOM_MID, 0, -44);
    lv_obj_add_flag(btn_ota_apply, LV_OBJ_FLAG_HIDDEN); /* shown only once an update is found */
}
