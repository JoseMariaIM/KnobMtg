/* Regression guard for LVGL's fixed heap (LV_MEM_SIZE, see
 * knobby/lv_conf.h - 128KB on the device). All 35 screens are built
 * once at boot (knob_gui(), see knobby/knob.c) and never freed, so this
 * baseline is a hard floor: every byte it uses is unavailable for the
 * color wheel, the QR code, text entry buffers, and LVGL's own
 * LV_MEM_BUF_MAX_NUM draw-time scratch buffers for the rest of the
 * device's life. LV_USE_ASSERT_MALLOC + LV_ASSERT_HANDLER=while(1) means
 * exhausting this pool is a silent hang, not a caught error - so this
 * budget is checked here, in CI, where a regression shows up as a
 * failing test instead of a field report of a device that "resets by
 * itself, no idea why."
 *
 * This binary is built differently from the rest of sim/tests/: the
 * generic `test` target links against sim/lv_conf.h, which bumps
 * LV_MEM_SIZE to 512KB ("tight for all screens" - see that file) so
 * regular desktop testing isn't fighting the device's pool too. That
 * override would make *this* test meaningless (trivially passing no
 * matter how much a screen grows), so the `test-mem-budget` Makefile
 * target instead points LVGL at sim/tests/lv_conf_membudget/lv_conf.h,
 * which keeps the real 128KB ceiling, and compiles the whole thing
 * -m32 so struct/pointer sizes match Xtensa (a 64-bit desktop build
 * measures meaningfully larger than the device ever would).
 *
 * Measured right after this test was written, on that exact build:
 * ~98KB/128KB (~77%) used for all 35 screens. The threshold below
 * leaves headroom for that number to drift a little as screens change,
 * while still catching a regression that eats deeply into the margin
 * transient UI (color wheel, QR, keyboards, damage log pages) needs at
 * runtime. */
#include "test_harness.h"
#include <stdio.h>
#include <assert.h>

/* Chosen from the measured baseline plus margin, not the other way
 * around - if a new screen legitimately needs the pool to grow, raise
 * LV_MEM_SIZE in knobby/lv_conf.h deliberately and update this number
 * in the same change, rather than let it drift silently. */
#define MIN_FREE_HEADROOM_BYTES (20U * 1024U)

int main(void)
{
    lv_mem_monitor_t mon;
    uint32_t used_bytes;

    test_harness_init(); /* builds all 35 screens, same as firmware boot */

    lv_mem_monitor(&mon);
    used_bytes = (uint32_t)mon.total_size - (uint32_t)mon.free_size;

    printf("LVGL pool after boot: %u / %u bytes used (%u%%), %u bytes free, %u%% fragmentation\n",
           (unsigned)used_bytes, (unsigned)mon.total_size,
           (unsigned)(100ULL * used_bytes / mon.total_size),
           (unsigned)mon.free_size, (unsigned)mon.frag_pct);

    if (mon.free_size < MIN_FREE_HEADROOM_BYTES) {
        fprintf(stderr,
            "FAIL: only %u bytes free after boot (need >= %u) - a screen\n"
            "      added or grown since this budget was set is eating into\n"
            "      the margin transient UI (color wheel, QR, keyboards,\n"
            "      damage log pages) needs to run without hitting\n"
            "      LV_ASSERT_HANDLER's while(1) hang on the real device.\n",
            (unsigned)mon.free_size, (unsigned)MIN_FREE_HEADROOM_BYTES);
    }
    assert(mon.free_size >= MIN_FREE_HEADROOM_BYTES);
    printf("PASS: %u bytes of headroom remain after building every screen (>= %u required)\n",
           (unsigned)mon.free_size, (unsigned)MIN_FREE_HEADROOM_BYTES);

    printf("\nAll LVGL memory budget tests passed.\n");
    return 0;
}
