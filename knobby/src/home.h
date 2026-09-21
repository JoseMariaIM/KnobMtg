#ifndef _HOME_H
#define _HOME_H

/* "Return to the life counter" - the one navigation action that every
 * screen on the device needs and that none of them can own.
 *
 * Deciding where home IS means knowing both life screens and the
 * victory screen it must not load over; being callable means every
 * screen can reach it. Put the body anywhere and those two pull in
 * opposite directions: it lived in ui_1p.c, one of the two screens it
 * chooses between, and four files reached it through a hand-written
 * `extern void back_to_main(void);` that no compiler ever checked
 * against the real definition. Moving it up to nav.c fixed the owner
 * and made it worse - nav sits above the screens, so every screen then
 * depended upwards, and four cycles became eight.
 *
 * So this is the same seam game_hooks.h uses, for the same reason: a
 * dispatcher with no dependencies of its own, which nav registers the
 * real implementation with at boot. Screens depend on this leaf; the
 * implementation depends on the screens; nothing depends on both. */

#ifdef __cplusplus
extern "C" {
#endif

/* No-op until registered, so a test that only exercises game rules can
   leave it unset. */
void back_to_main(void);

/* Called once at boot from knob_gui(), with nav_go_home(). */
void back_to_main_register(void (*fn)(void));

#ifdef __cplusplus
}
#endif

#endif // _HOME_H
