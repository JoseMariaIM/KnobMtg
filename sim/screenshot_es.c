/* One-off manual verification tool: boots the app with the Spanish
 * language already persisted (as it would be after a real language
 * switch + reboot) and dumps a few representative screens as PNGs.
 * Not part of the normal build. Compile/run like test_partner_commander.c.
 */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "board_detect.h"
#include "sim_stubs.h"
#include <lvgl.h>
#include "knob.h"
#include "game.h"
#include "storage.h"
#include "settings.h"
#include "ui_1p.h"
#include "ui_mp.h"
#include "ui_player_menu.h"
#include "ui_wifi.h"
#include "wifi_ota.h"
#include "lang.h"
#include "dice.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define SCREEN_W 360
#define SCREEN_H 360

static lv_color_t framebuffer[SCREEN_W * SCREEN_H];
static lv_color_t draw_buf_data[SCREEN_W * 72];
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;

extern void sim_nvs_preset_i8(const char *key, int8_t value);

static void sim_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    int x, y;
    for (y = area->y1; y <= area->y2; y++)
        for (x = area->x1; x <= area->x2; x++)
            framebuffer[y * SCREEN_W + x] = *color_p++;
    lv_disp_flush_ready(drv);
}

static void save_png(const char *filename)
{
    uint8_t *rgb = malloc(SCREEN_W * SCREEN_H * 3);
    int x, y;
    for (y = 0; y < SCREEN_H; y++) {
        for (x = 0; x < SCREEN_W; x++) {
            int idx = y * SCREEN_W + x;
            lv_color_t c = framebuffer[idx];
            rgb[idx * 3 + 0] = (uint8_t)((c.ch.red * 255) / 31);
            rgb[idx * 3 + 1] = (uint8_t)((c.ch.green * 255) / 63);
            rgb[idx * 3 + 2] = (uint8_t)((c.ch.blue * 255) / 31);
        }
    }
    stbi_write_png(filename, SCREEN_W, SCREEN_H, 3, rgb, SCREEN_W * 3);
    free(rgb);
    printf("Saved: %s\n", filename);
}

static void render_and_save(lv_obj_t *screen, const char *filename)
{
    lv_scr_load(screen);
    lv_refr_now(NULL);
    save_png(filename);
}

int main(void)
{
    /* Simulates a device that already has Spanish saved from a prior
       session (the real flow: pick Spanish -> esp_restart() -> boots
       straight into Spanish, since screens are only ever built once). */
    sim_nvs_preset_i8("language", 1);

    board_detect();
    lv_init();
    lv_disp_draw_buf_init(&draw_buf, draw_buf_data, NULL, SCREEN_W * 72);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_W;
    disp_drv.ver_res = SCREEN_H;
    disp_drv.flush_cb = sim_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
    knob_gui();

    nvs_set_num_players(4);
    nvs_set_players_to_track(4);
    knob_life_reset();

    int page = settings_item_page("language");
    if (page >= 0) render_and_save(settings_pages[page], "screenshots/es_settings_language.png");

    open_language_picker_screen();
    render_and_save(screen_language_picker, "screenshots/es_language_picker.png");

    prepare_cmd_damage_for_player(1);
    open_select_screen();
    render_and_save(screen_select, "screenshots/es_select.png");

    open_player_menu(0);
    render_and_save(screen_player_menu, "screenshots/es_player_menu.png");

    open_counter_menu();
    render_and_save(screen_counter_menu, "screenshots/es_counter_menu.png");

    render_and_save(screen_quad_menu, "screenshots/es_main_menu.png");
    render_and_save(screen_tools_menu, "screenshots/es_tools_menu.png");

    event_tool_dice(NULL);
    render_and_save(screen_dice_menu, "screenshots/es_dice_menu.png");

    /* Tiles are d6, d12, d20, Coin in that order. */
    lv_obj_t *tile_d20 = lv_obj_get_child(screen_dice_menu, 2);
    lv_event_send(tile_d20, LV_EVENT_CLICKED, NULL);
    render_and_save(screen_dice, "screenshots/es_dice_d20.png");

    open_dice_menu_screen();
    lv_obj_t *tile_coin = lv_obj_get_child(screen_dice_menu, 3);
    lv_event_send(tile_coin, LV_EVENT_CLICKED, NULL);
    render_and_save(screen_dice, "screenshots/es_dice_coin.png");

    /* 4-player game, eliminate 3 of the 4 to trigger the victory screen
       for whoever's left. */
    nvs_set_players_to_track(4);
    rebuild_multiplayer_layout(4);
    manual_eliminate_player(0);
    manual_eliminate_player(1);
    manual_eliminate_player(2);
    /* open_victory_screen() triggered a 600ms fade transition (real
       lv_scr_load_anim, not an instant lv_scr_load) - pump real ticks
       until it finishes before loading any other screen, or a second
       lv_scr_load while the fade's internal transition layer is still
       live corrupts LVGL's screen-transition state (crashed here
       during development; not a bug in the feature itself, just this
       headless tool never otherwise ticks the animation system). */
    for (int i = 0; i < 70; i++) {
        sim_tick_advance(10);
        lv_timer_handler();
    }
    render_and_save(screen_victory, "screenshots/es_victory.png");

    wifi_connect("MiRedWifi", "unaContrasena123");
    open_wifi_settings_screen();
    render_and_save(screen_wifi_settings, "screenshots/es_wifi_settings.png");

    open_wifi_scan_list_screen();
    render_and_save(screen_wifi_scan_list, "screenshots/es_wifi_scan_list.png");

    /* Simulate tapping the "Password" tile (index 1 of the settings
       quad screen) to reach the text entry screen, same as a real
       touch would via its LV_EVENT_CLICKED handler. */
    lv_obj_t *pw_tile = lv_obj_get_child(screen_wifi_settings, 1);
    lv_event_send(pw_tile, LV_EVENT_CLICKED, NULL);
    render_and_save(screen_wifi_text_entry, "screenshots/es_wifi_password_entry.png");

    open_ota_update_screen();
    render_and_save(screen_ota_update, "screenshots/es_ota_before_check.png");
    ota_check_now();
    refresh_ota_update_ui();
    render_and_save(screen_ota_update, "screenshots/es_ota_after_check.png");

    return 0;
}
