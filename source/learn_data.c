#include "learn_data.h"
#include <string.h>
#include <stdio.h>

const OpenLine OPEN_LINES[] = {
    {"Italian Game", WHITE, "e4 e5 Nf3 Nc6 Bc4 Bc5 c3 Nf6 d4 exd4 cxd4 Bb4+ Bd2 Bxd2+ Nbxd2 d5 exd5 Nxd5"},
    {"Ruy Lopez (closed)", WHITE, "e4 e5 Nf3 Nc6 Bb5 a6 Ba4 Nf6 O-O Be7 Re1 b5 Bb3 d6 c3 O-O h3 Nb8"},
    {"Scotch Game", WHITE, "e4 e5 Nf3 Nc6 d4 exd4 Nxd4 Nf6 Nxc6 bxc6 e5 Qe7 Qe2 Nd5 c4 Ba6"},
    {"King's Gambit", WHITE, "e4 e5 f4 exf4 Nf3 g5 h4 g4 Ne5 Nf6 Bc4 d5 exd5 Bd6"},
    {"Queen's Gambit Declined", WHITE, "d4 d5 c4 e6 Nc3 Nf6 Bg5 Be7 e3 O-O Nf3 h6 Bh4 b6 cxd5 Nxd5"},
    {"London System", WHITE, "d4 d5 Bf4 Nf6 e3 c5 c3 Nc6 Nd2 e6 Ngf3 Bd6 Bg3 O-O Bd3 b6"},
    {"Sicilian Najdorf", BLACK, "e4 c5 Nf3 d6 d4 cxd4 Nxd4 Nf6 Nc3 a6 Be3 e5 Nb3 Be6 f3 Be7 Qd2 O-O"},
    {"French Winawer", BLACK, "e4 e6 d4 d5 Nc3 Bb4 e5 c5 a3 Bxc3+ bxc3 Ne7 Qg4 Qc7 Qxg7 Rg8 Qxh7 cxd4"},
    {"Caro-Kann Classical", BLACK, "e4 c6 d4 d5 Nc3 dxe4 Nxe4 Bf5 Ng3 Bg6 h4 h6 Nf3 Nd7 h5 Bh7 Bd3 Bxd3 Qxd3 e6"},
    {"Scandinavian", BLACK, "e4 d5 exd5 Qxd5 Nc3 Qa5 d4 Nf6 Nf3 c6 Bc4 Bf5 Bd2 e6 Nd5 Qd8"},
    {"King's Indian", BLACK, "d4 Nf6 c4 g6 Nc3 Bg7 e4 d6 Nf3 O-O Be2 e5 O-O Nc6 d5 Ne7 Ne1 Nd7"},
    {"Slav Defence", BLACK, "d4 d5 c4 c6 Nf3 Nf6 Nc3 dxc4 a4 Bf5 e3 e6 Bxc4 Bb4 O-O O-O Qe2 Nbd7"},
    {"Nimzo-Indian", BLACK, "d4 Nf6 c4 e6 Nc3 Bb4 e3 O-O Bd3 d5 Nf3 c5 O-O Nc6 a3 Bxc3 bxc3 dxc4 Bxc4 Qc7"},
    {"Petrov Defence", BLACK, "e4 e5 Nf3 Nf6 Nxe5 d6 Nf3 Nxe4 d4 d5 Bd3 Nc6 O-O Be7 c4 Nb4 Be2 O-O"},
};
const int OPEN_LINE_COUNT = (int)(sizeof OPEN_LINES / sizeof OPEN_LINES[0]);

const EndgamePos ENDGAMES[] = {
    {"King + Queen vs King", "Checkmate", "8/8/8/4k3/8/8/6Q1/4K3 w - - 0 1"},
    {"King + Rook vs King", "Checkmate", "8/8/8/4k3/8/8/8/R3K3 w - - 0 1"},
    {"Two bishops vs King", "Checkmate", "8/8/8/4k3/8/8/8/2B1KB2 w - - 0 1"},
    {"Bishop + Knight vs King", "Checkmate (hard!)", "8/8/8/4k3/8/8/8/2B1KN2 w - - 0 1"},
    {"King + Pawn vs King", "Promote the pawn and win", "4k3/8/4K3/4P3/8/8/8/8 w - - 0 1"},
    {"Lucena position", "Win with the bridge", "1K1k4/1P6/8/8/8/8/r7/2R5 w - - 0 1"},
};
const int ENDGAME_COUNT = (int)(sizeof ENDGAMES / sizeof ENDGAMES[0]);

bool learn_line_to_puzzle(const OpenLine *l, Puzzle *out) {
    memset(out, 0, sizeof *out);
    Pos p; pos_init(&p, VAR_STANDARD);
    char ucis[PZ_MAX_SOL + 4][8]; int n = 0;
    Pos pre = p; Move first = {0, 0, 0, 0}; bool have_first = false;
    const char *s = l->san;
    while (*s) {
        while (*s == ' ') s++;
        if (!*s) break;
        char tok[16]; int k = 0;
        while (*s && *s != ' ' && k < 15) tok[k++] = *s++;
        tok[k] = 0;
        Move m;
        if (!pos_parse_san(&p, tok, &m)) return false;
        if (l->color == BLACK && !have_first) { pre = p; first = m; have_first = true; pos_do(&p, m); out->start = p; continue; }   // white's first move is "already played"
        if (n >= PZ_MAX_SOL) break;
        move_to_uci(&p, m, ucis[n]); n++;
        pos_do(&p, m);
    }
    if (l->color == WHITE) pos_init(&out->start, VAR_STANDARD);
    for (int i = 0; i < n; i++) snprintf(out->sol[i], sizeof out->sol[0], "%.5s", ucis[i]);
    out->nsol = n;
    out->my_color = l->color;
    snprintf(out->id, sizeof out->id, "open");
    if (have_first) { out->prev = pre; out->last = first; out->has_last = true; out->last_from = first.from; out->last_to = first.to; }
    else { out->last_from = out->last_to = -1; }
    return pz_validate(out);
}
