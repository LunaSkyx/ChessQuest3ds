// Host test: SAN generation and parsing agree on random games of every variant
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chess.h"
int main(void) {
    srand(7); int checks = 0, fails = 0;
    for (int v = 0; v < VAR_COUNT; v++) {
        for (int g = 0; g < 3; g++) {
            Pos p; pos_init(&p, (Variant)v);
            for (int ply = 0; ply < 50; ply++) {
                Move mv[MAX_MOVES]; int n = pos_legal_moves(&p, mv); int w;
                if (!n || pos_outcome(&p, &w) != OUT_ONGOING) break;
                for (int i = 0; i < n; i++) {
                    char s[32]; move_to_san(&p, mv[i], s, sizeof s);
                    Move m; checks++;
                    if (!pos_parse_san(&p, s, &m) || !move_eq(m, mv[i])) { fails++; if (fails < 5) printf("FAIL v%d: %s\n", v, s); }
                }
                pos_do(&p, mv[rand() % n]);
            }
        }
    }
    Pos p; pos_init(&p, VAR_STANDARD); Move m;
    checks++; if (!pos_parse_san(&p, "Nf3", &m) || m.to != sq_from_name("f3")) { fails++; printf("FAIL Nf3\n"); }
    checks++; if (!pos_parse_san(&p, "e4!?", &m) || m.to != sq_from_name("e4")) { fails++; printf("FAIL e4!?\n"); }
    checks++; if (pos_parse_san(&p, "Nf6", &m)) { fails++; printf("FAIL illegal accepted\n"); }
    printf("%d checks, %d failures\n", checks, fails);
    return fails != 0;
}
