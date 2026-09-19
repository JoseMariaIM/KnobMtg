#include "ui_list.h"
#include <string.h>

/* See ui_list.h for what this is and why the rows are a fixed width. */

#define UI_LIST_ROW_W     260
#define UI_LIST_ROW_H      52
#define UI_LIST_ROW_GAP     8
#define UI_LIST_VIEW_Y     58
/* Ends at y=296, not at the glass. A row whose bottom edge sits where
   the circle has narrowed to meet it gets visibly sliced by the bezel;
   stopping the viewport short leaves the partial row that says "there
   is more below" reading as a partial row rather than as damage. */
#define UI_LIST_VIEW_H    238
#define UI_LIST_PAD_X      16

#define UI_LIST_BG_ROW      0x1A1A2E
#define UI_LIST_BG_SEL      0x2A3050
#define UI_LIST_EDGE_SEL    0xE0E4EF
#define UI_LIST_TEXT_NAME   0xFFFFFF
#define UI_LIST_TEXT_VALUE  0x8A90A6
#define UI_LIST_TEXT_TITLE  0x6E7486

void ui_list_split_label(const char *src, char *name, size_t name_len,
                         char *value, size_t value_len)
{
    char flat[UI_LIST_TEXT_LEN * 2];
    size_t i = 0, o = 0;
    char *split;

    name[0] = '\0';
    value[0] = '\0';
    if (src == NULL) return;

    /* Flatten. A newline right after a hyphen is a wrap inside one word
       ("Multi-\nSelect"), so it closes up rather than becoming a
       space. */
    while (src[i] != '\0' && o + 1 < sizeof(flat)) {
        if (src[i] == '\n') {
            if (o > 0 && flat[o - 1] != '-') flat[o++] = ' ';
        } else {
            flat[o++] = src[i];
        }
        i++;
    }
    flat[o] = '\0';

    /* Everything these labels encode is "<name> <value>" once flat, so
       the last space is the seam: "Auto-dim 30s", "Menus Face Player",
       "Multi-Select ON". A label with no space is all name. */
    split = strrchr(flat, ' ');
    if (split == NULL) {
        snprintf(name, name_len, "%s", flat);
        return;
    }
    *split = '\0';
    snprintf(name, name_len, "%s", flat);
    snprintf(value, value_len, "%s", split + 1);
}

static void ui_list_paint_row(ui_list_t *list, int i)
{
    bool sel = (i == list->selected);

    if (list->rows[i] == NULL) return;
    lv_obj_set_style_bg_color(list->rows[i],
        lv_color_hex(sel ? UI_LIST_BG_SEL : UI_LIST_BG_ROW), 0);
    lv_obj_set_style_border_width(list->rows[i], sel ? 2 : 0, 0);
    lv_obj_set_style_border_color(list->rows[i], lv_color_hex(UI_LIST_EDGE_SEL), 0);
}

void ui_list_refresh(ui_list_t *list)
{
    int i;

    if (list->model == NULL) return;

    /* "3/14" beside the title. Paging at least told you there was a
       page 4; a list that just scrolls does not, so say it. */
    if (list->title_lbl != NULL) {
        char title_buf[UI_LIST_TEXT_LEN + 12];
        snprintf(title_buf, sizeof(title_buf), "%s   %d/%d",
                 t(list->title), list->selected + 1, list->model->count);
        lv_label_set_text(list->title_lbl, title_buf);
    }

    for (i = 0; i < list->model->count && i < UI_LIST_MAX_ROWS; i++) {
        char name[UI_LIST_TEXT_LEN];
        char value[UI_LIST_TEXT_LEN];

        name[0] = '\0';
        value[0] = '\0';
        list->model->text(i, name, sizeof(name), value, sizeof(value));

        if (list->name_lbl[i] != NULL) lv_label_set_text(list->name_lbl[i], name);
        if (list->value_lbl[i] != NULL) {
            /* A row with no value navigates somewhere; say so with a
               chevron rather than leaving the right-hand side blank. */
            lv_label_set_text(list->value_lbl[i], value[0] != '\0' ? value : ">");
        }
        ui_list_paint_row(list, i);
    }
}

static void event_ui_list_row(lv_event_t *e)
{
    ui_list_t *list = lv_event_get_user_data(e);
    lv_obj_t *row = lv_event_get_target(e);
    int i;

    if (list == NULL || list->model == NULL) return;

    for (i = 0; i < list->model->count && i < UI_LIST_MAX_ROWS; i++) {
        if (list->rows[i] != row) continue;
        /* Tapping also moves the cursor there, so the knob carries on
           from where the finger left off instead of from wherever it
           happened to be. */
        list->selected = i;
        if (list->model->activate != NULL) list->model->activate(i);
        ui_list_refresh(list);
        return;
    }
}

void ui_list_build(ui_list_t *list)
{
    lv_obj_t *title;
    int count = (list->model->count < UI_LIST_MAX_ROWS)
                    ? list->model->count : UI_LIST_MAX_ROWS;
    int i;

    *list->screen = lv_obj_create(NULL);
    lv_obj_set_size(*list->screen, 360, 360);
    lv_obj_set_style_bg_color(*list->screen, lv_color_black(), 0);
    lv_obj_set_style_border_width(*list->screen, 0, 0);
    lv_obj_set_style_pad_all(*list->screen, 0, 0);
    lv_obj_set_scrollbar_mode(*list->screen, LV_SCROLLBAR_MODE_OFF);

    title = lv_label_create(*list->screen);
    lv_label_set_text(title, t(list->title));
    list->title_lbl = title;
    lv_obj_set_style_text_color(title, lv_color_hex(UI_LIST_TEXT_TITLE), 0);
    lv_obj_set_style_text_font(title, &lv_font_es_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 26);

    list->cont = lv_obj_create(*list->screen);
    lv_obj_remove_style_all(list->cont);
    lv_obj_set_size(list->cont, 360, UI_LIST_VIEW_H);
    lv_obj_set_pos(list->cont, 0, UI_LIST_VIEW_Y);
    lv_obj_set_scrollbar_mode(list->cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(list->cont, LV_DIR_VER);
    lv_obj_set_flex_flow(list->cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list->cont, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list->cont, UI_LIST_ROW_GAP, 0);

    for (i = 0; i < count; i++) {
        lv_obj_t *row = lv_obj_create(list->cont);
        lv_obj_t *lbl;

        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, UI_LIST_ROW_W, UI_LIST_ROW_H);
        lv_obj_set_style_radius(row, 12, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, event_ui_list_row, LV_EVENT_CLICKED, list);
        list->rows[i] = row;

        lbl = lv_label_create(row);
        lv_obj_set_style_text_color(lbl, lv_color_hex(UI_LIST_TEXT_NAME), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_es_16, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        /* Leave room for the value on the right so a long name
           ellipsises instead of running underneath it. */
        lv_obj_set_width(lbl, UI_LIST_ROW_W - UI_LIST_PAD_X * 2 - 74);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, UI_LIST_PAD_X, 0);
        list->name_lbl[i] = lbl;

        lbl = lv_label_create(row);
        lv_obj_set_style_text_color(lbl, lv_color_hex(UI_LIST_TEXT_VALUE), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_es_16, 0);
        lv_obj_align(lbl, LV_ALIGN_RIGHT_MID, -UI_LIST_PAD_X, 0);
        list->value_lbl[i] = lbl;
    }

    list->selected = 0;
    ui_list_refresh(list);
}

void ui_list_free(ui_list_t *list)
{
    int i;

    /* A list that was never opened has no screen pointer at all yet -
       eviction runs over all of them, including those. */
    if (list->screen == NULL || *list->screen == NULL) return;
    if (*list->screen == lv_scr_act()) return;

    lv_obj_del(*list->screen);
    *list->screen = NULL;
    list->cont = NULL;
    list->title_lbl = NULL;
    for (i = 0; i < UI_LIST_MAX_ROWS; i++) {
        list->rows[i] = NULL;
        list->name_lbl[i] = NULL;
        list->value_lbl[i] = NULL;
    }
}

void ui_list_open(ui_list_t *list)
{
    if (*list->screen == NULL) ui_list_build(list);
    list->selected = 0;
    ui_list_refresh(list);
    if (list->rows[0] != NULL) lv_obj_scroll_to_view(list->rows[0], LV_ANIM_OFF);
    load_screen_if_needed(*list->screen);
}

bool ui_list_focus(ui_list_t *list, int index)
{
    if (list->model == NULL) return false;
    if (index < 0 || index >= list->model->count) return false;
    if (index >= UI_LIST_MAX_ROWS) return false;

    list->selected = index;
    ui_list_refresh(list);
    if (list->rows[index] != NULL) {
        lv_obj_scroll_to_view(list->rows[index], LV_ANIM_OFF);
    }
    return true;
}

void ui_list_knob(ui_list_t *list, int dir)
{
    int count;

    if (list->model == NULL) return;
    count = (list->model->count < UI_LIST_MAX_ROWS)
                ? list->model->count : UI_LIST_MAX_ROWS;
    if (count <= 0) return;

    list->selected = (list->selected + dir + count) % count;
    ui_list_refresh(list);
    if (list->rows[list->selected] != NULL) {
        lv_obj_scroll_to_view(list->rows[list->selected], LV_ANIM_ON);
    }
}
