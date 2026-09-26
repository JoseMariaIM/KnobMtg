#ifndef _MP_LAYOUT_H
#define _MP_LAYOUT_H

#include "../../types.h"

/* Multiplayer panel geometry: WHICH players go where for each 2p/3p/4p
 * layout x orientation combination (mp_panel_spec_t/mp_layout_spec_t,
 * the panel tables, get_layout()), plus the wedge trig math that turns
 * a 3p pie-slice spec's angles into actual pixel positions
 * (wedge_compute_geometry() and the per-index accessors below). Split
 * out of ui_mp.c, which owns the actual widgets these positions get
 * applied to - this part is self-contained geometry with no widget
 * state of its own, used by ui_mp.c's own refresh/panel-construction
 * code and by mp_attack_gesture.c's hover-target detection. */

typedef struct {
    lv_coord_t x, y, w, h;
    lv_coord_t nudge_x;     /* x offset for life/name labels in non-centric modes */
    int player_index;       /* which player this panel displays */
    int color_index;        /* color slot (differs from player only for 2p) */
    /* Pie-slice panels: full-screen with an angular wedge mask. Angles use
       the LVGL arc convention (0 = 3 o'clock, clockwise, degrees). Both 0
       means a plain rectangular panel. All slice geometry — label anchors,
       text rotation, counter arc, separators — derives from these angles
       at layout-build time. */
    int16_t wedge_start;
    int16_t wedge_end;
} mp_panel_spec_t;

typedef struct {
    int panel_count;
    const mp_panel_spec_t *panels;
    int16_t (*angle_fn)(int orientation_mode, int panel_index);
    bool switch_font_by_orientation;
} mp_layout_spec_t;

#define WEDGE_CX 180
#define WEDGE_CY 180

bool spec_is_wedge(const mp_panel_spec_t *spec);
bool wedge_contains_angle(const mp_panel_spec_t *spec, int angle);

/* Round an lv_trigo_sin/cos value (scaled 1<<15) projected to a radius -
   the one piece of wedge trig math useful outside this file's own
   geometry cache (badge placement in ui_mp.c's refresh_counter_rows). */
lv_coord_t wedge_polar(int16_t trig, int radius);

const mp_layout_spec_t *get_layout(int track);
int16_t get_counter_row_angle(int orientation_mode, const mp_panel_spec_t *spec,
                              lv_obj_t *panel, int16_t panel_angle);

/* Recomputes the per-panel wedge cache (bisector angle, label anchor,
   separator endpoint) for the given panel set - call once per layout
   rebuild (see rebuild_multiplayer_layout() in ui_mp.c), before reading
   any of the accessors below. */
void wedge_compute_geometry(const mp_panel_spec_t *panels, int panel_count);
int16_t wedge_bis_deg(int i);
lv_coord_t wedge_label_dx(int i);
lv_coord_t wedge_label_dy(int i);
int wedge_sep_count(void);
const lv_point_t *wedge_sep_ends(void);

#endif // _MP_LAYOUT_H
