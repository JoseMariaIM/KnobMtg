/* cmd_threats_for_player() (game_state.c): the per-rival summary the main
 * screen's commander-damage chips are built from. The ordering rule is the
 * interesting part - a rival's danger is their nearest-to-lethal single
 * commander, never the two added together, because 21 from either one
 * kills on its own. */
#include "test_harness.h"
#include "../../knobby/src/entities/game_state.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static void clear_cmd_damage(void)
{
    memset(cmd_damage_totals, 0, sizeof(cmd_damage_totals));
    memset(partner_cmd_damage_totals, 0, sizeof(partner_cmd_damage_totals));
}

int main(void)
{
    cmd_threat_t threats[CMD_THREAT_MAX];
    int count;

    test_harness_init();
    test_harness_reset_4p();
    clear_cmd_damage();

    /* ---- nobody has hit them yet ---- */
    assert(cmd_threats_for_player(1, threats, CMD_THREAT_MAX) == 0);
    printf("PASS: an untouched player has no threats to show\n");

    /* ---- one rival, primary commander only ---- */
    cmd_damage_totals[0][1] = 12;
    count = cmd_threats_for_player(1, threats, CMD_THREAT_MAX);
    assert(count == 1);
    assert(threats[0].source == 0);
    assert(threats[0].commander == 12);
    assert(threats[0].partner == 0);
    printf("PASS: one rival's commander damage is reported against them alone\n");

    /* ---- that rival's partner is the same rival, kept separate ---- */
    partner_cmd_damage_totals[0][1] = 5;
    count = cmd_threats_for_player(1, threats, CMD_THREAT_MAX);
    assert(count == 1); /* still ONE rival, not two */
    assert(threats[0].commander == 12 && threats[0].partner == 5);
    printf("PASS: a partner commander rides along in its rival's own entry\n");

    /* ---- the damage a player dealt to others is not their own threat ---- */
    cmd_damage_totals[1][0] = 18;
    count = cmd_threats_for_player(1, threats, CMD_THREAT_MAX);
    assert(count == 1 && threats[0].source == 0);
    printf("PASS: damage a player dealt out never counts as damage to them\n");

    /* ---- worst first, by closest single commander, not by sum ---- */
    clear_cmd_damage();
    cmd_damage_totals[0][1] = 10;          /* 10 + 10 = 20 total, max 10 */
    partner_cmd_damage_totals[0][1] = 10;
    cmd_damage_totals[2][1] = 15;          /* 15 total, max 15 */
    count = cmd_threats_for_player(1, threats, CMD_THREAT_MAX);
    assert(count == 2);
    assert(threats[0].source == 2);        /* 15 is closer to lethal than 10 */
    assert(threats[1].source == 0);
    printf("PASS: the rival nearest to a single lethal 21 sorts first\n");

    /* ---- more rivals than the screen shows: the worst ones survive ---- */
    clear_cmd_damage();
    {
        int source;
        for (source = 0; source < MAX_GAME_PLAYERS; source++) {
            if (source == 1) continue;
            cmd_damage_totals[source][1] = source + 1;
        }
    }
    count = cmd_threats_for_player(1, threats, 2);
    assert(count == 2);
    assert(threats[0].commander == MAX_GAME_PLAYERS);       /* source 7 */
    assert(threats[1].commander == MAX_GAME_PLAYERS - 1);   /* source 6 */
    printf("PASS: a capped request keeps the worst rivals, drops the rest\n");

    /* ---- a target nobody can display is not a target ---- */
    assert(cmd_threats_for_player(-1, threats, CMD_THREAT_MAX) == 0);
    assert(cmd_threats_for_player(MAX_DISPLAY_PLAYERS, threats, CMD_THREAT_MAX) == 0);
    assert(cmd_threats_for_player(1, threats, 0) == 0);
    printf("PASS: out-of-range targets and a zero cap report nothing\n");

    printf("\nAll commander-damage threat summary tests passed.\n");
    return 0;
}
