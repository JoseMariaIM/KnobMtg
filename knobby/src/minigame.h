#ifndef _MINIGAME_H
#define _MINIGAME_H

#include "types.h"
#include "lang.h"
#include "prefs_scores.h"

/* The parts every minigame on this device has in common.
 *
 * Nine games in, each one was repeating the same ~120 lines: resolve
 * which player the run scores to, build a screen with a score label and
 * a centred message panel, drive an lv_timer that pauses itself when
 * the user navigates away, compose "Game Over / Score / Best / New
 * Best", and write the record back to NVS. All of that lives here now,
 * so a game file contains its rules and its drawing and nothing else.
 *
 * A game supplies a minigame_t describing itself plus three callbacks
 * (reset / tick / draw) and calls minigame_*() from its own knob and tap
 * handlers. It keeps ownership of its rules: this layer never decides
 * when a run ends, only what happens once the game says it has. */

typedef enum {
    MINIGAME_READY = 0,   /* built, waiting for the first input */
    MINIGAME_PLAYING,
    MINIGAME_PAUSED,      /* only reachable when .pausable is set */
    MINIGAME_OVER,
} minigame_state_t;

typedef struct minigame_s minigame_t;

struct minigame_s {
    /* ---- configuration: set once, in the game's own initialiser ---- */
    lv_obj_t      **screen;     /* the game's screen_<name> global */
    game_score_id_t score_id;   /* which column of the high-score table */
    string_id_t     hint;       /* one line shown under "Tap or Turn to Start" */
    uint32_t        tick_ms;
    bool            pausable;   /* false for games with no sensible pause */
    /* Set when the game invalidates its own screen regions and does not
       want the whole screen repainted after every tick. Worth it only
       for a game whose frame is expensive to draw - Breakout's 48
       anti-aliased arcs could not keep up at 50fps repainted whole.
       Everything else leaves this false and gets the simple behaviour. */
    bool            partial_redraw;

    void (*on_reset)(minigame_t *g);  /* clear the game's own state */
    void (*on_tick)(minigame_t *g);   /* advance one step */
    void (*on_draw)(lv_event_t *e);   /* LV_EVENT_DRAW_MAIN on the screen */
    /* The game's tap handler - normally its own <name>_handle_tap().
       minigame_build() is what subscribes it to LV_EVENT_CLICKED, so a
       game that fills in everything else but leaves this NULL is simply
       deaf to the touchscreen. */
    void (*on_tap)(void);
    /* Deliver the tap on finger-DOWN instead of on release.
     *
     * For an action game this is simply better - a shot should leave
     * when you press, not when you let go - and it is also robust to a
     * release that never arrives as a click. On the device the touch
     * path can swallow a release: a gesture that classifies as a swipe
     * calls lv_indev_reset(), a press that wakes a dimmed screen is
     * eaten deliberately, and a drag past the scroll threshold cancels
     * the click too. None of that is reproducible in the simulator,
     * which has its own input path, so the fix is to stop depending on
     * the release at all where the action is harmless to repeat.
     *
     * Games whose tap does something destructive or final - Tetris'
     * hard drop, Breakout's pause, a Rock Paper Scissors throw - stay
     * on release, where a gesture that turns into a swipe correctly
     * does NOT trigger them. */
    bool            tap_on_press;
    /* Optional, and a pair: on_build adds any widgets of the game's own
       on top of the shared screen, on_free drops the pointers to them
       just before that screen is deleted.
     *
     * on_build has to exist because the screen is built from
     * minigame_open() - the lazy path, and again after an eviction -
     * never from the game's own build_<name>_screen(). A game that
     * created its widgets there instead would get them exactly zero
     * times. */
    void (*on_build)(minigame_t *g);
    void (*on_free)(minigame_t *g);

    /* ---- runtime: owned by minigame.c, read freely by the game ---- */
    minigame_state_t state;
    int  score;
    int  player;                /* -1 when no player is selected */
    bool new_best;
    lv_obj_t   *score_lbl;
    lv_obj_t   *msg_lbl;
    lv_timer_t *timer;
};

/* Builds the screen (score label, message panel, paused tick timer) and
   runs on_reset. Call from the game's build_<name>_screen(). */
void minigame_build(minigame_t *g);

/* Lazy-builds if needed, re-resolves the scoring player, resets and
   shows the screen. Call from the game's open_<name>_screen().
 *
 * Also evicts every OTHER game's screen. Nine game screens left
 * resident came to ~24KB of a 128KB pool that never gets it back, which
 * took the high-water mark to 83% and left the transient UI (colour
 * wheel, QR code, keyboards) a margin too thin to be safe - and
 * exhausting this pool is a silent hang, not an error. Only one game
 * can be on screen at a time, so only one needs to exist; rebuilding is
 * a handful of objects on a user tap, far below anything noticeable. */
void minigame_open(minigame_t *g);

/* Freezes the loop on back-navigation. */
void minigame_leave(minigame_t *g);

void minigame_start(minigame_t *g);
void minigame_over(minigame_t *g);      /* records the high score, shows the panel */
void minigame_reset(minigame_t *g);     /* back to READY without leaving the screen */
void minigame_toggle_pause(minigame_t *g);

void minigame_set_score(minigame_t *g, int score);
void minigame_add_score(minigame_t *g, int delta);

/* The start/restart/resume half of a tap, which is identical in every
   game. Returns true when the tap was spent on that, so a game's tap
   handler is:  if (minigame_handle_tap(&g)) return;  <in-play action> */
bool minigame_handle_tap(minigame_t *g);

/* Same for a knob detent in games where turning also starts the run.
   Returns true when the detent was spent starting it. */
bool minigame_handle_turn_start(minigame_t *g);

/* Where the user last touched, in screen coordinates. Games with tap
   zones (Tetris' move/drop bands) read the point from here; returns
   false when there is no active pointer. */
bool minigame_touch_point(lv_point_t *out);

/* ---------- read-only accessors (unit tests) ----------
 * Every game keeps its minigame_t as a file static, so tests address
 * one by the screen global it already has to name anyway. Looked up in
 * the list minigame_build() registers into; returns MINIGAME_READY / 0
 * for a screen that is not a minigame. See sim/tests/test_minigames.c. */
minigame_state_t minigame_test_state(lv_obj_t *screen);
int minigame_test_score(lv_obj_t *screen);

/* ---------- partial redraw ----------
 * Repainting all 360x360 every frame costs ~259KB over QSPI and a full
 * software render at 80MHz, 25 times a second - by far the largest
 * thing this device does while a game is on screen. A game that knows
 * which pixels actually changed sets .partial_redraw and calls these
 * instead, once per moving thing, with its position BEFORE and AFTER
 * the move so the trail behind it gets repainted too.
 *
 * Coordinates are clamped to the panel, so callers can pass boxes that
 * run off the edge without checking. */
void minigame_invalidate_rect(lv_obj_t *screen, int x1, int y1, int x2, int y2);
/* A sprite as a centre and a half-extent - the shape most of these
   games' moving parts already have. */
void minigame_invalidate_box(lv_obj_t *screen, int cx, int cy, int half);
/* A band of an annulus, for the games built in polar coordinates: the
   bounding box is walked round the arc rather than taken from the two
   end points, which for anything wider than a quadrant would miss the
   bulge in between. Angles are degrees clockwise from 3 o'clock. */
void minigame_invalidate_arc(lv_obj_t *screen, int cx, int cy,
                             int r_in, int r_out, int start_deg, int end_deg);

#endif // _MINIGAME_H
