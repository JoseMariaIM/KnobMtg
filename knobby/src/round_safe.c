#include "round_safe.h"

int round_safe_width(int y1, int y2)
{
    int dy1 = y1 - ROUND_SAFE_CENTER;
    int dy2 = y2 - ROUND_SAFE_CENTER;
    if (dy1 < 0) dy1 = -dy1;
    if (dy2 < 0) dy2 = -dy2;
    int dy = (dy1 > dy2) ? dy1 : dy2;
    if (dy > ROUND_SAFE_RADIUS) dy = ROUND_SAFE_RADIUS;
    /* Integer sqrt via float - only runs a handful of times per screen
       open, not a hot path. */
    double h = (double)ROUND_SAFE_RADIUS * (double)ROUND_SAFE_RADIUS - (double)dy * (double)dy;
    return (int)(2.0 * (h > 0 ? __builtin_sqrt(h) : 0));
}
