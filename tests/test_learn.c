#include <stdio.h>
#include <string.h>
#include "learn_data.h"
#include "engine.h"
#include <time.h>
static uint64_t now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000; }
int main(void) {
    int fails = 0;
    for (int i = 0; i < OPEN_LINE_COUNT; i++) {
        Puzzle p;
        if (!learn_line_to_puzzle(&OPEN_LINES[i], &p)) { printf("FAIL opening line: %s\n", OPEN_LINES[i].name); fails++; continue; }
        if (p.start.turn != p.my_color) { printf("FAIL side to move in %s\n", OPEN_LINES[i].name); fails++; }
    }
    for (int i = 0; i < ENDGAME_COUNT; i++) {
        Pos p;
        if (!pos_from_fen(&p, ENDGAMES[i].fen, VAR_STANDARD)) { printf("FAIL fen: %s\n", ENDGAMES[i].name); fails++; continue; }
        int w; if (pos_outcome(&p, &w) != OUT_ONGOING) { printf("FAIL already over: %s\n", ENDGAMES[i].name); fails++; }
        // the engine (as the side to move) should see it is winning
        EngLimits lim = {6, 1500, 0}; EngResult r; eng_search(&p, NULL, 0, lim, now_ms, NULL, NULL, NULL, &r);
        printf("  %-28s engine score (white pov, d%d): %d\n", ENDGAMES[i].name, r.depth, r.score);
    }
    printf("%d openings, %d endgames, %d failures\n", OPEN_LINE_COUNT, ENDGAME_COUNT, fails);
    return fails != 0;
}
