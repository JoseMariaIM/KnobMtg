#ifndef _STORAGE_H
#define _STORAGE_H

#include "types.h"

void knob_nvs_init(void);

/* Writing is nobody's business but this module's.
 *
 * Every setter below marks the cache dirty and asks the scheduler for
 * a flush, so a caller can write a preference and stop thinking about
 * it. It used to be the other way round - the setter only marked, and
 * twelve modules were each expected to remember a settings_save()
 * afterwards. Some did it on the way out of a screen, which put the
 * decision in navigation code; ota_notice.c had to call it by hand
 * because nothing else would; and net_sync_apply_names() never did,
 * so a name that arrived from another device at the table showed up
 * on screen and was gone by the next power cycle. */

/* Commit right now. Only for the moments that are about to lose RAM:
   a reboot, deep sleep, an OTA flash. */
void prefs_flush(void);

/* Installed once at boot by prefs_autosave.c. storage calls arm() on
   every write; the scheduler is expected to call prefs_flush() once
   the writes stop. Nothing is written until one is installed, which
   is what unit tests want - they flush explicitly. */
void prefs_set_scheduler(void (*arm)(void));

int nvs_get_brightness(void);
void nvs_set_brightness(int value);
int nvs_get_auto_dim(void);
void nvs_set_auto_dim(int value);

int nvs_get_color_mode(void);
void nvs_set_color_mode(int value);
int nvs_get_deselect_timeout(void);
void nvs_set_deselect_timeout(int value);
int nvs_get_orientation(void);
void nvs_set_orientation(int value);
int nvs_get_display_rotation(void);
void nvs_set_display_rotation(int value);
int nvs_get_menu_facing(void);
void nvs_set_menu_facing(int value);
int nvs_get_language(void);
void nvs_set_language(int value);

int nvs_get_num_players(void);
void nvs_set_num_players(int value);
int nvs_get_players_to_track(void);
void nvs_set_players_to_track(int value);
int nvs_get_life_total(void);
void nvs_set_life_total(int value);

int nvs_get_auto_eliminate(void);
void nvs_set_auto_eliminate(int value);

int nvs_get_random_first(void);
void nvs_set_random_first(int value);

int nvs_get_multi_select(void);
void nvs_set_multi_select(int value);

#define NAME_LIST_COUNT 10
#define NAME_LIST_LEN   16
/* One bit per player index: set means that player fields a partner
   commander, which is what makes every partner control on the device
   appear at all. */
int  nvs_get_partner_mask(void);
void nvs_set_partner_mask(int mask);

/* The players' own names, as last set from the rename screen.
 *
 * Distinct from the name LIST above, which is the pool of remembered
 * names offered when renaming. This is who is actually sitting at the
 * table, and it survives a power cycle and a game reset - only the
 * rename screen changes it. */
#define PLAYER_NAME_COUNT MAX_GAME_PLAYERS
#define PLAYER_NAME_LEN   16
void nvs_get_player_names(char (*out)[PLAYER_NAME_LEN]);
void nvs_set_player_names(const char (*names)[PLAYER_NAME_LEN]);
/* True once a rename has been stored, so boot can tell "nobody has
   ever renamed anyone" from "everybody is called P1..P8 on purpose". */
bool nvs_has_player_names(void);

void nvs_get_name_list(char (*out)[NAME_LIST_LEN]);
void nvs_set_name_list(const char (*list)[NAME_LIST_LEN]);

#define WIFI_SSID_LEN 33 /* 32 chars + NUL, WPA2 max SSID length */
#define WIFI_PASS_LEN 65 /* 64 chars + NUL, WPA2 max PSK length */
void nvs_get_wifi_ssid(char *out, size_t out_len);
void nvs_set_wifi_ssid(const char *ssid);
void nvs_get_wifi_pass(char *out, size_t out_len);
void nvs_set_wifi_pass(const char *pass);

#define FW_VERSION_LEN 24 /* "vX.Y.Z" style tags, generous headroom */
void nvs_get_last_fw_version(char *out, size_t out_len);
void nvs_set_last_fw_version(const char *version);

/* Per-minigame, per-player high scores.
 *
 * One indexed table rather than a get/set pair per game: at nine games
 * the named-function-per-game shape was nine near-identical pairs, nine
 * cached arrays and nine NVS keys to keep in sync. Appending a game is
 * now one enum row - but only ever APPEND, and never reorder: the
 * enum's numeric values are the column indices inside the saved
 * "game_hi" blob, so moving one hands a player somebody else's record.
 * See the migration note in storage.c's knob_nvs_init(). */
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

int nvs_get_game_high_score(game_score_id_t game, int player);
void nvs_set_game_high_score(game_score_id_t game, int player, int score);

#endif // _STORAGE_H
