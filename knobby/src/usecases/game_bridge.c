#include "game_bridge.h"
#include "game.h"
#include "../presentation/screens/ui_cmd_damage.h"
#include "../presentation/screens/home.h"
#include "../presentation/screens/ui_mp.h"
#include "../presentation/screens/ui_player_menu.h"
#include "rename.h"

/* See game_bridge.h. */

static lv_timer_t *life_preview_timer = NULL;
static lv_timer_t *life_flash_timer = NULL;
static lv_timer_t *player_select_anim_timer = NULL;

static void life_preview_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    game_life_preview_commit();
}

static void life_flash_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    /* One-shot in effect: pause immediately, game_life_flash_end()
       only clears state and asks for a repaint. Re-armed by the
       life_flash_schedule hook (see start_life_flash() in
       game_state.c) next time All Damage fires. */
    lv_timer_pause(life_flash_timer);
    game_life_flash_end();
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

static void hook_life_flash_schedule(void)
{
    if (life_flash_timer == NULL) return;
    lv_timer_reset(life_flash_timer);
    lv_timer_resume(life_flash_timer);
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
        .life_flash_schedule = hook_life_flash_schedule,
        .player_select_anim_schedule = hook_player_select_anim_schedule,
    };
    game_hooks_register(&hooks);

    /* All three created paused, up front - simpler than each one's old
       lazy first-use creation, and the LVGL heap cost of three small
       lv_timer_t objects a little earlier at boot is negligible (see
       sim/tests/membudget/test_lvgl_memory_budget.c). */
    life_preview_timer = lv_timer_create(life_preview_timer_cb, 3000, NULL);
    if (life_preview_timer != NULL) lv_timer_pause(life_preview_timer);

    life_flash_timer = lv_timer_create(life_flash_timer_cb, 2500, NULL);
    if (life_flash_timer != NULL) lv_timer_pause(life_flash_timer);

    player_select_anim_timer = lv_timer_create(player_select_anim_timer_cb, 50, NULL);
    if (player_select_anim_timer != NULL) lv_timer_pause(player_select_anim_timer);
}
