#ifndef _KNOB_H
#define _KNOB_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "lvgl.h"
#include "bidi_switch_knob.h"

typedef enum {
	KNOB_SWIPE_NONE = 0,
	KNOB_SWIPE_LEFT,
	KNOB_SWIPE_RIGHT,
	KNOB_SWIPE_UP,
	KNOB_SWIPE_DOWN
} knob_swipe_direction_t;

#define KNOB_TOUCH_JITTER_PX 14
/* On a round display the usable drag "runway" near an edge is much
   shorter than the same distance would be on a rectangular screen -
   dragging outward from an edge-zone start quickly runs the finger off
   the touch surface as the circle curves away. A wide edge zone plus a
   long required travel (the original 56/84) meant many valid starting
   points didn't leave enough room to ever reach the threshold, so the
   gesture read as tedious/unreliable rather than just "needs practice".
   Both were loosened together: shorter travel to actually complete the
   swipe, wider edge zone so more of the border counts as a valid start. */
#define KNOB_SWIPE_THRESHOLD 56
#define KNOB_SWIPE_HINT_REVEAL_START 20
#define KNOB_SWIPE_MAX_LATERAL 72
#define KNOB_SWIPE_MIN_DURATION_MS 100
#define KNOB_SWIPE_LEFT_EDGE_ZONE 80
#define KNOB_SWIPE_RIGHT_EDGE_ZONE 80
#define KNOB_SWIPE_TOP_EDGE_ZONE 80
#define KNOB_SWIPE_BOTTOM_EDGE_ZONE 80
#define KNOB_SWIPE_AXIS_BIAS_NUM 3
#define KNOB_SWIPE_AXIS_BIAS_DEN 2

void knob_gui(void);

/* Reinicia la partida (vida, brillo desde NVS, todas las pantallas) y
   arranca la ruleta de selección de primer jugador. Llamado tras
   aplicar Game Mode y desde el menú de ajustes; declarado aquí (antes
   solo tenía `extern void reset_all_values(void);` sueltos en cada
   llamador, sin cabecera propia) para que quien lo use dependa de
   este header, no de una declaración a ciegas que el compilador nunca
   comprueba contra la definición real. No necesita el mecanismo de
   home.h (home_bind): knob.c es la raíz de la app y nadie por debajo
   necesita que knob.c le devuelva la llamada, así que no hay ciclo
   que romper, solo una declaración que faltaba. */
void reset_all_values(void);

void knob_change(knob_event_t k);
void knob_process_pending(void);
bool activity_kick(void);
knob_swipe_direction_t knob_classify_swipe_direction(lv_obj_t *screen,
													 int start_x, int start_y,
													 int dx, int dy,
													 int min_travel);
void knob_swipe_hint_update(int start_x, int start_y, int cur_x, int cur_y);
void knob_swipe_hint_clear(void);
bool knob_swipe_hint_fully_revealed(lv_obj_t *screen,
									int start_x, int start_y,
									int cur_x, int cur_y);
void knob_notify_swipe_up(void);
void knob_notify_swipe_down(void);
void knob_notify_swipe_left(void);
void knob_notify_swipe_right(void);
/* Records where the back gesture should return to once it eventually
   unwinds to screen_quad_menu (see handle_back_navigation() in knob.c).
   Anything that jumps straight to a settings sub-screen from outside
   the normal settings menu chain must call this first, or back leaves
   the user stranded with no path to the life counter. */
void knob_remember_return_screen(lv_obj_t *screen);
float knob_read_battery_voltage(void);
void scr_display_on(void);
/* Tells the panel to stop driving GRAM (MIPI DCS Display Off) - paired
   with the backlight going to 0% for the "screen blank" power state
   (see screen_blank_enter() in hw.c). Not a sleep-in/power-off: the
   panel wakes instantly on the next scr_display_on(), same as today's
   dim/undim. */
void scr_display_off(void);
void display_apply_rotation(int rot);


#ifdef __cplusplus
}
#endif

#endif
