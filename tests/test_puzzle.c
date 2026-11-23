#include <stdio.h>
#include <string.h>
#include "puzzle.h"
static int fails, checks;
#define CHECK(c, msg) do { checks++; if (!(c)) { fails++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static void run(const char *pgn, const char *sol, const char *fen_expect) {
    Puzzle pz; memset(&pz, 0, sizeof pz);
    Move last; int lf, lt;
    int n = pz_replay_pgn(pgn, &pz.start, &last, &lf, &lt);
    CHECK(n > 0, "pgn replays");
    pz.my_color = pz.start.turn;
    const char *s = sol;
    while (*s && pz.nsol < PZ_MAX_SOL) { memcpy(pz.sol[pz.nsol], s, 4); pz.sol[pz.nsol][4] = 0; pz.nsol++; s += 4; while (*s == ' ') s++; }
    CHECK(pz_validate(&pz), "solution legal from the replayed position");
    if (fen_expect) { char f[100]; pos_to_fen(&pz.start, f, sizeof f); size_t L = strlen(fen_expect) - 4; /* ignore the move counters */ CHECK(!strncmp(f, fen_expect, L), "fen matches the API's fen"); if (strncmp(f, fen_expect, L)) printf("  got %s\n", f); }
    // the first solution move must be accepted, a random other legal move rejected (unless it mates)
    Move m; CHECK(pos_parse_uci(&pz.start, pz.sol[0], &m), "sol0 parses");
    CHECK(pz_check(&pz, &pz.start, 0, m) == PZ_CORRECT, "sol0 accepted");
    Move mv[MAX_MOVES]; int c = pos_legal_moves(&pz.start, mv), wrong = 0;
    for (int i = 0; i < c; i++) if (!move_eq(mv[i], m) && pz_check(&pz, &pz.start, 0, mv[i]) == PZ_WRONG) wrong++;
    CHECK(wrong > 0, "other moves rejected");
}

int main(void) {
    // from the live API: puzzle/next (fen given), daily (fen given), batch (no fen)
    run("e4 c6 d4 d5 e5 Bf5 Nf3 e6 Bd3 Bg6 O-O c5 Bb5+ Nc6 c3 Qb6 Bxc6+ Qxc6 b3 Bh5 Nbd2 Ne7 Qe1 f5 exf6 gxf6 Bb2 Kd7 dxc5 Qxc5 Qe3 Qxe3 fxe3 Rg8 Kh1 Bh6 c4 e5 e4 d4 b4 Rg4 h3 Rg3 Bc1 Rag8 Nh4 Be3 Ndf3 Bxf3 Rxf3 Nc6 Rxf6 Nxb4 Bxe3 Rxe3 Rf7+ Kc6 Rxh7 Rxe4 Rh6+ Kc5 Nf5 Rd8 a3 Nc6 Rb1 b6 a4 d3",
        "b1b5 c5c4 h6c6", "3r4/p7/1pn4R/2k1pN2/P1P1r3/3p3P/6P1/1R5K w - - 0 1");
    run("e4 Nc6 Nf3 d5 exd5 Qxd5 Nc3 Qh5 Be2 Bg4 O-O O-O-O Ne4 Ne5", "e4g3 e5f3 g2f3 h5h4 f3g4", "2kr1bnr/ppp1pppp/8/4n2q/4N1b1/5N2/PPPPBPPP/R1BQ1RK1 w - - 1 1");
    run("e4 c5 Nf3 Nc6 d4 cxd4 Nxd4 e5 Nb5 a6 Nd6+ Bxd6 Qxd6 Qf6 Qd1 Qg6 Nc3 d5 Nxd5 Qxe4+ Ne3 Nf6 Bd3 Qb4+ c3 Qe7 O-O O-O Nf5 Bxf5 Bxf5 Rfd8 Qe2 h6 Be3 e4 Bb6 Rd3 Rad1 Qd6 Rxd3 exd3 Qxd3 Qe5 Bh3 Re8 g3 Nd5 Bd4 Nxd4 cxd4 Qd6 Bg2 Nb4 Qb3 Qxd4 a3 Nd3 Qxb7 Re2 Qc8+ Kh7 Qf5+ Kg8 Rd1 Qxf2+ Qxf2 Nxf2 Rd8+ Kh7 b4 Ng4 Rd1",
        "e2g2 g1g2 g4e3 g2f3 e3d1", NULL);
    char l[40]; pz_theme_label("mateIn2", l, sizeof l); CHECK(!strcmp(l, "Mate in 2"), "label");
    pz_theme_label("trappedKingXyz", l, sizeof l); CHECK(!strcmp(l, "Trapped king xyz"), "fallback label");
    printf("%d checks, %d failures\n", checks, fails);
    return fails != 0;
}
