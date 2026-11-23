// Puzzle logic (pure C): replaying the game that leads to a puzzle, checking the solver's moves
#pragma once
#include <stdbool.h>
#include "chess.h"

#define PZ_MAX_SOL 32

typedef struct {
    char id[12];
    int rating, plays;
    char themes[160];            // space separated theme keys
    char game_id[12];
    char white[32], black[32];
    int wr, br;
    char perf[16];
    int nsol;
    char sol[PZ_MAX_SOL][6];     // solution moves (uci); sol[0] is the solver's first move
    Pos start;                   // position the solver sees first
    int last_from, last_to;      // the opponent's last move (highlight), -1 when unknown
    Pos prev; Move last;         // position before that move, and the move (to animate it)
    bool has_last;
    int my_color;                // side of the solver
} Puzzle;

// Plays "e4 e5 Nf3 ..." from the start position; returns the number of plies replayed (0 on failure). *last = the final move
int  pz_replay_pgn(const char *pgn, Pos *out, Move *last, int *last_from, int *last_to);
// Fills start/prev/last/last_from/last_to/has_last of a puzzle from its PGN
bool pz_setup_from_pgn(Puzzle *p, const char *pgn);
// Checks the whole puzzle: the solution must be legal from `start`
bool pz_validate(const Puzzle *p);
typedef enum { PZ_WRONG, PZ_CORRECT, PZ_CORRECT_MATE } PzVerdict;
// Is `m` an acceptable answer at solution index `step`? (a different checkmating move is accepted too, like on Lichess)
PzVerdict pz_check(const Puzzle *p, const Pos *cur, int step, Move m);
// "mateIn2" -> "Mate in 2" etc., for display
void pz_theme_label(const char *key, char *out, size_t n);
