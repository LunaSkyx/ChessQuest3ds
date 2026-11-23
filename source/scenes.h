// Scene management
#pragma once
#include "app.h"

typedef enum { SC_NONE, SC_DEMO, SC_BOOT, SC_LOGIN, SC_HOME, SC_PLAY, SC_GAME, SC_GAMES, SC_SETTINGS, SC_HISTORY, SC_ANALYSIS, SC_PUZZLE, SC_MORE, SC_RATING, SC_FRIENDS, SC_TV, SC_TOURNEY, SC_LEARN, SC_GUIDE, SC_BOTS, SC_CLOCK, SC_COUNT } SceneId;

#include "api.h"
#include "game.h"

// parameters of the last started game (for rematch / "new game")
typedef struct {
    TimeCtl tc;
    Variant variant;
    bool rated;
    int color;               // 0 random, 1 white, 2 black as requested
    char opp[32];            // opponent name (human rematch)
    int ai_level;            // > 0: vs Stockfish
    int backend;             // 0 Lichess, 1 FICS, 2 offline
} GameParams;
extern GameParams g_last_params;

typedef struct {
    const char *name;
    void (*enter)(void);
    void (*update)(float dt);
    void (*draw_top)(void);
    void (*draw_bot)(void);
    void (*leave)(void);
} Scene;

void scene_goto(SceneId id);
SceneId scene_current(void);
const char *scene_name(SceneId id);
float scene_age(void);                  // seconds since the scene was entered
void scene_frame(float dt);             // update + draw handled by main loop through these
void scene_draw_top(void);
void scene_draw_bottom(void);
float scene_fade(void);                 // 0..1 transition alpha for the main loop overlay

extern const Scene scene_boot, scene_login, scene_home, scene_play, scene_game, scene_games, scene_settings, scene_history, scene_analysis, scene_puzzle, scene_more, scene_rating, scene_friends, scene_tv, scene_tourney, scene_learn, scene_guide, scene_bots, scene_clock;
void history_save_game(const Game *g);                      // stores a finished FICS game for the history

void scene_game_open_bot(int bot, int color);             // bot ladder game (color 0 random, 1 white, 2 black)
void scene_game_open_bot_fen(int bot, const char *fen);
void scene_game_open_offline_fen(int level, const char *fen);   // training position vs the engine
void scene_game_open_offline(int level, bool clock, TimeCtl tc, int color);   // play the built-in engine (no network)
void guide_open(SceneId back);                              // step by step how-to-connect guide
void social_follow(const char *username);                    // follow a Lichess user (friends list)
void puzzle_open_normal(void);                              // Home > Puzzles
void puzzle_start_storm(void);                              // 3 minute puzzle storm (Learn hub)
void puzzle_start_opening(int line);                        // opening trainer on OPEN_LINES[line]
int puzzle_test(const char *what);                         // scenario hook (see scene_puzzle.c)
void scene_game_open_fics(void);                            // switch to the game scene for the running FICS game
void scene_game_open(const char *id);                       // start/resume a game and switch to the game scene
bool scene_game_square_xy(int sq, int *x, int *y);          // scenario helper: board square -> screen pixel
bool scene_game_cond(const char *name, const char *args, bool *known);
bool scene_game_cmd(const char *name, const char *args);
void scene_play_autostart(void);                            // start a quick-pairing search with the stored last_tc
void scene_play_set_mode(int mode);                         // 0 quick, 1 computer, 2 friend, 3 FICS, 4 offline
