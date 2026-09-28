/* The commander-damage chips on the main screen (mp_threat_chips.c).
 *
 * What is worth pinning down here is who a chip says the damage came
 * from. The chip's color cannot carry that on its own - with the color
 * mode set to life, every panel is colored by its life tier, so two
 * rivals on similar life are the same color - so each chip also names
 * its rival in the one or two characters it has room for. */
#include "test_harness.h"
#include "../../knobby/src/presentation/screens/ui_mp.h"
#include "../../knobby/src/presentation/screens/ui_mp_internal.h"
#include "../../knobby/src/presentation/screens/mp_threat_chips.h"
#include "../../knobby/src/entities/game_state.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

/* The chips belong to the panel showing that player, which is not
   always the panel at that index (see panels_2p, where the two swap). */
static const char *chip_text_for(int player, int chip_index)
{
    const mp_layout_spec_t *layout = mp_current_layout();
    int i;

    assert(layout != NULL);
    for (i = 0; i < layout->panel_count; i++) {
        if (layout->panels[i].player_index == player) {
            lv_obj_t *chip = mp_threat_chips_obj(i, chip_index);
            assert(chip != NULL);
            if (lv_obj_has_flag(chip, LV_OBJ_FLAG_HIDDEN)) return NULL;
            return lv_label_get_text(chip);
        }
    }
    assert(0 && "no panel shows that player");
    return NULL;
}

int main(void)
{
    test_harness_init();
    test_harness_reset_4p();
    memset(cmd_damage_totals, 0, sizeof(cmd_damage_totals));
    memset(partner_cmd_damage_totals, 0, sizeof(partner_cmd_damage_totals));

    /* ---- no commander damage, no chips ---- */
    refresh_multiplayer_ui();
    assert(chip_text_for(1, 0) == NULL);
    printf("PASS: a player nobody has hit shows no chip at all\n");

    /* ---- still on the P1..P8 defaults: every initial would be the
       same 'P', so a chip names the seat instead ---- */
    cmd_damage_totals[0][1] = 12;
    refresh_multiplayer_ui();
    assert(strcmp(chip_text_for(1, 0), "1\n12") == 0);
    printf("PASS: an unrenamed rival is named by seat number, not by 'P'\n");

    /* ---- renamed: the initial identifies them ---- */
    snprintf(player_names[0], sizeof(player_names[0]), "%s", "Wanda");
    refresh_multiplayer_ui();
    assert(strcmp(chip_text_for(1, 0), "W\n12") == 0);
    printf("PASS: a renamed rival is named by their initial\n");

    /* ---- an accented initial is two bytes: half of one renders as
       nothing, so the whole character has to come across ---- */
    snprintf(player_names[0], sizeof(player_names[0]), "%s", "Ángel");
    refresh_multiplayer_ui();
    assert(strcmp(chip_text_for(1, 0), "Á\n12") == 0);
    printf("PASS: an accented initial survives whole\n");

    /* ---- the number is the nearest commander to lethal, not the sum:
       the two never pool, so 21 from either one is what kills ---- */
    partner_cmd_damage_totals[0][1] = 17;
    refresh_multiplayer_ui();
    assert(strcmp(chip_text_for(1, 0), "Á\n17") == 0);
    printf("PASS: a rival's chip shows their commander nearest to lethal\n");

    /* ---- one chip per rival, worst first ---- */
    cmd_damage_totals[2][1] = 20;
    refresh_multiplayer_ui();
    assert(strcmp(chip_text_for(1, 0), "3\n20") == 0);  /* P3, still unrenamed */
    assert(strcmp(chip_text_for(1, 1), "Á\n17") == 0);
    printf("PASS: rivals get one chip each, the worst of them first\n");

    /* ---- coloring by life instead of by player changes no text: that
       is the whole reason the chips carry one ---- */
    prefs_set_color_mode(COLOR_MODE_LIFE);
    refresh_multiplayer_ui();
    assert(strcmp(chip_text_for(1, 0), "3\n20") == 0);
    assert(strcmp(chip_text_for(1, 1), "Á\n17") == 0);
    printf("PASS: the life color mode leaves every chip still naming its rival\n");

    printf("\nAll commander-damage chip tests passed.\n");
    return 0;
}
