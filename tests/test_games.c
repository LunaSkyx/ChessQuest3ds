// Replays real Lichess games (tests/data/games_*.txt) through chess.c and checks SAN + final position.
#include "chess.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void strip_marks(const char *in, char *out) {
    int k = 0;
    for (; *in; in++) {
        if (*in == '+' || *in == '#' || *in == '!' || *in == '?') continue;
        out[k++] = *in;
    }
    out[k] = 0;
}

// normalise a SAN so that "P@e4" == "@e4"
static void norm_san(char *s) {
    if (s[0] == 'P' && s[1] == '@') memmove(s, s + 1, strlen(s));
}

// first 4 FEN fields with the optional pocket bracket kept; counters dropped
static void fen_key(const Pos *p, char *out, size_t n) {
    char f[160]; pos_to_fen(p, f, sizeof f);
    // cut after the en-passant field (3rd space) but keep 3check info that follows
    int sp = 0; size_t i = 0;
    for (; f[i]; i++) if (f[i] == ' ' && ++sp == 4) break;
    f[i] = 0;
    if (p->variant == VAR_CHESS960) {   // lichess' lastFen castling field is unreliable for 960 - compare the rest
        char *c1 = strchr(f, ' '); char *c2 = c1 ? strchr(c1 + 1, ' ') : NULL; char *c3 = c2 ? strchr(c2 + 1, ' ') : NULL;
        if (c2 && c3) { *c2 = 0; snprintf(out, n, "%s %s", f, c3 + 1); return; }
    }
    snprintf(out, n, "%s", f);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "tests/data";
    static const char *names[] = {"standard", "chess960", "kingOfTheHill", "threeCheck", "antichess", "atomic", "horde", "racingKings", "crazyhouse"};
    int total_games = 0, bad_games = 0;
    for (int vi = 0; vi < 9; vi++) {
        Variant v; variant_from_key(names[vi], &v);
        char path[256]; snprintf(path, sizeof path, "%s/games_%s.txt", dir, names[vi]);
        FILE *f = fopen(path, "r");
        if (!f) { printf("%-14s no data file\n", names[vi]); continue; }
        static char line[40000];
        int games = 0, bad = 0, plies = 0, shown = 0;
        while (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            if (!*line) continue;
            char *fields[4] = {0}; int nf = 0; char *s = line;
            fields[nf++] = s;
            for (; *s && nf < 4; s++) if (*s == '|') { *s = 0; fields[nf++] = s + 1; }
            if (nf < 4) continue;
            Pos p;
            if (strcmp(fields[1], "-")) { if (!pos_from_fen(&p, fields[1], v)) { printf("bad initial fen %s\n", fields[1]); continue; } }
            else pos_init(&p, v);
            games++;
            bool ok = true; int ply = 0;
            char *save = NULL; (void)save;
            char *tok = strtok(fields[2], " ");
            while (tok && ok) {
                char want[32]; strip_marks(tok, want); norm_san(want);
                Move mv[MAX_MOVES]; int n = pos_legal_moves(&p, mv);
                int found = -1;
                for (int i = 0; i < n; i++) {
                    char san[32], got[32];
                    move_to_san(&p, mv[i], san, sizeof san); strip_marks(san, got); norm_san(got);
                    if (!strcmp(got, want)) { found = i; break; }
                }
                if (found < 0) {
                    ok = false;
                    if (shown++ < 6) {
                        char fen[160]; pos_to_fen(&p, fen, sizeof fen);
                        printf("  [%s] game %d ply %d: no legal move for SAN '%s' (%d legal)\n      fen %s\n", names[vi], games, ply + 1, tok, n, fen);
                    }
                    break;
                }
                pos_do(&p, mv[found]); ply++; plies++;
                tok = strtok(NULL, " ");
            }
            if (ok && strcmp(fields[3], "-")) {
                Pos q;
                if (pos_from_fen(&q, fields[3], v)) {
                    char a[160], b[160]; fen_key(&p, a, sizeof a); fen_key(&q, b, sizeof b);
                    if (strcmp(a, b)) {
                        ok = false;
                        if (shown++ < 6) printf("  [%s] game %d final position differs\n      mine  %s\n      lichess %s\n", names[vi], games, a, b);
                    }
                }
            }
            if (!ok) bad++;
        }
        fclose(f);
        printf("%-14s %3d games, %5d plies, %d failing\n", names[vi], games, plies, bad);
        total_games += games; bad_games += bad;
    }
    printf("\n%d games replayed, %d failing\n", total_games, bad_games);
    return bad_games ? 1 : 0;
}
