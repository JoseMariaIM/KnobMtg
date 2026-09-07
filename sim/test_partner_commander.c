/* Standalone regression test for independent partner-commander damage
 * tracking. Not part of the normal build; compile/run manually:
 *
 *   gcc -DLV_CONF_INCLUDE_SIMPLE -DSIMULATOR=1 -I. -Iesp_stubs \
 *       -I$LVGL_PATH -I$LVGL_PATH/src -I../knobby -I../knobby/src \
 *       test_partner_commander.c \
 *       build/UP__knobby__knob.o build/UP__knobby__board_pins.o \
 *       build/UP__knobby__src__*.o build/UP__knobby__src__fonts__*.o \
 *       build/__*lvgl*.o -o test_partner_commander -lm
 *   ./test_partner_commander
 */
#include "board_detect.h"
#include <lvgl.h>
#include "knob.h"
#include "game.h"
#include "storage.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#define SCREEN_W 360
#define SCREEN_H 360

static lv_color_t framebuffer[SCREEN_W * SCREEN_H];
static lv_color_t draw_buf_data[SCREEN_W * 72];
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;

static void sim_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    int x, y;
    for (y = area->y1; y <= area->y2; y++)
        for (x = area->x1; x <= area->x2; x++)
            framebuffer[y * SCREEN_W + x] = *color_p++;
    lv_disp_flush_ready(drv);
}

/* Mirrors one "pick opponent row, dial the knob, Apply" cycle on the
   already-open Commander Damage flow (screen_select -> screen_damage).
   Does NOT call prepare_cmd_damage_for_player: that only runs once, when
   the flow is entered from the player menu, exactly like the real UI. */
static void select_and_apply(int source_row, int amount)
{
    selected_enemy = source_row;
    damage_enter();
    add_damage_to_selected_enemy(amount);
    damage_apply();
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    board_detect();
    lv_init();
    lv_disp_draw_buf_init(&draw_buf, draw_buf_data, NULL, SCREEN_W * 72);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_W;
    disp_drv.ver_res = SCREEN_H;
    disp_drv.flush_cb = sim_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
    knob_gui();

    /* 4-player game: Alice(0), Bob(1), Carol(2), Dave(3). Bob is
       tracking damage from Alice's two commanders independently.
       Auto-elimination only engages in multiplayer (see
       check_player_elimination), so players-to-track must be > 1. */
    nvs_set_num_players(4);
    nvs_set_players_to_track(4);
    knob_life_reset();

    /* Enter the Commander Damage flow for Bob once, exactly as the
       player menu does. Row 0 in the enemy list is Alice
       (get_cmd_target_player_index skips Bob himself). */
    prepare_cmd_damage_for_player(/*target=*/1);

    select_and_apply(/*source_row=*/0, 18);
    assert(cmd_damage_totals[0][1] == 18);
    assert(partner_cmd_damage_totals[0][1] == 0);
    assert(!player_eliminated[1]);
    printf("PASS: primary commander damage recorded independently (18)\n");

    /* Flip to the Partner tab, same as tapping it on screen_select. */
    cmd_damage_slot = 1;
    refresh_cmd_damage_slot();
    select_and_apply(/*source_row=*/0, 15);
    assert(cmd_damage_totals[0][1] == 18);          /* untouched by the partner edit */
    assert(partner_cmd_damage_totals[0][1] == 15);
    assert(!player_eliminated[1]);                  /* neither commander has hit 21 yet */
    printf("PASS: partner commander damage tracked separately (15), primary untouched\n");

    /* Push the partner commander to lethal; the primary commander's 18
       must not contribute to it. */
    select_and_apply(/*source_row=*/0, 21 - 15);
    assert(partner_cmd_damage_totals[0][1] == 21);
    assert(player_eliminated[1]);
    printf("PASS: 21 from the partner commander alone eliminates Bob\n");

    manual_uneliminate_player(1);
    assert(!player_eliminated[1]);
    assert(partner_cmd_damage_totals[0][1] <= 20);
    assert(cmd_damage_totals[0][1] == 18);
    printf("PASS: revive clamps only the lethal slot, primary total untouched\n");

    printf("\nAll partner-commander tests passed.\n");
    return 0;
}
