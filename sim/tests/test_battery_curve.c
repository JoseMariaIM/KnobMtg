/* battery_percent_from_voltage() (hw.c): the piecewise-linear curve
 * that turns a raw ADC voltage into a displayed percentage. No LVGL
 * runtime needed - it's a pure lookup - so this links against hw.o
 * directly without booting the UI. */
#include "../../knobby/src/adapters/hw.h"
#include <stdio.h>
#include <assert.h>

int main(void)
{
    /* Below the lowest calibration point clamps to 0, not negative. */
    assert(battery_percent_from_voltage(0.0f) == 0);
    assert(battery_percent_from_voltage(3.0f) == 0);
    assert(battery_percent_from_voltage(3.35f) == 0);
    printf("PASS: voltages at/below the floor read 0%%\n");

    /* Above the highest calibration point clamps to 100, not runaway. */
    assert(battery_percent_from_voltage(4.18f) == 100);
    assert(battery_percent_from_voltage(4.5f) == 100);
    assert(battery_percent_from_voltage(5.0f) == 100);
    printf("PASS: voltages at/above the ceiling read 100%%\n");

    /* Every published curve point maps back to its exact percentage -
       a regression here would mean someone edited one table but not
       the other (see battery_curve_voltages/percentages in hw.c). */
    {
        static const float volts[] = {3.35f, 3.55f, 3.68f, 3.74f, 3.80f, 3.88f, 3.96f, 4.06f, 4.18f};
        static const int   pcts[]  = {0, 5, 12, 22, 34, 48, 64, 82, 100};
        size_t i;
        for (i = 0; i < sizeof(volts) / sizeof(volts[0]); i++) {
            assert(battery_percent_from_voltage(volts[i]) == pcts[i]);
        }
    }
    printf("PASS: all calibration points map to their exact percentage\n");

    /* Monotonic: a device that's losing charge must never see the
       displayed percentage tick upward from ADC noise alone. Sampled
       across the whole range in fine steps. */
    {
        float v;
        int prev = battery_percent_from_voltage(3.0f);
        for (v = 3.0f; v <= 4.3f; v += 0.005f) {
            int pct = battery_percent_from_voltage(v);
            assert(pct >= prev);
            prev = pct;
        }
    }
    printf("PASS: percentage is monotonically non-decreasing with voltage\n");

    printf("\nAll battery curve tests passed.\n");
    return 0;
}
