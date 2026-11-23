#include "premove.h"
#include <stdlib.h>
#include <string.h>

static const int8_t KN[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
static const int8_t KG[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
static inline bool on_board(int f, int r) { return f >= 0 && f < 8 && r >= 0 && r < 8; }
static inline uint64_t BIT(int s) { return 1ULL << s; }

bool pm_push(PreQueue *q, PreMove pm) {
    if (q->n >= PM_MAX) return false;
    q->q[q->n++] = pm;
    return true;
}
void pm_clear(PreQueue *q) { q->n = 0; }

static void apply_one(Pos *p, int color, PreMove pm) {
    if (pm.drop) {
        p->b[pm.to] = MKPC(color, pm.drop);
        if (p->pocket[color][pm.drop - 1]) p->pocket[color][pm.drop - 1]--;
        p->ep = NO_SQ;
        return;
    }
    uint8_t pc = p->b[pm.from];
    if (!pc) return;
    int t = PTYPE(pc);
    int rank = color == WHITE ? 0 : 7;
    if (t == KING && RANK_OF(pm.from) == rank && FILE_OF(pm.from) == p->king_file[color]) {
        // castling: king moves two files (or onto its own rook in Chess960)
        int side = -1;
        if (FILE_OF(pm.to) == 6 && RANK_OF(pm.to) == rank && (p->castle & (1 << (color * 2)))) side = 0;
        else if (FILE_OF(pm.to) == 2 && RANK_OF(pm.to) == rank && (p->castle & (1 << (color * 2 + 1)))) side = 1;
        if (p->b[pm.to] == MKPC(color, ROOK)) side = FILE_OF(pm.to) > FILE_OF(pm.from) ? 0 : 1;
        if (side >= 0 && (abs(FILE_OF(pm.to) - FILE_OF(pm.from)) >= 2 || p->b[pm.to] == MKPC(color, ROOK)) && pm.to != pm.from) {
            int rsq = SQ(p->rook_file[color * 2 + side], rank);
            p->b[pm.from] = 0; p->b[rsq] = 0;
            p->b[SQ(side == 0 ? 6 : 2, rank)] = MKPC(color, KING);
            p->b[SQ(side == 0 ? 5 : 3, rank)] = MKPC(color, ROOK);
            p->castle &= (uint8_t)~(3 << (2 * color));
            p->ep = NO_SQ;
            return;
        }
    }
    if (t == PAWN && FILE_OF(pm.from) != FILE_OF(pm.to) && !p->b[pm.to]) p->b[SQ(FILE_OF(pm.to), RANK_OF(pm.from))] = 0;   // blind en passant
    p->b[pm.from] = 0;
    p->b[pm.to] = pm.promo ? MKPC(color, pm.promo) : pc;
    if (t == KING) p->castle &= (uint8_t)~(3 << (2 * color));
    if (t == ROOK) for (int s = 0; s < 2; s++) if (RANK_OF(pm.from) == rank && FILE_OF(pm.from) == p->rook_file[color * 2 + s]) p->castle &= (uint8_t)~(1 << (color * 2 + s));
    p->ep = NO_SQ;
}

void pm_virtual(const Pos *real, const PreQueue *q, int color, Pos *virt) {
    *virt = *real;
    virt->turn = (uint8_t)color;
    for (int i = 0; i < q->n; i++) apply_one(virt, color, q->q[i]);
}

bool pm_is_promotion(const Pos *v, int color, int from, int to) {
    if (v->b[from] != MKPC(color, PAWN)) return false;
    if (v->variant == VAR_ANTICHESS || v->variant == VAR_HORDE || true) return RANK_OF(to) == (color == WHITE ? 7 : 0);
    return false;
}

uint64_t pm_dests(const Pos *p, int color, int from) {
    uint64_t out = 0;
    uint8_t pc = p->b[from];
    if (!pc || PCOLOR(pc) != color) return 0;
    int t = PTYPE(pc), f = FILE_OF(from), r = RANK_OF(from);
    int dir = color == WHITE ? 1 : -1;
#define OWN(s) (p->b[s] && PCOLOR(p->b[s]) == color)
    if (t == PAWN) {
        int r1 = r + dir;
        if (!on_board(f, r1)) return 0;
        if (!OWN(SQ(f, r1))) out |= BIT(SQ(f, r1));              // (straight moves are never onto our own pieces)
        bool start = color == WHITE ? (r == 1 || (p->variant == VAR_HORDE && r == 0)) : (r == 6);
        if (start && !OWN(SQ(f, r1)) && on_board(f, r + 2 * dir) && !OWN(SQ(f, r + 2 * dir))) out |= BIT(SQ(f, r + 2 * dir));
        for (int df = -1; df <= 1; df += 2)
            if (on_board(f + df, r1)) out |= BIT(SQ(f + df, r1));      // may land on our own piece: it can be captured first (recapture)
    } else if (t == KNIGHT || t == KING) {
        for (int i = 0; i < 8; i++) {
            int nf = f + (t == KNIGHT ? KN[i][0] : KG[i][0]), nr = r + (t == KNIGHT ? KN[i][1] : KG[i][1]);
            if (on_board(nf, nr)) out |= BIT(SQ(nf, nr));              // own-occupied squares allowed: a recapture premove
        }
        if (t == KING && p->variant != VAR_ANTICHESS && p->variant != VAR_RACING) {
            int rank = color == WHITE ? 0 : 7;
            if (r == rank && f == p->king_file[color]) {
                for (int side = 0; side < 2; side++) {
                    if (!(p->castle & (1 << (color * 2 + side)))) continue;
                    int rf = p->rook_file[color * 2 + side];
                    if (p->b[SQ(rf, rank)] != MKPC(color, ROOK)) continue;
                    int kd = side == 0 ? 6 : 2, rd = side == 0 ? 5 : 3;
                    int lo = f, hi = f, vals[3] = {rf, kd, rd};
                    for (int i = 0; i < 3; i++) { if (vals[i] < lo) lo = vals[i]; if (vals[i] > hi) hi = vals[i]; }
                    bool clear = true;
                    for (int ff = lo; ff <= hi; ff++) { int s = SQ(ff, rank); if (s != from && s != SQ(rf, rank) && p->b[s]) clear = false; }
                    if (!clear) continue;
                    out |= BIT(SQ(kd, rank));
                    if (p->variant == VAR_CHESS960) out |= BIT(SQ(rf, rank));
                }
            }
        }
    } else {
        for (int d = 0; d < 8; d++) {
            bool diag = d & 1;
            if (t == BISHOP && !diag) continue;
            if (t == ROOK && diag) continue;
            int nf = f + KG[d][0], nr = r + KG[d][1];
            while (on_board(nf, nr)) {
                int s = SQ(nf, nr);
                out |= BIT(s);
                if (OWN(s)) break;                                      // can recapture on a square we hold, but not slide through it
                nf += KG[d][0]; nr += KG[d][1];      // enemy pieces may move away: premoves look through them
            }
        }
    }
#undef OWN
    return out;
}

uint64_t pm_drop_dests(const Pos *p, int color, int type) {
    uint64_t out = 0;
    (void)color;
    for (int s = 0; s < 64; s++) {
        if (p->b[s]) continue;
        if (type == PAWN && (RANK_OF(s) == 0 || RANK_OF(s) == 7)) continue;
        out |= BIT(s);
    }
    return out;
}

bool pm_execute(const Pos *real, PreMove pm, Move *out) {
    Move mv[MAX_MOVES]; int n = pos_legal_moves(real, mv);
    int color = real->turn;
    if (pm.drop) {
        for (int i = 0; i < n; i++) if ((mv[i].flags & MF_DROP) && mv[i].from == pm.drop && mv[i].to == pm.to) { *out = mv[i]; return true; }
        return false;
    }
    int promo = pm.promo;
    if (!promo && real->b[pm.from] == MKPC(color, PAWN) && RANK_OF(pm.to) == (color == WHITE ? 7 : 0)) promo = QUEEN;
    for (int i = 0; i < n; i++)
        if (!(mv[i].flags & (MF_DROP | MF_CASTLE)) && mv[i].from == pm.from && mv[i].to == pm.to && mv[i].promo == promo) { *out = mv[i]; return true; }
    for (int i = 0; i < n; i++)
        if ((mv[i].flags & MF_CASTLE) && mv[i].from == pm.from && (mv[i].to == pm.to || castle_rook_sq(real, mv[i]) == pm.to)) { *out = mv[i]; return true; }
    return false;
}
