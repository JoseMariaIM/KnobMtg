/* The players' names belong to the table, not to the game.
 *
 * Renaming P1 to Chema is a statement about who is sitting there. It
 * should outlive the game they are playing, and it should outlive the
 * device being switched off - which it did not: the names lived only
 * in RAM, so every power cycle put everyone back to P1..P8.
 *
 * The rule this locks in is narrow on purpose: the rename screen is
 * the only thing that changes a name. A game reset, a new player
 * count, a different starting life and a reboot all leave them alone.
 */
#include "test_harness.h"
#include "sim_stubs.h"
#include "entities/game_state.h"
#include "adapters/prefs_roster.h"
#include "adapters/prefs_table.h"
#include "usecases/rename.h"
#include "presentation/screens/ui_player_menu.h"
#include "presentation/screens/attack.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

extern void reset_all_values(void);

/* What a power cycle does to this state: RAM goes back to the
   compiled-in defaults, then boot reads NVS over the top (see
   knob_hw_init). NVS itself survives, which is the whole point. */
static void simulate_reboot(void)
{
    static const char *defaults[MAX_GAME_PLAYERS] = {
        "P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8"
    };
    int i;

    for (i = 0; i < MAX_GAME_PLAYERS; i++) {
        snprintf(player_names[i], sizeof(player_names[i]), "%s", defaults[i]);
    }
    prefs_init();
    player_names_restore();
}

static void test_a_rename_outlives_the_power_switch(void)
{
    assert(strcmp(player_names[0], "P1") == 0);

    menu_player = 0;
    rename_test_apply("Chema");
    assert(strcmp(player_names[0], "Chema") == 0);
    /* Committing a name hands control back to the player menu. rename
       does not name that target itself any more - knob.c registers it
       with rename_set_return_hook(), so a missing registration shows up
       here as the rename screen never being left. */
    assert(lv_scr_act() == screen_player_menu);

    simulate_reboot();
    if (strcmp(player_names[0], "Chema") != 0) {
        printf("FAIL: after a reboot player 0 is '%s', expected 'Chema'\n",
               player_names[0]);
        assert(0);
    }
    /* And nobody else was touched on the way through. */
    assert(strcmp(player_names[1], "P2") == 0);
    assert(strcmp(player_names[7], "P8") == 0);
    printf("PASS: a rename survives a power cycle, and only renames that player\n");
}

static void test_a_game_reset_leaves_the_table_alone(void)
{
    menu_player = 2;
    rename_test_apply("Marta");
    assert(strcmp(player_names[2], "Marta") == 0);

    /* Reset is about the game - life totals, damage, counters. The
       people at the table have not changed. */
    reset_all_values();
    assert(strcmp(player_names[0], "Chema") == 0);
    assert(strcmp(player_names[2], "Marta") == 0);
    printf("PASS: resetting the game keeps the names\n");

    /* Nor does re-shaping the game. */
    prefs_set_num_players(6);
    prefs_set_life_total(20);
    reset_all_values();
    simulate_reboot();
    assert(strcmp(player_names[0], "Chema") == 0);
    assert(strcmp(player_names[2], "Marta") == 0);
    prefs_set_num_players(4);
    prefs_set_life_total(40);
    printf("PASS: changing the player count and starting life keeps them too\n");
}

static void test_renaming_back_is_also_remembered(void)
{
    menu_player = 0;
    rename_test_apply("P1");
    simulate_reboot();
    if (strcmp(player_names[0], "P1") != 0) {
        printf("FAIL: after renaming back, player 0 is '%s'\n", player_names[0]);
        assert(0);
    }
    printf("PASS: renaming back to a default is remembered as well\n");

    menu_player = 0;
    rename_test_apply("Chema");
}

/* The attack dial shows both names side by side in its hub, and the
   boxes are fixed-width and ellipsised. At 48px "Chema" came out
   clipped, which is what prompted the hub to grow. */
static void test_the_attack_hub_holds_a_real_name(void)
{
    static const char *names[] = { "Chema", "Marta", "Ana", "Luis", "Jordi" };
    size_t n;

    open_attack_screen(0, 0, 1, 1);
    lv_obj_update_layout(screen_attack);

    for (n = 0; n < sizeof(names) / sizeof(names[0]); n++) {
        lv_obj_t *lbl = attack_test_source_label();
        lv_point_t size;
        lv_coord_t box;

        assert(lbl != NULL);
        box = lv_obj_get_width(lbl);
        lv_txt_get_size(&size, names[n], lv_obj_get_style_text_font(lbl, LV_PART_MAIN),
                        0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x > box) {
            printf("FAIL: '%s' needs %dpx but the hub's name box is %dpx\n",
                   names[n], (int)size.x, (int)box);
            assert(0);
        }
    }
    printf("PASS: ordinary first names fit the attack hub on one line\n");
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);

    test_harness_init();
    test_harness_settle_intro();
    test_harness_reset_4p();

    test_a_rename_outlives_the_power_switch();
    test_a_game_reset_leaves_the_table_alone();
    test_renaming_back_is_also_remembered();
    test_the_attack_hub_holds_a_real_name();

    printf("\nAll player name tests passed.\n");
    return 0;
}
