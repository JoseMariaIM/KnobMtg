/* Regression test for lazy screen construction: the WiFi/OTA cluster
 * (6 screens, see ensure_wifi_ota_screens_built() in ui_wifi.c) and the
 * two minigames (see the build-on-open guards in snake.c/pong.c) are no
 * longer built eagerly at boot in knob_gui() - they're built on first
 * entry instead, to keep them out of LVGL's fixed 128KB heap
 * (LV_MEM_SIZE, knobby/lv_conf.h) until a session actually uses them.
 * This locks in the two things that matter: nothing pokes at those
 * screens' widgets before they exist (screen_* stays NULL until the
 * matching open_*_screen() call), and opening twice doesn't rebuild
 * (the lv_obj_t* pointer identity must stay the same, or every repeat
 * visit would leak the previous screen's objects into LVGL's heap). */
#include "test_harness.h"
#include "../../knobby/src/presentation/screens/ui_wifi.h"
#include "../../knobby/src/presentation/minigames/snake.h"
#include "../../knobby/src/presentation/minigames/pong.h"
#include "../../knobby/src/presentation/screens/settings.h"
#include "../../knobby/src/nav.h"
#include "../../knobby/src/presentation/screens/ui_table_sync.h"
#include "../../knobby/src/presentation/screens/ui_language.h"
#include "../../knobby/src/presentation/screens/ui_partners.h"
#include "../../knobby/src/presentation/screens/ui_battery.h"
#include "../../knobby/src/presentation/screens/minigames_menu.h"
#include <stdio.h>
#include <assert.h>

int main(void)
{
    lv_obj_t *first_build;

    test_harness_init();

    /* ---- Partners: a quad page most sessions never open ---- */
    assert(screen_partners == NULL);
    open_partners_screen();
    assert(screen_partners != NULL);
    first_build = screen_partners;
    open_partners_screen();
    assert(screen_partners == first_build);
    printf("PASS: the partners menu is built on first open, once\n");

    /* ---- WiFi/OTA cluster: NULL until the first of its three entry
       points is opened, then built as one unit ---- */
    assert(screen_wifi_settings == NULL);
    assert(screen_wifi_scan_list == NULL);
    assert(screen_wifi_text_entry == NULL);
    assert(screen_wifi_status == NULL);
    assert(screen_ota_update == NULL);
    assert(screen_ota_qr == NULL);
    printf("PASS: the WiFi/OTA cluster is not built at boot\n");

    open_wifi_settings_screen();
    assert(screen_wifi_settings != NULL);
    /* Entering via WiFi settings builds the whole cross-linked cluster,
       not just that one screen - see the comment on
       ensure_wifi_ota_screens_built(). */
    assert(screen_wifi_scan_list != NULL);
    assert(screen_wifi_text_entry != NULL);
    assert(screen_wifi_status != NULL);
    assert(screen_ota_update != NULL);
    assert(screen_ota_qr != NULL);
    printf("PASS: opening WiFi settings builds the entire cluster in one shot\n");

    first_build = screen_wifi_settings;
    open_wifi_settings_screen();
    assert(screen_wifi_settings == first_build); /* not rebuilt */
    open_ota_update_screen();
    assert(screen_wifi_settings == first_build); /* still not rebuilt */
    printf("PASS: re-entering the cluster from either entry point does not rebuild it\n");

    /* ---- Snake/Pong: NULL until their own open_*_screen(), independent
       of each other and of the WiFi/OTA cluster above ---- */
    assert(screen_snake == NULL);
    assert(screen_pong == NULL);
    printf("PASS: Snake and Pong are not built at boot\n");

    open_snake_screen();
    assert(screen_snake != NULL);
    assert(screen_pong == NULL); /* opening Snake must not also build Pong */
    printf("PASS: opening Snake builds only Snake, not Pong\n");

    first_build = screen_snake;
    snake_leave_screen();
    open_snake_screen();
    assert(screen_snake == first_build); /* not rebuilt on a second visit */
    printf("PASS: re-entering Snake does not rebuild it\n");

    open_pong_screen();
    assert(screen_pong != NULL);
    printf("PASS: opening Pong builds it independently of Snake\n");

    printf("\nAll lazy screen tests passed.\n");
    return 0;
}
