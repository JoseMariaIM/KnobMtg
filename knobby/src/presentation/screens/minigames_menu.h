#ifndef _MINIGAMES_MENU_H
#define _MINIGAMES_MENU_H

#include "knob.h"
#include "adapters/lang.h"

/* The launcher: which games exist, in what order, and the quad pages
 * that show them.
 *
 * This is a menu over the games, not a setting, and keeping it in
 * settings.c meant settings.c had to include all ten game headers to
 * name their open_*_screen() functions - ten of its twenty-two
 * includes, for a screen that has nothing to do with brightness or
 * auto-dim. The games themselves know nothing about this file; the
 * table in minigames_menu.c is the one place a game is listed. */

#ifdef __cplusplus
extern "C" {
#endif

/* One quad page per three games, plus "More". Sized for comfortably
   more games than exist, so adding one is only a row in
   minigame_entries[] (minigames_menu.c). */
#define MINIGAMES_PAGE_MAX 6

/* One game as the menu sees it: a name and the door into it. The game's
   own knob/tap/back behaviour is per-screen and lives in knob.c's
   screen_registry[]. */
typedef struct {
    string_id_t name;
    void (*open)(void);
} minigame_entry_t;

extern lv_obj_t *screen_minigames_menu;
extern lv_obj_t *minigames_pages[MINIGAMES_PAGE_MAX];
extern int minigames_page_count;

void build_minigames_menu_screen(void);
/* Fresh entry from Settings: always page 1. */
void open_minigames_menu(void);
/* Leaving a game: back to the page it was started from. */
void open_minigames_menu_at_launch_page(void);
/* Knob left/right flips pages, with wraparound. False when the active
   screen is not one of them. */
bool minigames_knob_page(int dir);

#ifdef __cplusplus
}
#endif

#endif // _MINIGAMES_MENU_H
