#include "mp_layout.h"
#include "storage.h"

/* ---------- wedge geometry, derived once per layout rebuild ----------
   Everything that depends on the slice angles (label anchors, text
   rotation, counter arc, separators) reads this cache, so the spec's
   wedge_start/wedge_end stay the single source of truth and refresh/draw
   paths do no trigonometry. */
#define WEDGE_LABEL_RADIUS 88

typedef struct {
    int16_t bis_deg;                /* slice bisector angle */
    lv_coord_t label_dx, label_dy;  /* label anchor offset from center */
} wedge_geom_t;

static wedge_geom_t wedge_geom[MULTIPLAYER_COUNT];
static lv_point_t wedge_sep_ends_arr[MULTIPLAYER_COUNT];
static int wedge_sep_count_val = 0;

bool spec_is_wedge(const mp_panel_spec_t *spec)
{
    return spec->wedge_start != spec->wedge_end;
}

bool wedge_contains_angle(const mp_panel_spec_t *spec, int angle)
{
    if (spec->wedge_start <= spec->wedge_end)
        return angle >= spec->wedge_start && angle < spec->wedge_end;
    /* range wraps past 0 degrees */
    return angle >= spec->wedge_start || angle < spec->wedge_end;
}

/* Round an lv_trigo_sin/cos value (scaled 1<<15) projected to a radius */
lv_coord_t wedge_polar(int16_t trig, int radius)
{
    int32_t v = (int32_t)trig * radius;
    return (lv_coord_t)((v + (v >= 0 ? 16384 : -16384)) / 32768);
}

static int16_t wedge_bisector_deg(const mp_panel_spec_t *spec)
{
    int delta = (spec->wedge_end >= spec->wedge_start)
              ? spec->wedge_end - spec->wedge_start
              : 360 - spec->wedge_start + spec->wedge_end;
    return (int16_t)((spec->wedge_start + delta / 2) % 360);
}

void wedge_compute_geometry(const mp_panel_spec_t *panels, int panel_count)
{
    int i;

    wedge_sep_count_val = 0;
    for (i = 0; i < panel_count && i < MULTIPLAYER_COUNT; i++) {
        const mp_panel_spec_t *spec = &panels[i];
        int16_t bis = wedge_bisector_deg(spec);

        wedge_geom[i].bis_deg = bis;
        wedge_geom[i].label_dx = wedge_polar(lv_trigo_cos(bis), WEDGE_LABEL_RADIUS);
        wedge_geom[i].label_dy = wedge_polar(lv_trigo_sin(bis), WEDGE_LABEL_RADIUS);

        /* One boundary per panel covers every separator exactly once */
        wedge_sep_ends_arr[i].x = WEDGE_CX + wedge_polar(lv_trigo_cos(spec->wedge_start), 180);
        wedge_sep_ends_arr[i].y = WEDGE_CY + wedge_polar(lv_trigo_sin(spec->wedge_start), 180);
        wedge_sep_count_val++;
    }
}

int16_t wedge_bis_deg(int i)
{
    if (i < 0 || i >= MULTIPLAYER_COUNT) return 0;
    return wedge_geom[i].bis_deg;
}

lv_coord_t wedge_label_dx(int i)
{
    if (i < 0 || i >= MULTIPLAYER_COUNT) return 0;
    return wedge_geom[i].label_dx;
}

lv_coord_t wedge_label_dy(int i)
{
    if (i < 0 || i >= MULTIPLAYER_COUNT) return 0;
    return wedge_geom[i].label_dy;
}

int wedge_sep_count(void)
{
    return wedge_sep_count_val;
}

const lv_point_t *wedge_sep_ends(void)
{
    return wedge_sep_ends_arr;
}

int16_t get_counter_row_angle(int orientation_mode, const mp_panel_spec_t *spec,
                              lv_obj_t *panel, int16_t panel_angle)
{
    /* Wedge panels: counters follow the slice angle in every orientation,
       matching the life/name labels */
    if (spec_is_wedge(spec)) return panel_angle;

    if (orientation_mode == ORIENTATION_MODE_CENTRIC) {
        lv_obj_t *parent;

        if (panel == NULL) return 0;

        parent = lv_obj_get_parent(panel);
        if (parent == NULL) return 0;

        if ((lv_obj_get_y(panel) + (lv_obj_get_height(panel) / 2)) < (lv_obj_get_height(parent) / 2)) {
            return 1800;
        }

        return 0;
    }

    return panel_angle;
}

/* ---------- per-mode angle functions ---------- */
static int16_t get_2p_orientation_angle(int mode, int panel_index)
{
    if (mode == ORIENTATION_MODE_ABSOLUTE) return 0;
    return (panel_index == 0) ? 1800 : 0;
}

static int16_t get_3p_orientation_angle(int mode, int panel_index)
{
    switch (mode) {
        case ORIENTATION_MODE_CENTRIC:
            /* Bisector minus 90 so text reads upright from each seat */
            return (int16_t)(((wedge_geom[panel_index].bis_deg + 270) % 360) * 10);
        case ORIENTATION_MODE_TABLETOP:
            return (panel_index < 2) ? 1800 : 0;
        default:
            return 0;
    }
}

static int16_t get_4p_orientation_angle(int mode, int panel_index)
{
    static const int16_t angled_rot[MULTIPLAYER_COUNT] = {450, 1350, 2250, 3150};

    switch (mode) {
        case ORIENTATION_MODE_CENTRIC:
            return angled_rot[panel_index];
        case ORIENTATION_MODE_TABLETOP:
            return (panel_index == 1 || panel_index == 2) ? 1800 : 0;
        default:
            return 0;
    }
}

/* ---------- per-mode panel specs ---------- */
/* 2p: top = P2 (player 1), bottom = P1 (player 0). Colors intentionally swapped. */
static const mp_panel_spec_t panels_2p[] = {
    {0,   0, 360, 178, 0, 1, 0},
    {0, 182, 360, 178, 0, 0, 1},
};

/* 3p: three equal 120-degree pie slices — top-left = P2, top-right = P3,
   bottom = P1. Bisectors at 210/330/90 degrees. */
static const mp_panel_spec_t panels_3p[] = {
    {0, 0, 360, 360, 0, 1, 1, 150, 270},
    {0, 0, 360, 360, 0, 2, 2, 270,  30},
    {0, 0, 360, 360, 0, 0, 0,  30, 150},
};

/* 4p: quadrants in order P1, P2, P3, P4 */
static const mp_panel_spec_t panels_4p[] = {
    {  0, 180, 180, 180,  10, 0, 0},
    {  0,   0, 180, 180,  10, 1, 1},
    {180,   0, 180, 180, -10, 2, 2},
    {180, 180, 180, 180, -10, 3, 3},
};

static const mp_layout_spec_t layout_2p = {
    .panel_count = 2,
    .panels = panels_2p,
    .angle_fn = get_2p_orientation_angle,
    .switch_font_by_orientation = false,
};

static const mp_layout_spec_t layout_3p = {
    .panel_count = 3,
    .panels = panels_3p,
    .angle_fn = get_3p_orientation_angle,
    .switch_font_by_orientation = true,
};

static const mp_layout_spec_t layout_4p = {
    .panel_count = 4,
    .panels = panels_4p,
    .angle_fn = get_4p_orientation_angle,
    .switch_font_by_orientation = true,
};

const mp_layout_spec_t *get_layout(int track)
{
    if (track == 2) return &layout_2p;
    if (track == 3) return &layout_3p;
    return &layout_4p;
}

/* Snap a player's seat angle to upright-or-flipped (display-rotation step
   0 or 2) so menus can face them via the hardware rotation. Exact for
   tabletop (only 0/180 there); centric's diagonal seats flip like tabletop
   — top-half seats 180, bottom-half upright — since sideways (90/270)
   menus read as weird rather than helpful. 0 in absolute mode,
   single-player, or for a player without a panel. */
int mp_player_seat_rotation(int player)
{
    int track = nvs_get_players_to_track();
    int mode = nvs_get_orientation();
    const mp_layout_spec_t *layout;
    int i;

    if (track < 2 || mode == ORIENTATION_MODE_ABSOLUTE) return 0;
    layout = get_layout(track);
    for (i = 0; i < layout->panel_count; i++) {
        if (layout->panels[i].player_index == player) {
            int16_t angle = layout->angle_fn(mode, i); /* 0.1-degree units */
            return ((angle + 900) / 1800 * 2) & 3;
        }
    }
    return 0;
}
