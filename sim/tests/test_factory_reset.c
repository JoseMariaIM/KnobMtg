/* prefs_factory_reset(): the whole stored state goes, and what is left
 * in RAM is what a factory-fresh unit boots with.
 *
 * Worth a test because the failure is silent and only visible later: the
 * reset erases NVS, and the running image keeps serving whatever is
 * still in prefs.c's cache until it reboots. A cache left holding the
 * old values would make the reset look like it worked (the reboot hides
 * it) right up until the values came back; a cache zeroed instead of
 * defaulted would hand out 0% brightness and 0 starting life. Neither
 * shows up in a screenshot.
 */
#include "test_harness.h"
#include "../../knobby/src/adapters/prefs_device.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

int main(void)
{
    char names[PLAYER_NAME_COUNT][PLAYER_NAME_LEN];
    char buf[64];
    unsigned commits_before;

    test_harness_init();

    /* ---- Configure the device the way a tester's would be ---- */
    prefs_set_brightness(25);
    prefs_set_auto_dim(AUTO_DIM_OFF);
    prefs_set_life_total(20);
    prefs_set_num_players(6);
    prefs_set_auto_eliminate(0);
    prefs_set_language(1);
    prefs_set_wifi_ssid("CasaDeChema");
    prefs_set_wifi_pass("un-secreto");
    prefs_set_device_name("Mesa Chema");
    prefs_set_last_fw_version("v1.2.3");
    prefs_set_game_high_score(GAME_SCORE_SNAKE, 0, 42);
    memset(names, 0, sizeof(names));
    snprintf(names[0], PLAYER_NAME_LEN, "%s", "Chema");
    prefs_set_player_names((const char (*)[PLAYER_NAME_LEN])names);
    prefs_flush();
    assert(prefs_has_player_names());
    assert(prefs_has_device_name());

    /* ---- Reset ---- */
    prefs_factory_reset();

    /* Every default back, from the cache the running image still reads
       between the erase and the reboot. */
    assert(prefs_get_brightness() == DEFAULT_BRIGHTNESS_PERCENT);
    assert(prefs_get_auto_dim() == AUTO_DIM_30S);
    assert(prefs_get_life_total() == DEFAULT_LIFE_TOTAL);
    assert(prefs_get_num_players() == 4);
    assert(prefs_get_players_to_track() == 1);
    assert(prefs_get_auto_eliminate() == 1);
    assert(prefs_get_random_first() == 1);
    assert(prefs_get_multi_select() == 0);
    assert(prefs_get_language() == 0);
    assert(prefs_get_partner_mask() == 0);
    assert(prefs_get_game_high_score(GAME_SCORE_SNAKE, 0) == 0);
    printf("PASS: every setting is back to its factory default\n");

    /* Nothing personal survives: the WiFi password included, which is
       the point of handing a unit to somebody else with a clean slate. */
    prefs_get_wifi_ssid(buf, sizeof(buf));
    assert(buf[0] == '\0');
    prefs_get_wifi_pass(buf, sizeof(buf));
    assert(buf[0] == '\0');
    prefs_get_device_name(buf, sizeof(buf));
    assert(buf[0] == '\0');
    assert(!prefs_has_device_name());
    prefs_get_player_names(names);
    assert(names[0][0] == '\0');
    assert(!prefs_has_player_names());
    /* Cleared too, so the next boot does not think an OTA just landed
       and show the "updated to..." toast on a wiped device. */
    prefs_get_last_fw_version(buf, sizeof(buf));
    assert(buf[0] == '\0');
    printf("PASS: names, WiFi credentials and the device name are gone\n");

    /* A write was pending when the reset ran (every setter above marked
       the cache dirty). Flushing it afterwards would put the pre-reset
       blob straight back into the namespace that was just erased. */
    commits_before = sim_nvs_commit_count();
    prefs_flush();
    assert(sim_nvs_commit_count() == commits_before);
    printf("PASS: the pending write is dropped, not replayed over the erase\n");

    printf("PASS: factory reset\n");
    return 0;
}
