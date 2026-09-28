#include "ui_device_name.h"
#include "../../adapters/prefs_device.h"
#include "../../adapters/hw.h"
#include "../../adapters/lang.h"
#include "../widgets/custom_keyboard.h"
#include <string.h>

/* See ui_device_name.h. */

lv_obj_t *screen_device_name = NULL;

// ---------- widgets ----------
static lv_obj_t *label_device_name_title = NULL;
static lv_obj_t *label_device_name_hint = NULL;
static lv_obj_t *textarea_device_name = NULL;
static custom_keyboard_t device_name_kb;

/* Set only for the first-boot question, and cleared the moment it is
   answered: everything modal about this screen (back accepts the
   default, and the boot tail runs afterwards) is gated on it, so a
   later visit from Settings behaves like any other sub-screen. */
static bool first_boot_mode = false;
static void (*first_boot_done)(void) = NULL;

static void ensure_built(void);

/* See device_name_set_return_hook(). */
static void (*return_hook)(void) = NULL;

void device_name_set_return_hook(void (*fn)(void))
{
    return_hook = fn;
}

// ---------- default name ----------
/* What the device calls itself when nobody types anything. Ends in the
   same six hex digits the Updates screen shows and the tester list
   targets, so an unnamed unit is still tellable apart from the next one
   on the table. */
static void default_device_name(char *out, size_t out_len)
{
    snprintf(out, out_len, "Knobby-%s", hw_device_id());
}

// ---------- commit ----------
static void finish(void)
{
    void (*done)(void) = first_boot_done;

    first_boot_mode = false;
    first_boot_done = NULL;
    /* Boot continues where it left off; a visit from Settings goes back
       to the page the row is on. Either way something else has to end up
       on screen - leaving the keyboard there with the name already saved
       reads as if nothing happened. */
    if (done != NULL) {
        done();
    } else if (return_hook != NULL) {
        return_hook();
    }
}

static void apply_name_and_return(const char *name)
{
    if (name != NULL && name[0] != '\0') {
        prefs_set_device_name(name);
    } else if (!prefs_has_device_name()) {
        /* Empty on first boot (or after a reset): take the default, so
           the naming step is answered and never asked again. Empty from
           Settings, on a device that already has a name, deliberately
           changes nothing - clearing the field is far more likely to be
           a mistyped edit than a request to be renamed Knobby-XXXXXX. */
        char fallback[DEVICE_NAME_LEN];
        default_device_name(fallback, sizeof(fallback));
        prefs_set_device_name(fallback);
    }
    finish();
}

static void event_device_name_save(lv_event_t *e)
{
    (void)e;
    if (textarea_device_name == NULL) return;
    apply_name_and_return(lv_textarea_get_text(textarea_device_name));
}

// ---------- knob / back ----------
void device_name_screen_knob(int dir)
{
    if (textarea_device_name == NULL) return;
    if (dir < 0) lv_textarea_cursor_left(textarea_device_name);
    else if (dir > 0) lv_textarea_cursor_right(textarea_device_name);
}

bool device_name_handle_back(void)
{
    if (!first_boot_mode) return false;
    apply_name_and_return(NULL); /* no answer given: keep the default */
    return true;
}

// ---------- open ----------
static void prepare(bool welcome)
{
    char current[DEVICE_NAME_LEN];

    ensure_built();
    if (label_device_name_title != NULL) {
        lv_label_set_text(label_device_name_title,
                          welcome ? t(STR_DEVICE_NAME_WELCOME) : t(STR_DEVICE_NAME_TITLE));
    }
    if (label_device_name_hint != NULL) {
        /* The id is on screen while the name is being chosen on purpose:
           it is what has to be read out to enrol this unit in the test
           channel, and this is the one screen where somebody is already
           looking at "which device is this". */
        lv_label_set_text(label_device_name_hint, hw_device_id());
    }
    if (textarea_device_name != NULL) {
        prefs_get_device_name(current, sizeof(current));
        lv_textarea_set_text(textarea_device_name, current);
    }
    /* The keyboard's LVGL mode slots are global - every screen that owns
       one has to claim them back on open. See custom_keyboard.h. */
    custom_keyboard_reset(&device_name_kb);
    load_screen_if_needed(screen_device_name);
}

void open_device_name_screen(void)
{
    first_boot_mode = false;
    first_boot_done = NULL;
    prepare(false);
}

void device_name_open_first_boot(void (*on_done)(void))
{
    first_boot_mode = true;
    first_boot_done = on_done;
    prepare(true);
}

void device_name_test_apply(const char *name) { apply_name_and_return(name); }
lv_obj_t *device_name_test_textarea(void) { return textarea_device_name; }

/* Handed back the moment this screen is off screen, by whichever exit
   the user took (Enter, back, or the boot continuing on its way). A full
   on-screen keyboard is the most expensive screen on the device (~5.5KB
   of LVGL's fixed 128KB pool, see sim/tests/membudget) and this one is
   opened once at setup and then essentially never - keeping it resident
   for the rest of the session would spend that margin on nothing.
   Deletion is deferred: this runs from inside LVGL's screen-load, which
   is still holding the object it is unloading. */
static void device_name_unloaded_cb(lv_event_t *e)
{
    lv_obj_t *dead = lv_event_get_target(e);

    if (dead != screen_device_name) return;
    screen_device_name = NULL;
    label_device_name_title = NULL;
    label_device_name_hint = NULL;
    textarea_device_name = NULL;
    memset(&device_name_kb, 0, sizeof(device_name_kb));
    lv_obj_del_async(dead);
}

// ---------- build ----------
/* Built on first open, not at boot: this screen carries a full
   custom_keyboard, and LVGL's heap here is a fixed 128KB pool shared by
   every screen (see LV_MEM_SIZE in lv_conf.h and sim's
   test-mem-budget). Most sessions never rename the device. */
static void ensure_built(void)
{
    if (screen_device_name == NULL) build_device_name_screen();
}

void build_device_name_screen(void)
{
    screen_device_name = lv_obj_create(NULL);
    lv_obj_set_size(screen_device_name, 360, 360);
    lv_obj_set_style_bg_color(screen_device_name, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_device_name, 0, 0);
    lv_obj_set_scrollbar_mode(screen_device_name, LV_SCROLLBAR_MODE_OFF);

    label_device_name_title = lv_label_create(screen_device_name);
    lv_label_set_text(label_device_name_title, t(STR_DEVICE_NAME_TITLE));
    lv_obj_set_style_text_color(label_device_name_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_device_name_title, &lv_font_es_16, 0);
    lv_obj_set_style_text_align(label_device_name_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label_device_name_title, LV_ALIGN_TOP_MID, 0, 22);

    /* 44px tall at y=48 and the keyboard's top row starting at y=130
       (see custom_keyboard.c's ROW_TOP_BAND) leaves room for the id line
       between them without either crossing into the bezel. */
    textarea_device_name = lv_textarea_create(screen_device_name);
    lv_obj_set_size(textarea_device_name, 240, 44);
    lv_obj_align(textarea_device_name, LV_ALIGN_TOP_MID, 0, 48);
    lv_textarea_set_max_length(textarea_device_name, DEVICE_NAME_LEN - 1);
    lv_textarea_set_one_line(textarea_device_name, true);

    label_device_name_hint = lv_label_create(screen_device_name);
    lv_label_set_text(label_device_name_hint, "");
    lv_obj_set_style_text_color(label_device_name_hint, lv_color_hex(0x6A6A6A), 0);
    lv_obj_set_style_text_font(label_device_name_hint, &lv_font_es_14, 0);
    lv_obj_align(label_device_name_hint, LV_ALIGN_TOP_MID, 0, 100);

    /* Enter is the only confirm control: a separate Save button would
       have to live under the keyboard, which fills everything below
       y=130 on this round display. */
    custom_keyboard_build(&device_name_kb, screen_device_name);
    custom_keyboard_set_textarea(&device_name_kb, textarea_device_name);
    custom_keyboard_set_ready_cb(&device_name_kb, event_device_name_save);

    /* Built on open, freed on leave - see device_name_unloaded_cb(). */
    lv_obj_add_event_cb(screen_device_name, device_name_unloaded_cb,
                        LV_EVENT_SCREEN_UNLOADED, NULL);
}
