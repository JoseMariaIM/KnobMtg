/* Compile-only proof that game_state.h has zero LVGL dependency: this
 * file is built by the `test-game-state-purity` Makefile target with NO
 * -I to LVGL's headers and no -DLV_CONF_INCLUDE_SIMPLE at all - if
 * game_state.h (or anything it includes: game_types.h, game_hooks.h,
 * net_sync.h) ever pulls in an lv_ type again, this fails to compile.
 * That's the actual deliverable behind the game.c split: a domain
 * header a test can include - and link real logic against, see the
 * encode/decode round-trip below - without lvgl.h anywhere on the
 * include path or a framebuffer/lv_init() in sight. See the comment at
 * the top of game_state.h for the full rationale and what stayed
 * behind in game.h (color math, the 3 lv_timer_t objects) instead. */
#include "../../../knobby/src/entities/game_state.h"
#include <assert.h>
#include <stddef.h>

int main(void)
{
    int source, slot, decoded_source, decoded_slot;

    /* Constants: prove the real game_types.h values, not stand-ins. */
    assert(MAX_DISPLAY_PLAYERS == 4);
    assert(MAX_GAME_PLAYERS == 8);
    assert(LIFE_MIN == -999 && LIFE_MAX == 999);

    /* encode_cmd_source()/decode_cmd_source() are `static inline` in the
       header itself, so this actually exercises real logic - not just
       an #include no-op - with no link step needed at all. */
    for (source = 0; source < MAX_GAME_PLAYERS; source++) {
        for (slot = 0; slot < 2; slot++) {
            int encoded = encode_cmd_source(source, slot);
            decode_cmd_source(encoded, &decoded_source, &decoded_slot);
            assert(decoded_source == source);
            assert(decoded_slot == slot);
        }
    }

    /* Touch a couple more real declarations so a future edit that
       quietly narrows this header's surface (rather than adding an
       LVGL type to it) still gets caught by a compile error here. */
    {
        counter_type_t t = COUNTER_TYPE_POISON;
        game_hooks_t h = {0};
        (void)t;
        (void)h;
    }

    return 0;
}
