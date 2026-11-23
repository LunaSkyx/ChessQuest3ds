// Live game model (state sync with the Lichess game stream, clocks, premove queue)
#pragma once
#include <jansson.h>
#include "app.h"
#include "premove.h"
#include "fics_parse.h"

#define MAX_PLIES 1024

typedef struct {
    char name[32];
    char title[8];
    int rating;
    bool prov;
    int ai_level;           // > 0: Stockfish level
} Player;

typedef struct {
    bool active, loaded;     // loaded: gameFull received
    char id[16];
    Variant variant;
    Pos start, pos;
    int nply;
    Move mv[MAX_PLIES];
    char uci[MAX_PLIES][8];
    char san[MAX_PLIES][12];
    int my_color;            // WHITE / BLACK / -1 (spectator)
    Player pl[2];
    bool rated;
    char speed[16];
    bool has_clock;
    int init_ms, inc_ms;
    int t_ms[2];             // clock values (ms) at t_stamp
    u64 t_stamp;
    int t_turn;              // colour whose clock was running at t_stamp, -1 none
    char status[16];
    bool finished;
    int winner;              // WHITE / BLACK / -1
    bool draw_offer[2];      // [colour] offers a draw
    bool takeback_offer[2];
    bool opp_gone;
    int gone_secs;
    PreQueue pre;
    int server_nply;         // plies confirmed by the server
    int backend;             // 0 Lichess, 1 FICS, 2 offline (built-in engine)
    char why[40];            // result detail text (FICS)
} Game;

extern Game g_game;

void game_reset(Game *g, const char *id);
bool game_parse_full(Game *g, json_t *o, const char *my_id);
// Applies a "gameState" object. Returns the number of NEW plies appended (>= 0); *diverged = server contradicted our local line.
int  game_parse_state(Game *g, json_t *state, bool *diverged);
// FICS: apply a style-12 board line. appended = new plies; diverged = the server changed our line (take-back / resync)
bool game_fics_update(Game *g, const Style12 *s, int *appended, bool *diverged);
void game_truncate(Game *g, int ply);                    // drop plies after `ply` (offline take-back)
bool game_local_move(Game *g, Move m);                   // optimistic move by me; false if illegal
int  game_clock(const Game *g, int color);
void game_pos_at(const Game *g, int ply, Pos *out);      // position after `ply` plies (0 = start)
bool game_my_turn(const Game *g);
void game_result_text(const Game *g, char *head, size_t hn, char *sub, size_t sn);
int  game_material_diff(const Pos *p);                   // white minus black, in pawns
void game_captured(const Pos *p, int color, int counts[7]);   // pieces of `color` missing from the board (vs. a full set)
void game_player_label(const Game *g, int color, char *out, size_t n);
