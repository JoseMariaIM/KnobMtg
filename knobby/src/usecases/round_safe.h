#ifndef _ROUND_SAFE_H
#define _ROUND_SAFE_H

#ifdef __cplusplus
extern "C" {
#endif

/* This device's display is a 360x360 canvas but only the inscribed
 * circle (radius 180 around its center) is visible under the round
 * glass. Any rectangular UI element whose bounding box corners fall
 * outside that circle gets clipped by the bezel - see custom_keyboard.c
 * for the row-band version of this same idea. */
#define ROUND_SAFE_CENTER 180
#define ROUND_SAFE_RADIUS 180

/* Widest a rectangle spanning y in [y1,y2] (any order) can be while
 * keeping all four corners inside the visible circle. Use this to size
 * any vertically-scrolling list/container so no row - top, bottom, or
 * anywhere it scrolls to within that fixed viewport - pokes past the
 * glass. */
int round_safe_width(int y1, int y2);

#ifdef __cplusplus
}
#endif

#endif // _ROUND_SAFE_H
