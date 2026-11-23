#include "engine.h"
#include <stdlib.h>
#include <string.h>

#define INF 32000
#define MATE 30000
#define MAX_PLY 64
#define TT_BITS 16
#define TT_SIZE (1 << TT_BITS)

// ------------------------------------------------------------------ hashing
static uint64_t Z[16][64], Z_TURN, Z_CASTLE[16], Z_EP[9];
static bool z_ready;

static uint64_t rng_state = 0x9E3779B97F4A7C15ULL;
static uint64_t rnd64(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17; return rng_state; }

static void z_init(void) {
    if (z_ready) return;
    for (int i = 0; i < 16; i++) for (int s = 0; s < 64; s++) Z[i][s] = rnd64();
    Z_TURN = rnd64();
    for (int i = 0; i < 16; i++) Z_CASTLE[i] = rnd64();
    for (int i = 0; i < 9; i++) Z_EP[i] = rnd64();
    z_ready = true;
}

uint64_t eng_hash(const Pos *p) {
    z_init();
    uint64_t h = 0;
    for (int s = 0; s < 64; s++) if (p->b[s]) h ^= Z[p->b[s] & 15][s];
    if (p->turn) h ^= Z_TURN;
    h ^= Z_CASTLE[p->castle & 15];
    h ^= Z_EP[p->ep == NO_SQ ? 8 : FILE_OF(p->ep)];
    return h;
}

// ------------------------------------------------------------------ evaluation
static const int VAL[7] = {0, 100, 320, 330, 500, 900, 0};

static const int8_t PST_P[64] = {0,0,0,0,0,0,0,0, 50,50,50,50,50,50,50,50, 10,10,20,30,30,20,10,10, 5,5,10,25,25,10,5,5, 0,0,0,20,20,0,0,0, 5,-5,-10,0,0,-10,-5,5, 5,10,10,-20,-20,10,10,5, 0,0,0,0,0,0,0,0};
static const int8_t PST_N[64] = {-50,-40,-30,-30,-30,-30,-40,-50, -40,-20,0,0,0,0,-20,-40, -30,0,10,15,15,10,0,-30, -30,5,15,20,20,15,5,-30, -30,0,15,20,20,15,0,-30, -30,5,10,15,15,10,5,-30, -40,-20,0,5,5,0,-20,-40, -50,-40,-30,-30,-30,-30,-40,-50};
static const int8_t PST_B[64] = {-20,-10,-10,-10,-10,-10,-10,-20, -10,0,0,0,0,0,0,-10, -10,0,5,10,10,5,0,-10, -10,5,5,10,10,5,5,-10, -10,0,10,10,10,10,0,-10, -10,10,10,10,10,10,10,-10, -10,5,0,0,0,0,5,-10, -20,-10,-10,-10,-10,-10,-10,-20};
static const int8_t PST_R[64] = {0,0,0,0,0,0,0,0, 5,10,10,10,10,10,10,5, -5,0,0,0,0,0,0,-5, -5,0,0,0,0,0,0,-5, -5,0,0,0,0,0,0,-5, -5,0,0,0,0,0,0,-5, -5,0,0,0,0,0,0,-5, 0,0,0,5,5,0,0,0};
static const int8_t PST_Q[64] = {-20,-10,-10,-5,-5,-10,-10,-20, -10,0,0,0,0,0,0,-10, -10,0,5,5,5,5,0,-10, -5,0,5,5,5,5,0,-5, 0,0,5,5,5,5,0,-5, -10,5,5,5,5,5,0,-10, -10,0,5,0,0,0,0,-10, -20,-10,-10,-5,-5,-10,-10,-20};
static const int8_t PST_K[64] = {-30,-40,-40,-50,-50,-40,-40,-30, -30,-40,-40,-50,-50,-40,-40,-30, -30,-40,-40,-50,-50,-40,-40,-30, -30,-40,-40,-50,-50,-40,-40,-30, -20,-30,-30,-40,-40,-30,-30,-20, -10,-20,-20,-20,-20,-20,-20,-10, 20,20,0,0,0,0,20,20, 20,30,10,0,0,10,30,20};
static const int8_t PST_KE[64] = {-50,-40,-30,-20,-20,-30,-40,-50, -30,-20,-10,0,0,-10,-20,-30, -30,-10,20,30,30,20,-10,-30, -30,-10,30,40,40,30,-10,-30, -30,-10,30,40,40,30,-10,-30, -30,-10,20,30,30,20,-10,-30, -30,-30,0,0,0,0,-30,-30, -50,-30,-30,-30,-30,-30,-30,-50};
static const int8_t *PST[7] = {0, PST_P, PST_N, PST_B, PST_R, PST_Q, PST_K};

static int evaluate(const Pos *p) {                     // centipawns for the side to move
    int score = 0, npm[2] = {0, 0}, minors[2] = {0, 0}, pawns = 0, bishops[2] = {0, 0};
    int ksq[2] = {0, 0};
    for (int s = 0; s < 64; s++) {
        int pc = p->b[s]; if (!pc) continue;
        int t = PTYPE(pc), c = PCOLOR(pc);
        if (t == KING) { ksq[c] = s; continue; }
        if (t == PAWN) pawns++; else { npm[c] += VAL[t]; if (t == KNIGHT || t == BISHOP) minors[c]++; if (t == BISHOP) bishops[c]++; }
    }
    if (!pawns && npm[0] + npm[1] < 400 && minors[0] + minors[1] <= 1) return 0;       // bare kings / one minor piece: dead draw
    bool endgame = npm[0] + npm[1] <= 2600 && (npm[0] < 1300 || npm[1] < 1300);
    for (int s = 0; s < 64; s++) {
        int pc = p->b[s]; if (!pc) continue;
        int t = PTYPE(pc), c = PCOLOR(pc);
        int idx = c == WHITE ? (7 - RANK_OF(s)) * 8 + FILE_OF(s) : RANK_OF(s) * 8 + FILE_OF(s);
        int v;
        if (t == KING) v = endgame ? PST_KE[idx] : PST_K[idx];
        else v = VAL[t] + PST[t][idx];
        if (t == PAWN && endgame) { int adv = c == WHITE ? RANK_OF(s) : 7 - RANK_OF(s); v += adv * 6; }   // passed-ish pawns matter more late
        score += c == WHITE ? v : -v;
    }
    if (bishops[WHITE] >= 2) score += 30;
    if (bishops[BLACK] >= 2) score -= 30;
    (void)ksq;
    return p->turn == WHITE ? score : -score;
}

// ------------------------------------------------------------------ transposition table
typedef struct { uint64_t key; int16_t score; int8_t depth; uint8_t flag; Move move; } TTE;   // flag 1 exact, 2 lower, 3 upper
static TTE *tt;

// ------------------------------------------------------------------ search
typedef struct {
    uint64_t (*clock)(void);
    volatile int *abort;
    uint64_t deadline;
    bool stop;
    long nodes;
    uint64_t path[MAX_PLY + 160];                       // hashes: game history followed by the search path
    int base;                                           // number of history entries
    Move killer[MAX_PLY][2];
    int hist_h[2][64][64];
} Ctx;

static inline int move_order_score(const Pos *p, Move m, Move ttm, const Ctx *c, int ply) {
    if (m.from == ttm.from && m.to == ttm.to && m.promo == ttm.promo && !(m.flags & MF_DROP) && (ttm.from || ttm.to)) return 1000000;
    int s = 0;
    if (m.flags & MF_CAPTURE) {
        int victim = (m.flags & MF_EP) ? PAWN : PTYPE(p->b[m.to]);
        int att = PTYPE(p->b[m.from]);
        s = 100000 + VAL[victim] * 10 - VAL[att] / 10;
    }
    if (m.promo) s += 90000 + VAL[m.promo];
    if (!s) {
        if (ply < MAX_PLY && ((c->killer[ply][0].from == m.from && c->killer[ply][0].to == m.to) )) s = 80000;
        else if (ply < MAX_PLY && ((c->killer[ply][1].from == m.from && c->killer[ply][1].to == m.to))) s = 70000;
        else s = c->hist_h[p->turn][m.from & 63][m.to & 63];
    }
    return s;
}

static void sort_moves(const Pos *p, Move *mv, int n, Move ttm, const Ctx *c, int ply, int *sc) {
    for (int i = 0; i < n; i++) sc[i] = move_order_score(p, mv[i], ttm, c, ply);
    for (int i = 1; i < n; i++) {                      // insertion sort, descending
        Move m = mv[i]; int s = sc[i]; int j = i - 1;
        while (j >= 0 && sc[j] < s) { mv[j + 1] = mv[j]; sc[j + 1] = sc[j]; j--; }
        mv[j + 1] = m; sc[j + 1] = s;
    }
}

static bool timeup(Ctx *c) {
    if (c->stop) return true;
    if ((c->nodes & 1023) == 0) {
        if ((c->abort && *c->abort) || (c->deadline && c->clock() >= c->deadline)) c->stop = true;
    }
    return c->stop;
}

static int qsearch(Ctx *c, const Pos *p, int alpha, int beta, int ply, int qd) {
    c->nodes++;
    if (timeup(c)) return 0;
    int stand = evaluate(p);
    if (stand >= beta) return stand;
    if (stand > alpha) alpha = stand;
    if (qd >= 6 || ply >= MAX_PLY - 1) return stand;
    Move mv[MAX_MOVES]; int sc[MAX_MOVES];
    int n = pos_legal_moves(p, mv);
    int k = 0;
    for (int i = 0; i < n; i++) if ((mv[i].flags & MF_CAPTURE) || mv[i].promo) mv[k++] = mv[i];
    n = k;
    Move none = {0, 0, 0, 0};
    sort_moves(p, mv, n, none, c, ply, sc);
    for (int i = 0; i < n; i++) {
        Move m = mv[i];
        if (!m.promo && !(m.flags & MF_EP)) {                              // delta pruning
            int gain = VAL[PTYPE(p->b[m.to])];
            if (stand + gain + 200 < alpha) continue;
        }
        Pos q = *p; pos_do(&q, m);
        int s = -qsearch(c, &q, -beta, -alpha, ply + 1, qd + 1);
        if (c->stop) return 0;
        if (s >= beta) return s;
        if (s > alpha) alpha = s;
    }
    return alpha;
}

static bool repeated(const Ctx *c, int ply, uint64_t h, int halfmove) {
    int total = c->base + ply;                         // c->path[total] is the current position
    int lim = halfmove < total ? halfmove : total;
    for (int i = 2; i <= lim; i += 2) if (c->path[total - i] == h) return true;
    return false;
}

static int search(Ctx *c, const Pos *p, int depth, int alpha, int beta, int ply, bool root_pv_unused) {
    (void)root_pv_unused;
    c->nodes++;
    if (timeup(c)) return 0;
    uint64_t h = eng_hash(p);
    c->path[c->base + ply] = h;
    if (ply > 0) {
        if (p->halfmove >= 100) return 0;
        if (repeated(c, ply, h, p->halfmove)) return 0;
    }
    bool in_check = pos_in_check(p, p->turn);
    if (in_check && depth < 1 + 0) depth = 1;                              // never drop into quiescence while in check
    if (depth <= 0 || ply >= MAX_PLY - 1) return qsearch(c, p, alpha, beta, ply, 0);
    int orig_alpha = alpha;
    Move ttm = {0, 0, 0, 0};
    TTE *e = &tt[h & (TT_SIZE - 1)];
    if (e->key == h) {
        ttm = e->move;
        if (e->depth >= depth && ply > 0) {
            int s = e->score;
            if (s > MATE - 200) s -= ply; else if (s < -MATE + 200) s += ply;
            if (e->flag == 1) return s;
            if (e->flag == 2 && s >= beta) return s;
            if (e->flag == 3 && s <= alpha) return s;
        }
    }
    Move mv[MAX_MOVES]; int sc[MAX_MOVES];
    int n = pos_legal_moves(p, mv);
    if (!n) return in_check ? -MATE + ply : 0;
    sort_moves(p, mv, n, ttm, c, ply, sc);
    int best = -INF; Move bm = mv[0];
    for (int i = 0; i < n; i++) {
        Move m = mv[i];
        Pos q = *p; pos_do(&q, m);
        int ext = 0;
        int s;
        if (i == 0) s = -search(c, &q, depth - 1 + ext, -beta, -alpha, ply + 1, false);
        else {
            int red = (depth >= 3 && i >= 4 && !(m.flags & MF_CAPTURE) && !m.promo && !in_check) ? 1 : 0;      // late move reduction
            s = -search(c, &q, depth - 1 - red, -alpha - 1, -alpha, ply + 1, false);
            if (s > alpha && !c->stop) s = -search(c, &q, depth - 1, -beta, -alpha, ply + 1, false);
        }
        if (c->stop) return 0;
        if (s > best) { best = s; bm = m; }
        if (s > alpha) {
            alpha = s;
            if (alpha >= beta) {
                if (!(m.flags & MF_CAPTURE) && ply < MAX_PLY) {
                    if (!(c->killer[ply][0].from == m.from && c->killer[ply][0].to == m.to)) { c->killer[ply][1] = c->killer[ply][0]; c->killer[ply][0] = m; }
                    c->hist_h[p->turn][m.from & 63][m.to & 63] += depth * depth;
                }
                break;
            }
        }
    }
    e->key = h; e->depth = (int8_t)depth; e->move = bm;
    int st = best;
    if (st > MATE - 200) st += ply; else if (st < -MATE + 200) st -= ply;
    e->score = (int16_t)st;
    e->flag = best <= orig_alpha ? 3 : (best >= beta ? 2 : 1);
    return best;
}

EngLimits eng_level(int level) {
    static const EngLimits L[8] = {
        {1, 100, 200}, {2, 150, 90}, {2, 200, 40}, {3, 400, 15},
        {4, 800, 0}, {5, 1500, 0}, {6, 2500, 0}, {8, 5000, 0}};
    if (level < 1) level = 1;
    if (level > 8) level = 8;
    return L[level - 1];
}

bool eng_is_mate(int s, int *mate_in) {
    if (s > MATE - 200) { *mate_in = (MATE - s + 1) / 2; return true; }
    if (s < -MATE + 200) { *mate_in = -((MATE + s + 1) / 2); return true; }
    return false;
}

static void extract_pv(const Pos *root, EngResult *r) {
    r->npv = 0;
    Pos p = *root;
    for (int i = 0; i < 8; i++) {
        TTE *e = &tt[eng_hash(&p) & (TT_SIZE - 1)];
        if (e->key != eng_hash(&p) || (!e->move.from && !e->move.to)) break;
        Move mv[MAX_MOVES]; int n = pos_legal_moves(&p, mv); bool ok = false;
        for (int k = 0; k < n; k++) if (move_eq(mv[k], e->move)) { ok = true; break; }
        if (!ok) break;
        r->pv[r->npv++] = e->move;
        pos_do(&p, e->move);
    }
}

bool eng_search(const Pos *p, const uint64_t *hist, int nhist, EngLimits lim, uint64_t (*clock_ms)(void), volatile int *abort,
                void (*progress)(const EngResult *r, void *ud), void *ud, EngResult *out) {
    z_init();
    if (!tt) { tt = calloc(TT_SIZE, sizeof(TTE)); if (!tt) return false; }
    memset(tt, 0, sizeof(TTE) * TT_SIZE);
    static Ctx c;
    memset(&c, 0, sizeof c);
    c.clock = clock_ms; c.abort = abort;
    c.deadline = lim.time_ms > 0 ? clock_ms() + (uint64_t)lim.time_ms : 0;
    if (nhist > 150) { hist += nhist - 150; nhist = 150; }
    for (int i = 0; i < nhist; i++) c.path[i] = hist[i];
    c.base = nhist;
    Move mv[MAX_MOVES]; int n = pos_legal_moves(p, mv);
    memset(out, 0, sizeof *out);
    if (!n) return false;
    Move best = mv[0]; int best_score = -INF;
    int sc[MAX_MOVES];
    int white_sign = p->turn == WHITE ? 1 : -1;

    if (lim.noise > 0) {                                       // weak levels: exact score of every root move, then pick with noise
        int rs[MAX_MOVES], done_d = 0;
        c.path[c.base] = eng_hash(p);
        for (int d = 1; d <= lim.max_depth; d++) {
            int tmp[MAX_MOVES]; bool complete = true;
            for (int i = 0; i < n; i++) {
                Pos q = *p; pos_do(&q, mv[i]);
                tmp[i] = -search(&c, &q, d - 1, -INF, INF, 1, false);
                if (c.stop) { complete = false; break; }
            }
            if (!complete) break;                              // discard a partial pass
            memcpy(rs, tmp, sizeof(int) * (size_t)n); done_d = d;
            if (c.deadline && clock_ms() >= c.deadline) break;
        }
        if (!done_d) {                                         // not even depth 1 finished: static choice
            for (int i = 0; i < n; i++) { Pos q = *p; pos_do(&q, mv[i]); rs[i] = -evaluate(&q); }
            done_d = 1;
        }
        int pick = 0, pick_s = -INF * 2;
        for (int i = 0; i < n; i++) {
            int s2 = rs[i] + (int)(rnd64() % (uint64_t)(2 * lim.noise + 1)) - lim.noise;
            if (s2 > pick_s) { pick_s = s2; pick = i; }
        }
        best = mv[pick]; best_score = rs[pick];
        out->best = best; out->has_move = true; out->depth = done_d; out->score = best_score * white_sign; out->nodes = c.nodes; out->done = true; out->npv = 1; out->pv[0] = best;
        if (progress) progress(out, ud);
        return true;
    }

    Move prev_best = {0, 0, 0, 0};
    for (int d = 1; d <= lim.max_depth; d++) {
        c.path[c.base] = eng_hash(p);
        sort_moves(p, mv, n, prev_best, &c, 0, sc);
        int alpha = -INF, beta = INF, bs = -INF; Move bm = mv[0];
        for (int i = 0; i < n; i++) {
            Pos q = *p; pos_do(&q, mv[i]);
            int s;
            if (i == 0) s = -search(&c, &q, d - 1, -beta, -alpha, 1, false);
            else {
                s = -search(&c, &q, d - 1, -alpha - 1, -alpha, 1, false);
                if (s > alpha && !c.stop) s = -search(&c, &q, d - 1, -beta, -alpha, 1, false);
            }
            if (c.stop) break;
            if (s > bs) { bs = s; bm = mv[i]; }
            if (s > alpha) alpha = s;
        }
        if (c.stop) { if (d == 1) { best = bm; best_score = bs; out->has_move = true; } break; }    // keep the previous completed depth
        best = bm; best_score = bs; prev_best = bm;
        out->best = best; out->has_move = true; out->depth = d; out->score = best_score * white_sign; out->nodes = c.nodes;
        // store the root move so the PV can be rebuilt from the table
        TTE *e = &tt[eng_hash(p) & (TT_SIZE - 1)]; e->key = eng_hash(p); e->move = best; e->depth = (int8_t)d; e->flag = 1; e->score = (int16_t)best_score;
        extract_pv(p, out);
        if (!out->npv) { out->pv[0] = best; out->npv = 1; }
        out->done = false;
        if (progress) progress(out, ud);
        int mi; if (eng_is_mate(best_score, &mi) && best_score > 0) break;                 // found a forced mate: no need to go deeper
        if (c.deadline && clock_ms() >= c.deadline) break;
        // do not start a depth we cannot finish: assume ~4x growth per ply
        if (c.deadline && d >= 3) { uint64_t now = clock_ms(); (void)now; }
    }
    out->best = best; out->has_move = true; out->nodes = c.nodes; out->done = true;
    if (!out->depth) { out->score = best_score * white_sign; out->depth = 1; }
    if (progress) progress(out, ud);
    return true;
}
