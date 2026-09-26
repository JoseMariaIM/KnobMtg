#include "ui_1p.h"
#include "home.h"
#include "ui_player_menu.h"
#include "usecases/game.h"
#include "adapters/hw.h"
#include "adapters/lang.h"

// ---------- screens ----------
lv_obj_t *screen_1p = NULL;

// ---------- main UI widgets ----------
static lv_obj_t *life_hitbox = NULL;
static lv_obj_t *label_life_total = NULL;
static lv_obj_t *label_life_preview_total = NULL;

// ---------- 1p counter widgets ----------
static lv_obj_t *counter_row_1p[COUNTER_TYPE_COUNT];
static lv_obj_t *counter_value_1p[COUNTER_TYPE_COUNT];


// ---------- refresh functions ----------
static void refresh_life_digits(void)
{
    bool flash_here = life_flash_active && life_flash_delta[0] != 0;
    bool showing_change = life_preview_active || flash_here;
    /* Live preview shows the still-pending delta; the flash is
       read-only feedback for a change already committed, so its delta
       is just for display and player_life[0] below is already final. */
    int display_value = life_preview_active ? pending_life_delta
                       : flash_here ? life_flash_delta[0]
                       : player_life[0];
    bool negative = (display_value < 0);
    lv_color_t c;
    char buf[16];

    if (showing_change) {
        c = negative ? lv_color_hex(0xFF1744)
                     : lv_color_hex(0x06D6A0);
        if (display_value > 0)
            snprintf(buf, sizeof(buf), "+%d", display_value);
        else
            snprintf(buf, sizeof(buf), "%d", display_value);
    } else {
        c = get_effective_player_color(0, 0, LIFE_VIB_MID);
        snprintf(buf, sizeof(buf), "%d", display_value);
    }

    lv_label_set_text(label_life_total, buf);
    lv_obj_set_style_text_color(label_life_total, c, 0);
    lv_obj_align(label_life_total, LV_ALIGN_CENTER, 0, -6);

    if (showing_change && label_life_preview_total != NULL) {
        int new_total = life_preview_active ? (player_life[0] + pending_life_delta) : player_life[0];
        snprintf(buf, sizeof(buf), "= %d", new_total);
        lv_label_set_text(label_life_preview_total, buf);
        lv_obj_clear_flag(label_life_preview_total, LV_OBJ_FLAG_HIDDEN);
    } else if (label_life_preview_total != NULL) {
        lv_obj_add_flag(label_life_preview_total, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh_1p_counters(void)
{
    int type;
    int visible_count = 0;
    int visible_types[COUNTER_TYPE_COUNT];
    char buf[8];
    const lv_coord_t step = 30;
    const lv_coord_t counter_y = 46;
    lv_color_t text_color = lv_color_white();

    for (type = 0; type < COUNTER_TYPE_COUNT; type++) {
        if (counter_row_1p[type] == NULL || counter_value_1p[type] == NULL) continue;

        if (!counter_type_is_enabled((counter_type_t)type) ||
            get_counter_value(0, (counter_type_t)type) <= 0) {
            lv_obj_add_flag(counter_row_1p[type], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        visible_types[visible_count] = type;
        visible_count++;
    }

    for (type = 0; type < visible_count; type++) {
        int value;
        int counter_type = visible_types[type];
        lv_coord_t x_offset = (lv_coord_t)((type * step) - ((visible_count - 1) * step / 2));

        value = get_counter_value(0, (counter_type_t)counter_type);

        snprintf(buf, sizeof(buf), "%d", value);
        lv_label_set_text(counter_value_1p[counter_type], buf);
        lv_obj_set_style_text_color(counter_value_1p[counter_type], text_color, 0);
        lv_obj_clear_flag(counter_row_1p[counter_type], LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(counter_row_1p[counter_type], LV_ALIGN_TOP_MID, x_offset, counter_y);
    }

}

void refresh_main_ui(void)
{
    refresh_life_digits();
    refresh_1p_counters();
}

// ---------- events ----------
static void event_open_1p_menu(lv_event_t *e)
{
    (void)e;
    open_player_menu(0);
    lv_indev_wait_release(lv_indev_get_act());
}


static void event_back_main(lv_event_t *e)
{
    (void)e;
    back_to_main();
}

// ---------- counter row helper ----------
static const lv_font_t *get_counter_badge_font_1p(const counter_definition_t *definition)
{
    if (definition != NULL && definition->icon_text != NULL) {
        return &mana_counter_icons_16;
    }

    return &lv_font_es_14;
}

static const char *get_counter_badge_text_1p(const counter_definition_t *definition)
{
    if (definition == NULL) return "?";
    if (definition->icon_text != NULL) return definition->icon_text;
    if (definition->badge_text != NULL) return definition->badge_text;
    return "?";
}

static void create_counter_row_1p(lv_obj_t *parent, counter_type_t type,
                                  lv_obj_t **row_out, lv_obj_t **value_out)
{
    const counter_definition_t *definition = get_counter_definition(type);
    lv_obj_t *row;
    lv_obj_t *glyph;

    row = make_plain_box(parent, 34, 34);
    lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);

    glyph = lv_label_create(row);
    lv_label_set_text(glyph, get_counter_badge_text_1p(definition));
    lv_obj_set_style_text_color(glyph, lv_color_white(), 0);
    lv_obj_set_style_text_font(glyph,
        (type == COUNTER_TYPE_POISON) ? &mana_poison_icon_bold_16
                                      : get_counter_badge_font_1p(definition), 0);
    lv_obj_align(glyph, LV_ALIGN_TOP_MID, 0, 0);

    *value_out = lv_label_create(row);
    lv_label_set_text(*value_out, "0");
    lv_obj_set_style_text_color(*value_out, lv_color_white(), 0);
    lv_obj_set_style_text_font(*value_out, &lv_font_es_14, 0);
    lv_obj_align(*value_out, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_align(*value_out, LV_TEXT_ALIGN_CENTER, 0);

    *row_out = row;
}

// ---------- screen builders ----------
void build_main_screen(void)
{
    screen_1p = lv_obj_create(NULL);
    lv_obj_set_size(screen_1p, 360, 360);
    lv_obj_set_style_bg_color(screen_1p, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_1p, 0, 0);
    lv_obj_set_scrollbar_mode(screen_1p, LV_SCROLLBAR_MODE_OFF);

    life_hitbox = make_plain_box(screen_1p, 360, 360);
    lv_obj_align(life_hitbox, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(life_hitbox, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(life_hitbox, event_open_1p_menu, LV_EVENT_LONG_PRESSED, NULL);

    label_life_total = lv_label_create(screen_1p);
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", player_life[0]);
        lv_label_set_text(label_life_total, buf);
    }
    lv_obj_set_style_text_font(label_life_total, &lv_font_montserrat_bold_116, 0);
    lv_obj_set_style_text_color(label_life_total, lv_color_white(), 0);
    lv_obj_align(label_life_total, LV_ALIGN_CENTER, 0, -6);

    label_life_preview_total = lv_label_create(screen_1p);
    lv_label_set_text(label_life_preview_total, "");
    lv_obj_set_style_text_color(label_life_preview_total, lv_color_hex(0x06D6A0), 0);
    lv_obj_set_style_text_font(label_life_preview_total, &lv_font_montserrat_regular_48, 0);
    lv_obj_align(label_life_preview_total, LV_ALIGN_CENTER, 0, 80);
    lv_obj_add_flag(label_life_preview_total, LV_OBJ_FLAG_HIDDEN);

    create_counter_row_1p(screen_1p, COUNTER_TYPE_COMMANDER_TAX,
        &counter_row_1p[COUNTER_TYPE_COMMANDER_TAX],
        &counter_value_1p[COUNTER_TYPE_COMMANDER_TAX]);
    create_counter_row_1p(screen_1p, COUNTER_TYPE_PARTNER_TAX,
        &counter_row_1p[COUNTER_TYPE_PARTNER_TAX],
        &counter_value_1p[COUNTER_TYPE_PARTNER_TAX]);
    create_counter_row_1p(screen_1p, COUNTER_TYPE_POISON,
        &counter_row_1p[COUNTER_TYPE_POISON],
        &counter_value_1p[COUNTER_TYPE_POISON]);
    create_counter_row_1p(screen_1p, COUNTER_TYPE_EXPERIENCE,
        &counter_row_1p[COUNTER_TYPE_EXPERIENCE],
        &counter_value_1p[COUNTER_TYPE_EXPERIENCE]);

    {
        lv_obj_t *batt = lv_label_create(screen_1p);
        lv_label_set_text(batt, LV_SYMBOL_BATTERY_EMPTY);
        lv_obj_set_style_text_color(batt, lv_palette_main(LV_PALETTE_RED), 0);
        lv_obj_set_style_text_font(batt, &lv_font_es_22, 0);
        lv_obj_align(batt, LV_ALIGN_TOP_MID, 0, 28);
        battery_icon_register(batt);
    }
}

