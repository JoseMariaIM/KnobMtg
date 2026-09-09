#include "ui_mp.h"
#include "ui_player_menu.h"
#include "ui_1p.h"
#include "game.h"
#include "storage.h"
#include "hw.h"
#include "lang.h"
#include "attack.h"

extern void reset_all_values(void);

static lv_obj_t *add_low_battery_icon(lv_obj_t *parent)
{
    lv_obj_t *batt = lv_label_create(parent);
    lv_label_set_text(batt, LV_SYMBOL_BATTERY_EMPTY);
    lv_obj_set_style_text_color(batt, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_text_font(batt, &lv_font_es_22, 0);
    lv_obj_align(batt, LV_ALIGN_TOP_MID, 0, 28);
    battery_icon_register(batt);
    return batt;
}

static lv_obj_t *mp_battery_icon = NULL;

#include <string.h>

// ---------- screens ----------
lv_obj_t *screen_multiplayer = NULL;

// ---------- layout specs ----------
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

static bool spec_is_wedge(const mp_panel_spec_t *spec)
{
    return spec->wedge_start != spec->wedge_end;
}

/* ---------- wedge geometry, derived once per layout rebuild ----------
   Everything that depends on the slice angles (label anchors, text
   rotation, counter arc, separators) reads this cache, so the spec's
   wedge_start/wedge_end stay the single source of truth and refresh/draw
   paths do no trigonometry. */
#define WEDGE_CX 180
#define WEDGE_CY 180
#define WEDGE_LABEL_RADIUS 88

typedef struct {
    int16_t bis_deg;                /* slice bisector angle */
    lv_coord_t label_dx, label_dy;  /* label anchor offset from center */
} wedge_geom_t;

static wedge_geom_t wedge_geom[MULTIPLAYER_COUNT];
static lv_point_t wedge_sep_ends[MULTIPLAYER_COUNT];
static int wedge_sep_count = 0;

/* Round an lv_trigo_sin/cos value (scaled 1<<15) projected to a radius */
static lv_coord_t wedge_polar(int16_t trig, int radius)
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

static void wedge_compute_geometry(const mp_panel_spec_t *panels, int panel_count)
{
    int i;

    wedge_sep_count = 0;
    for (i = 0; i < panel_count && i < MULTIPLAYER_COUNT; i++) {
        const mp_panel_spec_t *spec = &panels[i];
        int16_t bis = wedge_bisector_deg(spec);

        wedge_geom[i].bis_deg = bis;
        wedge_geom[i].label_dx = wedge_polar(lv_trigo_cos(bis), WEDGE_LABEL_RADIUS);
        wedge_geom[i].label_dy = wedge_polar(lv_trigo_sin(bis), WEDGE_LABEL_RADIUS);

        /* One boundary per panel covers every separator exactly once */
        wedge_sep_ends[i].x = WEDGE_CX + wedge_polar(lv_trigo_cos(spec->wedge_start), 180);
        wedge_sep_ends[i].y = WEDGE_CY + wedge_polar(lv_trigo_sin(spec->wedge_start), 180);
        wedge_sep_count++;
    }
}

typedef struct {
    int panel_count;
    const mp_panel_spec_t *panels;
    int16_t (*angle_fn)(int orientation_mode, int panel_index);
    bool switch_font_by_orientation;
} mp_layout_spec_t;

/* ---------- shared widget state ---------- */
static struct {
    lv_obj_t *panels[MULTIPLAYER_COUNT];
    lv_obj_t *life_labels[MULTIPLAYER_COUNT];
    lv_obj_t *name_labels[MULTIPLAYER_COUNT];
    lv_obj_t *counter_rows[MULTIPLAYER_COUNT][COUNTER_TYPE_COUNT];
    lv_obj_t *counter_values[MULTIPLAYER_COUNT][COUNTER_TYPE_COUNT];
    const mp_layout_spec_t *layout;
} mp_state;

static lv_timer_t *select_timeout_timer = NULL;

/* ---------- small helpers ---------- */
static const lv_font_t *get_counter_badge_font(const counter_definition_t *definition)
{
    if (definition != NULL && definition->icon_text != NULL) {
        return &mana_counter_icons_16;
    }

    return &lv_font_es_14;
}

static const char *get_counter_badge_text(const counter_definition_t *definition)
{
    if (definition == NULL) return "?";
    if (definition->icon_text != NULL) return definition->icon_text;
    if (definition->badge_text != NULL) return definition->badge_text;
    return "?";
}

static void apply_object_rotation(lv_obj_t *obj, int16_t angle, int pivot_x, int pivot_y)
{
    if (obj == NULL) return;

    lv_obj_set_style_transform_angle(obj, angle, 0);
    if (angle != 0) {
        lv_obj_update_layout(obj);
        lv_obj_set_style_transform_pivot_x(obj,
            lv_obj_get_width(obj) / 2 + pivot_x, 0);
        lv_obj_set_style_transform_pivot_y(obj,
            lv_obj_get_height(obj) / 2 + pivot_y, 0);
    }
}

static void create_counter_row(lv_obj_t *parent, counter_type_t type,
                               lv_obj_t **row_out, lv_obj_t **value_out, int player_index)
{
    const counter_definition_t *definition = get_counter_definition(type);
    lv_obj_t *row;
    lv_obj_t *glyph;

    row = make_plain_box(parent, 34, 34);
    lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);

    glyph = lv_label_create(row);
    lv_label_set_text(glyph, get_counter_badge_text(definition));
    lv_obj_set_style_text_color(glyph, get_player_text_color(player_index), 0);
    lv_obj_set_style_text_font(glyph,
        (type == COUNTER_TYPE_POISON) ? &mana_poison_icon_bold_16
                                      : get_counter_badge_font(definition), 0);
    lv_obj_align(glyph, LV_ALIGN_TOP_MID, 0, 0);

    *value_out = lv_label_create(row);
    lv_label_set_text(*value_out, "0");
    lv_obj_set_style_text_color(*value_out, get_player_text_color(player_index), 0);
    lv_obj_set_style_text_font(*value_out, &lv_font_es_14, 0);
    lv_obj_align(*value_out, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_align(*value_out, LV_TEXT_ALIGN_CENTER, 0);

    *row_out = row;
}

static void apply_label_rotation(lv_obj_t *life_lbl, lv_obj_t *name_lbl,
                                  int16_t angle, int life_pivot_y, int name_pivot_y)
{
    apply_object_rotation(life_lbl, angle, 0, life_pivot_y);
    apply_object_rotation(name_lbl, angle, 0, name_pivot_y);
}

static void get_counter_equator_anchor(lv_obj_t *panel,
                                       lv_coord_t *anchor_x, lv_coord_t *anchor_y)
{
    lv_obj_t *parent;
    lv_coord_t panel_y;
    lv_coord_t panel_h;
    lv_coord_t parent_h;
    lv_coord_t panel_center_y;
    lv_coord_t target_world_y;
    const lv_coord_t equator_gap = 24;
    const lv_coord_t edge_margin = 24;

    if (anchor_x == NULL || anchor_y == NULL) return;

    *anchor_x = 0;
    *anchor_y = 0;
    if (panel == NULL) return;

    parent = lv_obj_get_parent(panel);
    if (parent == NULL) return;

    panel_y = lv_obj_get_y(panel);
    panel_h = lv_obj_get_height(panel);
    parent_h = lv_obj_get_height(parent);
    panel_center_y = panel_y + (panel_h / 2);

    target_world_y = (parent_h / 2) + ((panel_center_y < (parent_h / 2)) ? -equator_gap : equator_gap);
    if (target_world_y < panel_y + edge_margin) target_world_y = panel_y + edge_margin;
    if (target_world_y > panel_y + panel_h - edge_margin) target_world_y = panel_y + panel_h - edge_margin;

    *anchor_x = 0;
    *anchor_y = target_world_y - panel_center_y;
}

static int16_t get_counter_row_angle(int orientation_mode, const mp_panel_spec_t *spec,
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

static const mp_layout_spec_t *get_layout(int track)
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

/* ---------- per-panel refresh ---------- */
static void refresh_counter_rows(const mp_panel_spec_t *spec, int16_t wedge_bis,
                                 lv_obj_t *panel, lv_obj_t **rows, lv_obj_t **value_labels,
                                 int player_index, lv_color_t text_color,
                                 int16_t panel_angle, int16_t row_angle)
{
    int type;
    int visible_count = 0;
    int visible_types[COUNTER_TYPE_COUNT];
    char buf[8];
    const lv_coord_t step = 30;
    lv_coord_t anchor_x = 0;
    lv_coord_t anchor_y = 0;

    (void)panel_angle;
    if (!spec_is_wedge(spec)) {
        get_counter_equator_anchor(panel, &anchor_x, &anchor_y);
    }

    for (type = 0; type < COUNTER_TYPE_COUNT; type++) {
        if (rows[type] == NULL || value_labels[type] == NULL) continue;

        if (!counter_type_is_enabled((counter_type_t)type) ||
            get_counter_value(player_index, (counter_type_t)type) <= 0) {
            lv_obj_add_flag(rows[type], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        visible_types[visible_count] = type;
        visible_count++;
    }

    for (type = 0; type < visible_count; type++) {
        int value;
        int counter_type = visible_types[type];
        lv_coord_t x_offset = (lv_coord_t)((type * step) - ((visible_count - 1) * step / 2));
        lv_coord_t local_x;
        lv_coord_t local_y;

        if (spec_is_wedge(spec)) {
            /* Badges on an arc near the rim, centered on the wedge
               bisector: constant clearance from both the rim and the
               life/name labels regardless of badge count. */
            const int radius = 152;
            const int step_deg = 12;
            int a = (wedge_bis + ((visible_count - 1) * step_deg / 2)
                     - (type * step_deg) + 360) % 360;
            local_x = wedge_polar(lv_trigo_cos((int16_t)a), radius);
            local_y = wedge_polar(lv_trigo_sin((int16_t)a), radius);
        } else {
            local_x = anchor_x + x_offset;
            local_y = anchor_y;
        }

        value = get_counter_value(player_index, (counter_type_t)counter_type);

        snprintf(buf, sizeof(buf), "%d", value);
        lv_label_set_text(value_labels[counter_type], buf);
        lv_obj_set_style_text_color(value_labels[counter_type], text_color, 0);
        {
            lv_obj_t *glyph = lv_obj_get_child(rows[counter_type], 0);
            if (glyph != NULL) {
                lv_obj_set_style_text_color(glyph, text_color, 0);
            }
        }
        lv_obj_clear_flag(rows[counter_type], LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(rows[counter_type], LV_ALIGN_CENTER, local_x, local_y);
        apply_object_rotation(rows[counter_type], row_angle, 0, 0);
    }
}

static lv_color_t refresh_mp_panel(lv_obj_t *panel, lv_obj_t *life_lbl, lv_obj_t *name_lbl, int i, int color_i)
{
    char buf[8];
    bool selected = is_player_selected(i);
    bool live_preview_here = life_preview_active && selected;
    bool flash_here = all_damage_flash_active && all_damage_flash_player[i];
    bool preview_here = live_preview_here || flash_here;
    lv_color_t bg_color;
    lv_color_t text_color;

    {
        int vib;
        if (selection_count() == 0) vib = LIFE_VIB_MID;
        else vib = selected ? LIFE_VIB_VIV : LIFE_VIB_DIM;
        bg_color = get_effective_player_color(i, color_i, vib);
        text_color = color_is_light(bg_color) ? lv_color_black() : lv_color_white();
    }

    if (player_eliminated[i]) {
        bg_color = lv_color_hex(0x404040);
        text_color = lv_color_hex(0x808080);
    }

    /* Only write the color when it actually changed: every style write
       invalidates the whole panel, which on full-screen wedge panels means
       a full-screen redraw. (bg_opa is set once at build time.) */
    if (panel != NULL &&
        lv_obj_get_style_bg_color(panel, LV_PART_MAIN).full != bg_color.full) {
        lv_obj_set_style_bg_color(panel, bg_color, 0);
    }

    if (life_lbl != NULL) {
        if (preview_here) {
            /* Live preview still adds pending_life_delta on top of the
               unchanged player_life[i]; the flash is read-only feedback
               for a change already committed, so its delta is just for
               display and player_life[i] below is already the result. */
            int shown_delta = live_preview_here ? pending_life_delta : all_damage_flash_delta;
            snprintf(buf, sizeof(buf), "%+d", shown_delta);
            lv_label_set_text(life_lbl, buf);
            {
                lv_color_t preview_c;
                if (nvs_get_color_mode() == COLOR_MODE_PLAYER && !player_has_override[i]) {
                    preview_c = get_player_preview_color(color_i, shown_delta);
                    if (color_is_light(bg_color) && color_is_light(preview_c))
                        preview_c = lv_color_black();
                    else if (!color_is_light(bg_color) && !color_is_light(preview_c))
                        preview_c = lv_color_white();
                } else {
                    preview_c = color_is_light(bg_color) ? lv_color_black() : lv_color_white();
                }
                lv_obj_set_style_text_color(life_lbl, preview_c, 0);
            }
        } else {
            snprintf(buf, sizeof(buf), "%d", player_life[i]);
            lv_label_set_text(life_lbl, buf);
            lv_obj_set_style_text_color(life_lbl, text_color, 0);
        }
    }

    if (name_lbl != NULL) {
        if (preview_here) {
            char total_buf[16];
            int new_total = live_preview_here ? (player_life[i] + pending_life_delta) : player_life[i];
            snprintf(total_buf, sizeof(total_buf), "= %d", new_total);
            lv_label_set_text(name_lbl, total_buf);
        } else {
            lv_label_set_text(name_lbl, player_names[i]);
        }
        lv_obj_set_style_text_color(name_lbl, text_color, 0);
    }

    return text_color;
}

/* ---------- victory screen ---------- */
lv_obj_t *screen_victory = NULL;
static lv_obj_t *label_victory_title = NULL;
static lv_obj_t *label_victory_name = NULL;
static bool victory_shown = false;

static void event_victory_reset(lv_event_t *e)
{
    (void)e;
    victory_shown = false;
    reset_all_values();
    back_to_main();
    lv_indev_wait_release(lv_indev_get_act());
}

void build_victory_screen(void)
{
    screen_victory = lv_obj_create(NULL);
    lv_obj_set_size(screen_victory, 360, 360);
    lv_obj_set_style_border_width(screen_victory, 0, 0);
    lv_obj_set_scrollbar_mode(screen_victory, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(screen_victory, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen_victory, event_victory_reset, LV_EVENT_LONG_PRESSED, NULL);

    label_victory_title = lv_label_create(screen_victory);
    lv_obj_set_style_text_font(label_victory_title, &lv_font_es_22, 0);
    lv_label_set_text(label_victory_title, t(STR_VICTORY_WINNER));
    lv_obj_align(label_victory_title, LV_ALIGN_CENTER, 0, -50);

    label_victory_name = lv_label_create(screen_victory);
    /* Not lv_font_montserrat_bold_44 - that one's subset to digits only
       for life totals and tofu-boxes any letter (player names need the
       full alphabet, so an accented-Latin font like the others). */
    lv_obj_set_style_text_font(label_victory_name, &lv_font_es_32, 0);
    lv_obj_set_style_text_align(label_victory_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label_victory_name, 320);
    lv_obj_align(label_victory_name, LV_ALIGN_CENTER, 0, 10);
}

/* Only one player left standing (life <= 0 or otherwise eliminated
   ends everyone else) - fade from the multiplayer view into a full
   screen of their color with their name front and center. Holding
   anywhere on this screen resets for another game (event_victory_reset
   above), same "hold to reset" convention as the rest of the app. */
static void open_victory_screen(int player_index)
{
    lv_color_t bg = get_player_base_color(player_index);
    lv_color_t text_color = get_player_text_color(player_index);

    lv_obj_set_style_bg_color(screen_victory, bg, 0);
    lv_obj_set_style_bg_opa(screen_victory, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(label_victory_title, text_color, 0);
    lv_obj_set_style_text_color(label_victory_name, text_color, 0);
    lv_label_set_text(label_victory_name, player_names[player_index]);

    victory_shown = true;
    lv_scr_load_anim(screen_victory, LV_SCR_LOAD_ANIM_FADE_IN, 600, 0, false);
}

static void check_for_winner(void)
{
    const mp_layout_spec_t *layout = mp_state.layout;
    int i, alive_count = 0, alive_index = -1;

    if (victory_shown || layout == NULL) return;
    if (nvs_get_players_to_track() <= 1) return;

    for (i = 0; i < layout->panel_count; i++) {
        int p = layout->panels[i].player_index;
        if (!player_eliminated[p]) {
            alive_count++;
            alive_index = p;
        }
    }
    if (alive_count == 1) {
        open_victory_screen(alive_index);
    }
}

bool mp_victory_active(void)
{
    return victory_shown;
}

/* ---------- unified refresh ---------- */
void refresh_multiplayer_ui(void)
{
    const mp_layout_spec_t *layout = mp_state.layout;
    int orientation_mode;
    int i;

    if (layout == NULL) return;
    orientation_mode = nvs_get_orientation();

    for (i = 0; i < layout->panel_count; i++) {
        const mp_panel_spec_t *spec = &layout->panels[i];
        lv_obj_t *panel = mp_state.panels[i];
        lv_obj_t *life_lbl = mp_state.life_labels[i];
        lv_obj_t *name_lbl = mp_state.name_labels[i];
        int16_t angle = layout->angle_fn(orientation_mode, i);
        int16_t counter_angle = get_counter_row_angle(orientation_mode, spec, panel, angle);
        lv_coord_t nx = (orientation_mode != ORIENTATION_MODE_CENTRIC) ? spec->nudge_x : 0;
        lv_coord_t bx = nx;
        lv_coord_t by = 0;
        lv_color_t text_color;

        if (spec_is_wedge(spec)) {
            /* Wedge panels are full-screen: anchor the label stack on the
               wedge bisector instead of the panel center. */
            bx = wedge_geom[i].label_dx;
            by = wedge_geom[i].label_dy;
            if (angle == 1800) {
                /* The 180-degree flip rotates the life/name pair around
                   their shared pivot, moving the name ~30px toward the
                   rim; pull the anchor inward to keep the same clearance
                   from the counter arc as in absolute mode. */
                bx = (lv_coord_t)((bx * 85) / 100);
                by = (lv_coord_t)((by * 85) / 100);
            }
        }

        text_color = refresh_mp_panel(panel, life_lbl, name_lbl,
                                      spec->player_index, spec->color_index);

        if (layout->switch_font_by_orientation) {
            const lv_font_t *life_font;
            lv_coord_t life_pivot_y;

            if (spec_is_wedge(spec)) {
                /* Pie slices have room for the big font in every
                   orientation; drop to the smaller one only when the
                   value is too wide and would reach into the counter
                   arc beside the number (3+ digits). */
                life_font = &lv_font_montserrat_bold_56;
                if (life_lbl != NULL) {
                    lv_point_t ts;
                    lv_txt_get_size(&ts, lv_label_get_text(life_lbl),
                                    life_font, 0, 0, LV_COORD_MAX,
                                    LV_TEXT_FLAG_NONE);
                    if (ts.x > 84) life_font = &lv_font_montserrat_bold_44;
                }
            } else if (orientation_mode == ORIENTATION_MODE_CENTRIC) {
                /* Rect quadrants: rotated bold-56 labels don't fit */
                life_font = &lv_font_montserrat_bold_44;
            } else {
                life_font = &lv_font_montserrat_bold_56;
            }
            life_pivot_y = (life_font == &lv_font_montserrat_bold_56) ? 12 : 10;

            if (life_lbl != NULL) {
                lv_obj_clear_flag(life_lbl, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_style_text_font(life_lbl, life_font, 0);
                lv_obj_align(life_lbl, LV_ALIGN_CENTER, bx, by - life_pivot_y);
            }
            if (name_lbl != NULL) {
                lv_obj_clear_flag(name_lbl, LV_OBJ_FLAG_HIDDEN);
                lv_obj_align(name_lbl, LV_ALIGN_CENTER, bx, by + 30);
            }
            apply_label_rotation(life_lbl, name_lbl, angle, life_pivot_y, -30);
        } else {
            apply_label_rotation(life_lbl, name_lbl, angle, 10, -30);
        }

        refresh_counter_rows(spec, wedge_geom[i].bis_deg, panel,
                             mp_state.counter_rows[i], mp_state.counter_values[i],
                             spec->player_index, text_color, angle, counter_angle);
    }

    check_for_winner();
}

/* ---------- attack drag gesture ----------
   Dragging from one player's wedge onto another's arms an "Attack" flow
   (see attack.c): press-down picks the source, live position draws a
   following arrow, and release over a different, live wedge opens the
   mode/amount screen for that pair. A quick tap without enough movement
   never sets attack_drag_active, so it still reaches the normal
   SHORT_CLICKED/LONG_PRESSED handlers below untouched. Wedge panels
   clear LV_OBJ_FLAG_PRESS_LOCK (see rebuild_multiplayer_layout), so
   LVGL re-targets PRESSED to whichever wedge is currently under the
   finger as it crosses boundaries - exactly what's needed to read off
   the target at release time from the event's own panel index. */
#define ATTACK_DRAG_THRESHOLD_PX 14

static int attack_drag_source = -1;
static int attack_drag_source_color_idx = 0; /* spec->color_index for attack_drag_source */
static bool attack_drag_active = false;
static bool attack_drag_suppress_click = false;
static lv_point_t attack_drag_start;
static lv_point_t attack_drag_current;

/* Trailing points behind the current touch, oldest first, used to draw a
   tapering comet-trail instead of one flat line (see event_attack_drag_draw).
   Recorded roughly every ATTACK_TRAIL_MIN_STEP_PX of movement so a slow
   drag doesn't pack them all into one spot. */
#define ATTACK_TRAIL_MAX 40
#define ATTACK_TRAIL_MIN_STEP_PX 4
static lv_point_t attack_trail[ATTACK_TRAIL_MAX];
static int attack_trail_count = 0;

static void attack_trail_reset(lv_point_t pt)
{
    attack_trail[0] = pt;
    attack_trail_count = 1;
}

static void attack_trail_push(lv_point_t pt)
{
    if (attack_trail_count > 0) {
        lv_point_t *last = &attack_trail[attack_trail_count - 1];
        int ddx = pt.x - last->x;
        int ddy = pt.y - last->y;
        if (ddx * ddx + ddy * ddy < ATTACK_TRAIL_MIN_STEP_PX * ATTACK_TRAIL_MIN_STEP_PX) {
            *last = pt; /* still moving toward the same spot: just update the tip */
            return;
        }
    }
    if (attack_trail_count >= ATTACK_TRAIL_MAX) {
        memmove(&attack_trail[0], &attack_trail[1], sizeof(lv_point_t) * (ATTACK_TRAIL_MAX - 1));
        attack_trail_count = ATTACK_TRAIL_MAX - 1;
    }
    attack_trail[attack_trail_count++] = pt;
}

/* Gestures that start inside this band of the four edges are left alone
   for knob_classify_swipe_direction (menu-open swipe) instead of arming
   the attack drag - matches the swipe classifier's own start-zone gating
   in knob.c/knob.h, so a deliberate edge-grab still works. */
static bool point_in_swipe_edge_zone(lv_coord_t x, lv_coord_t y)
{
    return x <= KNOB_SWIPE_LEFT_EDGE_ZONE || x >= (360 - KNOB_SWIPE_RIGHT_EDGE_ZONE) ||
           y <= KNOB_SWIPE_TOP_EDGE_ZONE || y >= (360 - KNOB_SWIPE_BOTTOM_EDGE_ZONE);
}

/* Used by knob.c's swipe classifier so a drag that crosses an edge zone
   can't also be read as the back/menu swipe gesture. */
bool attack_gesture_in_progress(void)
{
    return attack_drag_active;
}




/* ---------- events ---------- */
static void event_multiplayer_select(lv_event_t *e)
{
    int player = (int)(intptr_t)lv_event_get_user_data(e);

    if (attack_drag_suppress_click) {
        /* This CLICKED is the tail end of a drag gesture that already
           opened (or declined to open) the Attack screen - not a real
           tap on this wedge. */
        attack_drag_suppress_click = false;
        return;
    }
    bool had_pending;
    bool was_selected;

    if (player < 0 || player >= MULTIPLAYER_COUNT) return;
    if (player_eliminated[player]) return;

    /* A tap during the first-player roulette stops the spin and leaves
       nothing selected (the spinning highlight is not a real selection). */
    if (player_selection_animation_active()) {
        stop_player_selection_animation();
        selection_clear();
        refresh_multiplayer_ui();
        return;
    }

    /* Capture state before committing: the commit clears the selection in
       multi-select mode, so we can't read it afterwards. */
    had_pending = life_preview_active;
    was_selected = is_player_selected(player);

    /* Apply any pending delta to the current set before the selection
       changes. */
    if (life_preview_active) {
        life_preview_commit_cb(NULL);
    }

    if (nvs_get_multi_select()) {
        if (had_pending) {
            /* A pending change was just applied (and the set cleared).
               Tapping a player that was part of the set ends the operation
               with nothing selected; tapping a different player starts a
               fresh selection with just that player. */
            if (!was_selected) {
                selection_set_single(player);
            }
        } else {
            /* No pending change: tap toggles this player in/out of the set. */
            selection_toggle(player);
        }
    } else {
        /* Single-select (default): tapping the only selected player deselects;
           tapping any other player switches the selection to it. */
        if (was_selected && selection_count() == 1) {
            selection_clear();
        } else {
            selection_set_single(player);
        }
    }
    select_kick_timer();
    refresh_multiplayer_ui();
}

static void event_multiplayer_open_menu(lv_event_t *e)
{
    int player = (int)(intptr_t)lv_event_get_user_data(e);

    if (player < 0 || player >= MULTIPLAYER_COUNT) return;

    /* A long hold that's already past the drag threshold is an in-flight
       attack gesture, not a request for this wedge's menu. */
    if (attack_drag_active) return;

    if (player_selection_animation_active()) {
        stop_player_selection_animation();
        selection_clear();
    }

    /* Resolve any pending dialed delta before leaving the screen, so the
       auto-commit can't fire later against a context the user left. */
    if (life_preview_active) {
        life_preview_commit_cb(NULL);
    }

    /* A long-press is a deliberate gesture, so it always opens the
       pressed player's menu (e.g. to apply commander damage), even when
       one or more players are selected for life changes; the selection
       is left untouched. */
    if (player_eliminated[player]) {
        menu_player = player;
        load_screen_if_needed(screen_eliminated_player_menu);
        lv_indev_wait_release(lv_indev_get_act());
        return;
    }

    open_player_menu(player);
    lv_indev_wait_release(lv_indev_get_act());
}

/* ---------- selection timeout ---------- */
static void select_timeout_cb(lv_timer_t *timer)
{
    (void)timer;
    /* Apply a still-pending dialed delta rather than silently dropping it.
       (Today the 3s preview always commits before the >=5s timeout, but
       nothing else enforces that ordering.) */
    if (life_preview_active) {
        life_preview_commit_cb(NULL);
    }
    selection_clear();
    if (select_timeout_timer != NULL)
        lv_timer_pause(select_timeout_timer);
    refresh_multiplayer_ui();
}

void select_kick_timer(void)
{
    int idx = nvs_get_deselect_timeout();
    int ms = deselect_ms[idx];

    if (select_timeout_timer == NULL) {
        select_timeout_timer = lv_timer_create(select_timeout_cb, 15000, NULL);
        lv_timer_pause(select_timeout_timer);
    }
    if (selection_count() > 0 && ms > 0) {
        lv_timer_set_period(select_timeout_timer, (uint32_t)ms);
        lv_timer_reset(select_timeout_timer);
        lv_timer_resume(select_timeout_timer);
    } else {
        lv_timer_pause(select_timeout_timer);
    }
}

/* ---------- pie-slice (wedge) panels ----------
   Full-screen panels clipped to an angular wedge with a draw-time angle
   mask (the arc widget's primitive), and hit-tested by angle so taps land
   on the right slice. This avoids rotating containers, whose transform
   layers would not fit in the LVGL heap. */
static lv_draw_mask_angle_param_t wedge_mask_params[MULTIPLAYER_COUNT];
static int16_t wedge_mask_ids[MULTIPLAYER_COUNT];

static bool wedge_contains_angle(const mp_panel_spec_t *spec, int angle)
{
    if (spec->wedge_start <= spec->wedge_end)
        return angle >= spec->wedge_start && angle < spec->wedge_end;
    /* range wraps past 0 degrees */
    return angle >= spec->wedge_start || angle < spec->wedge_end;
}

static void event_wedge_panel(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const mp_panel_spec_t *spec;
    lv_event_code_t code = lv_event_get_code(e);

    if (mp_state.layout == NULL || idx < 0 || idx >= mp_state.layout->panel_count)
        return;
    spec = &mp_state.layout->panels[idx];

    if (code == LV_EVENT_COVER_CHECK) {
        /* The wedge mask means this panel does not fully cover its
           rectangle, so siblings underneath must still be drawn. */
        lv_cover_check_info_t *info = lv_event_get_param(e);
        info->res = LV_COVER_RES_MASKED;
    } else if (code == LV_EVENT_DRAW_MAIN_BEGIN) {
        lv_draw_mask_angle_init(&wedge_mask_params[idx], WEDGE_CX, WEDGE_CY,
                                spec->wedge_start, spec->wedge_end);
        wedge_mask_ids[idx] = lv_draw_mask_add(&wedge_mask_params[idx], NULL);
    } else if (code == LV_EVENT_DRAW_MAIN_END) {
        /* Remove before children draw so labels are not clipped */
        lv_draw_mask_remove_id(wedge_mask_ids[idx]);
    } else if (code == LV_EVENT_HIT_TEST) {
        lv_hit_test_info_t *info = lv_event_get_param(e);
        int dx = info->point->x - WEDGE_CX;
        int dy = info->point->y - WEDGE_CY;
        if (dx == 0 && dy == 0) dx = 1; /* lv_atan2 needs a non-zero vector */
        /* lv_atan2(y, x) matches the mask/arc angle convention
           (0 = 3 o'clock, clockwise) — same call order as lv_arc */
        info->res = wedge_contains_angle(spec, lv_atan2(dy, dx));
    }
}

static void event_wedge_separators(lv_event_t *e)
{
    static const lv_point_t sep_center = {WEDGE_CX, WEDGE_CY};
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    lv_draw_line_dsc_t dsc;
    int s;

    lv_draw_line_dsc_init(&dsc);
    dsc.color = lv_color_black();
    dsc.width = 2;
    for (s = 0; s < wedge_sep_count; s++) {
        lv_draw_line(draw_ctx, &dsc, &sep_center, &wedge_sep_ends[s]);
    }
}

static void event_wedge_drag(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const mp_panel_spec_t *spec;
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev;

    if (mp_state.layout == NULL || idx < 0 || idx >= mp_state.layout->panel_count)
        return;
    spec = &mp_state.layout->panels[idx];

    if (code == LV_EVENT_PRESSED) {
        lv_point_t pt;

        /* Only the very first wedge touched in a gesture arms it - a
           re-search caused by the finger sliding onto a new wedge (see
           the comment above attack_drag_source's declaration) must not
           reset the source to wherever it currently is. */
        if (attack_drag_source >= 0) return;
        if (player_selection_animation_active()) return;
        if (player_eliminated[spec->player_index]) return;
        indev = lv_indev_get_act();
        if (indev == NULL) return;
        lv_indev_get_point(indev, &pt);
        /* Starting in the edge band is a deliberate swipe-to-menu grab -
           leave attack_drag_source unset so this whole gesture is never
           armed, and the swipe classifier gets an unobstructed read at
           release (see point_in_swipe_edge_zone). */
        if (point_in_swipe_edge_zone(pt.x, pt.y)) return;
        attack_drag_source = spec->player_index;
        attack_drag_source_color_idx = spec->color_index;
        attack_drag_active = false;
        attack_drag_start = pt;
        attack_drag_current = pt;
        attack_trail_reset(pt);
    } else if (code == LV_EVENT_PRESSING) {
        int ddx, ddy;

        if (attack_drag_source < 0) return;
        indev = lv_indev_get_act();
        if (indev != NULL) lv_indev_get_point(indev, &attack_drag_current);
        if (!attack_drag_active) {
            ddx = attack_drag_current.x - attack_drag_start.x;
            ddy = attack_drag_current.y - attack_drag_start.y;
            if (ddx * ddx + ddy * ddy >= ATTACK_DRAG_THRESHOLD_PX * ATTACK_DRAG_THRESHOLD_PX) {
                attack_drag_active = true;
            }
        }
        if (attack_drag_active) {
            attack_trail_push(attack_drag_current);
            lv_obj_invalidate(screen_multiplayer);
        }
    } else if (code == LV_EVENT_RELEASED) {
        int target = spec->player_index;

        if (attack_drag_source < 0) return;
        if (attack_drag_active) {
            attack_drag_active = false;
            attack_drag_suppress_click = true;
            if (target != attack_drag_source && !player_eliminated[target] &&
                !player_eliminated[attack_drag_source]) {
                open_attack_screen(attack_drag_source, target);
            }
            lv_obj_invalidate(screen_multiplayer);
        }
        attack_drag_source = -1;
    }
}

/* Live arrow from where the attack drag started to the current touch
   point, drawn on top of everything else (registered on the same
   full-screen overlay as the wedge separators, after them). Also glows
   the wedge currently under the finger, when it differs from the
   source, as a preview of who's about to be targeted. */
/* Finds which panel (other than the source) the point currently sits
   over, for the hover glow below. Works for both real angular wedges
   (3p) and the plain rectangular quadrants 2p/4p actually use. */
static int attack_hover_panel_index(lv_point_t pt, bool layout_is_wedge)
{
    int i;

    if (mp_state.layout == NULL) return -1;
    for (i = 0; i < mp_state.layout->panel_count; i++) {
        const mp_panel_spec_t *spec = &mp_state.layout->panels[i];
        bool hit;

        if (spec->player_index == attack_drag_source) continue;

        if (layout_is_wedge) {
            int hdx = pt.x - WEDGE_CX;
            int hdy = pt.y - WEDGE_CY;
            if (hdx == 0 && hdy == 0) hdx = 1;
            hit = wedge_contains_angle(spec, lv_atan2(hdy, hdx));
        } else {
            hit = pt.x >= spec->x && pt.x < spec->x + spec->w &&
                  pt.y >= spec->y && pt.y < spec->y + spec->h;
        }
        if (hit) return i;
    }
    return -1;
}

/* Live comet-trail from where the attack drag started to the current
   touch point, drawn on top of everything else (registered on the same
   full-screen overlay as the wedge separators, after them): a tapering
   halo, tinted with the SOURCE player's own assigned color (whatever
   they actually see on their panel - custom override, life-color mode,
   or the 2p color swap all included, see get_effective_player_color),
   under a thin white core along the recent path (attack_trail, sampled
   densely enough that the short segments read as one continuous stroke
   rather than separate lines), ending in a small filled "head" at the
   fingertip. Also glows the panel currently under the finger, tinted
   with ITS OWN color, as a preview of who's about to be targeted. */
static void event_attack_drag_draw(lv_event_t *e)
{
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    lv_draw_line_dsc_t halo_dsc, core_dsc;
    lv_draw_rect_dsc_t tip_dsc;
    lv_area_t tip_area;
    lv_color_t src_color;
    lv_point_t tip;
    int denom, i, hover_idx;
    bool layout_is_wedge;

    if (!attack_drag_active || attack_trail_count == 0) return;

    src_color = get_effective_player_color(attack_drag_source, attack_drag_source_color_idx, LIFE_VIB_MID);
    tip = attack_trail[attack_trail_count - 1];
    layout_is_wedge = mp_state.layout != NULL && mp_state.layout->panel_count > 0 &&
                      spec_is_wedge(&mp_state.layout->panels[0]);

    /* Target preview glow, tinted with the hovered player's own color -
       drawn first so the trail/head sit on top of it. */
    hover_idx = attack_hover_panel_index(tip, layout_is_wedge);
    if (hover_idx >= 0 && mp_state.layout != NULL) {
        const mp_panel_spec_t *spec = &mp_state.layout->panels[hover_idx];
        lv_draw_rect_dsc_t glow_dsc;
        lv_area_t glow_area;
        lv_coord_t gx, gy;

        if (layout_is_wedge) {
            gx = WEDGE_CX + wedge_geom[hover_idx].label_dx;
            gy = WEDGE_CY + wedge_geom[hover_idx].label_dy;
        } else {
            gx = spec->x + spec->w / 2;
            gy = spec->y + spec->h / 2;
        }

        lv_draw_rect_dsc_init(&glow_dsc);
        glow_dsc.radius = LV_RADIUS_CIRCLE;
        glow_dsc.bg_color = get_effective_player_color(spec->player_index, spec->color_index, LIFE_VIB_VIV);
        glow_dsc.bg_opa = LV_OPA_50;
        glow_dsc.border_width = 3;
        glow_dsc.border_color = lv_color_white();
        glow_dsc.border_opa = LV_OPA_70;
        glow_area.x1 = gx - 48;
        glow_area.y1 = gy - 48;
        glow_area.x2 = gx + 48;
        glow_area.y2 = gy + 48;
        lv_draw_rect(draw_ctx, &glow_dsc, &glow_area);
    }

    /* Tapering trail: a wide, fixed-color halo under a thin white core,
       both growing from faint/thin at the tail to solid/thick at the
       tip - reads as a glowing comet rather than a flat ruler line.
       Points are recorded every ATTACK_TRAIL_MIN_STEP_PX (see
       attack_trail_push), dense enough that consecutive round-capped
       segments overlap into what reads as one continuous stroke instead
       of a few visible straight pieces. */
    lv_draw_line_dsc_init(&halo_dsc);
    halo_dsc.color = src_color;
    halo_dsc.round_start = 1;
    halo_dsc.round_end = 1;
    lv_draw_line_dsc_init(&core_dsc);
    core_dsc.color = lv_color_white();
    core_dsc.round_start = 1;
    core_dsc.round_end = 1;

    denom = (attack_trail_count > 2) ? (attack_trail_count - 2) : 1;
    for (i = 0; i < attack_trail_count - 1; i++) {
        halo_dsc.width = (lv_coord_t)(3 + (i * 11) / denom);
        halo_dsc.opa = (lv_opa_t)(70 + (i * (255 - 70)) / denom);
        lv_draw_line(draw_ctx, &halo_dsc, &attack_trail[i], &attack_trail[i + 1]);

        core_dsc.width = (lv_coord_t)(1 + (i * 3) / denom);
        core_dsc.opa = (lv_opa_t)(90 + (i * (255 - 90)) / denom);
        lv_draw_line(draw_ctx, &core_dsc, &attack_trail[i], &attack_trail[i + 1]);
    }

    /* Comet head: a small filled, ringed dot at the fingertip - stands
       in for a directional arrowhead without needing a filled polygon
       primitive (this LVGL build only draws lines/rects/arcs). */
    lv_draw_rect_dsc_init(&tip_dsc);
    tip_dsc.radius = LV_RADIUS_CIRCLE;
    tip_dsc.bg_color = src_color;
    tip_dsc.bg_opa = LV_OPA_COVER;
    tip_dsc.border_width = 2;
    tip_dsc.border_color = lv_color_white();
    tip_dsc.border_opa = LV_OPA_COVER;
    tip_area.x1 = tip.x - 8;
    tip_area.y1 = tip.y - 8;
    tip_area.x2 = tip.x + 8;
    tip_area.y2 = tip.y + 8;
    lv_draw_rect(draw_ctx, &tip_dsc, &tip_area);
}

/* ---------- layout rebuild ---------- */
void rebuild_multiplayer_layout(int track)
{
    const mp_layout_spec_t *layout = get_layout(track);
    int i;

    if (screen_multiplayer == NULL) return;

    /* Panels are about to be destroyed and rebuilt (lv_obj_clean below) -
       any in-flight drag gesture refers to indices that won't exist. */
    attack_drag_source = -1;
    attack_drag_active = false;
    attack_drag_suppress_click = false;

    victory_shown = false;

    if (mp_battery_icon != NULL) {
        battery_icon_unregister(mp_battery_icon);
        mp_battery_icon = NULL;
    }

    lv_obj_clean(screen_multiplayer);
    memset(&mp_state, 0, sizeof(mp_state));
    mp_state.layout = layout;

    if (layout->panel_count > 0 && spec_is_wedge(&layout->panels[0])) {
        wedge_compute_geometry(layout->panels, layout->panel_count);
    }

    for (i = 0; i < layout->panel_count; i++) {
        const mp_panel_spec_t *spec = &layout->panels[i];
        int p = spec->player_index;
        lv_obj_t *panel;
        lv_obj_t *name_lbl;
        lv_obj_t *life_lbl;

        panel = lv_btn_create(screen_multiplayer);
        lv_obj_remove_style_all(panel);
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
        lv_obj_set_size(panel, spec->w, spec->h);
        lv_obj_set_pos(panel, spec->x, spec->y);
        lv_obj_set_style_radius(panel, 0, 0);
        lv_obj_set_style_shadow_width(panel, 0, 0);
        if (spec_is_wedge(spec)) {
            /* Wedges adjoin; separators are drawn as lines on top */
            lv_obj_set_style_border_width(panel, 0, 0);
            lv_obj_add_flag(panel, LV_OBJ_FLAG_ADV_HITTEST);
            /* Register per event code so the dispatcher filters the many
               events (presses, draw phases) the handler doesn't act on */
            lv_obj_add_event_cb(panel, event_wedge_panel, LV_EVENT_COVER_CHECK, (void *)(intptr_t)i);
            lv_obj_add_event_cb(panel, event_wedge_panel, LV_EVENT_DRAW_MAIN_BEGIN, (void *)(intptr_t)i);
            lv_obj_add_event_cb(panel, event_wedge_panel, LV_EVENT_DRAW_MAIN_END, (void *)(intptr_t)i);
            lv_obj_add_event_cb(panel, event_wedge_panel, LV_EVENT_HIT_TEST, (void *)(intptr_t)i);
        } else {
            lv_obj_set_style_border_width(panel, 1, 0);
            lv_obj_set_style_border_color(panel, lv_color_black(), 0);
        }
        /* Attack-drag gesture tracking: attached regardless of panel shape.
           2p/4p use plain rectangular quadrants (see panels_4p/panels_2p),
           only 3p is an actual angular wedge layout - the drag/target
           logic itself doesn't care which, it only needs each panel's own
           PRESSED/RELEASED and player_index (see event_wedge_drag). */
        lv_obj_add_event_cb(panel, event_wedge_drag, LV_EVENT_PRESSED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(panel, event_wedge_drag, LV_EVENT_PRESSING, (void *)(intptr_t)i);
        lv_obj_add_event_cb(panel, event_wedge_drag, LV_EVENT_RELEASED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(panel, event_multiplayer_select, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)p);
        lv_obj_add_event_cb(panel, event_multiplayer_open_menu, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)p);
        mp_state.panels[i] = panel;

        name_lbl = lv_label_create(panel);
        lv_label_set_text(name_lbl, player_names[p]);
        lv_obj_set_style_text_color(name_lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(name_lbl, &lv_font_es_22, 0);
        lv_obj_align(name_lbl, LV_ALIGN_CENTER, 0, 30);
        mp_state.name_labels[i] = name_lbl;

        life_lbl = lv_label_create(panel);
        lv_label_set_text(life_lbl, "40");
        lv_obj_set_style_text_color(life_lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(life_lbl, &lv_font_montserrat_bold_56, 0);
        lv_obj_align(life_lbl, LV_ALIGN_CENTER, 0, -10);
        mp_state.life_labels[i] = life_lbl;

        create_counter_row(panel, COUNTER_TYPE_COMMANDER_TAX,
            &mp_state.counter_rows[i][COUNTER_TYPE_COMMANDER_TAX],
            &mp_state.counter_values[i][COUNTER_TYPE_COMMANDER_TAX], p);
        create_counter_row(panel, COUNTER_TYPE_PARTNER_TAX,
            &mp_state.counter_rows[i][COUNTER_TYPE_PARTNER_TAX],
            &mp_state.counter_values[i][COUNTER_TYPE_PARTNER_TAX], p);
        create_counter_row(panel, COUNTER_TYPE_POISON,
            &mp_state.counter_rows[i][COUNTER_TYPE_POISON],
            &mp_state.counter_values[i][COUNTER_TYPE_POISON], p);
        create_counter_row(panel, COUNTER_TYPE_EXPERIENCE,
            &mp_state.counter_rows[i][COUNTER_TYPE_EXPERIENCE],
            &mp_state.counter_values[i][COUNTER_TYPE_EXPERIENCE], p);
    }

    if (layout->panel_count > 0) {
        /* Transparent full-screen overlay, drawn last (on top): paints the
           wedge separator lines (LV_USE_LINE is disabled, so drawn
           directly) for pie layouts, and the attack-drag arrow for any
           layout - both are no-ops when there's nothing to show. */
        lv_obj_t *overlay = make_plain_box(screen_multiplayer, 360, 360);
        lv_obj_set_pos(overlay, 0, 0);
        if (spec_is_wedge(&layout->panels[0])) {
            lv_obj_add_event_cb(overlay, event_wedge_separators, LV_EVENT_DRAW_MAIN, NULL);
        }
        lv_obj_add_event_cb(overlay, event_attack_drag_draw, LV_EVENT_DRAW_MAIN, NULL);
    }

    mp_battery_icon = add_low_battery_icon(screen_multiplayer);

    refresh_multiplayer_ui();
}

/* ---------- screen lifecycle ---------- */
void build_multiplayer_screen(void)
{
    screen_multiplayer = lv_obj_create(NULL);
    lv_obj_set_size(screen_multiplayer, 360, 360);
    lv_obj_set_style_bg_color(screen_multiplayer, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_multiplayer, 0, 0);
    lv_obj_set_scrollbar_mode(screen_multiplayer, LV_SCROLLBAR_MODE_OFF);

    rebuild_multiplayer_layout(nvs_get_players_to_track());
}

