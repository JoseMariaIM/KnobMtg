/* damage_log.c: ring ordering (index 0 = newest), the
 * damage_log_remove_last_for used by elimination undo, and the ring's
 * DAMAGE_LOG_MAX capacity wrap-around. */
#include "test_harness.h"
#include "damage_log.h"
#include <stdio.h>
#include <assert.h>

int main(void)
{
    int player, delta, source;
    uint8_t event_type;

    test_harness_init();
    test_harness_reset_4p();
    assert(damage_log_test_count() == 0);

    /* ---- ordering: index 0 is always the most recent entry ---- */
    apply_life_delta(0, -3);
    apply_life_delta(1, -5);
    apply_life_delta(2, -7);
    assert(damage_log_test_count() == 3);
    assert(damage_log_test_peek(0, &player, &delta, &event_type, &source));
    assert(player == 2 && delta == -7);
    assert(damage_log_test_peek(1, &player, &delta, &event_type, &source));
    assert(player == 1 && delta == -5);
    assert(damage_log_test_peek(2, &player, &delta, &event_type, &source));
    assert(player == 0 && delta == -3);
    printf("PASS: newest entry is always at index 0\n");

    /* ---- a zero delta is not logged (damage_log_add's early return) ---- */
    apply_life_delta(0, 0);
    assert(damage_log_test_count() == 3);
    printf("PASS: a zero-delta life change is not logged\n");

    /* ---- damage_log_remove_last_for only removes the newest matching
       entry, not an older one for the same player/type ---- */
    test_harness_reset_4p();
    apply_life_delta(0, -1); /* older LOG_EVT_LIFE entry for player 0 */
    apply_life_delta(0, -2); /* newer LOG_EVT_LIFE entry for player 0 */
    damage_log_remove_last_for(0, LOG_EVT_LIFE);
    assert(damage_log_test_count() == 1);
    assert(damage_log_test_peek(0, &player, &delta, &event_type, &source));
    assert(player == 0 && delta == -1); /* the older one survives */
    printf("PASS: remove_last_for removes only the newest matching entry\n");

    /* ---- capacity wrap: pushing past DAMAGE_LOG_MAX entries evicts the
       oldest, count never exceeds the cap, and ordering stays intact ----
       auto-elimination is switched off here so every one of the
       DAMAGE_LOG_MAX+extra pushes is a plain, always-logged -1 instead
       of tripping the elimination guard partway through (life clamps
       at LIFE_MIN long before this loop gets anywhere close to it). */
    test_harness_reset_4p();
    nvs_set_auto_eliminate(0);
    {
        int i;
        int extra = 20;
        for (i = 0; i < DAMAGE_LOG_MAX + extra; i++) {
            apply_life_delta(0, -1);
        }
        assert(damage_log_test_count() == DAMAGE_LOG_MAX);
        /* The most recent push is still index 0. */
        assert(damage_log_test_peek(0, &player, &delta, &event_type, &source));
        assert(player == 0 && delta == -1);
        /* The oldest surviving entry (index MAX-1) is still readable
           and the ring didn't silently grow past its cap. */
        assert(damage_log_test_peek(DAMAGE_LOG_MAX - 1, &player, &delta, &event_type, &source));
        assert(!damage_log_test_peek(DAMAGE_LOG_MAX, &player, &delta, &event_type, &source));
    }
    printf("PASS: the ring caps at DAMAGE_LOG_MAX and keeps evicting the oldest entry\n");

    /* ---- damage_log_reset empties the ring ---- */
    damage_log_reset();
    assert(damage_log_test_count() == 0);
    assert(!damage_log_test_peek(0, &player, &delta, &event_type, &source));
    printf("PASS: damage_log_reset empties the ring\n");

    printf("\nAll damage_log tests passed.\n");
    return 0;
}
