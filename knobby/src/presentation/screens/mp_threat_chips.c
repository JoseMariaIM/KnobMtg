/* See mp_threat_chips.h. */
#include "mp_threat_chips.h"
#include "ui_mp_internal.h"
#include "../../entities/game_state.h"
#include "../../usecases/game.h"
#include <string.h>

/* Same box as a counter badge (see COUNTER_BADGE_SIZE in ui_mp.c) so the
   two sit in one row at one step without the layout having to care which
   kind each item is. */
#define CHIP_SIZE 34
/* Two lines of CHIP_FONT in that box. The font's own line spacing puts
   the pair over the circle's height, so it is pulled back in - the two
   lines are one reading, not a paragraph. */
#define CHIP_FONT lv_font_es_14
#define CHIP_LINE_SPACE (-4)
#define CHIP_PAD_TOP 2

static lv_obj_t *chips[MULTIPLAYER_COUNT][MP_THREAT_CHIPS_MAX];

/* Everything about a chip that never varies, held once instead of as
   local style properties on each of the twelve of them - LVGL stores
   local properties per object, and at four panels' worth that overhead
   was most of what the chips cost the shared 128KB pool. Only the two
   colors, which differ per rival, stay local. */
static lv_style_t chip_style;
static bool chip_style_ready = false;

static void chip_style_init(void)
{
    if (chip_style_ready) return;

    lv_style_init(&chip_style);
    lv_style_set_width(&chip_style, CHIP_SIZE);
    lv_style_set_height(&chip_style, CHIP_SIZE);
    lv_style_set_text_font(&chip_style, &CHIP_FONT);
    lv_style_set_text_align(&chip_style, LV_TEXT_ALIGN_CENTER);
    lv_style_set_pad_top(&chip_style, CHIP_PAD_TOP);
    lv_style_set_text_line_space(&chip_style, CHIP_LINE_SPACE);
    lv_style_set_bg_opa(&chip_style, LV_OPA_COVER);
    lv_style_set_radius(&chip_style, LV_RADIUS_CIRCLE);
    chip_style_ready = true;
}

/* The color the rival is actually painted in on this screen, which is not
   simply their player index: the 2p layout hands its two panels the other
   one's color slot, and any player may carry a custom color override.
   Both live in the panel's own effective color, so look the rival's panel
   up rather than recomputing from the index. */
static lv_color_t rival_color(int source)
{
    const mp_layout_spec_t *layout = mp_current_layout();
    int i;

    if (layout != NULL) {
        for (i = 0; i < layout->panel_count; i++) {
            if (layout->panels[i].player_index == source) {
                return get_effective_player_color(source, layout->panels[i].color_index,
                                                  LIFE_VIB_VIV);
            }
        }
    }

    /* A rival with no panel of their own - the 5th+ seat in a game bigger
       than the four the device shows. Nothing on screen to match, so
       their own slot's color is as good as it gets. */
    return get_player_color_vib(source, LIFE_VIB_VIV);
}

void mp_threat_chips_create(lv_obj_t *panel, int panel_index)
{
    int i;

    if (panel == NULL || panel_index < 0 || panel_index >= MULTIPLAYER_COUNT) return;

    /* One object per chip, not a box with a label inside it: four panels
       of these are paid for out of the same 128KB LVGL pool every screen
       shares, and the second object per chip cost more of it than the
       whole feature was worth (see sim/tests/membudget). A label carries
       its own background, so it can BE the chip. */
    chip_style_init();

    for (i = 0; i < MP_THREAT_CHIPS_MAX; i++) {
        lv_obj_t *chip = lv_label_create(panel);

        lv_label_set_text(chip, "0");
        lv_obj_add_style(chip, &chip_style, 0);
        lv_obj_add_flag(chip, LV_OBJ_FLAG_HIDDEN);

        chips[panel_index][i] = chip;
    }
}

void mp_threat_chips_reset(void)
{
    memset(chips, 0, sizeof(chips));
}

/* Who the damage came from, in the one or two characters a chip holds.
 *
 * The chip's color alone cannot answer this: with the color mode set to
 * life (globally, or per player), every panel is colored by its life
 * tier, so two rivals on similar life are the same color and a rival's
 * color changes as they take damage. The initial does not move.
 *
 * Players who have never been renamed are still P1..P8, where every
 * initial would be the same 'P' - those show their seat number instead. */
static void rival_tag(int source, char *out, size_t out_size)
{
    const char *name = player_names[source];
    char def[16];
    size_t len = 1;

    snprintf(def, sizeof(def), "P%d", source + 1);
    if (name[0] == '\0' || strcmp(name, def) == 0) {
        snprintf(out, out_size, "%d", source + 1);
        return;
    }

    /* One whole UTF-8 character, not one byte: an accented first letter
       is two bytes and half of it renders as nothing. */
    while (len < out_size - 1 && ((unsigned char)name[len] & 0xC0) == 0x80) len++;
    memcpy(out, name, len);
    out[len] = '\0';
}

static void set_chip(int panel_index, int slot, int source, int damage, bool is_partner)
{
    lv_obj_t *chip = chips[panel_index][slot];
    lv_color_t color = rival_color(source);
    char tag[16];
    char buf[32];

    if (chip == NULL) return;

    /* The partner's chip is the same rival, so it keeps their hue and
       goes a shade lighter rather than taking a color of its own. */
    if (is_partner) color = lv_color_lighten(color, LV_OPA_30);

    rival_tag(source, tag, sizeof(tag));
    snprintf(buf, sizeof(buf), "%s\n%d", tag, damage);
    lv_label_set_text(chip, buf);
    lv_obj_set_style_text_color(chip,
        color_is_light(color) ? lv_color_black() : lv_color_white(), 0);
    lv_obj_set_style_bg_color(chip, color, 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_HIDDEN);
}

int mp_threat_chips_update(int panel_index, int player_index)
{
    cmd_threat_t threats[CMD_THREAT_MAX];
    int threat_count = 0;
    int shown = 0;
    int i;

    if (panel_index < 0 || panel_index >= MULTIPLAYER_COUNT) return 0;
    if (player_index < 0 || player_index >= MAX_DISPLAY_PLAYERS) return 0;

    /* An eliminated player's commander damage no longer decides anything,
       and their panel is greyed out - nothing left to warn about. */
    if (!player_eliminated[player_index]) {
        threat_count = cmd_threats_for_player(player_index, threats, CMD_THREAT_MAX);
    }

    /* One chip per RIVAL, not per commander: a rival fielding a partner
       would otherwise take two slots in a row that also has to hold the
       counter badges. The number shown is whichever of their two
       commanders is closest to killing this player - the two never pool,
       so the nearer one is the whole story, and the full pair-by-pair
       breakdown is one long-press away on the commander damage screen. */
    for (i = 0; i < threat_count && shown < MP_THREAT_CHIPS_MAX; i++) {
        bool partner_leads = threats[i].partner > threats[i].commander;
        int damage = partner_leads ? threats[i].partner : threats[i].commander;

        if (damage <= 0) continue;
        set_chip(panel_index, shown, threats[i].source, damage, partner_leads);
        shown++;
    }

    for (i = shown; i < MP_THREAT_CHIPS_MAX; i++) {
        if (chips[panel_index][i] != NULL) {
            lv_obj_add_flag(chips[panel_index][i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    return shown;
}

lv_obj_t *mp_threat_chips_obj(int panel_index, int index)
{
    if (panel_index < 0 || panel_index >= MULTIPLAYER_COUNT) return NULL;
    if (index < 0 || index >= MP_THREAT_CHIPS_MAX) return NULL;
    return chips[panel_index][index];
}
