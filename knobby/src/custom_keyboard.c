#include "custom_keyboard.h"
#include "lang.h"
#include "round_safe.h"

/* ---------- geometry ----------
 * Screen is a 360x360 square canvas but only the inscribed circle
 * (radius 180 around its center) is actually visible under the round
 * glass. For a row spanning y in [y1,y2], the widest rectangle that
 * still fits entirely inside the circle has half-width
 * sqrt(R^2 - dy^2), where dy is the *more extreme* of the two edges'
 * distance from the vertical center - using the closer edge would let
 * the farther one poke out past the glass (see round_safe.h). These
 * four bands were picked so the last row (closest to the bottom edge,
 * where the available width shrinks fastest) still has enough room for
 * its controls - it reaches a bit past its old y=312 stop, trading
 * some width for less dead margin below it, but not so far down that
 * the corner buttons below (see CORNER_*_W) would get too narrow to be
 * useful. */
#define KB_CENTER_X 180

/* Face color for the keyboard's "skin": the row containers' own
 * background (so the seams between keys read as this color) and a
 * backdrop disc filling the round glass behind the rows, so the whole
 * keyboard reads as one continuous round face with white keys on it
 * rather than rectangles floating over bare screen. Matches the
 * screen's own black background by design - see custom_keyboard_build()
 * history for the alternative (a lighter dark-gray face) this replaced. */
#define KB_FACE_COLOR 0x000000

typedef struct {
    int y1, y2;
} kb_row_band_t;

static const kb_row_band_t ROW_TOP_BAND     = {130, 178};
static const kb_row_band_t ROW_HOME_BAND    = {178, 226};
static const kb_row_band_t ROW_BOTTOM_BAND  = {226, 274};
static const kb_row_band_t ROW_CONTROL_BAND = {274, 316};

/* Shift/backspace (flanking row_bottom) and mode-switch/Enter (flanking
 * row_control) used to be ordinary cells inside those rows' uniform-
 * width button matrices - stuck at the same skinny width as every
 * letter key even though the circle has more room to spare right next
 * to them (row_bottom's own band is wider than row_control's, so the
 * safe rectangle sized for the *whole* row wastes real width at its
 * corners). Pulling these four out as standalone buttons lets each one
 * claim that leftover width for itself, and stacking a wider one (row_
 * bottom's band) over a narrower one (row_control's band) at the same
 * outer edge reads as a single flared corner tab that tapers inward
 * with the glass, instead of two small squares sitting in front of
 * bare dark space. */
#define CORNER_BOTTOM_W  46
#define CORNER_CONTROL_W 60

static int row_half_width(kb_row_band_t band)
{
    return round_safe_width(band.y1, band.y2) / 2;
}

static void position_row(lv_obj_t *row, kb_row_band_t band)
{
    int half_w = row_half_width(band);
    int w = half_w * 2;
    int h = band.y2 - band.y1;
    lv_obj_set_size(row, w, h);
    lv_obj_align(row, LV_ALIGN_TOP_LEFT, KB_CENTER_X - half_w, band.y1);
}

/* Same as position_row(), but shrunk by `inset` on each side - for the
 * inner button matrix of a row that also has standalone corner buttons
 * claiming the outer `inset` pixels on both sides. */
static void position_row_inset(lv_obj_t *row, kb_row_band_t band, int inset)
{
    int half_w = row_half_width(band) - inset;
    int w;
    if (half_w < 0) half_w = 0;
    w = half_w * 2;
    lv_obj_set_size(row, w, band.y2 - band.y1);
    lv_obj_align(row, LV_ALIGN_TOP_LEFT, KB_CENTER_X - half_w, band.y1);
}

/* Positions a standalone corner button at the outer edge (left or
 * right) of the safe rectangle for `band`, `width` pixels wide. */
static void position_corner(lv_obj_t *btn, kb_row_band_t band, int width, bool left)
{
    int half_w = row_half_width(band);
    int h = band.y2 - band.y1;
    int x = left ? (KB_CENTER_X - half_w) : (KB_CENTER_X + half_w - width);
    lv_obj_set_size(btn, width, h);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, x, band.y1);
}

/* Space bar fills the whole gap between the mode-switch and Enter
 * corner buttons in row_control's band. It used to share that gap with
 * left/right cursor-arrow buttons, but those were both hard to hit
 * (squeezed to a sliver between two much bigger neighbors) and
 * unreliable to tap even when hit - see custom_keyboard_build()
 * history. Cursor movement lives on the knob now (see
 * wifi_text_entry_knob() / name_screen_knob()), which freed this whole
 * band for one big space bar. */
static void position_space(lv_obj_t *space, kb_row_band_t band, int corner_w)
{
    int half_w = row_half_width(band) - corner_w;
    int h = band.y2 - band.y1;
    if (half_w < 0) half_w = 0;
    lv_obj_set_size(space, half_w * 2, h);
    lv_obj_align(space, LV_ALIGN_TOP_LEFT, KB_CENTER_X - half_w, band.y1);
}

/* ---------- character maps ----------
 * Same full set the app's previous lv_keyboard offered (letters,
 * digits, and every punctuation/symbol from its lower/special maps),
 * just regrouped into rows sized to fit the round display. Shift,
 * backspace, the mode-switch key and Enter are NOT in these maps -
 * they're the standalone corner buttons above, since their behavior
 * (and, for shift/mode, their label) doesn't depend on which of these
 * pages is showing. */
static const char *ROW_TOP_LOWER[]    = {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", ""};
static const char *ROW_TOP_UPPER[]    = {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", ""};
static const char *ROW_HOME_LOWER_EN[] = {"a", "s", "d", "f", "g", "h", "j", "k", "l", ""};
static const char *ROW_HOME_LOWER_ES[] = {"a", "s", "d", "f", "g", "h", "j", "k", "l", "\xC3\xB1", ""}; /* n with tilde, UTF-8 */
static const char *ROW_HOME_UPPER_EN[] = {"A", "S", "D", "F", "G", "H", "J", "K", "L", ""};
static const char *ROW_HOME_UPPER_ES[] = {"A", "S", "D", "F", "G", "H", "J", "K", "L", "\xC3\x91", ""}; /* N with tilde */
static const char *ROW_BOTTOM_LOWER[] = {"z", "x", "c", "v", "b", "n", "m", ""};
static const char *ROW_BOTTOM_UPPER[] = {"Z", "X", "C", "V", "B", "N", "M", ""};

static const char *ROW_TOP_NUM[]    = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", ""};
static const char *ROW_HOME_NUM[]   = {"-", "_", ":", ";", "(", ")", "$", "&", "@", "\"", ""};
static const char *ROW_BOTTOM_NUM[] = {".", ",", "'", "+", "*", "=", "%", "!", "?", ""};

static const char *ROW_TOP_SYM[]    = {"#", "<", ">", "\\", ""};
static const char *ROW_HOME_SYM[]   = {"{", "}", "[", "]", ""};
static const char *ROW_BOTTOM_SYM[] = {""}; /* nothing left once backspace is standalone; row_bottom hides itself in this mode */

/* Width units (low 4 bits of a btnmatrix ctrl entry, see
   lv_btnmatrix_set_btn_width). lv_keyboard_update_ctrl_map()
   unconditionally memcpy's btn_cnt entries out of this array with no
   NULL check, so every row needs a real ctrl map (all-1s = plain,
   uniform-width buttons) even when there's nothing special to flag -
   passing NULL segfaults. */
static const lv_btnmatrix_ctrl_t DEFAULT_CTRL[16] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

typedef enum {
    KB_MODE_LOWER = 0,
    KB_MODE_UPPER,
    KB_MODE_NUM,
    KB_MODE_SYM,
} kb_mode_t;

/* lv_keyboard_set_map() writes into a *global* table keyed only by
 * mode enum (see lv_keyboard.c's kb_map[]/kb_ctrl[] statics) - it is
 * not per-widget. Each row is permanently pinned to its own USER_n
 * slot purely so the rows of one keyboard don't overwrite each other;
 * see the header comment for why two different screens' custom
 * keyboards still cooperate safely despite sharing these same slots. */
#define ROW_TOP_SLOT     LV_KEYBOARD_MODE_USER_1
#define ROW_HOME_SLOT    LV_KEYBOARD_MODE_USER_2
#define ROW_BOTTOM_SLOT  LV_KEYBOARD_MODE_USER_3

static void update_mode_label(custom_keyboard_t *kb)
{
    lv_obj_t *lbl = lv_obj_get_child(kb->btn_mode, 0);
    const char *txt;
    if (lbl == NULL) return;
    switch ((kb_mode_t)kb->mode) {
        case KB_MODE_NUM: txt = "#+="; break;
        case KB_MODE_SYM: txt = "ABC"; break;
        default:          txt = "123"; break;
    }
    lv_label_set_text(lbl, txt);
}

static void apply_mode(custom_keyboard_t *kb, kb_mode_t mode)
{
    bool es = (lang_get() == LANG_ES);
    bool is_letters = (mode == KB_MODE_LOWER || mode == KB_MODE_UPPER);
    kb->mode = (int)mode;

    switch (mode) {
        case KB_MODE_UPPER:
            lv_keyboard_set_map(kb->row_top, ROW_TOP_SLOT, (const char **)ROW_TOP_UPPER, DEFAULT_CTRL);
            lv_keyboard_set_map(kb->row_home, ROW_HOME_SLOT,
                                 (const char **)(es ? ROW_HOME_UPPER_ES : ROW_HOME_UPPER_EN), DEFAULT_CTRL);
            lv_keyboard_set_map(kb->row_bottom, ROW_BOTTOM_SLOT, (const char **)ROW_BOTTOM_UPPER, DEFAULT_CTRL);
            break;
        case KB_MODE_NUM:
            lv_keyboard_set_map(kb->row_top, ROW_TOP_SLOT, (const char **)ROW_TOP_NUM, DEFAULT_CTRL);
            lv_keyboard_set_map(kb->row_home, ROW_HOME_SLOT, (const char **)ROW_HOME_NUM, DEFAULT_CTRL);
            lv_keyboard_set_map(kb->row_bottom, ROW_BOTTOM_SLOT, (const char **)ROW_BOTTOM_NUM, DEFAULT_CTRL);
            break;
        case KB_MODE_SYM:
            lv_keyboard_set_map(kb->row_top, ROW_TOP_SLOT, (const char **)ROW_TOP_SYM, DEFAULT_CTRL);
            lv_keyboard_set_map(kb->row_home, ROW_HOME_SLOT, (const char **)ROW_HOME_SYM, DEFAULT_CTRL);
            lv_keyboard_set_map(kb->row_bottom, ROW_BOTTOM_SLOT, (const char **)ROW_BOTTOM_SYM, DEFAULT_CTRL);
            break;
        case KB_MODE_LOWER:
        default:
            lv_keyboard_set_map(kb->row_top, ROW_TOP_SLOT, (const char **)ROW_TOP_LOWER, DEFAULT_CTRL);
            lv_keyboard_set_map(kb->row_home, ROW_HOME_SLOT,
                                 (const char **)(es ? ROW_HOME_LOWER_ES : ROW_HOME_LOWER_EN), DEFAULT_CTRL);
            lv_keyboard_set_map(kb->row_bottom, ROW_BOTTOM_SLOT, (const char **)ROW_BOTTOM_LOWER, DEFAULT_CTRL);
            break;
    }

    /* Shift only means something in the letter pages; row_bottom's
       matrix is empty in SYM (everything else moved to standalone
       buttons), so hide it there instead of showing a blank strip. */
    if (is_letters) lv_obj_clear_flag(kb->btn_shift, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(kb->btn_shift, LV_OBJ_FLAG_HIDDEN);
    if (mode == KB_MODE_SYM) lv_obj_add_flag(kb->row_bottom, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(kb->row_bottom, LV_OBJ_FLAG_HIDDEN);

    update_mode_label(kb);
}

/* ---------- input handling ----------
 * The default lv_keyboard click handler (auto-attached by
 * lv_keyboard_create()) recognizes a fixed set of strings ("ABC",
 * "1#", the symbol icons...) and treats everything else as literal
 * text to insert - so it would type stray labels into the field if
 * left in charge here. Each row gets its own fully custom handler
 * instead (the default one is removed right after creation) so every
 * button's behavior is explicit and unambiguous. */
static void row_top_home_event_cb(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_target(e);
    uint16_t id = lv_btnmatrix_get_selected_btn(row);
    lv_obj_t *ta = lv_keyboard_get_textarea(row);
    const char *txt;
    if (id == LV_BTNMATRIX_BTN_NONE || ta == NULL) return;
    txt = lv_btnmatrix_get_btn_text(row, id);
    if (txt == NULL) return;
    lv_textarea_add_text(ta, txt);
}

static void row_bottom_event_cb(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_target(e);
    uint16_t id = lv_btnmatrix_get_selected_btn(row);
    lv_obj_t *ta = lv_keyboard_get_textarea(row);
    const char *txt;
    if (id == LV_BTNMATRIX_BTN_NONE || ta == NULL) return;
    txt = lv_btnmatrix_get_btn_text(row, id);
    if (txt == NULL) return;
    lv_textarea_add_text(ta, txt);
}

static void btn_shift_event_cb(lv_event_t *e)
{
    custom_keyboard_t *kb = (custom_keyboard_t *)lv_event_get_user_data(e);
    apply_mode(kb, kb->mode == KB_MODE_LOWER ? KB_MODE_UPPER : KB_MODE_LOWER);
}

static void btn_backspace_event_cb(lv_event_t *e)
{
    custom_keyboard_t *kb = (custom_keyboard_t *)lv_event_get_user_data(e);
    if (kb->ta != NULL) lv_textarea_del_char(kb->ta);
}

static void btn_space_event_cb(lv_event_t *e)
{
    custom_keyboard_t *kb = (custom_keyboard_t *)lv_event_get_user_data(e);
    if (kb->ta != NULL) lv_textarea_add_char(kb->ta, ' ');
}

static void btn_mode_event_cb(lv_event_t *e)
{
    custom_keyboard_t *kb = (custom_keyboard_t *)lv_event_get_user_data(e);
    switch ((kb_mode_t)kb->mode) {
        case KB_MODE_NUM: apply_mode(kb, KB_MODE_SYM); break;
        case KB_MODE_SYM: apply_mode(kb, KB_MODE_LOWER); break;
        default:          apply_mode(kb, KB_MODE_NUM); break;
    }
}

static void btn_enter_event_cb(lv_event_t *e)
{
    custom_keyboard_t *kb = (custom_keyboard_t *)lv_event_get_user_data(e);
    if (kb->ta != NULL) lv_event_send(kb->ta, LV_EVENT_READY, NULL);
    lv_event_send(kb->btn_enter, LV_EVENT_READY, NULL);
}

static lv_obj_t *make_row(lv_obj_t *parent, lv_event_cb_t cb, custom_keyboard_t *kb)
{
    lv_obj_t *row = lv_keyboard_create(parent);
    lv_obj_remove_event_cb(row, lv_keyboard_def_event_cb);
    lv_obj_add_event_cb(row, cb, LV_EVENT_VALUE_CHANGED, kb);
    lv_obj_set_style_bg_color(row, lv_color_hex(KB_FACE_COLOR), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 2, 0);
    lv_obj_set_style_pad_gap(row, 2, 0);
    lv_obj_set_style_radius(row, 8, LV_PART_ITEMS);
    return row;
}

/* Standalone corner button - shift/backspace/mode-switch/Enter. Same
 * white-key look as the button-matrix cells, but a plain clickable
 * lv_obj instead of a btnmatrix entry, so it can be sized and placed
 * independently of the row next to it (see CORNER_*_W above). */
static lv_obj_t *make_corner_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, custom_keyboard_t *kb)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, kb);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    return btn;
}

/* Fills the circle behind the rows - from the keyboard's top edge down
 * to the bottom of the screen - with the same dark-face color, so the
 * black margins between the tapered rows and the round glass read as
 * part of one continuous keyboard face instead of bare screen. A disc
 * exactly matching the screen's own circle, clipped to a container
 * starting at the keyboard's top, produces that shape - the clip
 * container hides everything above its own top edge, leaving just the
 * bottom chord of the disc visible. */
static lv_obj_t *make_backdrop(lv_obj_t *parent, int top_y)
{
    lv_obj_t *clip = lv_obj_create(parent);
    lv_obj_remove_style_all(clip);
    lv_obj_clear_flag(clip, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(clip, 360, 360 - top_y);
    lv_obj_align(clip, LV_ALIGN_TOP_LEFT, 0, top_y);

    lv_obj_t *disc = lv_obj_create(clip);
    lv_obj_remove_style_all(disc);
    lv_obj_clear_flag(disc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(disc, 360, 360);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc, lv_color_hex(KB_FACE_COLOR), 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    lv_obj_align(disc, LV_ALIGN_TOP_LEFT, 0, -top_y);

    return clip;
}

void custom_keyboard_build(custom_keyboard_t *kb, lv_obj_t *parent)
{
    kb->ta = NULL;

    kb->backdrop    = make_backdrop(parent, ROW_TOP_BAND.y1);
    kb->row_top     = make_row(parent, row_top_home_event_cb, kb);
    kb->row_home    = make_row(parent, row_top_home_event_cb, kb);
    kb->row_bottom  = make_row(parent, row_bottom_event_cb, kb);
    position_row(kb->row_top, ROW_TOP_BAND);
    position_row(kb->row_home, ROW_HOME_BAND);
    position_row_inset(kb->row_bottom, ROW_BOTTOM_BAND, CORNER_BOTTOM_W);

    kb->btn_shift     = make_corner_btn(parent, LV_SYMBOL_UP, btn_shift_event_cb, kb);
    kb->btn_backspace = make_corner_btn(parent, LV_SYMBOL_BACKSPACE, btn_backspace_event_cb, kb);
    kb->btn_mode      = make_corner_btn(parent, "123", btn_mode_event_cb, kb);
    kb->btn_enter     = make_corner_btn(parent, LV_SYMBOL_OK, btn_enter_event_cb, kb);
    kb->btn_space     = make_corner_btn(parent, "", btn_space_event_cb, kb);
    position_corner(kb->btn_shift, ROW_BOTTOM_BAND, CORNER_BOTTOM_W, true);
    position_corner(kb->btn_backspace, ROW_BOTTOM_BAND, CORNER_BOTTOM_W, false);
    position_corner(kb->btn_mode, ROW_CONTROL_BAND, CORNER_CONTROL_W, true);
    position_corner(kb->btn_enter, ROW_CONTROL_BAND, CORNER_CONTROL_W, false);
    position_space(kb->btn_space, ROW_CONTROL_BAND, CORNER_CONTROL_W);

    /* Backspace also repeats while held, so clearing a long entry
       doesn't take one tap per character. */
    lv_obj_add_event_cb(kb->btn_backspace, btn_backspace_event_cb, LV_EVENT_LONG_PRESSED_REPEAT, kb);

    lv_keyboard_set_mode(kb->row_top, ROW_TOP_SLOT);
    lv_keyboard_set_mode(kb->row_home, ROW_HOME_SLOT);
    lv_keyboard_set_mode(kb->row_bottom, ROW_BOTTOM_SLOT);

    custom_keyboard_reset(kb);
}

void custom_keyboard_set_textarea(custom_keyboard_t *kb, lv_obj_t *ta)
{
    kb->ta = ta;
    lv_keyboard_set_textarea(kb->row_top, ta);
    lv_keyboard_set_textarea(kb->row_home, ta);
    lv_keyboard_set_textarea(kb->row_bottom, ta);
}

void custom_keyboard_set_ready_cb(custom_keyboard_t *kb, lv_event_cb_t cb)
{
    lv_obj_add_event_cb(kb->btn_enter, cb, LV_EVENT_READY, NULL);
}

void custom_keyboard_reset(custom_keyboard_t *kb)
{
    apply_mode(kb, KB_MODE_LOWER);
}

void custom_keyboard_set_hidden(custom_keyboard_t *kb, bool hidden)
{
    lv_obj_t *objs[9] = {
        kb->backdrop, kb->row_top, kb->row_home, kb->row_bottom,
        kb->btn_shift, kb->btn_backspace, kb->btn_mode, kb->btn_enter, kb->btn_space,
    };
    int i;
    for (i = 0; i < 9; i++) {
        if (objs[i] == NULL) continue;
        if (hidden) lv_obj_add_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
    }
    /* Unhiding shows everything unconditionally above, but shift/
       row_bottom's visibility also depends on the current mode -
       reapply that on top of it rather than duplicating the rule. */
    if (!hidden) apply_mode(kb, (kb_mode_t)kb->mode);
}
