/* Partner commanders, and the rule that nothing about them shows up
 * until somebody says they have one.
 *
 * Most tables play no partners at all, and the device used to show the
 * second commander-damage tab and the partner-tax counter to everyone
 * regardless - which made it look like it was tracking something that
 * was not on the table. The flag is now per player, set once in
 * Settings, and every partner control on the device is gated on it.
 *
 * What is worth guarding here is the gating, not the toggle: a control
 * that reappears for a player with no partner is a second 21-damage
 * clock nobody declared. */
#include "test_harness.h"
#include "sim_stubs.h"
#include "game_state.h"
#include "storage.h"
#include "settings.h"
#include "ui_1p.h"
#include "ui_player_menu.h"
#include <stdio.h>
#include <assert.h>

static void test_the_flag_is_per_player_and_survives_a_save(void)
{
    int i;

    for (i = 0; i < MAX_GAME_PLAYERS; i++) assert(!player_has_partner(i));
    printf("PASS: nobody has a partner by default\n");

    set_player_has_partner(2, true);
    set_player_has_partner(5, true);
    for (i = 0; i < MAX_GAME_PLAYERS; i++) {
        bool want = (i == 2 || i == 5);
        if (player_has_partner(i) != want) {
            printf("FAIL: player %d reads %d, expected %d\n",
                   i, (int)player_has_partner(i), (int)want);
            assert(0);
        }
    }
    printf("PASS: the flag lands on the players it was set for, and no others\n");

    /* Stored as a bitmask beside the other settings, so the mask a
       save writes has to be the mask the getters describe. */
    assert(nvs_get_partner_mask() == ((1 << 2) | (1 << 5)));
    settings_save();
    assert(nvs_get_partner_mask() == ((1 << 2) | (1 << 5)));
    printf("PASS: the mask survives a settings save\n");

    set_player_has_partner(2, false);
    set_player_has_partner(5, false);
    assert(!any_player_has_partner());
    printf("PASS: clearing the last one leaves nothing set\n");
}

/* The commander-damage screen's slot tabs, which are the control the
   partner flag was added for. Asked of the opponents LISTED for this
   target: a partner elsewhere in the game is not a choice this screen
   can make. */
static void test_the_slot_tabs_follow_the_listed_opponents(void)
{
    /* Bob(1) is the target, so Alice(0), Carol(2) and Dave(3) are the
       rows. Give the target himself a partner and nobody else: his own
       partner is irrelevant here, because he is not a source. */
    set_player_has_partner(1, true);
    prepare_cmd_damage_for_player(1);
    refresh_select_ui();
    assert(!select_test_slot_tabs_visible());
    printf("PASS: the target's own partner does not raise the tabs\n");

    set_player_has_partner(2, true);
    prepare_cmd_damage_for_player(1);
    refresh_select_ui();
    assert(select_test_slot_tabs_visible());
    printf("PASS: one listed opponent with a partner raises the tabs\n");

    /* And taking it away pins the screen back to the primary tally
       rather than leaving it parked on a slot with no tabs to leave. */
    cmd_damage_slot = 1;
    set_player_has_partner(2, false);
    refresh_select_ui();
    assert(!select_test_slot_tabs_visible());
    assert(cmd_damage_slot == 0);
    printf("PASS: losing the last partner hides the tabs and resets the slot\n");

    set_player_has_partner(1, false);
}

/* The partner-tax counter tile, which is per player rather than per
   screen - the menu is built once and visited for whoever was held. */
static void test_the_partner_tax_tile_follows_the_player(void)
{
    set_player_has_partner(0, true);

    open_player_menu(0);
    open_counter_menu();
    assert(counter_menu_test_partner_tile_visible());
    printf("PASS: the partner-tax tile shows for a player who has one\n");

    open_player_menu(1);
    open_counter_menu();
    assert(!counter_menu_test_partner_tile_visible());
    printf("PASS: the same menu hides it for the next player along\n");

    set_player_has_partner(0, false);
}

/* The screen itself: quad pages like every other menu here, not a list
 * of rows.
 *
 * What is worth pinning down is the chunking, because it has two
 * shapes. A table that fits in one page uses all four quarters for
 * players - spending one on a "More" that leads back to itself would
 * be silly - while a bigger one chunks three-plus-"More" exactly as
 * the settings and minigames menus do. Either way every player must
 * appear exactly once: a player who fell off the end could never be
 * given a partner, and one listed twice would have two tiles
 * disagreeing about the same flag. */
static lv_indev_drv_t test_pointer_drv;
static lv_point_t test_pointer_at;
static bool test_pointer_down;

static void test_pointer_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    data->point = test_pointer_at;
    data->state = test_pointer_down ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

static void tick_ms(int ms)
{
    sim_tick_advance((uint32_t)ms);
    lv_timer_handler();
}

/* Taps the middle of one quarter, the way a finger would. */
static void tap_quarter(int slot)
{
    static const int qx[4] = { 89, 271, 89, 271 };
    static const int qy[4] = { 89, 89, 271, 271 };
    int i;

    test_pointer_at.x = (lv_coord_t)qx[slot];
    test_pointer_at.y = (lv_coord_t)qy[slot];
    test_pointer_down = true;
    for (i = 0; i < 3; i++) tick_ms(20);
    test_pointer_down = false;
    for (i = 0; i < 3; i++) tick_ms(20);
}

/* Every player must be reachable, exactly once, across the pages.
   One who fell off the end could never be given a partner; one listed
   twice would have two tiles disagreeing about the same flag. */
static void check_every_player_has_exactly_one_tile(int num)
{
    int seen[MAX_GAME_PLAYERS] = {0};
    int page, slot, i;
    int pages = partners_test_page_count();

    for (page = 0; page < pages; page++) {
        for (slot = 0; slot < 4; slot++) {
            int player = partners_test_tile_player(slot);
            if (player < 0) continue;
            assert(player < MAX_GAME_PLAYERS);
            seen[player]++;
        }
        partners_knob_page(1);
    }
    for (i = 0; i < num; i++) {
        if (seen[i] != 1) {
            printf("FAIL: %d players - player %d appears on %d tiles\n",
                   num, i, seen[i]);
            assert(0);
        }
    }
    for (i = num; i < MAX_GAME_PLAYERS; i++) assert(seen[i] == 0);
}

static void test_the_pages_chunk_like_the_other_menus(void)
{
    nvs_set_num_players(4);
    open_partners_screen();
    tick_ms(20);
    assert(partners_test_page_count() == 1);
    check_every_player_has_exactly_one_tile(4);
    /* All four quarters are players: no "More" leading back to the
       page you are already on. */
    assert(partners_test_tile_player(3) == 3);
    printf("PASS: a four-player table is one page, one player per quarter\n");

    nvs_set_num_players(8);
    open_partners_screen();
    tick_ms(20);
    assert(partners_test_page_count() == 3);
    check_every_player_has_exactly_one_tile(8);
    /* Three players plus "More", the settings-menu shape. */
    assert(partners_test_tile_player(3) == -1);
    printf("PASS: an eight-player table chunks three-plus-More across %d pages\n",
           partners_test_page_count());

    /* One screen for every table size, repainted - three pages' worth
       of separate screens cost about 13KB of the 128KB LVGL pool and
       failed the memory budget outright. */
    {
        lv_obj_t *at_eight = screen_partners;
        nvs_set_num_players(4);
        open_partners_screen();
        assert(screen_partners == at_eight);
        assert(partners_test_page_count() == 1);
        check_every_player_has_exactly_one_tile(4);
        printf("PASS: the same screen serves every table size\n");
    }

    /* Shrinking the table while parked on a page that no longer exists
       must not leave four empty quarters. */
    nvs_set_num_players(8);
    open_partners_screen();
    partners_knob_page(1);
    partners_knob_page(1);
    assert(partners_test_page() == 2);
    nvs_set_num_players(4);
    open_partners_screen();
    assert(partners_test_page() == 0);
    assert(partners_test_tile_player(0) == 0);
    printf("PASS: a shrunken table lands back on a page that exists\n");
}

static void test_tapping_a_quarter_toggles_that_player(void)
{
    nvs_set_num_players(4);
    open_partners_screen();
    tick_ms(20);
    assert(lv_scr_act() == screen_partners);

    assert(!player_has_partner(2));
    tap_quarter(2);   /* bottom-left quarter is player index 2 */
    assert(player_has_partner(2));
    assert(!player_has_partner(0));
    assert(!player_has_partner(1));
    assert(!player_has_partner(3));
    printf("PASS: a tap sets the partner for that quarter's player alone\n");

    tap_quarter(2);
    assert(!player_has_partner(2));
    printf("PASS: tapping again takes it back off\n");
}

/* Paging is a repaint, not a navigation: the knob flips which three
   players the quarters stand for, and the "More" quarter does the same
   thing with a finger. */
static void test_paging(void)
{
    nvs_set_num_players(8);
    open_partners_screen();
    tick_ms(20);
    assert(partners_test_page() == 0);
    assert(partners_test_tile_player(0) == 0);

    assert(partners_knob_page(1));
    assert(partners_test_page() == 1);
    assert(partners_test_tile_player(0) == 3);
    assert(lv_scr_act() == screen_partners);   /* no screen change */
    assert(partners_knob_page(-1));
    assert(partners_test_page() == 0);
    /* Wraps rather than dead-ending, same as the settings pages. */
    assert(partners_knob_page(-1));
    assert(partners_test_page() == partners_test_page_count() - 1);
    printf("PASS: the knob flips partner pages, wraps, and stays on one screen\n");

    partners_page_reset_for_test();
    tap_quarter(3);   /* the "More" quarter */
    assert(partners_test_page() == 1);
    printf("PASS: the More quarter advances the page too\n");

    /* And a tap on a page other than the first has to hit the player
       that page is showing, not the one in that slot on page 0. */
    assert(!player_has_partner(3));
    tap_quarter(0);
    assert(player_has_partner(3));
    assert(!player_has_partner(0));
    set_player_has_partner(3, false);
    printf("PASS: a tap on a later page toggles that page's player\n");

    nvs_set_num_players(4);
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);

    test_harness_init();
    lv_indev_drv_init(&test_pointer_drv);
    test_pointer_drv.type = LV_INDEV_TYPE_POINTER;
    test_pointer_drv.read_cb = test_pointer_read;
    lv_indev_drv_register(&test_pointer_drv);
    test_harness_settle_intro();
    test_harness_reset_4p();

    test_the_flag_is_per_player_and_survives_a_save();
    test_the_slot_tabs_follow_the_listed_opponents();
    test_the_partner_tax_tile_follows_the_player();
    test_the_pages_chunk_like_the_other_menus();
    test_tapping_a_quarter_toggles_that_player();
    test_paging();

    printf("\nAll partner tests passed.\n");
    return 0;
}
