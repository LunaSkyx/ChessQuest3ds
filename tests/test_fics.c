// Host test: FICS protocol parsing against lines captured from the live server
#include <stdio.h>
#include <string.h>
#include "fics_parse.h"

static int fails, checks;
#define CHECK(c, msg) do { checks++; if (!(c)) { fails++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

int main(void) {
    Style12 s; Pos p;
    CHECK(fics_parse_style12("<12> rnbqkbnr pppppppp -------- -------- -------- -------- PPPPPPPP RNBQKBNR W -1 1 1 1 1 0 40 GuestMNSP GuestCLNX 1 4 3 39 39 240000 240000 1 none (0:00.000) none 0 0 0", &s), "parse start");
    CHECK(s.gnum == 40 && s.relation == 1 && s.white_turn && s.movenum == 1 && s.wt_ms == 240000 && s.init_min == 4 && s.inc == 3, "start fields");
    CHECK(!strcmp(s.white, "GuestMNSP") && !strcmp(s.black, "GuestCLNX"), "names");
    CHECK(fics_s12_to_pos(&s, &p), "start -> pos");
    char fen[100]; pos_to_fen(&p, fen, sizeof fen);
    CHECK(!strncmp(fen, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq", 50), "start fen");

    CHECK(fics_parse_style12("<12> rnbqkbnr pppp-ppp -------- ----p--- ----P--- -----N-- PPPP-PPP RNBQKB-R B -1 1 1 1 1 1 40 GuestMNSP GuestCLNX -1 4 3 39 39 242881 240000 2 N/g1-f3 (0:00.119) Nf3 0 1 0", &s), "parse mid");
    CHECK(s.relation == -1 && !s.white_turn && s.ticking && s.wt_ms == 242881 && !strcmp(s.verbose, "N/g1-f3") && !strcmp(s.san, "Nf3"), "mid fields");
    CHECK(fics_s12_to_pos(&s, &p), "mid -> pos");
    Pos before; pos_init(&before, VAR_STANDARD);
    Move m; CHECK(pos_parse_uci(&before, "e2e4", &m), "e4"); pos_do(&before, m);
    CHECK(pos_parse_uci(&before, "e7e5", &m), "e5"); pos_do(&before, m);
    CHECK(fics_verbose_to_move(&before, "N/g1-f3", &m), "verbose Nf3");
    char mv[16]; fics_move_string(&before, m, mv, sizeof mv); CHECK(!strcmp(mv, "g1f3"), "move string");
    pos_do(&before, m);
    CHECK(!memcmp(before.b, p.b, 64) && before.turn == p.turn, "replayed position equals server board");

    // castling + promotion strings
    Pos c; pos_from_fen(&c, "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", VAR_STANDARD);
    CHECK(fics_verbose_to_move(&c, "o-o", &m) && (m.flags & MF_CASTLE) && m.to > m.from, "o-o");
    fics_move_string(&c, m, mv, sizeof mv); CHECK(!strcmp(mv, "o-o"), "send o-o");
    CHECK(fics_verbose_to_move(&c, "o-o-o", &m) && m.to < m.from, "o-o-o");
    fics_move_string(&c, m, mv, sizeof mv); CHECK(!strcmp(mv, "o-o-o"), "send o-o-o");
    Pos pr; pos_from_fen(&pr, "8/P6k/8/8/8/8/8/K7 w - - 0 1", VAR_STANDARD);
    CHECK(fics_verbose_to_move(&pr, "P/a7-a8=Q", &m) && m.promo == QUEEN, "promotion parse");
    fics_move_string(&pr, m, mv, sizeof mv); CHECK(!strcmp(mv, "a7a8=Q"), "promotion string");

    int g, w; char st[16], why[40];
    CHECK(fics_parse_result("{Game 40 (GuestMNSP vs. GuestCLNX) GuestMNSP resigns} 0-1", &g, &w, st, sizeof st, why, sizeof why), "resign result");
    CHECK(g == 40 && w == BLACK && !strcmp(st, "resign"), "resign fields");
    CHECK(!fics_parse_result("{Game 40 (A vs. B) Creating unrated blitz match.}", &g, &w, st, sizeof st, why, sizeof why), "creating is no result");
    CHECK(fics_parse_result("{Game 7 (A vs. B) B checkmated} 1-0", &g, &w, st, sizeof st, why, sizeof why) && w == WHITE && !strcmp(st, "mate"), "mate");
    CHECK(fics_parse_result("{Game 7 (A vs. B) A forfeits on time} 0-1", &g, &w, st, sizeof st, why, sizeof why) && w == BLACK && !strcmp(st, "outoftime"), "time");
    CHECK(fics_parse_result("{Game 7 (A vs. B) Game drawn by mutual agreement} 1/2-1/2", &g, &w, st, sizeof st, why, sizeof why) && w == -1 && !strcmp(st, "draw"), "draw agreement");
    CHECK(fics_parse_result("{Game 7 (A vs. B) Game aborted on move 1} *", &g, &w, st, sizeof st, why, sizeof why) && !strcmp(st, "aborted"), "aborted");
    CHECK(fics_parse_result("{Game 7 (A vs. B) Game drawn by stalemate} 1/2-1/2", &g, &w, st, sizeof st, why, sizeof why) && !strcmp(st, "stalemate"), "stalemate");

    // en passant file round trip
    CHECK(fics_parse_style12("<12> rnbqkbnr pppp-ppp -------- ---pP--- -------- -------- PPPP-PPP RNBQKBNR W 3 1 1 1 1 0 9 A B -1 5 0 39 39 300000 300000 3 P/d7-d5 (0:00.100) d5 0 1 0", &s), "ep line");
    CHECK(fics_s12_to_pos(&s, &p), "ep pos");
    CHECK(pos_parse_uci(&p, "e5d6", &m) && (m.flags & MF_EP), "en passant available from style12");
    printf("%d checks, %d failures\n", checks, fails);
    return fails != 0;
}
