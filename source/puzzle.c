#include "puzzle.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

int pz_replay_pgn(const char *pgn, Pos *out, Move *last, int *last_from, int *last_to) {
    Pos p; pos_init(&p, VAR_STANDARD);
    int n = 0;
    const char *s = pgn;
    Move lm = {0, 0, 0, 0};
    while (*s) {
        while (*s == ' ') s++;
        if (!*s) break;
        char tok[16]; int k = 0;
        while (*s && *s != ' ' && k < 15) tok[k++] = *s++;
        tok[k] = 0;
        if (strchr(tok, '.')) continue;                              // move numbers, just in case
        Move m;
        if (!pos_parse_san(&p, tok, &m)) return 0;
        pos_do(&p, m); lm = m; n++;
    }
    *out = p;
    if (last) *last = lm;
    if (last_from) *last_from = n ? lm.from : -1;
    if (last_to) *last_to = n ? lm.to : -1;
    return n;
}

bool pz_setup_from_pgn(Puzzle *pz, const char *pgn) {
    Pos p; pos_init(&p, VAR_STANDARD); Pos prev = p; Move lm = {0, 0, 0, 0}; int n = 0;
    const char *s = pgn;
    while (*s) {
        while (*s == ' ') s++;
        if (!*s) break;
        char tok[16]; int k = 0;
        while (*s && *s != ' ' && k < 15) tok[k++] = *s++;
        tok[k] = 0;
        if (strchr(tok, '.')) continue;
        Move m;
        if (!pos_parse_san(&p, tok, &m)) return false;
        prev = p; pos_do(&p, m); lm = m; n++;
    }
    pz->start = p; pz->prev = prev; pz->last = lm; pz->has_last = n > 0;
    pz->last_from = n ? lm.from : -1; pz->last_to = n ? lm.to : -1;
    pz->my_color = p.turn;
    return true;
}

bool pz_validate(const Puzzle *pz) {
    Pos p = pz->start;
    if (pz->nsol < 1) return false;
    for (int i = 0; i < pz->nsol; i++) {
        Move m;
        if (!pos_parse_uci(&p, pz->sol[i], &m)) return false;
        pos_do(&p, m);
    }
    return true;
}

PzVerdict pz_check(const Puzzle *pz, const Pos *cur, int step, Move m) {
    if (step >= pz->nsol) return PZ_WRONG;
    Move want;
    if (pos_parse_uci(cur, pz->sol[step], &want) && move_eq(want, m)) return PZ_CORRECT;
    Pos after = *cur; pos_do(&after, m);
    int w; if (pos_outcome(&after, &w) == OUT_CHECKMATE) return PZ_CORRECT_MATE;
    return PZ_WRONG;
}

void pz_theme_label(const char *key, char *out, size_t n) {
    static const struct { const char *k, *v; } T[] = {
        {"mateIn1", "Mate in 1"}, {"mateIn2", "Mate in 2"}, {"mateIn3", "Mate in 3"}, {"mateIn4", "Mate in 4"}, {"mateIn5", "Mate in 5"},
        {"backRankMate", "Back rank mate"}, {"hangingPiece", "Hanging piece"}, {"discoveredAttack", "Discovered attack"},
        {"defensiveMove", "Defensive move"}, {"advancedPawn", "Advanced pawn"}, {"attackingF2F7", "Attacking f2/f7"},
        {"kingsideAttack", "Kingside attack"}, {"queensideAttack", "Queenside attack"}, {"smotheredMate", "Smothered mate"},
        {"doubleCheck", "Double check"}, {"exposedKing", "Exposed king"}, {"trappedPiece", "Trapped piece"},
        {"quietMove", "Quiet move"}, {"xRayAttack", "X-ray attack"}, {"zugzwang", "Zugzwang"}, {"endgame", "Endgame"},
        {"middlegame", "Middlegame"}, {"opening", "Opening"}, {"short", "Short"}, {"long", "Long"}, {"veryLong", "Very long"},
        {"oneMove", "One-move"}, {"crushing", "Crushing"}, {"advantage", "Advantage"}, {"equality", "Equality"},
        {"master", "Master game"}, {"masterVsMaster", "Master vs master"}, {"superGM", "Super GM game"},
        {"deflection", "Deflection"}, {"attraction", "Attraction"}, {"sacrifice", "Sacrifice"}, {"clearance", "Clearance"},
        {"interference", "Interference"}, {"intermezzo", "Intermezzo"}, {"capturingDefender", "Capturing defender"},
        {"pawnEndgame", "Pawn endgame"}, {"rookEndgame", "Rook endgame"}, {"bishopEndgame", "Bishop endgame"},
        {"knightEndgame", "Knight endgame"}, {"queenEndgame", "Queen endgame"}, {"queenRookEndgame", "Queen + rook endgame"},
        {"fork", "Fork"}, {"pin", "Pin"}, {"skewer", "Skewer"}, {"promotion", "Promotion"}, {"underPromotion", "Underpromotion"},
        {"castling", "Castling"}, {"enPassant", "En passant"}, {"hookMate", "Hook mate"}, {"arabianMate", "Arabian mate"},
        {"anastasiaMate", "Anastasia's mate"}, {"bodenMate", "Boden's mate"}, {"dovetailMate", "Dovetail mate"},
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) if (!strcmp(T[i].k, key)) { snprintf(out, n, "%s", T[i].v); return; }
    // fallback: camelCase -> "Camel case"
    size_t k = 0;
    for (const char *s = key; *s && k + 2 < n; s++) {
        if (isupper((unsigned char)*s) && k) out[k++] = ' ';
        out[k] = k == 0 ? (char)toupper((unsigned char)*s) : (char)tolower((unsigned char)*s);
        k++;
    }
    out[k] = 0;
}
