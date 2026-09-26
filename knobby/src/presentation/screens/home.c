#include "home.h"
#include <stddef.h>

/* See home.h. */

static const home_screen_t *bound = NULL;

void home_bind(const home_screen_t *screen)
{
    bound = screen;
}

void back_to_main(void)
{
    if (bound != NULL && bound->go != NULL) bound->go();
}

void refresh_player_ui(void)
{
    if (bound != NULL && bound->refresh != NULL) bound->refresh();
}
