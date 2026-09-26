#include "minigames_menu.h"
#include "quad_screen.h"
#include "../../types.h"
#include <string.h>
#include "../minigames/snake.h"
#include "../minigames/pong.h"
#include "../minigames/dino.h"
#include "../minigames/tetris.h"
#include "../minigames/breakout.h"
#include "../minigames/flappy.h"
#include "../minigames/eggs.h"
#include "../minigames/invaders.h"
#include "../minigames/rps.h"
#include "../minigames/asteroids.h"

/* See minigames_menu.h. */

lv_obj_t *screen_minigames_menu = NULL;
/* Page 0 of the minigames menu IS screen_minigames_menu, so the
   settings item that opens it (and settings_handle_back's scan over
   nav_screen pointers) keeps working unchanged. */
lv_obj_t *minigames_pages[MINIGAMES_PAGE_MAX] = {NULL};
int minigames_page_count = 0;

/* Every game on the device, in menu order. This table is the only place
   a game has to be listed for it to appear, paginate and open: adding
   one is a row here plus its screen_registry[] row in knob.c (for the
   knob/back dispatch, which is per-screen, not per-menu-entry).

   Ordered oldest-first so the games people already have records in stay
   on the first page where they have always been. */
static const minigame_entry_t minigame_entries[] = {
    { STR_MINIGAME_SNAKE,    open_snake_screen    },
    { STR_MINIGAME_PONG,     open_pong_screen     },
    { STR_MINIGAME_DINO,     open_dino_screen     },
    { STR_MINIGAME_TETRIS,   open_tetris_screen   },
    { STR_MINIGAME_BREAKOUT, open_breakout_screen },
    { STR_MINIGAME_FLAPPY,   open_flappy_screen   },
    { STR_MINIGAME_EGGS,     open_eggs_screen     },
    { STR_MINIGAME_INVADERS, open_invaders_screen },
    { STR_MINIGAME_RPS,      open_rps_screen      },
    { STR_MINIGAME_ASTEROIDS, open_asteroids_screen },
};
#define MINIGAME_ENTRY_COUNT \
    ((int)(sizeof(minigame_entries) / sizeof(minigame_entries[0])))

/* Which menu page the running game was started from, so leaving it
   comes back to the tile the user actually pressed rather than dumping
   them on page 1 to page their way back. Recorded at launch rather than
   derived from a game->page table because the page the user was looking
   at IS the page that game's tile is on - and this stays right if a
   game is ever listed twice or the order changes. */
static int minigames_launch_page = 0;

static void event_open_minigame(lv_event_t *e)
{
    const minigame_entry_t *entry = lv_event_get_user_data(e);
    int i;

    for (i = 0; i < minigames_page_count; i++) {
        if (lv_scr_act() == minigames_pages[i]) {
            minigames_launch_page = i;
            break;
        }
    }
    if (entry != NULL && entry->open != NULL) entry->open();
}

static void event_minigames_more(lv_event_t *e)
{
    int page = (int)(intptr_t)lv_event_get_user_data(e);
    if (page >= 0 && page < minigames_page_count)
        lv_scr_load(minigames_pages[page]);
}

/* Fresh entry from Settings: always page 1. */
void open_minigames_menu(void)
{
    if (screen_minigames_menu == NULL) build_minigames_menu_screen();
    minigames_launch_page = 0;
    load_screen_if_needed(screen_minigames_menu);
}

/* Leaving a game: back to the page it was started from. */
void open_minigames_menu_at_launch_page(void)
{
    if (screen_minigames_menu == NULL) build_minigames_menu_screen();
    if (minigames_launch_page < 0 || minigames_launch_page >= minigames_page_count)
        minigames_launch_page = 0;
    load_screen_if_needed(minigames_pages[minigames_launch_page]);
}

/* Same 3-items-plus-"More" chunking as build_settings_pages(); see the
   comment there for why "More" wraps rather than dead-ending. */
void build_minigames_menu_screen(void)
{
    int idx = 0;
    int page = 0;
    int total_pages = (MINIGAME_ENTRY_COUNT + 2) / 3;

    while (idx < MINIGAME_ENTRY_COUNT && page < MINIGAMES_PAGE_MAX) {
        int remaining = MINIGAME_ENTRY_COUNT - idx;
        int on_page = (remaining < 3) ? remaining : 3;
        int s;
        quad_item_t q[4];

        memset(q, 0, sizeof(q));
        for (s = 0; s < 4; s++) q[s].label = "";
        for (s = 0; s < on_page; s++, idx++) {
            q[s].label = t(minigame_entries[idx].name);
            q[s].cb = event_open_minigame;
            q[s].enabled = true;
            q[s].event = LV_EVENT_CLICKED;
            q[s].user_data = (void *)&minigame_entries[idx];
        }
        q[3].label = t(STR_SETTINGS_MORE);
        q[3].cb = event_minigames_more;
        q[3].enabled = true;
        q[3].event = LV_EVENT_CLICKED;
        q[3].user_data = (void *)(intptr_t)((page + 1) % total_pages);
        build_quad_screen(&minigames_pages[page], q);
        page++;
    }
    minigames_page_count = page;
    /* Page 0 doubles as the menu's public screen global - see the
       comment where it is declared. */
    screen_minigames_menu = minigames_pages[0];
}

bool minigames_knob_page(int dir)
{
    int i;

    for (i = 0; i < minigames_page_count; i++) {
        if (lv_scr_act() == minigames_pages[i]) {
            lv_scr_load(minigames_pages[(i + dir + minigames_page_count) %
                                        minigames_page_count]);
            return true;
        }
    }
    return false;
}
