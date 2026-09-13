/* lv_conf.h variant used ONLY by the `test-mem-budget` build (see
 * sim/Makefile). Unlike sim/lv_conf.h, this deliberately does NOT bump
 * LV_MEM_SIZE up to 512KB - the whole point of this build is to measure
 * against the real device's 128KB pool (knobby/lv_conf.h), so LVGL's
 * mem allocator itself must be compiled with that same 128KB ceiling.
 * Everything else (tick source, color swap, assert handler) is copied
 * from sim/lv_conf.h since this is still a desktop build. */
#include <stdlib.h>
#include "../../../knobby/lv_conf.h"

/* No LV_MEM_SIZE override here - keep the device's 128KB. */

/* No SPI byte swap needed on desktop. */
#undef LV_COLOR_16_SWAP
#define LV_COLOR_16_SWAP 0

/* POSIX-ish clock instead of Arduino's millis(). */
#undef LV_TICK_CUSTOM_INCLUDE
#define LV_TICK_CUSTOM_INCLUDE "sim_stubs.h"
#undef LV_TICK_CUSTOM_SYS_TIME_EXPR
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (sim_millis())

/* abort() instead of an infinite loop, so a pool exhaustion fails the
 * test binary loudly (nonzero exit / stack trace) instead of hanging
 * the CI job until it times out. */
#undef LV_ASSERT_HANDLER
#define LV_ASSERT_HANDLER abort();
