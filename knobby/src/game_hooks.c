#include "game_hooks.h"
#include <string.h>

static game_hooks_t hooks = {0};

void game_hooks_register(const game_hooks_t *new_hooks)
{
    if (new_hooks == NULL) return;
    hooks = *new_hooks;
}

void game_hooks_reset(void)
{
    memset(&hooks, 0, sizeof(hooks));
}

const game_hooks_t *game_hooks_get(void)
{
    return &hooks;
}
