#!/bin/sh
# Host-side unit tests (plain C, no 3DS needed). Run from the repository root:  sh tests/run_tests.sh
# Needs a C compiler (gcc or clang). Output goes to build/hosttest/.
set -e
CC=${CC:-gcc}
OUT=build/hosttest
mkdir -p "$OUT"
fail=0
run() {   # run NAME SOURCES...
    name=$1; shift
    srcs=""; for f in "$@"; do srcs="$srcs source/$f"; done
    $CC -O2 -std=gnu11 -Isource "tests/test_$name.c" $srcs -o "$OUT/t_$name" -lm
    printf '== %s\n' "$name"
    if ! "$OUT/t_$name"; then fail=1; fi
}
run chess   chess.c                                 # move generator: perft against published node counts
run games   chess.c                                 # replays real Lichess games of every variant (tests/data)
run san     chess.c                                 # SAN round trips
run premove chess.c premove.c                       # multi-premove queue
run fics    chess.c fics_parse.c                    # FICS style-12 / line parser
run puzzle  chess.c puzzle.c                        # puzzle solution checking
run engine  chess.c engine.c                        # mates, tactics, legal self-play
run learn   chess.c engine.c puzzle.c learn_data.c  # opening lines and endgames are legal and solvable
run bots    chess.c engine.c bots.c                 # ladder: each bot should beat the one below it
[ $fail -eq 0 ] && echo "ALL TESTS PASSED" || { echo "SOME TESTS FAILED"; exit 1; }
