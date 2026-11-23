// Sanity check of the bot ladder: adjacent bots play a few quick games (time-capped); the stronger one should come out ahead
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "bots.h"
#include "chess.h"
static uint64_t now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000; }

static int play(int w, int b, int cap_ms) {            // returns +1 white wins, -1 black wins, 0 draw
    Pos p; pos_init(&p, VAR_STANDARD);
    static uint64_t hist[600]; int nh = 0;
    for (int ply = 0; ply < 300; ply++) {
        int win; OutKind o = pos_outcome(&p, &win);
        if (o == OUT_CHECKMATE) return win == WHITE ? 1 : -1;
        if (o != OUT_ONGOING || p.halfmove >= 100) return 0;
        int rep = 0; uint64_t h = eng_hash(&p); for (int i = 0; i < nh; i++) if (hist[i] == h) rep++;
        if (rep >= 2) return 0;
        const Bot *bot = &BOTS[p.turn == WHITE ? w : b];
        EngLimits lim = bot->lim; if (lim.time_ms > cap_ms) lim.time_ms = cap_ms;
        EngResult r; eng_search(&p, hist, nh, lim, now_ms, NULL, NULL, NULL, &r);
        if (!r.has_move) return 0;
        hist[nh++] = h; pos_do(&p, r.best);
    }
    return 0;
}

int main(int argc, char **argv) {
    int cap = argc > 1 ? 150 : 150; (void)argv; int games = 4, bad = 0;
    for (int i = 0; i + 1 < BOT_COUNT - 3; i++) {                 // top bots need seconds per move; the low ladder is checked here
        double score = 0;                                          // from the point of view of the stronger bot (i+1)
        for (int g = 0; g < games; g++) {
            int r = (g & 1) ? play(i, i + 1, cap) : play(i + 1, i, cap);
            score += (g & 1) ? (r < 0 ? 1 : (r == 0 ? 0.5 : 0)) : (r > 0 ? 1 : (r == 0 ? 0.5 : 0));
        }
        printf("%-16s vs %-16s : stronger bot scores %.1f / %d%s\n", BOTS[i].name, BOTS[i + 1].name, score, games, score < games / 2.0 ? "   <-- NOT stronger" : "");
        if (score < games / 2.0) bad++;
    }
    printf("%d ladder steps out of order (cap %d ms/move)\n", bad, cap);
    return 0;
}
