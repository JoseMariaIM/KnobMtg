/* The LVGL-facing half of the game.h facade: color math (returns
 * lv_color_t, so it can't live in the pure game_state.h/.c - see the
 * comment at the top of that file) and the bridge that wires
 * game_state.c's game_hooks up to real UI refresh functions and owns
 * the 3 lv_timer_t objects those hooks schedule. */
#include "game.h"
#include "storage.h"
#include "ui_1p.h"
#include "ui_mp.h"
#include "ui_player_menu.h"
#include "rename.h"

// ---------- player colors ----------
static const uint32_t player_color_table[MAX_GAME_PLAYERS][LIFE_VIB_COUNT] = {
    /*  dim        mid        vivid  */
    {0x024D3A, 0x06D6A0, 0x66FFD9},  /* P1 green  (bottom-left) */
    {0x2A0A4D, 0x7B1FE0, 0x9C4DFF},  /* P2 purple (top-left)    */
    {0x0A3A4D, 0x29B6F6, 0x4FC3F7},  /* P3 blue   (top-right)   */
    {0x4D4400, 0xFFD600, 0xFFEA61},  /* P4 yellow (bottom-right) */
    {0x4D1C1C, 0xF44336, 0xFF5252},  /* P5 red    */
    {0x4D3300, 0xFF9800, 0xFFB74D},  /* P6 orange */
    {0x004D4D, 0x00BCD4, 0x4DD0E1},  /* P7 cyan   */
    {0x3D0A4D, 0xE040FB, 0xEA80FC},  /* P8 pink   */
};

// ---------- per-player color state (runtime only, lost on reboot) ----------
/* One HSV per player instead of an index into a fixed palette - picked
   freely off the color wheel (see build_player_color_picker_screen()).
   Stored as HSV rather than the RGB565 lv_color_t the wheel also
   exposes so re-deriving the three vibrancy tiers below never
   compounds rounding from the display's 16-bit color depth, and so
   reopening the picker shows exactly the hue/sat last picked.
   player_life_color[]/player_has_override[] (the bools) live in
   game_state.c - only this HSV value itself is LVGL-typed. */
lv_color_hsv_t player_custom_hsv[MAX_DISPLAY_PLAYERS] = {
    {160, 97, 84}, /* green,  ~0x06D6A0 */
    {268, 86, 88}, /* purple, ~0x7B1FE0 */
    {199, 83, 96}, /* blue,   ~0x29B6F6 */
    {50, 100, 100}, /* yellow, ~0xFFD600 */
};

lv_color_t get_player_color_vib(int index, int vibrancy)
{
    if (index < 0 || index >= MAX_GAME_PLAYERS) return lv_color_hex(0x303030);
    if (vibrancy < 0 || vibrancy >= LIFE_VIB_COUNT) vibrancy = LIFE_VIB_MID;
    return lv_color_hex(player_color_table[index][vibrancy]);
}

lv_color_t get_player_base_color(int index)
{
    return get_player_color_vib(index, LIFE_VIB_MID);
}

lv_color_t get_player_active_color(int index)
{
    return get_player_color_vib(index, LIFE_VIB_VIV);
}

lv_color_t get_player_text_color(int index)
{
    lv_color_t bg = get_player_base_color(index);
    return color_is_light(bg) ? lv_color_black() : lv_color_white();
}

lv_color_t get_player_preview_color(int index, int delta)
{
    if (index == 3) {
        /* P4 yellow: dark red / dark green for contrast */
        return (delta < 0) ? lv_color_hex(0x7A1020) : lv_color_hex(0x215A2A);
    }
    if (index == 0) {
        /* P1 green: bright red / white for contrast on green bg */
        return (delta < 0) ? lv_palette_main(LV_PALETTE_RED) : lv_color_white();
    }
    return (delta < 0) ? lv_palette_main(LV_PALETTE_RED) : lv_palette_main(LV_PALETTE_GREEN);
}

/* Derives the same dim/mid/vivid triple the fixed palettes above hand-
   picked, but from whatever hue/saturation the user dragged the wheel
   to: dim darkens toward black for backgrounds, vivid lightens toward
   white for the "selected" highlight, mid is the color as picked. */
lv_color_t get_custom_color_vib(lv_color_hsv_t hsv, int vibrancy)
{
    if (vibrancy < 0 || vibrancy >= LIFE_VIB_COUNT) vibrancy = LIFE_VIB_MID;

    switch (vibrancy) {
        case LIFE_VIB_DIM:
            hsv.v = (uint8_t)LV_MAX(8, (hsv.v * 35) / 100);
            break;
        case LIFE_VIB_VIV:
            hsv.s = (uint8_t)LV_MAX(0, (int)hsv.s - 20);
            hsv.v = (uint8_t)LV_MIN(100, hsv.v + 25);
            break;
        case LIFE_VIB_MID:
        default:
            break;
    }
    return lv_color_hsv_to_rgb(hsv.h, hsv.s, hsv.v);
}

lv_color_t get_effective_player_color(int player_i, int color_i, int vibrancy)
{
    /* Per-player override takes precedence over global mode */
    if (player_has_override[player_i]) {
        if (player_life_color[player_i]) {
            int life = player_life[player_i];
            int max_life = nvs_get_life_total();
            int tier = get_life_tier(life, max_life);
            return get_life_color_vib(tier, vibrancy);
        }
        return get_custom_color_vib(player_custom_hsv[player_i], vibrancy);
    }

    /* No override: use global mode */
    if (nvs_get_color_mode() == COLOR_MODE_LIFE) {
        int life = player_life[player_i];
        int max_life = nvs_get_life_total();
        int tier = get_life_tier(life, max_life);
        return get_life_color_vib(tier, vibrancy);
    }

    /* COLOR_MODE_PLAYER: use position color */
    return get_player_color_vib(color_i, vibrancy);
}

// ---------- bridge: game_hooks + the 3 lv_timer_t game_state.c schedules ----------
static lv_timer_t *life_preview_timer = NULL;
static lv_timer_t *all_damage_flash_timer = NULL;
static lv_timer_t *player_select_anim_timer = NULL;

static void life_preview_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    game_life_preview_commit();
}

static void all_damage_flash_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    /* One-shot in effect: pause immediately, game_all_damage_flash_end()
       only clears state and asks for a repaint. Re-armed by the
       all_damage_flash_schedule hook (see start_all_damage_flash() in
       game_state.c) next time All Damage fires. */
    lv_timer_pause(all_damage_flash_timer);
    game_all_damage_flash_end();
}

static void player_select_anim_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    int next_period_ms = game_player_select_anim_step();
    if (next_period_ms <= 0) {
        lv_timer_pause(player_select_anim_timer);
    } else {
        lv_timer_set_period(player_select_anim_timer, next_period_ms);
    }
}

static void hook_life_preview_schedule(bool active)
{
    if (life_preview_timer == NULL) return;
    if (active) {
        lv_timer_reset(life_preview_timer);
        lv_timer_resume(life_preview_timer);
    } else {
        lv_timer_pause(life_preview_timer);
    }
}

static void hook_all_damage_flash_schedule(void)
{
    if (all_damage_flash_timer == NULL) return;
    lv_timer_reset(all_damage_flash_timer);
    lv_timer_resume(all_damage_flash_timer);
}

static void hook_player_select_anim_schedule(bool active)
{
    if (player_select_anim_timer == NULL) return;
    if (active) {
        /* start_player_selection_animation() always sets a fresh period
           via game_state.c before this fires - see player_select_anim_period
           there - but lv_timer_set_period() needs the actual value, which
           only the bridge's timer object holds; game_state.c can't reach
           in and set it directly (no LVGL type crosses that boundary).
           40ms matches its own "start fast" initial value. */
        lv_timer_set_period(player_select_anim_timer, 40);
        lv_timer_resume(player_select_anim_timer);
    } else {
        lv_timer_pause(player_select_anim_timer);
    }
}

void game_bridge_init(void)
{
    static const game_hooks_t hooks = {
        .refresh_player_ui = refresh_player_ui,
        .refresh_select_ui = refresh_select_ui,
        .refresh_damage_ui = refresh_damage_ui,
        .refresh_all_damage_ui = refresh_all_damage_ui,
        .refresh_rename_ui = refresh_rename_ui,
        .select_kick_timer = select_kick_timer,
        .life_preview_schedule = hook_life_preview_schedule,
        .all_damage_flash_schedule = hook_all_damage_flash_schedule,
        .player_select_anim_schedule = hook_player_select_anim_schedule,
    };
    game_hooks_register(&hooks);

    /* All three created paused, up front - simpler than each one's old
       lazy first-use creation, and the LVGL heap cost of three small
       lv_timer_t objects a little earlier at boot is negligible (see
       sim/tests/membudget/test_lvgl_memory_budget.c). */
    life_preview_timer = lv_timer_create(life_preview_timer_cb, 3000, NULL);
    if (life_preview_timer != NULL) lv_timer_pause(life_preview_timer);

    all_damage_flash_timer = lv_timer_create(all_damage_flash_timer_cb, 2500, NULL);
    if (all_damage_flash_timer != NULL) lv_timer_pause(all_damage_flash_timer);

    player_select_anim_timer = lv_timer_create(player_select_anim_timer_cb, 50, NULL);
    if (player_select_anim_timer != NULL) lv_timer_pause(player_select_anim_timer);
}
