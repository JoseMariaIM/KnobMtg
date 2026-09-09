#include "intro.h"
#include "assets_intro.h"

// Forward declarations
extern void back_to_main(void);
extern void start_player_selection_animation(void);

lv_obj_t *screen_intro = NULL;

#define GENEX_LETTER_COUNT 5
static lv_obj_t *genex_letters[GENEX_LETTER_COUNT];
static lv_obj_t *comics_word = NULL;

/* Block layout in screen_intro's own coordinates. Both words are 300px
   wide (centered: (360-300)/2 = 30) and stacked around the screen's
   vertical center so the round bezel doesn't clip anything - see
   round_safe.h's radius for why 300px is the safe ceiling here. Letter
   positions/sizes keep the exact kerning from the source SVG. */
#define GENEX_BLOCK_X  30
#define GENEX_BLOCK_Y  100
#define COMICS_BLOCK_X 30
#define COMICS_BLOCK_Y 195

static const lv_img_dsc_t *const genex_letter_imgs[GENEX_LETTER_COUNT] = {
    &img_genex_0, &img_genex_1, &img_genex_2, &img_genex_3, &img_genex_4,
};
static const lv_point_t genex_letter_pos[GENEX_LETTER_COUNT] = {
    {0, 0}, {66, 1}, {120, 1}, {189, 1}, {237, 1},
};

#define GENEX_LETTER_STAGGER_MS 140
#define GENEX_LETTER_FADE_MS    260
#define COMICS_POP_DELAY_MS     (((GENEX_LETTER_COUNT - 1) * GENEX_LETTER_STAGGER_MS) + GENEX_LETTER_FADE_MS + 150)
#define COMICS_POP_FADE_MS      150
#define COMICS_POP_ZOOM_MS      450
#define COMICS_POP_ZOOM_START   160  /* out of LV_IMG_ZOOM_NONE=256, i.e. ~62% */
#define INTRO_HOLD_MS           700  /* keep the finished logo on screen a beat */

static void intro_finish_timer_cb(lv_timer_t *timer)
{
    lv_timer_del(timer);
    back_to_main();
    /* Boot is the one time nobody has picked a starting player yet -
       every other call to back_to_main() is just returning to a game
       already in progress, so this can't live there without re-rolling
       on every ordinary back navigation. Runs only when "random first
       player" is on and there's more than one player; a no-op otherwise. */
    start_player_selection_animation();
}

static void comics_pop_ready_cb(lv_anim_t *a)
{
    (void)a;
    lv_timer_create(intro_finish_timer_cb, INTRO_HOLD_MS, NULL);
}

void build_intro_screen(void)
{
    int i;

    screen_intro = lv_obj_create(NULL);
    lv_obj_set_size(screen_intro, 360, 360);
    lv_obj_set_style_bg_color(screen_intro, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_intro, 0, 0);
    lv_obj_set_scrollbar_mode(screen_intro, LV_SCROLLBAR_MODE_OFF);

    for (i = 0; i < GENEX_LETTER_COUNT; i++) {
        lv_obj_t *img = lv_img_create(screen_intro);
        lv_img_set_src(img, genex_letter_imgs[i]);
        lv_obj_set_pos(img, GENEX_BLOCK_X + genex_letter_pos[i].x,
                             GENEX_BLOCK_Y + genex_letter_pos[i].y);
        lv_obj_set_style_opa(img, LV_OPA_TRANSP, 0);
        genex_letters[i] = img;
    }

    comics_word = lv_img_create(screen_intro);
    lv_img_set_src(comics_word, &img_comics);
    lv_obj_set_pos(comics_word, COMICS_BLOCK_X, COMICS_BLOCK_Y);
    lv_obj_set_style_opa(comics_word, LV_OPA_TRANSP, 0);
    lv_img_set_zoom(comics_word, COMICS_POP_ZOOM_START);
}

void knob_intro_init(void)
{
    lv_anim_t zoom_anim;
    int i;

    for (i = 0; i < GENEX_LETTER_COUNT; i++) {
        if (genex_letters[i] == NULL) continue;
        lv_obj_fade_in(genex_letters[i], GENEX_LETTER_FADE_MS, (uint32_t)i * GENEX_LETTER_STAGGER_MS);
    }

    if (comics_word == NULL) return;

    lv_obj_fade_in(comics_word, COMICS_POP_FADE_MS, COMICS_POP_DELAY_MS);

    lv_anim_init(&zoom_anim);
    lv_anim_set_var(&zoom_anim, comics_word);
    lv_anim_set_values(&zoom_anim, COMICS_POP_ZOOM_START, LV_IMG_ZOOM_NONE);
    lv_anim_set_time(&zoom_anim, COMICS_POP_ZOOM_MS);
    lv_anim_set_delay(&zoom_anim, COMICS_POP_DELAY_MS);
    lv_anim_set_path_cb(&zoom_anim, lv_anim_path_overshoot);
    lv_anim_set_exec_cb(&zoom_anim, (lv_anim_exec_xcb_t)lv_img_set_zoom);
    lv_anim_set_ready_cb(&zoom_anim, comics_pop_ready_cb);
    lv_anim_start(&zoom_anim);
}
