#include "premove.h"
#include <stdio.h>
#include <string.h>
static int fails, checks;
#define CHECK(c, msg) do { checks++; if (!(c)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); fails++; } } while (0)

int main(void) {
    Pos p, v; PreQueue q = {0}; Move m;
    pos_init(&p, VAR_STANDARD);                       // white to move; I am black and premove
    pm_virtual(&p, &q, BLACK, &v);
    uint64_t d = pm_dests(&v, BLACK, sq_from_name("e7"));
    CHECK(d == ((1ULL << sq_from_name("e6")) | (1ULL << sq_from_name("e5")) | (1ULL << sq_from_name("d6")) | (1ULL << sq_from_name("f6"))), "pawn e7 premove dests (incl. empty diagonals)");
    CHECK(pm_dests(&v, BLACK, sq_from_name("a8")) == ((1ULL << sq_from_name("a7")) | (1ULL << sq_from_name("b8"))), "rook a8: only own-occupied neighbours (recapture), no sliding through");
    CHECK((pm_dests(&v, BLACK, sq_from_name("g8")) & (1ULL << sq_from_name("f6"))), "knight g8-f6");
    CHECK((pm_dests(&v, BLACK, sq_from_name("g8")) & (1ULL << sq_from_name("e7"))), "knight may premove onto own pawn (recapture)");
    CHECK((pm_dests(&v, BLACK, sq_from_name("g7")) & (1ULL << sq_from_name("h6"))), "pawn g7 can premove h6 (recapture)");

    // multi premove: ...e5 then ...Qh4 (queen path opens only after e5 in the virtual position) then ...Qxf2
    PreMove a = {sq_from_name("e7"), sq_from_name("e5"), 0, 0};
    CHECK(pm_push(&q, a), "push 1");
    pm_virtual(&p, &q, BLACK, &v);
    CHECK(v.b[sq_from_name("e5")] == MKPC(BLACK, PAWN) && !v.b[sq_from_name("e7")], "virtual position shows pawn on e5");
    uint64_t qd = pm_dests(&v, BLACK, sq_from_name("d8"));
    CHECK((qd & (1ULL << sq_from_name("h4"))) && (qd & (1ULL << sq_from_name("e7"))), "queen d8 sees h4/e7 after e5");
    PreMove b = {sq_from_name("d8"), sq_from_name("h4"), 0, 0};
    CHECK(pm_push(&q, b), "push 2");
    pm_virtual(&p, &q, BLACK, &v);
    PreMove c = {sq_from_name("h4"), sq_from_name("f2"), 0, 0};
    CHECK(pm_dests(&v, BLACK, sq_from_name("h4")) & (1ULL << sq_from_name("f2")), "queen h4 can premove onto f2 (enemy pawn)");
    CHECK(pm_push(&q, c), "push 3");

    // execution against the real position: white plays e4 -> ...e5 legal
    pm_clear(&q);
    pos_parse_uci(&p, "e2e4", &m); pos_do(&p, m);
    CHECK(pm_execute(&p, a, &m) && m.from == sq_from_name("e7") && m.to == sq_from_name("e5"), "execute e7e5");
    // a premove that is no longer legal (queen cannot reach h4 yet) must fail
    CHECK(!pm_execute(&p, b, &m), "illegal premove rejected at execution");
    // promotion defaults to queen
    pos_from_fen(&p, "8/P7/8/8/8/8/8/k6K w - - 0 1", VAR_STANDARD);
    PreMove pr = {sq_from_name("a7"), sq_from_name("a8"), 0, 0};
    CHECK(pm_execute(&p, pr, &m) && m.promo == QUEEN, "auto queen");
    // castling premove through own pieces cleared by an earlier premove
    pos_from_fen(&p, "r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R b KQkq - 0 1", VAR_STANDARD);
    pm_virtual(&p, &q, BLACK, &v);
    CHECK(pm_dests(&v, BLACK, sq_from_name("e8")) & (1ULL << sq_from_name("g8")), "O-O premove offered");
    PreMove cs = {sq_from_name("e8"), sq_from_name("g8"), 0, 0};
    CHECK(pm_execute(&p, cs, &m) && (m.flags & MF_CASTLE), "execute castling premove");
    printf("%d checks, %d failures\n", checks, fails);
    return fails != 0;
}
