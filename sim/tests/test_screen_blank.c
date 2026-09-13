/* hw.c's power states: active -> dim (auto_dim_ms[setting]) -> blank
 * (+BLANK_AFTER_DIM_MS more) -> reversed instantly by any input. This is
 * a screen timeout, not a power-off - the device never sleeps here, it
 * just stops driving the backlight and the panel while idle. Exercised
 * via sim_tick_advance() + lv_timer_handler() pumping (auto_dim_timer's
 * real callback, not a fake), and sim_display_panel_on (sim_stubs.c) to
 * observe what the real hardware can't report back. */
#include "test_harness.h"
#include "sim_stubs.h"
#include <stdio.h>
#include <assert.h>

static void pump(uint32_t ms)
{
    uint32_t elapsed = 0;
    while (elapsed < ms) {
        sim_tick_advance(50);
        lv_timer_handler();
        elapsed += 50;
    }
}

int main(void)
{
    test_harness_init();
    test_harness_reset_4p();

    /* AUTO_DIM_15S so the test doesn't need to pump a full minute. */
    nvs_set_auto_dim(AUTO_DIM_15S);
    activity_kick(); /* establish a known last_activity_tick baseline */
    assert(!dimmed);
    assert(!screen_blanked);
    assert(sim_display_panel_on);
    printf("PASS: starts active - not dimmed, not blanked, panel on\n");

    /* ---- dim engages after the configured timeout, panel still on ---- */
    pump(auto_dim_ms[AUTO_DIM_15S] + 500);
    assert(dimmed);
    assert(!screen_blanked);
    assert(sim_display_panel_on); /* dim only touches backlight, not the panel */
    printf("PASS: dims after the configured timeout without blanking the panel\n");

    /* ---- blank engages BLANK_AFTER_DIM_MS after that, panel off ---- */
    pump(BLANK_AFTER_DIM_MS + 500);
    assert(dimmed);
    assert(screen_blanked);
    assert(!sim_display_panel_on);
    printf("PASS: blanks the panel %d ms after dimming with still no input\n", BLANK_AFTER_DIM_MS);

    /* ---- any input reverses both instantly, panel back on ---- */
    bool was_asleep = activity_kick();
    assert(was_asleep); /* the swallow-gesture contract touch/knob code relies on */
    assert(!dimmed);
    assert(!screen_blanked);
    assert(sim_display_panel_on);
    printf("PASS: activity_kick() reverses dim+blank in one call and reports it woke something\n");

    /* ---- a second activity_kick() with nothing to wake reports false ---- */
    assert(!activity_kick());
    printf("PASS: activity_kick() on an already-awake screen reports nothing woke\n");

    /* ---- AUTO_DIM_OFF disables dim, and therefore blank too (blank only
       ever triggers from the already-dimmed branch) ---- */
    test_harness_reset_4p();
    nvs_set_auto_dim(AUTO_DIM_OFF);
    activity_kick();
    pump(auto_dim_ms[AUTO_DIM_15S] + BLANK_AFTER_DIM_MS + 2000);
    assert(!dimmed);
    assert(!screen_blanked);
    assert(sim_display_panel_on);
    printf("PASS: AUTO_DIM_OFF holds off both dim and blank indefinitely\n");

    printf("\nAll screen blank tests passed.\n");
    return 0;
}
