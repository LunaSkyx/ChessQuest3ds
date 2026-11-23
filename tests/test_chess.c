// Host-side tests for chess.c: perft against published node counts. Run: sh tests/run_tests.sh
#include "chess.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static unsigned long long perft(const Pos *p, int depth) {
    Move mv[MAX_MOVES];
    int n = pos_legal_moves(p, mv);
    if (depth == 1) return (unsigned long long)n;
    unsigned long long tot = 0;
    for (int i = 0; i < n; i++) { Pos t = *p; pos_do(&t, mv[i]); tot += perft(&t, depth - 1); }
    return tot;
}

static int fails = 0, checks = 0;

static void run(const char *name, Variant v, const char *fen, const unsigned long long *exp, int maxd) {
    Pos p;
    if (fen) { if (!pos_from_fen(&p, fen, v)) { printf("FEN parse failed: %s\n", fen); fails++; return; } }
    else pos_init(&p, v);
    printf("%-22s", name);
    for (int d = 1; d <= maxd; d++) {
        unsigned long long got = perft(&p, d);
        checks++;
        if (got != exp[d - 1]) { printf("  d%d %llu != %llu FAIL", d, got, exp[d - 1]); fails++; }
        else printf("  d%d ok", d);
        fflush(stdout);
    }
    printf("\n");
}

int main(int argc, char **argv) {
    int deep = argc > 1 && !strcmp(argv[1], "deep");
    clock_t t0 = clock();
    { unsigned long long e[] = {20, 400, 8902, 197281, 4865609}; run("startpos", VAR_STANDARD, NULL, e, deep ? 5 : 4); }
    { unsigned long long e[] = {48, 2039, 97862, 4085603}; run("kiwipete", VAR_STANDARD, "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", e, deep ? 4 : 3); }
    { unsigned long long e[] = {14, 191, 2812, 43238, 674624}; run("pos3", VAR_STANDARD, "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", e, deep ? 5 : 4); }
    { unsigned long long e[] = {6, 264, 9467, 422333}; run("pos4", VAR_STANDARD, "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", e, deep ? 4 : 3); }
    { unsigned long long e[] = {44, 1486, 62379, 2103487}; run("pos5", VAR_STANDARD, "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", e, deep ? 4 : 3); }
    { unsigned long long e[] = {46, 2079, 89890, 3894594}; run("pos6", VAR_STANDARD, "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", e, deep ? 4 : 3); }
    // Chess960 (X-FEN / Shredder castling letters)
    { unsigned long long e[] = {21, 528, 12189, 326672, 8146062}; run("960 #1", VAR_CHESS960, "bqnb1rkr/pp3ppp/3ppn2/2p5/5P2/P2P4/NPP1P1PP/BQ1BNRKR w HFhf - 2 9", e, deep ? 5 : 4); }
    { unsigned long long e[] = {21, 807, 18002, 667366}; run("960 #2", VAR_CHESS960, "2nnrbkr/p1qppppp/8/1ppb4/6PP/3PP3/PPP2P2/BQNNRBKR w HEhe - 1 9", e, 4); }
    { unsigned long long e[] = {20, 479, 10471, 273318}; run("960 #3", VAR_CHESS960, "b1q1rrkb/pppppppp/3nn3/8/P7/1PPP4/4PPPP/BQNNRKRB w GE - 1 9", e, 4); }
    // variants from their start positions (published counts)
    { unsigned long long e[] = {20, 400, 8067, 153299}; run("antichess", VAR_ANTICHESS, NULL, e, 4); }
    { unsigned long long e[] = {20, 400, 8902, 197326}; run("atomic", VAR_ATOMIC, NULL, e, 4); }
    { unsigned long long e[] = {8, 128, 1274, 23310}; run("horde", VAR_HORDE, NULL, e, 4); }
    { unsigned long long e[] = {21, 421, 11264, 296242}; run("racing kings", VAR_RACING, NULL, e, 4); }
    { unsigned long long e[] = {20, 400, 8902, 197281}; run("crazyhouse", VAR_CRAZYHOUSE, NULL, e, 4); }
    { unsigned long long e[] = {20, 400, 8902, 197281}; run("koth", VAR_KOTH, NULL, e, 4); }
    { unsigned long long e[] = {20, 400, 8902, 197281}; run("3check", VAR_3CHECK, NULL, e, 4); }

    // FEN round trip + UCI/SAN sanity
    Pos p; char fen[128], san[16], uci[8]; Move m;
    pos_init(&p, VAR_STANDARD);
    pos_to_fen(&p, fen, sizeof fen); checks++;
    if (strcmp(fen, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")) { printf("FEN roundtrip FAIL: %s\n", fen); fails++; }
    const char *line[] = {"e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6", "b5a4", "g8f6", "e1g1", "f8e7"};
    const char *sans[] = {"e4", "e5", "Nf3", "Nc6", "Bb5", "a6", "Ba4", "Nf6", "O-O", "Be7"};
    for (int i = 0; i < 10; i++) {
        checks++;
        if (!pos_parse_uci(&p, line[i], &m)) { printf("parse %s FAIL\n", line[i]); fails++; break; }
        move_to_san(&p, m, san, sizeof san); move_to_uci(&p, m, uci);
        if (strcmp(san, sans[i]) || strcmp(uci, line[i])) { printf("SAN/UCI %s -> %s/%s expected %s FAIL\n", line[i], san, uci, sans[i]); fails++; }
        pos_do(&p, m);
    }
    pos_to_fen(&p, fen, sizeof fen);
    checks++;
    if (strcmp(fen, "r1bqk2r/1pppbppp/p1n2n2/4p3/B3P3/5N2/PPPP1PPP/RNBQ1RK1 w kq - 4 6")) { printf("FEN after line FAIL: %s\n", fen); fails++; }
    printf("\n%d checks, %d failures, %.1fs\n", checks, fails, (double)(clock() - t0) / CLOCKS_PER_SEC);
    return fails ? 1 : 0;
}
