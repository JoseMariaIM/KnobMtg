/* encode_cmd_source()/decode_cmd_source() (game.h): the damage log and
 * elimination-undo bookkeeping both ride the partner-commander slot
 * along in a single "source" int (see the comment above these
 * functions in game.h). A silent off-by-one here would misattribute
 * damage to the wrong commander after an undo - exhaustively round-trip
 * every valid (source, slot) pair. Pure math, no LVGL runtime needed. */
#include "game.h"
#include <stdio.h>
#include <assert.h>

int main(void)
{
    int source, slot;

    for (source = 0; source < MAX_GAME_PLAYERS; source++) {
        for (slot = 0; slot < 2; slot++) {
            int encoded = encode_cmd_source(source, slot);
            int decoded_source, decoded_slot;
            decode_cmd_source(encoded, &decoded_source, &decoded_slot);
            assert(decoded_source == source);
            assert(decoded_slot == slot);
        }
    }
    printf("PASS: encode/decode round-trips every (source, slot) pair\n");

    /* Primary and partner slots for the same source must never collide. */
    for (source = 0; source < MAX_GAME_PLAYERS; source++) {
        assert(encode_cmd_source(source, 0) != encode_cmd_source(source, 1));
    }
    printf("PASS: primary and partner encodings never collide for the same source\n");

    /* Encoded values across the whole valid range must be pairwise unique -
       a collision would mean two different (source, slot) pairs
       decoding to the same commander. */
    {
        int seen[2 * MAX_GAME_PLAYERS];
        int count = 0;
        int i, j;
        for (source = 0; source < MAX_GAME_PLAYERS; source++) {
            for (slot = 0; slot < 2; slot++) {
                seen[count++] = encode_cmd_source(source, slot);
            }
        }
        for (i = 0; i < count; i++) {
            for (j = i + 1; j < count; j++) {
                assert(seen[i] != seen[j]);
            }
        }
    }
    printf("PASS: all %d encodings are pairwise unique\n", 2 * MAX_GAME_PLAYERS);

    printf("\nAll encode_cmd_source tests passed.\n");
    return 0;
}
