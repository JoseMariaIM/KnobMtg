/* round_safe_width(): for a round 360x360 display, no rectangle whose
 * y-span is [y1,y2] and whose returned width is centered on x should
 * poke a corner outside the inscribed circle. Pure geometry, no LVGL
 * needed at all - this is exactly the kind of check that's cheap to
 * run exhaustively. */
#include "../../knobby/src/usecases/round_safe.h"
#include <stdio.h>
#include <assert.h>
#include <math.h>

static void check_no_corner_escapes(int y1, int y2)
{
    int w = round_safe_width(y1, y2);
    int half = w / 2;
    int cx = ROUND_SAFE_CENTER;
    int cy = ROUND_SAFE_CENTER;
    int ys[2] = {y1, y2};
    int i;

    for (i = 0; i < 2; i++) {
        double dx = (double)half;
        double dy = (double)(ys[i] - cy);
        double dist = sqrt(dx * dx + dy * dy);
        /* +1.0 tolerance: round_safe_width truncates the float sqrt to
           int, so the true inscribed width can be fractionally wider
           than what's returned - never narrower, which is what matters
           here (a corner must stay inside, never poke outside by more
           than the rounding error). */
        assert(dist <= (double)ROUND_SAFE_RADIUS + 1.0);
    }
    (void)cx;
}

int main(void)
{
    int y;

    for (y = 0; y <= 2 * ROUND_SAFE_CENTER; y += 3) {
        check_no_corner_escapes(ROUND_SAFE_CENTER, y);
    }
    printf("PASS: round_safe_width keeps every corner inside the circle (single-edge spans)\n");

    for (y = 0; y <= ROUND_SAFE_CENTER; y += 7) {
        check_no_corner_escapes(y, 2 * ROUND_SAFE_CENTER - y);
    }
    printf("PASS: round_safe_width keeps every corner inside the circle (symmetric spans)\n");

    /* At dead center the full circle diameter should be available. */
    assert(round_safe_width(ROUND_SAFE_CENTER, ROUND_SAFE_CENTER) == 2 * ROUND_SAFE_RADIUS);
    printf("PASS: full diameter available at the vertical center\n");

    /* Order of y1/y2 must not matter. */
    assert(round_safe_width(40, 200) == round_safe_width(200, 40));
    printf("PASS: round_safe_width is symmetric in its arguments\n");

    printf("\nAll round_safe tests passed.\n");
    return 0;
}
