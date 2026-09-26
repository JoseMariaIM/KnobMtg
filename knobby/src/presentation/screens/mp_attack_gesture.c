#include "mp_attack_gesture.h"
#include "ui_mp_internal.h"
#include "ui_mp.h"
#include "../../usecases/game.h"
#include "attack.h"
#include <string.h>

/* ---------- attack drag gesture ----------
   Dragging from one player's wedge onto another's arms an "Attack" flow
   (see attack.c): press-down picks the source, live position draws a
   following arrow, and release over a different, live wedge opens the
   mode/amount screen for that pair. A quick tap without enough movement
   never sets attack_drag_active, so it still reaches the normal
   SHORT_CLICKED/LONG_PRESSED handlers (event_multiplayer_select/
   event_multiplayer_open_menu, ui_mp.c) untouched. Wedge panels clear
   LV_OBJ_FLAG_PRESS_LOCK (see rebuild_multiplayer_layout in ui_mp.c),
   so LVGL re-targets PRESSED to whichever wedge is currently under the
   finger as it crosses boundaries - exactly what's needed to read off
   the target at release time from the event's own panel index. */
#define ATTACK_DRAG_THRESHOLD_PX 14

static int attack_drag_source = -1;
static int attack_drag_source_panel = -1;    /* layout index of attack_drag_source's panel */
static int attack_drag_source_color_idx = 0; /* spec->color_index for attack_drag_source */
static bool attack_drag_active = false;
static bool attack_drag_suppress_click = false;
static lv_point_t attack_drag_start;
static lv_point_t attack_drag_current;

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

/* True mid-click-suppress-window: event_multiplayer_select (ui_mp.c)
   checks this so the CLICKED that tails a drag gesture isn't read as a
   real tap on the wedge. */
bool mp_attack_drag_suppress_click(void)
{
    if (!attack_drag_suppress_click) return false;
    attack_drag_suppress_click = false;
    return true;
}

/* True while a drag is past the threshold - event_multiplayer_open_menu
   (ui_mp.c) checks this so a long hold mid-drag doesn't also open the
   pressed player's menu. */
bool mp_attack_drag_active(void)
{
    return attack_drag_active;
}

static void event_wedge_drag(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const mp_layout_spec_t *layout = mp_current_layout();
    const mp_panel_spec_t *spec;
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev;

    if (layout == NULL || idx < 0 || idx >= layout->panel_count) return;
    spec = &layout->panels[idx];

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
        attack_drag_source_panel = idx;
        attack_drag_source_color_idx = spec->color_index;
        attack_drag_active = false;
        attack_drag_start = pt;
        attack_drag_current = pt;
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
                open_attack_screen(attack_drag_source, attack_drag_source_color_idx,
                                   target, spec->color_index);
            }
            lv_obj_invalidate(screen_multiplayer);
        }
        attack_drag_source = -1;
        attack_drag_source_panel = -1;
    }
}

/* Finds which panel (other than the source) the point currently sits
   over, for the target reticle below. Works for both real angular
   wedges (3p) and the plain rectangular quadrants 2p/4p actually use. */
static int attack_hover_panel_index(lv_point_t pt, bool layout_is_wedge)
{
    const mp_layout_spec_t *layout = mp_current_layout();
    int i;

    if (layout == NULL) return -1;
    for (i = 0; i < layout->panel_count; i++) {
        const mp_panel_spec_t *spec = &layout->panels[i];
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

/* Finds the on-screen center of a panel, used as both the beam's origin
   (source panel) and the target reticle's center. Wedge layouts (3p)
   anchor on the slice's label position rather than the panel rect,
   which is full-screen for every wedge. */
static void attack_panel_center(int panel_idx, bool layout_is_wedge,
                                lv_coord_t *out_x, lv_coord_t *out_y)
{
    const mp_layout_spec_t *layout = mp_current_layout();

    *out_x = WEDGE_CX;
    *out_y = WEDGE_CY;
    if (layout == NULL || panel_idx < 0 || panel_idx >= layout->panel_count) return;

    if (layout_is_wedge) {
        *out_x = WEDGE_CX + wedge_label_dx(panel_idx);
        *out_y = WEDGE_CY + wedge_label_dy(panel_idx);
    } else {
        const mp_panel_spec_t *spec = &layout->panels[panel_idx];
        *out_x = spec->x + spec->w / 2;
        *out_y = spec->y + spec->h / 2;
    }
}

/* The live attack indicator, drawn on top of everything else (registered
   on the same full-screen overlay as the wedge separators, after them).

   A straight beam from the SOURCE panel's center to the fingertip, in
   that player's own assigned color (custom override, life-color mode
   and the 2p color swap all included - see get_effective_player_color),
   ending in a chevron arrowhead, with a thin reticle ring around
   whichever panel is currently under the finger. Deliberately geometric:
   an earlier version traced the finger's recent path as a tapering
   "comet", which wobbled with every hand tremor and read as a smear
   rather than a deliberate "this player is attacking that one" - a
   fixed source-to-target line says the same thing far more cleanly.

   Every piece is drawn with lines/rects only: this LVGL build has no
   filled-polygon primitive, so the arrowhead is two rotated line
   segments rather than a triangle. */
#define ATTACK_BEAM_HALO_WIDTH   9
#define ATTACK_BEAM_CORE_WIDTH   3
#define ATTACK_ARROW_LENGTH     18   /* px along each chevron barb */
#define ATTACK_ARROW_SPREAD_DEG 148  /* barb angle away from the beam heading */
#define ATTACK_ORIGIN_RADIUS     7
#define ATTACK_RETICLE_RADIUS   52
static void event_attack_drag_draw(lv_event_t *e)
{
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    const mp_layout_spec_t *layout = mp_current_layout();
    lv_draw_line_dsc_t halo_dsc, core_dsc;
    lv_draw_rect_dsc_t ring_dsc;
    lv_area_t area;
    lv_color_t src_color;
    lv_point_t origin, tip;
    lv_coord_t ox, oy;
    int hover_idx, heading;
    bool layout_is_wedge;

    if (!attack_drag_active) return;

    layout_is_wedge = layout != NULL && layout->panel_count > 0 &&
                      spec_is_wedge(&layout->panels[0]);
    src_color = get_effective_player_color(attack_drag_source, attack_drag_source_color_idx, LIFE_VIB_MID);

    attack_panel_center(attack_drag_source_panel, layout_is_wedge, &ox, &oy);
    origin.x = ox;
    origin.y = oy;
    tip = attack_drag_current;

    /* Degenerate beam (finger still on the origin): nothing sensible to
       point at yet, and lv_atan2 needs a non-zero vector. */
    if (tip.x == origin.x && tip.y == origin.y) return;
    heading = lv_atan2(tip.y - origin.y, tip.x - origin.x);

    /* Target reticle first, so the beam and arrowhead sit on top: a thin
       double ring in the hovered player's own color rather than the
       filled translucent disc this used to paint over half their panel. */
    hover_idx = attack_hover_panel_index(tip, layout_is_wedge);
    if (hover_idx >= 0 && layout != NULL) {
        const mp_panel_spec_t *spec = &layout->panels[hover_idx];
        lv_coord_t gx, gy;
        int r;

        attack_panel_center(hover_idx, layout_is_wedge, &gx, &gy);

        lv_draw_rect_dsc_init(&ring_dsc);
        ring_dsc.radius = LV_RADIUS_CIRCLE;
        ring_dsc.bg_opa = LV_OPA_TRANSP;

        /* The reticle sits ON the target's own panel, which is already
           painted in that player's color - so the ring itself is white
           (the one color that reads against every player color) and the
           player's color goes on the outer echo, where it identifies
           who is being targeted without fighting the panel behind it. */
        ring_dsc.border_color = lv_color_white();
        ring_dsc.border_width = 3;
        ring_dsc.border_opa = LV_OPA_COVER;
        r = ATTACK_RETICLE_RADIUS;
        area.x1 = gx - r; area.y1 = gy - r;
        area.x2 = gx + r; area.y2 = gy + r;
        lv_draw_rect(draw_ctx, &ring_dsc, &area);

        ring_dsc.border_color = get_effective_player_color(spec->player_index, spec->color_index, LIFE_VIB_VIV);
        ring_dsc.border_width = 4;
        ring_dsc.border_opa = LV_OPA_80;
        r = ATTACK_RETICLE_RADIUS + 8;
        area.x1 = gx - r; area.y1 = gy - r;
        area.x2 = gx + r; area.y2 = gy + r;
        lv_draw_rect(draw_ctx, &ring_dsc, &area);
    }

    /* Beam: a soft wide halo in the source color under a bright thin
       white core, both round-capped so the ends read as a stroke rather
       than a cut-off rectangle. */
    lv_draw_line_dsc_init(&halo_dsc);
    halo_dsc.color = src_color;
    halo_dsc.width = ATTACK_BEAM_HALO_WIDTH;
    halo_dsc.opa = LV_OPA_60;
    halo_dsc.round_start = 1;
    halo_dsc.round_end = 1;
    lv_draw_line(draw_ctx, &halo_dsc, &origin, &tip);

    lv_draw_line_dsc_init(&core_dsc);
    core_dsc.color = lv_color_white();
    core_dsc.width = ATTACK_BEAM_CORE_WIDTH;
    core_dsc.opa = LV_OPA_COVER;
    core_dsc.round_start = 1;
    core_dsc.round_end = 1;
    lv_draw_line(draw_ctx, &core_dsc, &origin, &tip);

    /* Chevron arrowhead at the tip: two barbs swept back from the beam
       heading. wedge_polar() (mp_layout.h) projects an lv_trigo value
       onto a radius with correct rounding, same helper the wedge label
       anchors use. */
    {
        lv_point_t barb;
        int a;

        a = (heading + ATTACK_ARROW_SPREAD_DEG) % 360;
        barb.x = tip.x + wedge_polar(lv_trigo_cos((int16_t)a), ATTACK_ARROW_LENGTH);
        barb.y = tip.y + wedge_polar(lv_trigo_sin((int16_t)a), ATTACK_ARROW_LENGTH);
        lv_draw_line(draw_ctx, &halo_dsc, &tip, &barb);
        lv_draw_line(draw_ctx, &core_dsc, &tip, &barb);

        a = (heading - ATTACK_ARROW_SPREAD_DEG + 360) % 360;
        barb.x = tip.x + wedge_polar(lv_trigo_cos((int16_t)a), ATTACK_ARROW_LENGTH);
        barb.y = tip.y + wedge_polar(lv_trigo_sin((int16_t)a), ATTACK_ARROW_LENGTH);
        lv_draw_line(draw_ctx, &halo_dsc, &tip, &barb);
        lv_draw_line(draw_ctx, &core_dsc, &tip, &barb);
    }

    /* Origin marker: a hollow ring where the beam leaves the source
       player, so the direction of the attack is unambiguous even when
       the beam is short. */
    lv_draw_rect_dsc_init(&ring_dsc);
    ring_dsc.radius = LV_RADIUS_CIRCLE;
    ring_dsc.bg_opa = LV_OPA_TRANSP;
    ring_dsc.border_width = 3;
    ring_dsc.border_color = src_color;
    ring_dsc.border_opa = LV_OPA_COVER;
    area.x1 = origin.x - ATTACK_ORIGIN_RADIUS;
    area.y1 = origin.y - ATTACK_ORIGIN_RADIUS;
    area.x2 = origin.x + ATTACK_ORIGIN_RADIUS;
    area.y2 = origin.y + ATTACK_ORIGIN_RADIUS;
    lv_draw_rect(draw_ctx, &ring_dsc, &area);
}

void mp_attack_gesture_reset(void)
{
    attack_drag_source = -1;
    attack_drag_source_panel = -1;
    attack_drag_active = false;
    attack_drag_suppress_click = false;
}

void mp_attack_gesture_attach_panel_events(lv_obj_t *panel, int index)
{
    /* Attack-drag gesture tracking: attached regardless of panel shape.
       2p/4p use plain rectangular quadrants, only 3p is an actual
       angular wedge layout - the drag/target logic itself doesn't care
       which, it only needs each panel's own PRESSED/RELEASED and
       player_index (see event_wedge_drag above). */
    lv_obj_add_event_cb(panel, event_wedge_drag, LV_EVENT_PRESSED, (void *)(intptr_t)index);
    lv_obj_add_event_cb(panel, event_wedge_drag, LV_EVENT_PRESSING, (void *)(intptr_t)index);
    lv_obj_add_event_cb(panel, event_wedge_drag, LV_EVENT_RELEASED, (void *)(intptr_t)index);
}

void mp_attack_gesture_attach_overlay_events(lv_obj_t *overlay)
{
    lv_obj_add_event_cb(overlay, event_attack_drag_draw, LV_EVENT_DRAW_MAIN, NULL);
}
