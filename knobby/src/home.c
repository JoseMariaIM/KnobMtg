#include "home.h"
#include <stddef.h>

/* See home.h. */

static void (*home_fn)(void) = NULL;

void back_to_main_register(void (*fn)(void))
{
    home_fn = fn;
}

void back_to_main(void)
{
    if (home_fn != NULL) home_fn();
}
