#include "mp_attack_gesture.h"
#include "ui_mp_internal.h"
#include "ui_mp.h"
#include "game.h"
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
    const mp_layout_spec_t *layout = mp_current_layout();
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
    layout_is_wedge = layout != NULL && layout->panel_count > 0 &&
                      spec_is_wedge(&layout->panels[0]);

    /* Target preview glow, tinted with the hovered player's own color -
       drawn first so the trail/head sit on top of it. */
    hover_idx = attack_hover_panel_index(tip, layout_is_wedge);
    if (hover_idx >= 0 && layout != NULL) {
        const mp_panel_spec_t *spec = &layout->panels[hover_idx];
        lv_draw_rect_dsc_t glow_dsc;
        lv_area_t glow_area;
        lv_coord_t gx, gy;

        if (layout_is_wedge) {
            gx = WEDGE_CX + wedge_label_dx(hover_idx);
            gy = WEDGE_CY + wedge_label_dy(hover_idx);
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

void mp_attack_gesture_reset(void)
{
    attack_drag_source = -1;
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
