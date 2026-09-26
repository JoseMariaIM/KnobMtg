#ifndef _GAME_TYPES_H
#define _GAME_TYPES_H

/* Pure-C subset of types.h: constants, plain structs and inline helpers
 * that game_state.c/.h need and that don't touch LVGL at all. Split out
 * so game_state.h can be included (and its logic unit-tested) without
 * pulling in lvgl.h transitively - see the comment at the top of
 * game_state.h for why that matters. types.h includes this file too, so
 * every other translation unit in the app sees these exact same symbols
 * exactly where it always has; nothing outside game_state.h/.c should
 * need to include this file directly. */

#include <stdint.h>
#include <stdbool.h>

// ---------- constants ----------
#define MAX_GAME_PLAYERS 8
#define MAX_DISPLAY_PLAYERS 4
#define MAX_ENEMY_COUNT (MAX_GAME_PLAYERS - 1)
#define LIFE_MIN -999
#define LIFE_MAX 999
#define COUNTER_MIN 0
#define COUNTER_MAX 9999
#define DEFAULT_LIFE_TOTAL 40
#define DEFAULT_BRIGHTNESS_PERCENT 30
#define MULTIPLAYER_COUNT 4
#define KNOB_EVENT_QUEUE_SIZE 32

// ---------- color modes ----------
#define COLOR_MODE_PLAYER     0
#define COLOR_MODE_LIFE       1
#define COLOR_MODE_COUNT      2

// ---------- orientation modes ----------
#define ORIENTATION_MODE_ABSOLUTE 0
#define ORIENTATION_MODE_CENTRIC  1
#define ORIENTATION_MODE_TABLETOP 2
#define ORIENTATION_MODE_COUNT    3

// ---------- display rotation (physical, degrees = value * 90) ----------
#define DISPLAY_ROTATION_COUNT 4

// ---------- auto-dim timeout options ----------
#define AUTO_DIM_OFF  0
#define AUTO_DIM_15S  1
#define AUTO_DIM_30S  2
#define AUTO_DIM_60S  3
#define AUTO_DIM_COUNT 4

static const uint32_t auto_dim_ms[] = {0, 15000, 30000, 60000};

// ---------- deselect timeout options ----------
#define DESELECT_NEVER 0
#define DESELECT_5S    1
#define DESELECT_15S   2
#define DESELECT_30S   3
#define DESELECT_COUNT 4

static const int deselect_ms[] = {0, 5000, 15000, 30000};

// ---------- types ----------
typedef struct {
    const char *name;
    int damage;
} enemy_state_t;

// ---------- utility functions ----------
static inline int clamp_life(int value)
{
    if (value < LIFE_MIN) return LIFE_MIN;
    if (value > LIFE_MAX) return LIFE_MAX;
    return value;
}

static inline int clamp_brightness(int value)
{
    if (value < 1) return 1;
    if (value > 100) return 100;
    return value;
}

static inline int clamp_counter(int value)
{
    if (value < COUNTER_MIN) return COUNTER_MIN;
    if (value > COUNTER_MAX) return COUNTER_MAX;
    return value;
}

// ---------- life color tiers ----------
/* The tiers/table are plain data (int index in, uint32_t hex out) - pure.
 * Turning a tier into an lv_color_t (get_life_color/get_life_color_vib)
 * stays in types.h, since that return type is LVGL's. */
#define LIFE_TIER_RED    0
#define LIFE_TIER_YELLOW 1
#define LIFE_TIER_GREEN  2
#define LIFE_TIER_PURPLE 3
#define LIFE_TIER_COUNT  4

#define LIFE_VIB_DIM  0
#define LIFE_VIB_MID  1
#define LIFE_VIB_VIV  2
#define LIFE_VIB_COUNT 3

static const uint32_t life_color_table[LIFE_TIER_COUNT][LIFE_VIB_COUNT] = {
    /* dim        mid        vivid */
    {0x4D1C1C, 0xF44336, 0xFF0000},  /* red    */
    {0x4D4D00, 0xFFEB3B, 0xFFFF00},  /* yellow */
    {0x024D3A, 0x06D6A0, 0x66FFD9},  /* green  */
    {0x2E004D, 0x7B1FA2, 0xAA00FF},  /* purple */
};

static inline int get_life_tier(int value, int max_life)
{
    if (value > max_life)          return LIFE_TIER_PURPLE;
    if (value >= max_life * 3 / 4) return LIFE_TIER_GREEN;
    if (value >= max_life / 4)     return LIFE_TIER_YELLOW;
    return LIFE_TIER_RED;
}

#endif // _GAME_TYPES_H
