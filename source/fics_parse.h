// FICS (freechess.org) text protocol: style-12 board lines, game results, move strings. Pure C, host-testable.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "chess.h"

typedef struct {
    char board[8][9];        // rank 8 first; upper case = white, lower = black, '-' = empty
    bool white_turn;
    int ep_file;             // file of a double pawn push, -1 none
    bool castle[4];          // white short, white long, black short, black long
    int irrev;               // half-moves since the last irreversible move
    int gnum;
    char white[24], black[24];
    int relation;            // 1: I play and it is my move, -1: I play and it is the opponent's move, 0/-2/2: observing/examining
    int init_min, inc;
    int wt_ms, bt_ms;        // remaining clocks (needs `iset ms 1`)
    int movenum;             // number of the move to be played (1 = start position)
    char verbose[24];        // last move, e.g. "P/e2-e4", "o-o", "none"
    char san[16];
    bool ticking;            // a clock is running
} Style12;

bool fics_parse_style12(const char *line, Style12 *s);
// "{Game 12 (A vs. B) A resigns} 0-1"  -> gnum, winner (WHITE/BLACK/-1), status ("resign" "mate" "outoftime" "draw" "stalemate" "aborted"), why (display text)
bool fics_parse_result(const char *line, int *gnum, int *winner, char *status, size_t sn, char *why, size_t wn);
bool fics_s12_to_pos(const Style12 *s, Pos *p);
bool fics_verbose_to_move(const Pos *p, const char *verbose, Move *out);
void fics_move_string(const Pos *pre, Move m, char *out, size_t n);       // what to type into the server
