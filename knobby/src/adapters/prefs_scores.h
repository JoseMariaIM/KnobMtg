#ifndef _PREFS_SCORES_H
#define _PREFS_SCORES_H

/* Per-minigame, per-player high scores. See prefs.h. */

#include "prefs.h"

/* One indexed table rather than a get/set pair per game: at nine games
 * the named-function-per-game shape was nine near-identical pairs, nine
 * cached arrays and nine NVS keys to keep in sync. Appending a game is
 * now one enum row - but only ever APPEND, and never reorder: the
 * enum's numeric values are the column indices inside the saved
 * "game_hi" blob, so moving one hands a player somebody else's record.
 * See the migration note in prefs.c's prefs_init(). */
typedef enum {
    GAME_SCORE_SNAKE = 0,
    GAME_SCORE_PONG,
    GAME_SCORE_DINO,
    GAME_SCORE_TETRIS,
    GAME_SCORE_BREAKOUT,
    GAME_SCORE_FLAPPY,
    GAME_SCORE_EGGS,
    GAME_SCORE_INVADERS,
    GAME_SCORE_RPS,
    GAME_SCORE_ASTEROIDS,
    GAME_SCORE_COUNT,
    /* GAME_SCORE_TEMPEST removed 2026-09: Tempest was pulled from the
       device. Its column stays retired rather than reused - the saved
       "game_hi" blob is indexed by these values, so recycling the slot
       for a different game would hand new players someone else's old
       Tempest high score under a new game's name. */
} game_score_id_t;

int  prefs_get_game_high_score(game_score_id_t game, int player);
void prefs_set_game_high_score(game_score_id_t game, int player, int score);

#endif // _PREFS_SCORES_H
