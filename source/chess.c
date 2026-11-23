// Chess rules for all Lichess variants. Portable C11, no dependencies.
#include "chess.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int8_t KN[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
static const int8_t KG[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};  // odd = diagonal

static inline bool on_board(int f, int r) { return f >= 0 && f < 8 && r >= 0 && r < 8; }
static inline uint64_t BIT(int sq) { return 1ULL << sq; }

// ------------------------------------------------------------------ names
static const char *VAR_KEYS[VAR_COUNT] = {"standard", "chess960", "kingOfTheHill", "threeCheck", "antichess",
                                          "atomic", "horde", "racingKings", "crazyhouse"};
static const char *VAR_NAMES[VAR_COUNT] = {"Standard", "Chess960", "King of the Hill", "Three-check", "Antichess",
                                           "Atomic", "Horde", "Racing Kings", "Crazyhouse"};
const char *variant_key(Variant v) { return VAR_KEYS[v]; }
const char *variant_name(Variant v) { return VAR_NAMES[v]; }
bool variant_from_key(const char *key, Variant *out) {
    for (int i = 0; i < VAR_COUNT; i++)
        if (!strcmp(key, VAR_KEYS[i])) { *out = (Variant)i; return true; }
    if (!strcmp(key, "fromPosition")) { *out = VAR_STANDARD; return true; }
    return false;
}

int sq_from_name(const char *s) {
    if (!s || s[0] < 'a' || s[0] > 'h' || s[1] < '1' || s[1] > '8') return -1;
    return SQ(s[0] - 'a', s[1] - '1');
}
void sq_name(int sq, char *out) {
    out[0] = (char)('a' + FILE_OF(sq)); out[1] = (char)('1' + RANK_OF(sq)); out[2] = 0;
}
int material_value(int t) {
    static const int v[7] = {0, 1, 3, 3, 5, 9, 0};
    return v[t & 7];
}
bool move_eq(Move a, Move b) { return a.from == b.from && a.to == b.to && a.promo == b.promo && a.flags == b.flags; }

int pos_king_sq(const Pos *p, int color) {
    uint8_t k = MKPC(color, KING);
    for (int i = 0; i < 64; i++) if (p->b[i] == k) return i;
    return NO_SQ;
}

// ------------------------------------------------------------------ attacks
bool sq_attacked(const Pos *p, int sq, int by) {
    int f = FILE_OF(sq), r = RANK_OF(sq);
    int pr = r + (by == WHITE ? -1 : 1);
    if (pr >= 0 && pr < 8) {
        if (f > 0 && p->b[SQ(f - 1, pr)] == MKPC(by, PAWN)) return true;
        if (f < 7 && p->b[SQ(f + 1, pr)] == MKPC(by, PAWN)) return true;
    }
    for (int i = 0; i < 8; i++) {
        int nf = f + KN[i][0], nr = r + KN[i][1];
        if (on_board(nf, nr) && p->b[SQ(nf, nr)] == MKPC(by, KNIGHT)) return true;
        nf = f + KG[i][0]; nr = r + KG[i][1];
        if (on_board(nf, nr) && p->b[SQ(nf, nr)] == MKPC(by, KING)) return true;
    }
    for (int d = 0; d < 8; d++) {
        int nf = f + KG[d][0], nr = r + KG[d][1];
        while (on_board(nf, nr)) {
            uint8_t pc = p->b[SQ(nf, nr)];
            if (pc) {
                if (PCOLOR(pc) == by) {
                    int t = PTYPE(pc);
                    if (t == QUEEN || ((d & 1) ? t == BISHOP : t == ROOK)) return true;
                }
                break;
            }
            nf += KG[d][0]; nr += KG[d][1];
        }
    }
    return false;
}

static bool kings_adjacent(const Pos *p) {
    int a = pos_king_sq(p, WHITE), b = pos_king_sq(p, BLACK);
    if (a == NO_SQ || b == NO_SQ) return false;
    int df = FILE_OF(a) - FILE_OF(b), dr = RANK_OF(a) - RANK_OF(b);
    return df >= -1 && df <= 1 && dr >= -1 && dr <= 1;
}

bool pos_in_check(const Pos *p, int color) {
    if (p->variant == VAR_ANTICHESS) return false;
    int k = pos_king_sq(p, color);
    if (k == NO_SQ) return false;
    if (p->variant == VAR_ATOMIC && kings_adjacent(p)) return false;
    return sq_attacked(p, k, color ^ 1);
}

// ------------------------------------------------------------------ generation
static inline void add(Move *out, int *n, int from, int to, int promo, int flags) {
    out[*n].from = (uint8_t)from; out[*n].to = (uint8_t)to; out[*n].promo = (uint8_t)promo; out[*n].flags = (uint8_t)flags;
    (*n)++;
}

static void add_pawn(const Pos *p, Move *out, int *n, int from, int to, int flags) {
    int last = (p->turn == WHITE) ? 7 : 0;
    if (RANK_OF(to) == last) {
        static const uint8_t pr[5] = {QUEEN, ROOK, BISHOP, KNIGHT, KING};
        int cnt = (p->variant == VAR_ANTICHESS) ? 5 : 4;
        for (int i = 0; i < cnt; i++) add(out, n, from, to, pr[i], flags);
    } else add(out, n, from, to, 0, flags);
}

static int gen_pseudo(const Pos *p, Move *out, int only_sq) {
    int n = 0, us = p->turn, them = us ^ 1;
    int dir = (us == WHITE) ? 1 : -1;
    bool atomic = p->variant == VAR_ATOMIC;
    for (int s = 0; s < 64; s++) {
        uint8_t pc = p->b[s];
        if (!pc || PCOLOR(pc) != us) continue;
        if (only_sq >= 0 && s != only_sq) continue;
        int t = PTYPE(pc), f = FILE_OF(s), r = RANK_OF(s);
        if (t == PAWN) {
            int r1 = r + dir;
            if (r1 < 0 || r1 > 7) continue;
            if (!p->b[SQ(f, r1)]) {
                add_pawn(p, out, &n, s, SQ(f, r1), 0);
                bool start = (us == WHITE) ? (r == 1 || (p->variant == VAR_HORDE && r == 0)) : (r == 6);
                if (start && !p->b[SQ(f, r + 2 * dir)])
                    add(out, &n, s, SQ(f, r + 2 * dir), 0, MF_DOUBLE);
            }
            for (int df = -1; df <= 1; df += 2) {
                int nf = f + df;
                if (nf < 0 || nf > 7) continue;
                int to = SQ(nf, r1);
                uint8_t tp = p->b[to];
                if (tp && PCOLOR(tp) == them) add_pawn(p, out, &n, s, to, MF_CAPTURE);
                else if (!tp && to == p->ep) add(out, &n, s, to, 0, MF_CAPTURE | MF_EP);
            }
        } else if (t == KNIGHT || t == KING) {
            const int8_t(*tab)[2] = (t == KNIGHT) ? KN : KG;
            for (int i = 0; i < 8; i++) {
                int nf = f + tab[i][0], nr = r + tab[i][1];
                if (!on_board(nf, nr)) continue;
                int to = SQ(nf, nr);
                uint8_t tp = p->b[to];
                if (!tp) add(out, &n, s, to, 0, 0);
                else if (PCOLOR(tp) == them && !(atomic && t == KING)) add(out, &n, s, to, 0, MF_CAPTURE);
            }
        } else {
            for (int d = 0; d < 8; d++) {
                bool diag = d & 1;
                if (t == BISHOP && !diag) continue;
                if (t == ROOK && diag) continue;
                int nf = f + KG[d][0], nr = r + KG[d][1];
                while (on_board(nf, nr)) {
                    int to = SQ(nf, nr);
                    uint8_t tp = p->b[to];
                    if (!tp) add(out, &n, s, to, 0, 0);
                    else {
                        if (PCOLOR(tp) == them) add(out, &n, s, to, 0, MF_CAPTURE);
                        break;
                    }
                    nf += KG[d][0]; nr += KG[d][1];
                }
            }
        }
    }
    // castling (generic, Chess960 compatible)
    bool can_castle = p->variant != VAR_ANTICHESS && p->variant != VAR_RACING;
    if (can_castle && p->castle) {
        int rank = (us == WHITE) ? 0 : 7;
        for (int side = 0; side < 2; side++) {   // 0 = king side, 1 = queen side
            int idx = us * 2 + side;
            if (!(p->castle & (1 << idx))) continue;
            int kf = p->king_file[us], rf = p->rook_file[idx];
            int ksq = SQ(kf, rank), rsq = SQ(rf, rank);
            if (only_sq >= 0 && only_sq != ksq) continue;
            if (p->b[ksq] != MKPC(us, KING) || p->b[rsq] != MKPC(us, ROOK)) continue;
            int kd = side == 0 ? 6 : 2, rd = side == 0 ? 5 : 3;
            int lo = kf, hi = kf;
            int vals[3] = {rf, kd, rd};
            for (int i = 0; i < 3; i++) { if (vals[i] < lo) lo = vals[i]; if (vals[i] > hi) hi = vals[i]; }
            bool ok = true;
            for (int ff = lo; ff <= hi && ok; ff++) {
                int sq = SQ(ff, rank);
                if (sq != ksq && sq != rsq && p->b[sq]) ok = false;
            }
            if (!ok) continue;
            int a = kf < kd ? kf : kd, b2 = kf < kd ? kd : kf;
            for (int ff = a; ff <= b2 && ok; ff++)
                if (sq_attacked(p, SQ(ff, rank), them)) ok = false;
            if (!ok) continue;
            add(out, &n, ksq, SQ(kd, rank), 0, MF_CASTLE);
        }
    }
    // crazyhouse drops
    if (p->variant == VAR_CRAZYHOUSE && only_sq < 0) {
        for (int t = PAWN; t <= QUEEN; t++) {
            if (!p->pocket[us][t - 1]) continue;
            for (int s = 0; s < 64; s++) {
                if (p->b[s]) continue;
                if (t == PAWN && (RANK_OF(s) == 0 || RANK_OF(s) == 7)) continue;
                add(out, &n, t, s, 0, MF_DROP);
            }
        }
    }
    return n;
}

// ------------------------------------------------------------------ make move
static void fix_rights(Pos *p) {
    for (int c = 0; c < 2; c++) {
        int rank = c == WHITE ? 0 : 7;
        bool king_home = p->b[SQ(p->king_file[c], rank)] == MKPC(c, KING);
        for (int side = 0; side < 2; side++) {
            int idx = c * 2 + side;
            if (!(p->castle & (1 << idx))) continue;
            if (!king_home || p->b[SQ(p->rook_file[idx], rank)] != MKPC(c, ROOK)) p->castle &= (uint8_t)~(1 << idx);
        }
    }
}

int castle_rook_sq(const Pos *p, Move m) {
    int side = (FILE_OF(m.to) == 6) ? 0 : 1;
    int rank = RANK_OF(m.from);
    return SQ(p->rook_file[p->turn * 2 + side], rank);
}

void pos_do(Pos *p, Move m) {
    int us = p->turn, them = us ^ 1;
    int newep = NO_SQ;
    bool capture = (m.flags & MF_CAPTURE) != 0;
    bool pawn_move = false;
    if (m.flags & MF_DROP) {
        p->b[m.to] = MKPC(us, m.from);
        p->pocket[us][m.from - 1]--;
        pawn_move = m.from == PAWN;
    } else if (m.flags & MF_CASTLE) {
        int rsq = castle_rook_sq(p, m);
        int rank = RANK_OF(m.from);
        int side = (FILE_OF(m.to) == 6) ? 0 : 1;
        int rd = SQ(side == 0 ? 5 : 3, rank);
        p->b[m.from] = 0; p->b[rsq] = 0;
        p->b[m.to] = MKPC(us, KING); p->b[rd] = MKPC(us, ROOK);
        p->castle &= (uint8_t)~(3 << (2 * us));
    } else {
        uint8_t pc = p->b[m.from];
        uint8_t cap = p->b[m.to];
        int capsq = m.to;
        if (m.flags & MF_EP) { capsq = SQ(FILE_OF(m.to), RANK_OF(m.from)); cap = p->b[capsq]; p->b[capsq] = 0; }
        if (p->variant == VAR_CRAZYHOUSE && cap) {
            int t = (p->promoted & BIT(capsq)) ? PAWN : PTYPE(cap);
            p->pocket[us][t - 1]++;
        }
        p->promoted &= ~BIT(capsq);
        bool was_promoted = (p->promoted & BIT(m.from)) != 0;
        p->promoted &= ~BIT(m.from);
        p->b[m.from] = 0;
        p->b[m.to] = m.promo ? MKPC(us, m.promo) : pc;
        if (p->variant == VAR_CRAZYHOUSE && (m.promo || was_promoted)) p->promoted |= BIT(m.to);
        pawn_move = PTYPE(pc) == PAWN;
        if (pawn_move && (m.flags & MF_DOUBLE) && RANK_OF(m.from) != 0 && RANK_OF(m.from) != 7)
            newep = SQ(FILE_OF(m.from), (RANK_OF(m.from) + RANK_OF(m.to)) / 2);
        if (p->variant == VAR_ATOMIC && capture) {
            // explosion centred on the destination: capturer and every adjacent non-pawn disappear
            p->b[m.to] = 0;
            p->promoted &= ~BIT(m.to);
            int f = FILE_OF(m.to), r = RANK_OF(m.to);
            for (int df = -1; df <= 1; df++)
                for (int dr = -1; dr <= 1; dr++) {
                    if (!df && !dr) continue;
                    if (!on_board(f + df, r + dr)) continue;
                    int s = SQ(f + df, r + dr);
                    if (p->b[s] && PTYPE(p->b[s]) != PAWN) p->b[s] = 0;
                }
        }
    }
    fix_rights(p);
    p->ep = (uint8_t)newep;
    p->halfmove = (capture || pawn_move) ? 0 : (uint16_t)(p->halfmove + 1);
    if (us == BLACK) p->fullmove++;
    p->turn = (uint8_t)them;
    if (p->variant == VAR_3CHECK && pos_in_check(p, them)) p->checks[us]++;
}

static bool legal_after(const Pos *t, int mover) {
    switch (t->variant) {
    case VAR_ANTICHESS: return true;
    case VAR_ATOMIC: {
        int own = pos_king_sq(t, mover), opp = pos_king_sq(t, mover ^ 1);
        if (own == NO_SQ) return false;
        if (opp == NO_SQ) return true;
        return !pos_in_check(t, mover);
    }
    case VAR_RACING: return !pos_in_check(t, WHITE) && !pos_in_check(t, BLACK);
    default: return !pos_in_check(t, mover);
    }
}

static int filter_legal(const Pos *p, const Move *ps, int n, Move *out) {
    int cnt = 0;
    bool anti = p->variant == VAR_ANTICHESS;
    bool must_capture = false;
    if (anti) for (int i = 0; i < n; i++) if (ps[i].flags & MF_CAPTURE) { must_capture = true; break; }
    for (int i = 0; i < n; i++) {
        if (must_capture && !(ps[i].flags & MF_CAPTURE)) continue;
        Pos t = *p;
        pos_do(&t, ps[i]);
        if (legal_after(&t, p->turn)) out[cnt++] = ps[i];
    }
    return cnt;
}

int pos_legal_moves(const Pos *p, Move *out) {
    Move ps[MAX_MOVES];
    int n = gen_pseudo(p, ps, -1);
    return filter_legal(p, ps, n, out);
}

int pos_moves_from(const Pos *p, int sq, Move *out) {
    // antichess needs the global capture rule, so filter from the full list there
    if (p->variant == VAR_ANTICHESS) {
        Move all[MAX_MOVES]; int n = pos_legal_moves(p, all), c = 0;
        for (int i = 0; i < n; i++) if (!(all[i].flags & MF_DROP) && all[i].from == sq) out[c++] = all[i];
        return c;
    }
    Move ps[MAX_MOVES];
    int n = gen_pseudo(p, ps, sq);
    return filter_legal(p, ps, n, out);
}

int pos_drops(const Pos *p, int type, Move *out) {
    Move all[MAX_MOVES]; int n = pos_legal_moves(p, all), c = 0;
    for (int i = 0; i < n; i++) if ((all[i].flags & MF_DROP) && all[i].from == type) out[c++] = all[i];
    return c;
}

// ------------------------------------------------------------------ outcome
static bool in_center(int sq) { return (sq == 27 || sq == 28 || sq == 35 || sq == 36); }

OutKind pos_outcome(const Pos *p, int *winner) {
    int w = -1;
    if (winner) *winner = -1;
    switch (p->variant) {
    case VAR_KOTH:
        for (int c = 0; c < 2; c++) { int k = pos_king_sq(p, c); if (k != NO_SQ && in_center(k)) { if (winner) *winner = c; return OUT_VARIANT_WIN; } }
        break;
    case VAR_3CHECK:
        for (int c = 0; c < 2; c++) if (p->checks[c] >= 3) { if (winner) *winner = c; return OUT_VARIANT_WIN; }
        break;
    case VAR_ATOMIC:
        for (int c = 0; c < 2; c++) if (pos_king_sq(p, c) == NO_SQ) { if (winner) *winner = c ^ 1; return OUT_VARIANT_WIN; }
        break;
    case VAR_HORDE: {
        int cnt = 0;
        for (int i = 0; i < 64; i++) if (p->b[i] && PCOLOR(p->b[i]) == WHITE) cnt++;
        if (!cnt) { if (winner) *winner = BLACK; return OUT_VARIANT_WIN; }
        break;
    }
    case VAR_RACING: {
        int wk = pos_king_sq(p, WHITE), bk = pos_king_sq(p, BLACK);
        bool w8 = wk != NO_SQ && RANK_OF(wk) == 7, b8 = bk != NO_SQ && RANK_OF(bk) == 7;
        if (w8 && b8) return OUT_DRAW_MATERIAL;
        if (b8) { if (winner) *winner = BLACK; return OUT_VARIANT_WIN; }
        if (w8) {
            if (p->turn == WHITE) { if (winner) *winner = WHITE; return OUT_VARIANT_WIN; }
            Move mv[MAX_MOVES]; int n = pos_legal_moves(p, mv);
            for (int i = 0; i < n; i++)
                if (p->b[mv[i].from] == MKPC(BLACK, KING) && RANK_OF(mv[i].to) == 7) return OUT_ONGOING;
            if (winner) *winner = WHITE;
            return OUT_VARIANT_WIN;
        }
        break;
    }
    default: break;
    }
    Move mv[MAX_MOVES];
    int n = pos_legal_moves(p, mv);
    if (n == 0) {
        if (p->variant == VAR_ANTICHESS) { if (winner) *winner = p->turn; return OUT_VARIANT_WIN; }
        if (pos_in_check(p, p->turn)) { if (winner) *winner = p->turn ^ 1; return OUT_CHECKMATE; }
        return OUT_STALEMATE;
    }
    if (p->variant == VAR_STANDARD || p->variant == VAR_CHESS960) {
        int minor = 0, other = 0;
        for (int i = 0; i < 64; i++) {
            int t = PTYPE(p->b[i]);
            if (!t || t == KING) continue;
            if (t == KNIGHT || t == BISHOP) minor++; else other++;
        }
        if (!other && minor <= 1) return OUT_DRAW_MATERIAL;
    }
    (void)w;
    return OUT_ONGOING;
}

// ------------------------------------------------------------------ FEN
static const char START_FEN[] = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
static const char HORDE_FEN[] = "rnbqkbnr/pppppppp/8/1PP2PP1/PPPPPPPP/PPPPPPPP/PPPPPPPP/PPPPPPPP w kq - 0 1";
static const char RACING_FEN[] = "8/8/8/8/8/8/krbnNBRK/qrbnNBRQ w - - 0 1";

void pos_init(Pos *p, Variant v) {
    const char *f = START_FEN;
    if (v == VAR_HORDE) f = HORDE_FEN;
    else if (v == VAR_RACING) f = RACING_FEN;
    pos_from_fen(p, f, v);
}

static int piece_from_char(char c) {
    const char *s = strchr("pnbrqk", tolower((unsigned char)c));
    if (!s || !c) return 0;
    return MKPC(isupper((unsigned char)c) ? WHITE : BLACK, (int)(s - "pnbrqk") + 1);
}

bool pos_from_fen(Pos *p, const char *fen, Variant v) {
    memset(p, 0, sizeof *p);
    p->variant = (uint8_t)v;
    p->ep = NO_SQ;
    p->fullmove = 1;
    for (int c = 0; c < 2; c++) p->king_file[c] = 4;
    p->rook_file[0] = 7; p->rook_file[1] = 0; p->rook_file[2] = 7; p->rook_file[3] = 0;
    if (!fen || !*fen) return false;
    if (!strncmp(fen, "startpos", 8)) { pos_init(p, v); return true; }
    const char *s = fen;
    int f = 0, r = 7;
    bool in_pocket = false;
    for (; *s && *s != ' '; s++) {
        char c = *s;
        if (in_pocket) {
            if (c == ']') in_pocket = false;
            else { int pc = piece_from_char(c); if (pc && PTYPE(pc) != KING) p->pocket[PCOLOR(pc)][PTYPE(pc) - 1]++; }
            continue;
        }
        if (c == '[') { in_pocket = true; continue; }
        if (c == '/') {
            if (r == 0 && v == VAR_CRAZYHOUSE) { in_pocket = true; continue; }   // pocket as 9th "rank": .../RNBQKBNR/Qq
            f = 0; r--; if (r < 0) { /* pocket follows */ r = 0; in_pocket = (v == VAR_CRAZYHOUSE); } continue;
        }
        if (c == '~') { if (f > 0 && r >= 0) p->promoted |= BIT(SQ(f - 1, r)); continue; }
        if (c >= '1' && c <= '8') { f += c - '0'; continue; }
        int pc = piece_from_char(c);
        if (!pc || f > 7 || r < 0) return false;
        p->b[SQ(f, r)] = (uint8_t)pc;
        f++;
    }
    while (*s == ' ') s++;
    p->turn = (*s == 'b') ? BLACK : WHITE;
    while (*s && *s != ' ') s++;
    while (*s == ' ') s++;
    // castling
    int wk = pos_king_sq(p, WHITE), bk = pos_king_sq(p, BLACK);
    if (wk != NO_SQ && RANK_OF(wk) == 0) p->king_file[0] = (uint8_t)FILE_OF(wk);
    if (bk != NO_SQ && RANK_OF(bk) == 7) p->king_file[1] = (uint8_t)FILE_OF(bk);
    for (; *s && *s != ' '; s++) {
        char c = *s;
        if (c == '-') continue;
        int col = isupper((unsigned char)c) ? WHITE : BLACK;
        int rank = col == WHITE ? 0 : 7;
        int kf = p->king_file[col];
        char lc = (char)tolower((unsigned char)c);
        int side, file = -1;
        if (lc == 'k') {
            side = 0;
            for (int ff = 7; ff > kf; ff--) if (p->b[SQ(ff, rank)] == MKPC(col, ROOK)) { file = ff; break; }
        } else if (lc == 'q') {
            side = 1;
            for (int ff = 0; ff < kf; ff++) if (p->b[SQ(ff, rank)] == MKPC(col, ROOK)) { file = ff; break; }
        } else if (lc >= 'a' && lc <= 'h') {
            file = lc - 'a'; side = file > kf ? 0 : 1;
        } else continue;
        if (file < 0) continue;
        p->rook_file[col * 2 + side] = (uint8_t)file;
        p->castle |= (uint8_t)(1 << (col * 2 + side));
    }
    while (*s == ' ') s++;
    if (*s && *s != '-') { p->ep = (uint8_t)sq_from_name(s); if ((int8_t)p->ep < 0) p->ep = NO_SQ; }
    while (*s && *s != ' ') s++;
    while (*s == ' ') s++;
    // optional three-check "3+3" (remaining) before the clocks
    if (isdigit((unsigned char)s[0]) && s[1] == '+' && isdigit((unsigned char)s[2])) {
        p->checks[WHITE] = (uint8_t)(3 - (s[2] - '0'));   // black's remaining count belongs to checks white gave
        p->checks[BLACK] = (uint8_t)(3 - (s[0] - '0'));
        s += 3; while (*s == ' ') s++;
    }
    if (isdigit((unsigned char)*s)) { p->halfmove = (uint16_t)atoi(s); while (*s && *s != ' ') s++; while (*s == ' ') s++; }
    if (isdigit((unsigned char)*s)) { p->fullmove = (uint16_t)atoi(s); while (*s && *s != ' ') s++; while (*s == ' ') s++; }
    if (s[0] == '+' && isdigit((unsigned char)s[1]) && s[2] == '+' && isdigit((unsigned char)s[3])) {
        p->checks[WHITE] = (uint8_t)(s[1] - '0'); p->checks[BLACK] = (uint8_t)(s[3] - '0');
    }
    if (!p->fullmove) p->fullmove = 1;
    if (v == VAR_ANTICHESS || v == VAR_RACING) p->castle = 0;   // no castling in these variants
    fix_rights(p);
    return true;
}

void pos_to_fen(const Pos *p, char *out, size_t n) {
    char buf[200]; int k = 0;
    for (int r = 7; r >= 0; r--) {
        int empty = 0;
        for (int f = 0; f < 8; f++) {
            uint8_t pc = p->b[SQ(f, r)];
            if (!pc) { empty++; continue; }
            if (empty) { buf[k++] = (char)('0' + empty); empty = 0; }
            char c = "pnbrqk"[PTYPE(pc) - 1];
            buf[k++] = PCOLOR(pc) == WHITE ? (char)toupper(c) : c;
            if (p->promoted & BIT(SQ(f, r))) buf[k++] = '~';
        }
        if (empty) buf[k++] = (char)('0' + empty);
        if (r) buf[k++] = '/';
    }
    if (p->variant == VAR_CRAZYHOUSE) {
        buf[k++] = '[';
        for (int c = 0; c < 2; c++)
            for (int t = QUEEN; t >= PAWN; t--)
                for (int i = 0; i < p->pocket[c][t - 1]; i++) { char ch = "pnbrqk"[t - 1]; buf[k++] = c == WHITE ? (char)toupper(ch) : ch; }
        buf[k++] = ']';
    }
    buf[k++] = ' '; buf[k++] = p->turn == WHITE ? 'w' : 'b'; buf[k++] = ' ';
    int cs = k;
    for (int c = 0; c < 2; c++) {
        int rank = c == WHITE ? 0 : 7;
        for (int side = 0; side < 2; side++) {
            if (!(p->castle & (1 << (c * 2 + side)))) continue;
            int rf = p->rook_file[c * 2 + side], kf = p->king_file[c];
            bool outer = true;   // is it the outermost rook on that side? then K/Q suffices
            if (side == 0) { for (int ff = rf + 1; ff < 8; ff++) if (p->b[SQ(ff, rank)] == MKPC(c, ROOK)) outer = false; }
            else { for (int ff = 0; ff < rf; ff++) if (p->b[SQ(ff, rank)] == MKPC(c, ROOK)) outer = false; }
            (void)kf;
            char ch = outer ? (side == 0 ? 'k' : 'q') : (char)('a' + rf);
            buf[k++] = c == WHITE ? (char)toupper(ch) : ch;
        }
    }
    if (k == cs) buf[k++] = '-';
    buf[k++] = ' ';
    // en passant square only when a capture is actually possible (Lichess convention)
    bool ep_ok = false;
    if (p->ep != NO_SQ) {
        Move mv[MAX_MOVES]; int c = pos_legal_moves(p, mv);
        for (int i = 0; i < c; i++) if (mv[i].flags & MF_EP) ep_ok = true;
    }
    if (ep_ok) { char nm[3]; sq_name(p->ep, nm); buf[k++] = nm[0]; buf[k++] = nm[1]; } else buf[k++] = '-';
    buf[k] = 0;
    if (p->variant == VAR_3CHECK) k += snprintf(buf + k, sizeof buf - (size_t)k, " %d+%d", 3 - p->checks[BLACK], 3 - p->checks[WHITE]);
    snprintf(buf + k, sizeof buf - (size_t)k, " %d %d", p->halfmove, p->fullmove);
    snprintf(out, n, "%s", buf);
}

// ------------------------------------------------------------------ notation
void move_to_uci(const Pos *p, Move m, char *out) {
    char a[3], b[3];
    if (m.flags & MF_DROP) { snprintf(out, 8, "%c@%s", "PNBRQK"[m.from - 1], (sq_name(m.to, b), b)); return; }
    sq_name(m.from, a);
    if ((m.flags & MF_CASTLE) && p->variant == VAR_CHESS960) sq_name(castle_rook_sq(p, m), b);
    else sq_name(m.to, b);
    int n = snprintf(out, 8, "%s%s", a, b);
    if (m.promo) { out[n++] = "pnbrqk"[m.promo - 1]; out[n] = 0; }
}

bool pos_parse_uci(const Pos *p, const char *u, Move *out) {
    size_t len = strlen(u);
    if (len < 4) return false;
    Move mv[MAX_MOVES]; int n = pos_legal_moves(p, mv);
    if (u[1] == '@') {
        int t = piece_from_char(u[0]); int to = sq_from_name(u + 2);
        if (!t || to < 0) return false;
        for (int i = 0; i < n; i++) if ((mv[i].flags & MF_DROP) && mv[i].from == PTYPE(t) && mv[i].to == to) { *out = mv[i]; return true; }
        return false;
    }
    int from = sq_from_name(u), to = sq_from_name(u + 2);
    if (from < 0 || to < 0) return false;
    int promo = 0;
    if (len >= 5) { int pc = piece_from_char(u[4]); promo = PTYPE(pc); }
    for (int i = 0; i < n; i++)   // ordinary moves first
        if (!(mv[i].flags & (MF_DROP | MF_CASTLE)) && mv[i].from == from && mv[i].to == to && mv[i].promo == promo) { *out = mv[i]; return true; }
    for (int i = 0; i < n; i++)   // then castling: king destination or "king takes rook"
        if ((mv[i].flags & MF_CASTLE) && mv[i].from == from && (mv[i].to == to || castle_rook_sq(p, mv[i]) == to)) { *out = mv[i]; return true; }
    return false;
}

void move_to_san(const Pos *p, Move m, char *out, size_t n) {
    char buf[32]; int k = 0;
    char dst[3]; sq_name(m.to, dst);
    if (m.flags & MF_DROP) {
        if (m.from != PAWN) buf[k++] = "PNBRQK"[m.from - 1]; else buf[k++] = 'P';
        buf[k++] = '@'; buf[k++] = dst[0]; buf[k++] = dst[1];
    } else if (m.flags & MF_CASTLE) {
        k += snprintf(buf, sizeof buf, FILE_OF(m.to) == 6 ? "O-O" : "O-O-O");
    } else {
        int t = PTYPE(p->b[m.from]);
        if (t != PAWN) {
            buf[k++] = "PNBRQK"[t - 1];
            Move mv[MAX_MOVES]; int c = pos_legal_moves(p, mv);
            bool amb = false, same_file = false, same_rank = false;
            for (int i = 0; i < c; i++) {
                if (mv[i].flags & MF_DROP) continue;
                if (mv[i].from == m.from || mv[i].to != m.to || PTYPE(p->b[mv[i].from]) != t) continue;
                amb = true;
                if (FILE_OF(mv[i].from) == FILE_OF(m.from)) same_file = true;
                if (RANK_OF(mv[i].from) == RANK_OF(m.from)) same_rank = true;
            }
            if (amb) {
                if (!same_file) buf[k++] = (char)('a' + FILE_OF(m.from));
                else if (!same_rank) buf[k++] = (char)('1' + RANK_OF(m.from));
                else { buf[k++] = (char)('a' + FILE_OF(m.from)); buf[k++] = (char)('1' + RANK_OF(m.from)); }
            }
        } else if (m.flags & MF_CAPTURE) buf[k++] = (char)('a' + FILE_OF(m.from));
        if (m.flags & MF_CAPTURE) buf[k++] = 'x';
        buf[k++] = dst[0]; buf[k++] = dst[1];
        if (m.promo) { buf[k++] = '='; buf[k++] = "PNBRQK"[m.promo - 1]; }
    }
    Pos t = *p; pos_do(&t, m);
    int w; OutKind o = pos_outcome(&t, &w);
    if (o == OUT_CHECKMATE) buf[k++] = '#';
    else if (pos_in_check(&t, t.turn)) buf[k++] = '+';
    buf[k] = 0;
    snprintf(out, n, "%s", buf);
}

// SAN -> move: compare against the SAN of every legal move (ignoring check / annotation suffixes), so it always agrees with move_to_san.
static void san_norm(const char *in, char *out, size_t n) {
    size_t k = 0;
    for (; *in && k + 1 < n; in++) { char c = *in; if (c == '0') c = 'O'; if (c == '+' || c == '#' || c == '!' || c == '?') continue; out[k++] = c; }
    out[k] = 0;
}

bool pos_parse_san(const Pos *p, const char *san, Move *out) {
    char want[24]; san_norm(san, want, sizeof want);
    if (!want[0]) return false;
    Move mv[MAX_MOVES]; int n = pos_legal_moves(p, mv);
    for (int i = 0; i < n; i++) {
        char s[32], t[24]; move_to_san(p, mv[i], s, sizeof s); san_norm(s, t, sizeof t);
        if (!strcmp(t, want)) { *out = mv[i]; return true; }
    }
    return false;
}
