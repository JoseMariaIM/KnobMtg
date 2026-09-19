#ifndef _UI_LIST_H
#define _UI_LIST_H

#include "types.h"
#include "lang.h"

/* A vertical, knob-driven list screen.
 *
 * The device's quad grid is right for exactly four things and wrong for
 * fifteen: settings needed five pages of three items plus a "More"
 * tile, so a quarter of every page was spent on paging and you could
 * never see where you were. A list shows neighbours, scrolls
 * continuously under the knob, and has no page furniture at all.
 *
 * Rows are a fixed 260px wide rather than fitted per row. On a round
 * panel a row's safe width changes with its y, and rows here MOVE -
 * fitting each one would mean re-measuring the whole list on every
 * scroll. 260 is what stays inside the glass anywhere in the viewport
 * band (see UI_LIST_VIEW_Y/H and round_safe.h). */

#define UI_LIST_MAX_ROWS 16
#define UI_LIST_TEXT_LEN 40

typedef struct {
    int count;
    /* Fills the row's left-hand name and right-hand value. Leave value
       empty for a row that navigates somewhere instead of holding a
       setting - those get a chevron. */
    void (*text)(int index, char *name, size_t name_len,
                 char *value, size_t value_len);
    void (*activate)(int index);
} ui_list_model_t;

typedef struct {
    /* ---- set before ui_list_build ---- */
    lv_obj_t **screen;
    string_id_t title;
    const ui_list_model_t *model;

    /* ---- owned by ui_list.c ---- */
    lv_obj_t *title_lbl;
    lv_obj_t *cont;
    lv_obj_t *rows[UI_LIST_MAX_ROWS];
    lv_obj_t *name_lbl[UI_LIST_MAX_ROWS];
    lv_obj_t *value_lbl[UI_LIST_MAX_ROWS];
    int selected;
} ui_list_t;

void ui_list_build(ui_list_t *list);
/* Re-reads every row's text and repaints the selection. */
void ui_list_refresh(ui_list_t *list);
/* Lazily builds, resets the selection to the top and shows the screen. */
void ui_list_open(ui_list_t *list);
/* Knob handler: moves the selection, wrapping at both ends - a knob has
   no stops, so neither does this. */
void ui_list_knob(ui_list_t *list, int dir);

/* Deletes the screen and forgets its widgets, so the next open rebuilds
   it. Refuses to free the screen currently on display. */
void ui_list_free(ui_list_t *list);

/* Scrolls to and selects the given row. Returns false if out of range.
   Used by the simulator to screenshot a named setting. */
bool ui_list_focus(ui_list_t *list, int index);

/* Flattens a label that was authored as stacked lines ("Auto-dim\n30s")
   into one line, then splits it at the last space into name and value.
   Exposed because the settings labels are still authored that way for
   the quad menus' benefit, and because it is worth testing directly. */
void ui_list_split_label(const char *src, char *name, size_t name_len,
                         char *value, size_t value_len);

#endif // _UI_LIST_H
