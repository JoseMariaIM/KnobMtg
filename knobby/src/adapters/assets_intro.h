#ifndef _ASSETS_INTRO_H
#define _ASSETS_INTRO_H

#include "types.h"

/* Raster crops of the Genex Comics logo (source: GenexTextoBlanco.svg),
   baked onto a solid black background at the exact size/position the boot
   animation uses - see intro.c. Opaque (no alpha) since the intro screen
   is always solid black behind them; that halves the flash footprint
   versus TRUE_COLOR_ALPHA and lets the "fade in" effect just be the
   image's own object opacity ramping up against a background that's
   already the same black. */
extern const lv_img_dsc_t img_genex_0; /* G */
extern const lv_img_dsc_t img_genex_1; /* E */
extern const lv_img_dsc_t img_genex_2; /* N */
extern const lv_img_dsc_t img_genex_3; /* E */
extern const lv_img_dsc_t img_genex_4; /* X */
extern const lv_img_dsc_t img_comics;  /* whole word, pops in as one unit */

#endif // _ASSETS_INTRO_H
