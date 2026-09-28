/* The device's own name (prefs_device.h) and the one-time question that
 * asks for it.
 *
 * Two things here are easy to break without noticing on hardware, since
 * both only show up on a device that has never been named - i.e. never
 * on the developer's own unit after the first flash:
 *
 * - The question must run exactly once. If prefs_has_device_name() were
 *   ever true too early (or false too late) the device would either skip
 *   the question forever or ask it on every single boot.
 * - There must be no way to get stuck on it. Enter with an empty field
 *   and the back gesture are both answers, and both have to leave a
 *   stored name behind so the next boot goes straight through.
 */
#include "test_harness.h"
#include "../../knobby/src/adapters/prefs_device.h"
#include "../../knobby/src/presentation/screens/ui_device_name.h"
#include "../../knobby/src/presentation/screens/home.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Stands in for intro.c's boot tail, which is what the first-boot
   question defers: it counts the call and, like the real one, puts
   something else on screen. */
static int done_calls = 0;
static void on_done(void) { done_calls++; back_to_main(); }

int main(void)
{
    char name[DEVICE_NAME_LEN];
    char expected_default[DEVICE_NAME_LEN];
    lv_obj_t *first_build;

    test_harness_init();

    snprintf(expected_default, sizeof(expected_default), "Knobby-%s", hw_device_id());

    /* ---- A never-configured device has no name ---- */
    assert(!prefs_has_device_name());
    prefs_get_device_name(name, sizeof(name));
    assert(name[0] == '\0');
    /* And the screen that asks is not built until it is needed. */
    assert(screen_device_name == NULL);
    printf("PASS: a fresh device has no name and no naming screen built\n");

    /* ---- First boot: empty answer takes the default ---- */
    device_name_open_first_boot(on_done);
    assert(screen_device_name != NULL);
    first_build = screen_device_name;
    assert(lv_scr_act() == screen_device_name);
    assert(done_calls == 0); /* the boot tail waits for the answer */

    device_name_test_apply("");
    assert(done_calls == 1);
    prefs_get_device_name(name, sizeof(name));
    assert(strcmp(name, expected_default) == 0);
    assert(prefs_has_device_name());
    printf("PASS: Enter on an empty field stores %s and resumes the boot\n", name);

    /* Leaving hands the keyboard's memory back - the screen is rebuilt
       next time rather than sitting in LVGL's pool for the session (see
       device_name_unloaded_cb). The free is deferred to LVGL's next
       pass, same as on the device. */
    assert(lv_scr_act() != first_build);
    sim_tick_advance(20);
    lv_timer_handler();
    assert(screen_device_name == NULL);
    printf("PASS: the naming screen releases itself once it is off screen\n");

    /* ---- From Settings: prefilled, renames, and returns ---- */
    open_device_name_screen();
    assert(screen_device_name != NULL);
    assert(lv_scr_act() == screen_device_name);
    /* Prefilled with the current name: this is an edit, not a blank
       form - retyping it from scratch to change one letter would be
       absurd on a knob-and-keyboard device. */
    assert(strcmp(lv_textarea_get_text(device_name_test_textarea()), expected_default) == 0);

    device_name_test_apply("Mesa Chema");
    prefs_get_device_name(name, sizeof(name));
    assert(strcmp(name, "Mesa Chema") == 0);
    /* Enter is the only confirm control on this screen, so it has to
       navigate as well as save - otherwise the keyboard just sits there
       looking like nothing happened. */
    assert(lv_scr_act() != screen_device_name);
    printf("PASS: renaming from Settings stores the name and leaves the screen\n");

    /* An emptied field from Settings is a mistyped edit, not a request
       to be called Knobby-XXXXXX again - the stored name must survive. */
    open_device_name_screen();
    device_name_test_apply("");
    prefs_get_device_name(name, sizeof(name));
    assert(strcmp(name, "Mesa Chema") == 0);
    printf("PASS: an empty field from Settings leaves the name alone\n");

    /* Back from Settings is not this screen's business: nav.c unwinds to
       the settings page like it does for every other sub-screen. */
    open_device_name_screen();
    assert(!device_name_handle_back());

    /* ---- After a factory reset the question comes back ---- */
    prefs_factory_reset();
    assert(!prefs_has_device_name());
    done_calls = 0;
    device_name_open_first_boot(on_done);
    /* Back during the question means "I am not typing one" - it must
       answer it, not navigate away and leave the device nameless (which
       would ask again on the next boot, forever). */
    assert(device_name_handle_back());
    assert(done_calls == 1);
    prefs_get_device_name(name, sizeof(name));
    assert(strcmp(name, expected_default) == 0);
    /* Question answered: back is no longer handled here. */
    assert(!device_name_handle_back());
    printf("PASS: the question returns after a factory reset, and back answers it\n");

    printf("PASS: device name\n");
    return 0;
}
