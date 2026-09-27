/* battery_is_charging() (hw.c): this board has no VBUS/charge-status
 * pin, so "charger plugged in" is inferred from how the measured
 * voltage moves between samples - see the comment on
 * update_charging_state() in hw.c. This is the regression test for the
 * bug it fixes: opening the battery screen right after plugging in the
 * USB cable used to show a jumped-up, misleading percentage (e.g. 40%
 * at rest, then 70-100% the moment the charger's regulation and IR
 * drop push the BAT node up) with nothing to say it wasn't a real
 * reading. */
#include "test_harness.h"
#include <stdio.h>
#include <assert.h>

int main(void)
{
    test_harness_init();

    /* ---- a normal discharge never flags charging ---- */
    sim_battery_voltage = 4.00f;
    update_battery_measurement(true);
    assert(!battery_is_charging());

    sim_battery_voltage = 3.95f;
    update_battery_measurement(true);
    assert(!battery_is_charging());

    sim_battery_voltage = 3.88f;
    update_battery_measurement(true);
    assert(!battery_is_charging());

    sim_battery_voltage = 3.74f; /* ~22% on the curve */
    update_battery_measurement(true);
    assert(!battery_is_charging());
    printf("PASS: a slow, normal discharge never flags charging\n");

    /* ---- the reported bug: plug in the charger, open the battery
       screen right away (a forced read) - the BAT node jumps and the
       jump alone must flag charging on that very sample ---- */
    sim_battery_voltage = 4.10f; /* charger regulating the BAT node */
    update_battery_measurement(true);
    assert(battery_is_charging());
    printf("PASS: a fast voltage jump between two samples flags charging immediately\n");

    /* ---- normal CV-phase ripple near the top doesn't un-flag it ---- */
    sim_battery_voltage = 4.18f;
    update_battery_measurement(true);
    assert(battery_is_charging());

    sim_battery_voltage = 4.14f; /* small dip, well under the hysteresis */
    update_battery_measurement(true);
    assert(battery_is_charging());
    printf("PASS: small ripple near the charging peak keeps the flag set\n");

    /* ---- unplugging: voltage sags back down past the hysteresis
       margin off the peak - the flag must clear ---- */
    sim_battery_voltage = 3.90f;
    update_battery_measurement(true);
    assert(!battery_is_charging());
    printf("PASS: a real drop off the charging peak clears the flag\n");

    /* ---- and normal discharge resumes cleanly after that ---- */
    sim_battery_voltage = 3.85f;
    update_battery_measurement(true);
    assert(!battery_is_charging());
    printf("PASS: discharge after unplugging doesn't re-trigger the flag\n");

    printf("\nAll battery charging heuristic tests passed.\n");
    return 0;
}
