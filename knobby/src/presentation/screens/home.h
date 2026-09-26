#ifndef _HOME_H
#define _HOME_H

/* The life counter, as the rest of the device sees it: go back to it,
 * or repaint it. Two actions every screen needs and that none of them
 * can own.
 *
 * Both have to know WHICH life counter is current - the 1p screen or
 * the multiplayer grid, and for "back", the victory screen neither may
 * load over - while staying callable from everywhere. Put the bodies
 * anywhere and those two pull in opposite directions: back_to_main()
 * lived in ui_1p.c, one of the two screens it chooses between, and
 * four files reached it through a hand-written
 * `extern void back_to_main(void);` that no compiler ever checked
 * against the real definition. Moving it up to nav.c fixed the owner
 * and made it worse - nav sits above the screens, so every screen then
 * depended upwards, and four cycles became eight.
 *
 * So this is the same seam game_hooks.h uses, for the same reason: a
 * dispatcher with no dependencies of its own, which nav registers the
 * real implementations with at boot. Screens depend on this leaf; the
 * implementations depend on the screens; nothing depends on both. */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*go)(void);      /* load whichever life counter is current */
    void (*refresh)(void); /* repaint it where it stands */
} home_screen_t;

/* Called once at boot from knob_gui(). The struct must outlive the
   call - pass a static one. */
void home_bind(const home_screen_t *screen);

/* Both no-ops until bound, so a test that only exercises game rules
   can leave home unwired. */
void back_to_main(void);
void refresh_player_ui(void);

#ifdef __cplusplus
}
#endif

#endif // _HOME_H
