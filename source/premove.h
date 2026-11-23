// Multi-premove queue. Portable (host-testable).
#pragma once
#include "chess.h"

#define PM_MAX 8

typedef struct { uint8_t from, to, promo, drop; } PreMove;     // drop != 0: piece type dropped on `to`
typedef struct { PreMove q[PM_MAX]; int n; } PreQueue;

// Plays the queued premoves blindly onto a copy of the real position (side to move forced to `color`).
void pm_virtual(const Pos *real, const PreQueue *q, int color, Pos *virt);
// Squares the piece on `from` could be premoved to on the virtual position (bitboard).
uint64_t pm_dests(const Pos *virt, int color, int from);
uint64_t pm_drop_dests(const Pos *virt, int color, int type);
bool pm_push(PreQueue *q, PreMove pm);
void pm_clear(PreQueue *q);
// At execution time: find the legal move on the real position that realises the premove.
bool pm_execute(const Pos *real, PreMove pm, Move *out);
// true if a pawn move to `to` is a promotion for `color`
bool pm_is_promotion(const Pos *virt, int color, int from, int to);
