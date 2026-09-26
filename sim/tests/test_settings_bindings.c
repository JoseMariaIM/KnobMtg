/* Every settings row that opens a screen has to be pointed at that
 * screen by knob.c at boot (see settings_bind_screen). settings.c used
 * to name those screens itself, which is what tied the settings
 * subsystem to every feature its menu can reach; the binding is what
 * replaced that.
 *
 * The failure mode is quiet. An unbound row still appears on its page,
 * just dimmed and inert - a screenshot looks plausible and nothing
 * crashes, so the tile is simply dead until somebody notices. Adding a
 * row to settings_items[] and forgetting the matching bind in
 * knob_gui() is exactly how that happens, so assert it directly. */
#include "test_harness.h"
#include "presentation/screens/settings.h"
#include <stdio.h>
#include <assert.h>

int main(void)
{
    const char *unbound;
    int page;

    test_harness_init();

    unbound = settings_test_unbound_item();
    if (unbound != NULL) {
        printf("FAIL: settings row \"%s\" opens nothing - missing a "
               "settings_bind_screen() call in knob_gui()\n", unbound);
        assert(unbound == NULL);
    }

    /* The binding is by id, and a typo in either half is silently
       ignored - so check that the ids knob.c binds are ids the table
       actually has. settings_item_page() answers -1 for one it does
       not. */
    static const char *bound_ids[] = {
        "brightness", "battery", "table-sync", "minigames",
        "language", "wifi", "updates",
    };
    for (size_t i = 0; i < sizeof(bound_ids) / sizeof(bound_ids[0]); i++) {
        page = settings_item_page(bound_ids[i]);
        if (page < 0) {
            printf("FAIL: knob.c binds \"%s\", which is not in settings_items[]\n",
                   bound_ids[i]);
            assert(page >= 0);
        }
    }

    printf("PASS: every settings navigation row is bound to a screen\n");
    return 0;
}
