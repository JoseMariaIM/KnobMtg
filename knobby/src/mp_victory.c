#include "mp_victory.h"
#include "home.h"
#include "ui_mp_internal.h"
#include "ui_1p.h"
#include "game.h"
#include "prefs_table.h"
#include "lang.h"

extern void reset_all_values(void);

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

void check_for_winner(void)
{
    const mp_layout_spec_t *layout = mp_current_layout();
    int i, alive_count = 0, alive_index = -1;

    if (victory_shown || layout == NULL) return;
    if (prefs_get_players_to_track() <= 1) return;

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

void mp_victory_reset(void)
{
    victory_shown = false;
}
