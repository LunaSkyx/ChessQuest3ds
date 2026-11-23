// Training content: opening lines and endgame positions (pure data + helpers)
#pragma once
#include "chess.h"
#include "puzzle.h"

typedef struct { const char *name; int color; const char *san; } OpenLine;     // color: the side the learner plays (WHITE/BLACK)
typedef struct { const char *name, *goal, *fen; } EndgamePos;                  // the learner plays the side to move

extern const OpenLine OPEN_LINES[];
extern const int OPEN_LINE_COUNT;
extern const EndgamePos ENDGAMES[];
extern const int ENDGAME_COUNT;

// Builds a "puzzle" out of an opening line: the learner's moves are the solution, the opponent's replies are played automatically
bool learn_line_to_puzzle(const OpenLine *l, Puzzle *out);
