/* Who is responsible for a preference reaching flash.
 *
 * It used to be the caller. Every nvs_set_*() only marked a RAM cache
 * dirty, and twelve modules were each expected to remember a
 * settings_save() afterwards - some of them doing it on the way out of
 * a screen, which put the decision in navigation code. Anything that
 * wrote a preference from a path nobody had thought of lost it at the
 * next power cycle, which is exactly what net_sync_apply_names() did
 * with a name typed on the device next to you.
 *
 * Now prefs.c asks for the flush itself and prefs_autosave.c waits
 * for the writes to stop. That trade is only worth anything if BOTH
 * halves hold, so both are asserted here: the value does reach flash
 * without anyone asking, and a burst of writes still costs one erase
 * cycle rather than one per turn of the knob. */
#include "test_harness.h"
#include "sim_stubs.h"
#include "adapters/prefs_autosave.h"
#include "entities/game_state.h"
#include "adapters/net_sync.h"
#include "nvs.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

/* Long enough for the autosave's quiet period to elapse, pumped the
   way the real main loop pumps LVGL. */
static void let_the_dust_settle(void)
{
    int elapsed;
    for (elapsed = 0; elapsed < PREFS_AUTOSAVE_MS + 200; elapsed += 20) {
        sim_tick_advance(20);
        lv_timer_handler();
    }
}

/* What is actually ON "flash" - read back through the NVS API rather
   than through prefs.c's getters, which would happily answer from
   the RAM cache and tell us nothing. */
static int stored_i8(const char *key)
{
    nvs_handle_t h;
    int8_t v = -128;
    if (nvs_open("knobby", NVS_READONLY, &h) != ESP_OK) return -128;
    nvs_get_i8(h, key, &v);
    nvs_close(h);
    return v;
}

static void stored_player_name(int player, char *out, size_t out_len)
{
    nvs_handle_t h;
    char names[PLAYER_NAME_COUNT][PLAYER_NAME_LEN];
    size_t len = sizeof(names);

    out[0] = '\0';
    memset(names, 0, sizeof(names));
    if (nvs_open("knobby", NVS_READONLY, &h) != ESP_OK) return;
    if (nvs_get_blob(h, "pl_names", names, &len) == ESP_OK &&
        player >= 0 && player < PLAYER_NAME_COUNT) {
        snprintf(out, out_len, "%s", names[player]);
    }
    nvs_close(h);
}

static void test_a_write_reaches_flash_with_nobody_asking(void)
{
    unsigned before = sim_nvs_commit_count();

    prefs_set_life_total(31);
    /* Still only in RAM: the whole point of the delay is that a knob
       still in the player's fingers has not cost an erase cycle. */
    assert(sim_nvs_commit_count() == before);

    let_the_dust_settle();
    assert(sim_nvs_commit_count() == before + 1);
    if (stored_i8("life_total") != 31) {
        printf("FAIL: life_total on flash is %d, expected 31\n",
               stored_i8("life_total"));
        assert(0);
    }
    printf("PASS: a preference reaches flash on its own, after the writes stop\n");
}

static void test_a_burst_costs_one_erase_cycle(void)
{
    unsigned before = sim_nvs_commit_count();
    int i;

    /* Twenty detents of the brightness knob, the way a player turns it. */
    for (i = 0; i < 20; i++) {
        prefs_set_brightness(20 + i);
        sim_tick_advance(40);
        lv_timer_handler();
    }
    assert(sim_nvs_commit_count() == before); /* nothing yet - still turning */

    let_the_dust_settle();
    if (sim_nvs_commit_count() != before + 1) {
        printf("FAIL: 20 writes cost %u commits, expected 1\n",
               sim_nvs_commit_count() - before);
        assert(0);
    }
    assert(stored_i8("brightness") == 39);
    printf("PASS: a burst of writes still costs one commit, not twenty\n");
}

static void test_prefs_flush_does_not_wait(void)
{
    unsigned before = sim_nvs_commit_count();

    prefs_set_num_players(6);
    prefs_flush(); /* the reboot / deep-sleep / OTA path */
    assert(sim_nvs_commit_count() == before + 1);
    assert(stored_i8("num_players") == 6);
    printf("PASS: prefs_flush() commits on the spot, for the paths about to lose RAM\n");
}

/* The bug this stage was really about. */
static void test_a_name_from_another_device_survives(void)
{
    net_sync_names_t incoming;
    char stored[PLAYER_NAME_LEN];
    int i;

    memset(&incoming, 0, sizeof(incoming));
    incoming.version = (uint16_t)(1000);
    for (i = 0; i < NET_SYNC_MAX_SOURCES; i++)
        snprintf(incoming.names[i], NET_SYNC_NAME_LEN, "P%d", i + 1);
    snprintf(incoming.names[2], NET_SYNC_NAME_LEN, "Marta");

    net_sync_apply_names(&incoming, /*wins_ties=*/1);
    assert(strcmp(player_names[2], "Marta") == 0); /* on screen */

    let_the_dust_settle();
    stored_player_name(2, stored, sizeof(stored));
    if (strcmp(stored, "Marta") != 0) {
        printf("FAIL: an adopted name is '%s' on flash, expected 'Marta'\n", stored);
        assert(0);
    }
    printf("PASS: a name adopted from another device survives a power cycle\n");
}

int main(void)
{
    test_harness_init();
    test_harness_settle_intro();
    test_harness_reset_4p();
    let_the_dust_settle(); /* drain whatever boot itself dirtied */

    test_a_write_reaches_flash_with_nobody_asking();
    test_a_burst_costs_one_erase_cycle();
    test_prefs_flush_does_not_wait();
    test_a_name_from_another_device_survives();

    printf("\nAll preference autosave tests passed.\n");
    return 0;
}
