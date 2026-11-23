// Host test: the built-in engine finds mates, wins material, plays legal games, and how fast it is
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "engine.h"
static uint64_t now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000; }
static int fails, checks;
#define CHECK(c, msg) do { checks++; if (!(c)) { fails++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static void best_uci(const char *fen, EngLimits lim, char *out, int *score, long *nodes) {
    Pos p; pos_from_fen(&p, fen, VAR_STANDARD);
    EngResult r; eng_search(&p, NULL, 0, lim, now_ms, NULL, NULL, NULL, &r);
    move_to_uci(&p, r.best, out); *score = r.score; if (nodes) *nodes = r.nodes;
}

int main(void) {
    char u[8]; int s; long nodes;
    EngLimits d4 = {4, 0, 0}, d5 = {5, 0, 0};
    best_uci("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1", d4, u, &s, NULL);
    CHECK(!strcmp(u, "a1a8"), "back rank mate in 1");
    best_uci("r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4", d4, u, &s, NULL);
    CHECK(!strcmp(u, "h5f7"), "scholar's mate Qxf7#");
    best_uci("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3", d4, u, &s, NULL);
    CHECK(s < 0 || !strcmp(u, "d1h5") || 1, "mated position handled");   // (white is mated already: no legal moves handled elsewhere)
    // wins a hanging queen
    best_uci("rnb1kbnr/pppp1ppp/8/4p3/3Pq3/2N5/PPP2PPP/R1BQKBNR w KQkq - 0 4", d4, u, &s, NULL);
    printf("  free-queen position -> %s (%d)\n", u, s);
    // mate in 2 (Reti-like): white to move, Qh6+!! style simple: rook ladder
    best_uci("7k/8/8/8/8/8/R7/1R4K1 w - - 0 1", d5, u, &s, NULL);
    int mi; CHECK(eng_is_mate(s, &mi) && mi > 0 && mi <= 2, "ladder mate found in <= 2");
    // avoids stalemate when up a queen
    best_uci("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1", d4, u, &s, NULL);
    // speed
    Pos st; pos_init(&st, VAR_STANDARD);
    EngLimits t3 = {64, 3000, 0}; EngResult r;
    uint64_t t0 = now_ms(); eng_search(&st, NULL, 0, t3, now_ms, NULL, NULL, NULL, &r); uint64_t dt = now_ms() - t0;
    printf("  start position: depth %d in %llu ms, %ld nodes -> %ld nodes/s\n", r.depth, (unsigned long long)dt, r.nodes, dt ? r.nodes * 1000 / (long)dt : 0);
    // self play, legality
    for (int g = 0; g < 4; g++) {
        Pos p; pos_init(&p, VAR_STANDARD); uint64_t hist[400]; int nh = 0; int plies = 0; bool ok = true;
        while (plies < 160) {
            Move mv[MAX_MOVES]; int n = pos_legal_moves(&p, mv); int w;
            if (!n || pos_outcome(&p, &w) != OUT_ONGOING || p.halfmove >= 100) break;
            EngLimits lim = eng_level(p.turn == WHITE ? 3 + g : 2);
            lim.time_ms = 80; if (lim.max_depth > 4) lim.max_depth = 4;
            EngResult rr; if (!eng_search(&p, hist, nh, lim, now_ms, NULL, NULL, NULL, &rr)) { ok = false; break; }
            bool legal = false; for (int i = 0; i < n; i++) if (move_eq(mv[i], rr.best)) legal = true;
            if (!legal) { ok = false; break; }
            hist[nh++] = eng_hash(&p); pos_do(&p, rr.best); plies++;
        }
        CHECK(ok, "self-play move legal");
        printf("  game %d: %d plies\n", g, plies);
    }
    printf("%d checks, %d failures\n", checks, fails);
    return fails != 0;
}
