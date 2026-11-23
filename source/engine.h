// Small built-in chess engine (alpha-beta + quiescence + transposition table). Pure C, no 3DS dependencies.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "chess.h"

typedef struct {
    int max_depth;      // plies
    int time_ms;        // soft time limit (iterative deepening stops when exceeded)
    int noise;          // centipawns of random noise on root moves (weak levels)
} EngLimits;

typedef struct {
    int job;            // id returned by eng_async_start
    int depth;          // completed depth
    int score;          // centipawns from WHITE's point of view (mate: +-(30000 - plies)); see eng_is_mate
    Move best;
    Move pv[8]; int npv;
    long nodes;
    bool done;          // search finished (the last update of a job)
    bool has_move;
} EngResult;

EngLimits eng_level(int level);                       // 1..8
uint64_t eng_hash(const Pos *p);
// Blocking search. clock_ms: monotonic millisecond clock; abort: set non-zero to stop early.
// hist: hashes of earlier game positions (for repetition detection), may be NULL.
// progress is called after each completed depth (may be NULL).
bool eng_search(const Pos *p, const uint64_t *hist, int nhist, EngLimits lim, uint64_t (*clock_ms)(void), volatile int *abort,
                void (*progress)(const EngResult *r, void *ud), void *ud, EngResult *out);
bool eng_is_mate(int score_white_pov, int *mate_in);  // true when the score encodes a forced mate; mate_in > 0: white mates

// asynchronous use on the 3DS (engine_async.c): runs eng_search on a low priority thread
void eng_async_init(void);
void eng_async_exit(void);
int  eng_async_start(const Pos *p, const uint64_t *hist, int nhist, EngLimits lim);   // cancels a running job
void eng_async_cancel(void);
bool eng_async_poll(EngResult *out);                  // latest unseen update; true when something new arrived
