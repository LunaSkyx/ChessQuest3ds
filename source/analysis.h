// Analysis board: a game (or free position) that can be stepped through and played on for both sides, with Lichess cloud evaluations
#pragma once
#include "scenes.h"
#include "game.h"

#define AG_MAX MAX_PLIES

typedef struct {
    char white[32], black[32];
    int wr, br;
    char result[48];            // e.g. "1-0  Checkmate"
    char opening[48];
    Variant variant;
    Pos start;
    int n;
    Move mv[AG_MAX]; char uci[AG_MAX][8]; char san[AG_MAX][12];
    short ev[AG_MAX + 1];       // evaluation after ply i: centipawns, or the mate distance (white's point of view)
    uint8_t evk[AG_MAX + 1];    // 0 unknown, 1 centipawns, 2 mate, 3 not available
    uint8_t evsrc[AG_MAX + 1];  // 0 cloud/none, 1 built-in engine (still searching), 2 built-in engine (finished)
    uint8_t judg[AG_MAX + 1];   // 0 none, 1 inaccuracy, 2 mistake, 3 blunder (from a Lichess computer analysis)
    char best[AG_MAX + 1][6];   // best move (uci) the engine suggests in the position after ply i ("" unknown)
    bool from_game;
} AGame;

extern AGame g_ag;

void ag_reset(Variant v, const char *fen);               // empty line from the start position (or the given FEN)
bool ag_push_uci(const char *uci);
bool ag_push_san(const char *san);
void ag_from_game(const Game *g);                        // the game just played
void scene_analysis_open(int ply, SceneId back);         // opens the scene on g_ag at the given ply
