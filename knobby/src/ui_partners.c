#include "ui_partners.h"
#include "quad_screen.h"
#include "types.h"
#include "game.h"
#include "prefs_table.h"
#include "lang.h"
#include <string.h>

lv_obj_t *screen_partners = NULL;

// ---------- partners ----------
/* Which players field a partner commander.
 *
 * A game-setup choice, so it lives beside the player count and the
 * starting life rather than inside any one player's menu: you set the
 * table up once, and every partner control on the device - the second
 * commander-damage tally, the partner-tax counter, the attack dial's
 * second commander mode - appears or disappears from here.
 *
 * Default is nobody. Most tables have no partners at all, and showing
 * those controls to everyone made the device look like it was tracking
 * something that was not on the table.
 *
 * Laid out as quarters like every other menu on this device rather
 * than as a list of rows: a row list wastes the corners of a round
 * display and reads as a form, and this screen is a menu of players.
 *
 * ONE screen, repainted, rather than one screen per page the way the
 * settings and minigames menus do it. Those have a fixed number of
 * pages; this one is sized by the player count, and at a full table of
 * eight the three pages it would need cost about 13KB of a 128KB pool
 * - enough to fail the memory budget on their own. Repainting four
 * tiles costs nothing and there is no screen transition between pages,
 * which for a settings grid reads better anyway. */
#define PARTNERS_PER_PAGE 3   /* when paging is needed: three players + "More" */
static int partners_page = 0;

static int partners_page_count(void)
{
    int num = prefs_get_num_players();
    if (num <= 4) return 1;
    return (num + PARTNERS_PER_PAGE - 1) / PARTNERS_PER_PAGE;
}

/* The player a quarter stands for on the page being shown, or -1 when
   the quarter is "More" or past the end of the table. */
static int partners_tile_player(int slot)
{
    int num = prefs_get_num_players();
    int player;

    if (slot < 0 || slot >= 4) return -1;
    /* A table that fits uses all four quarters for players: spending
       one on a "More" that leads back to the same page would be silly. */
    if (num <= 4) return (slot < num) ? slot : -1;
    if (slot >= PARTNERS_PER_PAGE) return -1;   /* the "More" quarter */
    player = partners_page * PARTNERS_PER_PAGE + slot;
    return (player < num) ? player : -1;
}

static void refresh_partners_ui(void)
{
    int slot;

    if (screen_partners == NULL) return;

    for (slot = 0; slot < 4; slot++) {
        int player = partners_tile_player(slot);
        lv_obj_t *tile = lv_obj_get_child(screen_partners, slot);
        lv_obj_t *text;
        char buf[48];

        if (tile == NULL) continue;
        text = lv_obj_get_child(tile, 0);
        if (text == NULL) continue;

        if (player < 0) {
            bool is_more = (partners_page_count() > 1 && slot == 3);
            set_btn_color(tile, is_more ? 0x1A1A2E : 0x111111);
            lv_label_set_text(text, is_more ? t(STR_SETTINGS_MORE) : "");
            continue;
        }

        /* Spelled out under the name rather than left to the colour:
           this is the one screen where the answer has to be
           unambiguous, and "green means yes" is a convention the
           reader has to be told. */
        snprintf(buf, sizeof(buf), "%s\n%s", player_names[player],
                 t(player_has_partner(player) ? STR_PARTNER_YES : STR_PARTNER_NO));
        lv_label_set_text(text, buf);
        set_btn_color(tile, player_has_partner(player) ? TOGGLE_ON : 0x1A1A2E);
    }
}

static void event_partner_tile(lv_event_t *e)
{
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    int player = partners_tile_player(slot);

    /* The "More" quarter and the empty ones share this handler, since
       which is which depends on the page and the player count - both
       of which move without the screen being rebuilt. */
    if (player < 0) {
        int pages = partners_page_count();
        if (pages > 1 && slot == 3) partners_page = (partners_page + 1) % pages;
        refresh_partners_ui();
        return;
    }

    set_player_has_partner(player, !player_has_partner(player));
    refresh_partners_ui();
}

void build_partners_screen(void)
{
    quad_item_t q[4];
    int s;

    memset(q, 0, sizeof(q));
    for (s = 0; s < 4; s++) {
        q[s].label = "";
        q[s].cb = event_partner_tile;
        q[s].enabled = true;
        q[s].event = LV_EVENT_CLICKED;
        q[s].user_data = (void *)(intptr_t)s;
    }
    build_quad_screen(&screen_partners, q);
}

void open_partners_screen(void)
{
    if (screen_partners == NULL) build_partners_screen();
    /* The table can have shrunk since the last visit; landing on a
       page that no longer exists would show four empty quarters. */
    if (partners_page >= partners_page_count()) partners_page = 0;
    refresh_partners_ui();
    load_screen_if_needed(screen_partners);
}

/* Knob flips pages, with wraparound - the same gesture that flips
   settings pages, so a big table pages the way everything else does. */
bool partners_knob_page(int dir)
{
    int pages = partners_page_count();

    if (lv_scr_act() != screen_partners || screen_partners == NULL) return false;
    if (pages > 1) {
        partners_page = (partners_page + dir + pages) % pages;
        refresh_partners_ui();
    }
    return true;
}

/* ---------- read-only accessors (unit tests) ---------- */
int partners_test_page_count(void)      { return partners_page_count(); }
int partners_test_page(void)            { return partners_page; }
void partners_page_reset_for_test(void) { partners_page = 0; refresh_partners_ui(); }
int partners_test_tile_player(int slot) { return partners_tile_player(slot); }
