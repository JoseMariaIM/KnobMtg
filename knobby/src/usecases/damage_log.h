#ifndef _DAMAGE_LOG_H
#define _DAMAGE_LOG_H

#include "../types.h"

#define DAMAGE_LOG_MAX 256

typedef enum {
    LOG_EVT_LIFE = 0,
    LOG_EVT_CMD_DAMAGE,
    LOG_EVT_COUNTER,
} log_event_type_t;

extern lv_obj_t *screen_damage_log;

void damage_log_add(int player, int delta, uint8_t event_type, int source);
void damage_log_reset(void);
void damage_log_remove_last_for(int player, uint8_t event_type);
void damage_log_select_next(void);
void damage_log_select_prev(void);
void damage_log_undo_selected(void);

/* Read-only accessors for unit tests (see damage_log.c). */
int damage_log_test_count(void);
bool damage_log_test_peek(int index_from_newest, int *player, int *delta,
                          uint8_t *event_type, int *source);

void build_damage_log_screen(void);
void open_damage_log_screen(void);

#endif // _DAMAGE_LOG_H
