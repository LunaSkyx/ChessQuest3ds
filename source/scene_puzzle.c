// Puzzles: Lichess puzzle batches (cached on the SD card, so they also work offline), rated results reported back
#include "scenes.h"
#include "ui.h"
#include "board.h"
#include "audio.h"
#include "analysis.h"
#include "puzzle.h"
#include "learn_data.h"
static int g_storm_best;
#include <jansson.h>
#include <math.h>

#define PZFILE DATA_DIR "/puzzles.json"

static const char *ANGLES[16] = {"mix", "mateIn1", "mateIn2", "mateIn3", "fork", "pin", "skewer", "hangingPiece",
                                 "backRankMate", "discoveredAttack", "endgame", "middlegame", "opening", "sacrifice", "defensiveMove", "advancedPawn"};
static const char *ANGLE_LABELS[16] = {"Mix", "Mate in 1", "Mate in 2", "Mate in 3", "Fork", "Pin", "Skewer", "Hanging",
                                       "Back rank", "Discovered", "Endgame", "Midgame", "Opening", "Sacrifice", "Defence", "Adv. pawn"};
static const char *DIFFS[5] = {"easiest", "easier", "normal", "harder", "hardest"};
static const char *DIFF_LABELS[5] = {"Easiest", "Easier", "Normal", "Harder", "Hardest"};

// ------------------------------------------------------------------ persistent queue
static json_t *g_q, *g_pending;          // queued puzzles (API entries) / results not yet sent
static int g_rating;                     // your puzzle rating (0 unknown)
static bool g_loaded_file;
static int g_req_batch, g_req_daily, g_req_solve, g_sent_n;
static u64 g_net_retry;                  // do not hit the network again before this time
static char g_err[64];
static int g_solved_n, g_failed_n, g_streak;

static void pz_save(void) {
    json_t *o = json_object();
    json_object_set(o, "q", g_q); json_object_set(o, "p", g_pending);
    json_object_set_new(o, "rating", json_integer(g_rating));
    json_object_set_new(o, "storm_best", json_integer(g_storm_best));
    char *s = json_dumps(o, 0);
    if (s) { write_file(PZFILE, s, strlen(s)); free(s); }
    json_decref(o);
}

static void pz_load(void) {
    if (g_loaded_file) return;
    g_loaded_file = true;
    g_q = json_array(); g_pending = json_array();
    size_t len; char *txt = read_file(PZFILE, &len);
    if (!txt) return;
    json_error_t e; json_t *o = json_loads(txt, 0, &e); free(txt);
    if (!o) return;
    json_t *q = json_object_get(o, "q"), *p = json_object_get(o, "p");
    size_t i; json_t *v;
    if (json_is_array(q)) json_array_foreach(q, i, v) json_array_append(g_q, v);
    if (json_is_array(p)) json_array_foreach(p, i, v) json_array_append(g_pending, v);
    g_rating = (int)json_integer_value(json_object_get(o, "rating"));
    g_storm_best = (int)json_integer_value(json_object_get(o, "storm_best"));
    json_decref(o);
}

static bool queue_has(const char *id) {
    size_t i; json_t *v;
    json_array_foreach(g_q, i, v) { const char *x = json_string_value(json_object_get(json_object_get(v, "puzzle"), "id")); if (x && !strcmp(x, id)) return true; }
    return false;
}

static void queue_add_batch(json_t *arr) {
    size_t i; json_t *v; int added = 0;
    if (!json_is_array(arr)) return;
    json_array_foreach(arr, i, v) {
        const char *id = json_string_value(json_object_get(json_object_get(v, "puzzle"), "id"));
        if (!id || queue_has(id) || json_array_size(g_q) >= 40) continue;
        json_array_append(g_q, v); added++;
    }
    LOGF("puzzle: +%d queued (%d total)", added, (int)json_array_size(g_q));
}

// ------------------------------------------------------------------ puzzle state
typedef enum { S_NONE, S_LOADING, S_INTRO, S_PLAY, S_REPLY, S_WRONG, S_SHOW, S_DONE, S_STORMWAIT } PzState;
static int g_mode;                       // 0 puzzles, 1 puzzle storm, 2 opening trainer
static int g_start_mode, g_start_idx = -1;   // requested by the Learn hub, consumed by pz_enter
static int g_open_idx;
static float g_storm_left;               // seconds
static int g_storm_score, g_storm_fails; static bool g_storm_over;
static PzState g_state;
static Puzzle P;
static bool g_has, g_daily;
static Pos g_pos, g_saved;
static int g_step;
static bool g_failed, g_reported, g_gave_up, g_hint;
static float g_t, g_wait_t, g_done_t;
static Move g_wrong_mv;
static BoardView bv;
static bool g_menu, g_picker, g_promo, g_over;
static int g_pp_from, g_pp_to;
static char g_diff_id[12]; static int g_diff; static bool g_diff_valid;   // rating change of the puzzle that was just finished

static void refresh_marks(void) {
    if (g_state == S_INTRO && P.has_last) { bv.last_from = bv.last_to = -1; }
    else if (g_step == 0 && P.last_from >= 0 && g_state != S_NONE) { bv.last_from = P.last_from; bv.last_to = P.last_to; }
    bv.check_sq = pos_in_check(&g_pos, g_pos.turn) ? pos_king_sq(&g_pos, g_pos.turn) : -1;
}

static void clear_sel(void) { bv.sel = -1; bv.dests = bv.cap_dests = 0; bv.dragging = false; bv.drag_sq = -1; bv.drag_piece = 0; }

static const char *jstr(json_t *o, const char *k) { const char *s = json_string_value(json_object_get(o, k)); return s ? s : ""; }

static bool parse_entry(json_t *e, Puzzle *pz) {
    memset(pz, 0, sizeof *pz);
    json_t *g = json_object_get(e, "game"), *p = json_object_get(e, "puzzle");
    if (!json_is_object(g) || !json_is_object(p)) return false;
    snprintf(pz->id, sizeof pz->id, "%s", jstr(p, "id"));
    pz->rating = (int)json_integer_value(json_object_get(p, "rating"));
    pz->plays = (int)json_integer_value(json_object_get(p, "plays"));
    snprintf(pz->game_id, sizeof pz->game_id, "%s", jstr(g, "id"));
    snprintf(pz->perf, sizeof pz->perf, "%s", jstr(json_object_get(g, "perf"), "name"));
    json_t *th = json_object_get(p, "themes"), *sol = json_object_get(p, "solution"), *pl = json_object_get(g, "players");
    size_t i; json_t *v;
    pz->themes[0] = 0;
    if (json_is_array(th)) json_array_foreach(th, i, v) { const char *t = json_string_value(v); if (t && strlen(pz->themes) + strlen(t) + 2 < sizeof pz->themes) { if (pz->themes[0]) strcat(pz->themes, " "); strcat(pz->themes, t); } }
    if (!json_is_array(sol)) return false;
    json_array_foreach(sol, i, v) { const char *u = json_string_value(v); if (!u || pz->nsol >= PZ_MAX_SOL) break; snprintf(pz->sol[pz->nsol++], sizeof pz->sol[0], "%s", u); }
    if (json_is_array(pl)) json_array_foreach(pl, i, v) {
        bool white = !strcmp(jstr(v, "color"), "white");
        char *dst = white ? pz->white : pz->black; int *r = white ? &pz->wr : &pz->br;
        snprintf(dst, 32, "%s", jstr(v, "name")); *r = (int)json_integer_value(json_object_get(v, "rating"));
    }
    if (!pz_setup_from_pgn(pz, jstr(g, "pgn"))) return false;
    return pz_validate(pz);
}

static void begin_puzzle(void) {
    g_has = true; g_step = 0; g_failed = g_reported = g_gave_up = g_hint = false; g_diff_valid = false;
    g_menu = g_picker = g_promo = g_over = false; g_t = 0; g_done_t = 0;
    clear_sel(); board_anim_clear(&bv);
    board_set_flip(&bv, P.my_color == BLACK);
    if (P.has_last && g_mode != 1) { g_pos = P.prev; g_state = S_INTRO; }                 // the opponent's last move is played first
    else { g_pos = P.start; g_state = S_PLAY; }
    bv.last_from = bv.last_to = -1;
    refresh_marks();
}

static void fetch_more(void);
static void next_line(bool same) {
    if (!same) g_open_idx = (g_open_idx + 1) % OPEN_LINE_COUNT;
    Puzzle t;
    if (!learn_line_to_puzzle(&OPEN_LINES[g_open_idx], &t)) { ui_toast("Line could not be loaded"); return; }
    P = t; g_daily = false; begin_puzzle();
}

static void next_puzzle(void) {
    g_daily = false;
    if (g_mode == 2) { next_line(false); return; }
    while (json_array_size(g_q)) {
        json_t *e = json_array_get(g_q, 0);
        Puzzle t; bool ok = parse_entry(e, &t);
        json_array_remove(g_q, 0);
        if (ok) { P = t; pz_save(); begin_puzzle(); if (json_array_size(g_q) < 4) fetch_more(); return; }
    }
    pz_save();
    g_has = false; g_state = S_LOADING; g_wait_t = 0; g_err[0] = 0;
    fetch_more();
}

static void fetch_more(void) {
    if (g_req_batch || now_ms() < g_net_retry || g_api.net != 2) return;
    char path[160];
    snprintf(path, sizeof path, "/api/puzzle/batch/%s?nb=10&difficulty=%s", ANGLES[g_set.pz_angle & 15], DIFFS[g_set.pz_diff % 5]);
    g_req_batch = li_request(TAG_PUZ, "GET", path, NULL, false);
}

static void fetch_daily(void) {
    if (g_req_daily || g_api.net != 2) { if (g_api.net != 2) ui_toast("Needs a connection"); return; }
    g_req_daily = li_request(TAG_PUZ, "GET", "/api/puzzle/daily", NULL, false);
    g_has = false; g_state = S_LOADING; g_wait_t = 0; g_err[0] = 0; g_menu = false;
}

static void submit_pending(void) {
    if (g_req_solve || !g_set.pz_rated || !json_array_size(g_pending) || g_api.net != 2) return;
    json_t *body = json_object(), *sols = json_array();
    size_t i; json_t *v;
    json_array_foreach(g_pending, i, v) {
        json_t *o = json_object();
        json_object_set(o, "id", json_object_get(v, "id")); json_object_set(o, "win", json_object_get(v, "win")); json_object_set_new(o, "rated", json_true());
        json_array_append_new(sols, o);
    }
    g_sent_n = (int)json_array_size(g_pending);
    json_object_set_new(body, "solutions", sols);
    char *b = json_dumps(body, JSON_COMPACT);
    char path[120]; snprintf(path, sizeof path, "/api/puzzle/batch/%s?nb=%d", ANGLES[g_set.pz_angle & 15], json_array_size(g_q) < 5 ? 10 : 0);
    if (b) { g_req_solve = li_request(TAG_PUZ, "POST", path, b, false); free(b); }
    json_decref(body);
}

static void report(bool win) {
    if (g_reported || g_daily || !g_has || g_mode != 0) return;
    g_reported = true;
    if (!g_set.pz_rated) return;
    json_t *o = json_object();
    json_object_set_new(o, "id", json_string(P.id)); json_object_set_new(o, "win", json_boolean(win));
    json_array_append_new(g_pending, o);
    pz_save();
    submit_pending();
}

void puzzle_net(int req, int curl_rc, long http, const char *body) {
    if (req == g_req_batch) {
        g_req_batch = 0;
        if (curl_rc || http != 200) { g_net_retry = now_ms() + 6000; snprintf(g_err, sizeof g_err, curl_rc ? "No connection" : "Lichess said HTTP %ld", http); return; }
        json_error_t e; json_t *o = json_loads(body, 0, &e);
        if (o) { queue_add_batch(json_object_get(o, "puzzles")); json_decref(o); pz_save(); }
    } else if (req == g_req_daily) {
        g_req_daily = 0;
        json_error_t e; json_t *o = (!curl_rc && http == 200) ? json_loads(body, 0, &e) : NULL;
        Puzzle t;
        if (o && parse_entry(o, &t)) { P = t; begin_puzzle(); g_daily = true; }
        else { snprintf(g_err, sizeof g_err, "Could not load the daily puzzle"); g_state = g_has ? S_PLAY : S_LOADING; }
        if (o) json_decref(o);
    } else if (req == g_req_solve) {
        g_req_solve = 0;
        if (curl_rc || http != 200) { LOGF("puzzle: solve upload failed rc=%d http=%ld", curl_rc, http); g_net_retry = now_ms() + 6000; if (http == 401 || http == 403) ui_toast("Token needs the puzzle:write permission"); return; }
        json_error_t e; json_t *o = json_loads(body, 0, &e);
        if (!o) return;
        json_t *gl = json_object_get(o, "glicko");
        if (json_is_object(gl)) { double r = json_number_value(json_object_get(gl, "rating")); if (r > 0) g_rating = (int)floor(r + 0.5); }
        json_t *rounds = json_object_get(o, "rounds"), *rv; size_t i;
        if (json_is_array(rounds)) json_array_foreach(rounds, i, rv) {
            if (!strcmp(jstr(rv, "id"), g_diff_id)) { /* already stored */ }
            if (g_has && !strcmp(jstr(rv, "id"), P.id)) { snprintf(g_diff_id, sizeof g_diff_id, "%s", P.id); g_diff = (int)json_integer_value(json_object_get(rv, "ratingDiff")); g_diff_valid = true; }
        }
        for (int k = 0; k < g_sent_n && json_array_size(g_pending); k++) json_array_remove(g_pending, 0);
        g_sent_n = 0;
        queue_add_batch(json_object_get(o, "puzzles"));
        json_decref(o);
        pz_save();
        LOGF("puzzle: results uploaded, rating %d", g_rating);
        submit_pending();
    }
}

// ------------------------------------------------------------------ playing
static void finish(bool solved) {
    clear_sel(); g_menu = false;
    if (g_mode == 1) {                                         // storm: score and move straight on
        if (solved && !g_failed) { g_storm_score++; audio_play(SND_PROMOTE); }
        g_state = S_STORMWAIT; g_t = 0;
        return;
    }
    if (g_mode == 2) { g_state = S_DONE; g_done_t = 0; audio_play(SND_WIN); return; }
    g_state = S_DONE; g_done_t = 0;
    if (solved && !g_gave_up) {
        if (!g_failed) { g_solved_n++; g_streak++; } else { g_failed_n++; g_streak = 0; }
        report(!g_failed);
        audio_play(SND_WIN);
    } else { g_failed_n++; g_streak = 0; report(false); audio_play(SND_LOSE); }
}

static void anim(const Pos *pre, Move m, float dur) {
    if (!g_set.animations) return;
    uint8_t pc = pre->b[m.from], victim = pre->b[m.to];
    if (m.flags & MF_CASTLE) { board_anim_move(&bv, m.from, m.to, pc, 0, dur); int rs = castle_rook_sq(pre, m), rk = RANK_OF(m.from); board_anim_move(&bv, rs, SQ(FILE_OF(m.to) > FILE_OF(m.from) ? 5 : 3, rk), MKPC(pre->turn, ROOK), 0, dur); return; }
    board_anim_move(&bv, m.from, m.to, m.promo ? MKPC(pre->turn, PAWN) : pc, (m.flags & MF_EP) ? 0 : victim, dur);
}

static void play_solution_move(float dur, bool mine) {
    Move m;
    if (g_step >= P.nsol || !pos_parse_uci(&g_pos, P.sol[g_step], &m)) { finish(true); return; }
    Pos pre = g_pos; pos_do(&g_pos, m); g_step++;
    audio_play_for_move(&pre, m, &g_pos, mine);
    anim(&pre, m, dur);
    bv.last_from = m.from; bv.last_to = m.to;
    refresh_marks();
}

static void try_move(Move m) {
    Pos pre = g_pos;
    PzVerdict v = pz_check(&P, &g_pos, g_step, m);
    if (v == PZ_WRONG) {
        if (!g_failed) { g_failed = true; report(false); }
        g_saved = g_pos; g_wrong_mv = m;
        pos_do(&g_pos, m);
        audio_play_for_move(&pre, m, &g_pos, true);
        anim(&pre, m, 0.12f);
        bv.last_from = m.from; bv.last_to = m.to;
        refresh_marks();
        g_state = S_WRONG; g_t = 0; g_hint = false;
        if (g_mode == 1) { g_storm_left -= 10.0f; g_storm_fails++; ui_toast("-10 seconds"); }
        else ui_toast(g_mode == 2 ? "Not the book move" : "That is not the move");
        return;
    }
    pos_do(&g_pos, m);
    audio_play_for_move(&pre, m, &g_pos, true);
    anim(&pre, m, 0.12f);
    bv.last_from = m.from; bv.last_to = m.to;
    g_hint = false;
    g_step++;
    refresh_marks();
    if (v == PZ_CORRECT_MATE || g_step >= P.nsol) { finish(true); return; }
    g_state = S_REPLY; g_t = 0;
}

static void set_dests(int sq) {
    bv.sel = sq; bv.dests = bv.cap_dests = 0;
    Move mv[64]; int n = pos_moves_from(&g_pos, sq, mv);
    for (int i = 0; i < n; i++) { bv.dests |= 1ULL << mv[i].to; if (mv[i].flags & MF_CAPTURE) bv.cap_dests |= 1ULL << mv[i].to; }
}

static void commit(int from, int to) {
    Move mv[64]; int n = pos_moves_from(&g_pos, from, mv), found = -1, cnt = 0;
    for (int i = 0; i < n; i++) if (mv[i].to == to) { if (found < 0) found = i; cnt++; }
    if (found < 0) { audio_play(SND_ILLEGAL); return; }
    if (cnt > 1 || mv[found].promo) {
        if (g_set.auto_queen) { for (int i = 0; i < n; i++) if (mv[i].to == to && mv[i].promo == QUEEN) { try_move(mv[i]); return; } }
        g_promo = true; g_pp_from = from; g_pp_to = to; return;
    }
    try_move(mv[found]);
}

static bool my_move_now(void) { return g_state == S_PLAY && g_pos.turn == P.my_color && !g_menu && !g_picker && !g_promo && !g_over; }

static void on_press(int sq, bool touch, float px, float py) {
    if (sq < 0) { clear_sel(); return; }
    if (bv.sel >= 0 && ((bv.dests >> sq) & 1)) { int f = bv.sel; clear_sel(); commit(f, sq); return; }
    uint8_t pc = g_pos.b[sq];
    if (pc && PCOLOR(pc) == P.my_color) {
        set_dests(sq);
        if (touch) { bv.dragging = true; bv.drag_sq = sq; bv.drag_piece = pc; bv.px = px; bv.py = py; }
    } else clear_sel();
}

static void on_release(int sq) {
    if (!bv.dragging) return;
    bv.dragging = false;
    int from = bv.drag_sq;
    bv.drag_piece = 0; bv.drag_sq = -1;
    if (sq == from) return;
    if (sq >= 0 && ((bv.dests >> sq) & 1)) { int f = bv.sel; clear_sel(); commit(f, sq); } else clear_sel();
}

static void handle_input(void) {
    if (!my_move_now()) { if (bv.dragging) clear_sel(); return; }
    if (g_touch.pressed) { int sq = board_sq_at(&bv, g_touch.x, g_touch.y); if (sq >= 0) on_press(sq, true, (float)g_touch.x, (float)g_touch.y); }
    if (bv.dragging && g_touch.down) { bv.px = (float)g_touch.x; bv.py = (float)g_touch.y; }
    if (g_touch.released && bv.dragging) on_release(board_sq_at(&bv, g_touch.x, g_touch.y));
    if (g_kdown & (KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT)) {
        if (bv.cursor < 0) bv.cursor = SQ(4, 1);
        else {
            int f = FILE_OF(bv.cursor), r = RANK_OF(bv.cursor), s = bv.flipped ? -1 : 1;
            if (g_kdown & KEY_DUP) r += s;
            if (g_kdown & KEY_DDOWN) r -= s;
            if (g_kdown & KEY_DRIGHT) f += s;
            if (g_kdown & KEY_DLEFT) f -= s;
            bv.cursor = SQ((f + 8) & 7, (r + 8) & 7);
        }
    }
    if ((g_kdown & KEY_A) && bv.cursor >= 0) on_press(bv.cursor, false, 0, 0);
    if (g_kdown & KEY_B) clear_sel();
}

static void show_solution(void) {
    if (g_state != S_PLAY || g_pos.turn != P.my_color) return;
    g_gave_up = true; g_failed = true; report(false);
    g_menu = false; clear_sel(); g_state = S_SHOW; g_t = 0;
}

// test hook for scenarios: "right" plays the expected move, "wrong" plays some other legal move, "state" returns the state number
int puzzle_test(const char *what) {
    if (!strcmp(what, "state")) return (int)g_state;
    if (g_state != S_PLAY || g_pos.turn != P.my_color) return -1;
    Move m;
    if (!strcmp(what, "right")) { if (pos_parse_uci(&g_pos, P.sol[g_step], &m)) { try_move(m); return 1; } }
    if (!strcmp(what, "wrong")) {
        Move mv[MAX_MOVES]; int n = pos_legal_moves(&g_pos, mv);
        for (int i = 0; i < n; i++) if (pz_check(&P, &g_pos, g_step, mv[i]) == PZ_WRONG) { try_move(mv[i]); return 1; }
    }
    return 0;
}

// ------------------------------------------------------------------ scene
void puzzle_start_storm(void) { g_start_mode = 1; scene_goto(SC_PUZZLE); }
void puzzle_start_opening(int idx) { g_start_mode = 2; g_start_idx = idx; scene_goto(SC_PUZZLE); }
void puzzle_open_normal(void) {
    if (g_mode != 0) { g_mode = 0; g_has = false; g_state = S_NONE; }
    scene_goto(SC_PUZZLE);
}

static void pz_enter(void) {
    gfx_set_piece_set(g_set.piece_set);
    board_reset(&bv);
    bv.ox = g_set.board_left ? 0 : 80;
    pz_load();
    g_menu = g_picker = g_promo = g_over = false;
    g_wait_t = 0; g_err[0] = 0;
    if (g_api.puzzle_rating && !g_rating) g_rating = g_api.puzzle_rating;
    if (g_start_mode) {                                           // opened from the Learn hub
        g_mode = g_start_mode; g_start_mode = 0; g_has = false; g_state = S_NONE;
        if (g_mode == 1) { g_storm_left = 180.0f; g_storm_score = g_storm_fails = 0; g_storm_over = false; if (json_array_size(g_q) < 12) fetch_more(); next_puzzle(); }
        else { g_open_idx = g_start_idx < 0 ? 0 : g_start_idx; next_line(true); }
    }
    else if (g_has && g_state != S_LOADING && g_state != S_NONE) { board_set_flip(&bv, P.my_color == BLACK); refresh_marks(); }
    else next_puzzle();
    submit_pending();
}

static void pz_leave(void) {
    if (g_has && !g_reported && g_failed) report(false);
    pz_save();
}

static void pz_update(float dt) {
    g_wait_t += dt; g_t += dt;
    board_update(&bv, dt);
    bv.ox = g_set.board_left ? 0 : 80;
    if (g_api.puzzle_rating && !g_rating) g_rating = g_api.puzzle_rating;
    if (g_mode == 1 && !g_storm_over && g_has && (g_state == S_INTRO || g_state == S_PLAY || g_state == S_REPLY || g_state == S_WRONG || g_state == S_STORMWAIT)) {
        g_storm_left -= dt;
        if (g_storm_left <= 0) {
            g_storm_left = 0; g_storm_over = true; g_state = S_DONE; g_done_t = 0; clear_sel();
            if (g_storm_score > g_storm_best) { g_storm_best = g_storm_score; pz_save(); }
            audio_play(SND_WIN);
        }
    }
    switch (g_state) {
    case S_LOADING:
        if (json_array_size(g_q)) { next_puzzle(); break; }
        if (!g_req_batch && !g_req_daily && now_ms() >= g_net_retry) fetch_more();
        break;
    case S_INTRO:
        if (g_t > 0.55f) { Move m = P.last; Pos pre = g_pos; pos_do(&g_pos, m); anim(&pre, m, 0.2f); audio_play_for_move(&pre, m, &g_pos, false); g_state = S_PLAY; g_t = 0; bv.last_from = m.from; bv.last_to = m.to; refresh_marks(); }
        break;
    case S_REPLY:
        if (g_t > 0.45f) { play_solution_move(0.2f, false); if (g_state == S_REPLY) { if (g_step >= P.nsol) finish(true); else { g_state = S_PLAY; g_t = 0; } } }
        break;
    case S_WRONG:
        if (g_t > 0.85f) {
            Move m = g_wrong_mv; uint8_t pc = g_saved.b[m.from];
            g_pos = g_saved;
            if (g_set.animations) board_anim_move(&bv, m.to, m.from, m.promo ? MKPC(g_saved.turn, PAWN) : pc, 0, 0.18f);
            refresh_marks();
            g_state = S_PLAY; g_t = 0;
            if (g_mode == 1) next_puzzle();                       // storm: a mistake costs the puzzle
            else if (g_mode == 2) g_hint = true;                  // trainer: show which piece to move
        }
        break;
    case S_STORMWAIT:
        if (g_t > 0.3f) next_puzzle();
        break;
    case S_SHOW:
        if (g_t > 0.8f) { g_t = 0; play_solution_move(0.2f, g_pos.turn == P.my_color); if (g_state == S_SHOW && g_step >= P.nsol) finish(true); }
        break;
    case S_DONE:
        g_done_t += dt;
        if (g_done_t > 0.7f && !g_over) g_over = true;
        break;
    default: break;
    }
    handle_input();
    if (g_kdown & KEY_X) board_set_flip(&bv, !bv.flipped);
    if (g_kdown & KEY_START) { if (g_state == S_DONE) g_over = !g_over; else g_menu = !g_menu; }
}

// ------------------------------------------------------------------ top screen
static void themes_line(float y) {
    char chips[160]; snprintf(chips, sizeof chips, "%s", P.themes);
    float x = 14; char *sv = NULL;
    for (char *t = strtok_r(chips, " ", &sv); t; t = strtok_r(NULL, " ", &sv)) {
        char l[40]; pz_theme_label(t, l, sizeof l);
        float w = g_text_w(F_R11, l) + 14;
        if (x + w > 388) { y += 20; x = 14; if (y > 224) break; }
        g_rrect(x, y - 8, w, 17, 8, C_PANEL2);
        g_text(F_R11, x + 7, y, C_TEXT2, AL_LEFT, l);
        x += w + 5;
    }
}

static void top_draw(void) {
    ui_top_bg();
    g_rrect(6, 6, 388, 46, 8, C_PANEL);
    g_icon(g_mode == 1 ? ICON_ZAP : (g_mode == 2 ? ICON_CROWN : ICON_FLAME), 28, 29, 24, C_ORANGE);
    if (g_mode == 1) {
        g_text(F_B16, 48, 22, C_TEXT, AL_LEFT, "Puzzle Storm");
        g_textf(F_R11, 48, 38, C_TEXT3, AL_LEFT, "score %d   -   best %d   -   -10 s per mistake", g_storm_score, g_storm_best);
        char t[16]; snprintf(t, sizeof t, "%d:%02d", (int)g_storm_left / 60, (int)g_storm_left % 60);
        g_text(F_B21, 384, 28, g_storm_left < 20 ? C_RED : C_TEXT, AL_RIGHT, t);
    } else if (g_mode == 2) {
        g_text_fit(F_B16, 48, 22, C_TEXT, AL_LEFT, 250, OPEN_LINES[g_open_idx].name);
        g_textf(F_R11, 48, 38, C_TEXT3, AL_LEFT, "Opening trainer  -  you play %s", OPEN_LINES[g_open_idx].color == WHITE ? "White" : "Black");
    } else if (g_has) {
        g_textf(F_B16, 48, 22, C_TEXT, AL_LEFT, "%s", g_daily ? "Puzzle of the day" : "Puzzle");
        if (g_state == S_DONE) g_textf(F_R11, 48, 38, C_TEXT3, AL_LEFT, "#%s  -  rating %d  -  %d plays", P.id, P.rating, P.plays);
        else g_text(F_R11, 48, 38, C_TEXT3, AL_LEFT, "rating hidden until you finish");
    } else g_text(F_B16, 48, 29, C_TEXT, AL_LEFT, "Puzzles");
    if (g_rating && g_mode == 0) { g_textf(F_B21, 384, 22, C_TEXT, AL_RIGHT, "%d", g_rating); g_text(F_R11, 384, 40, C_TEXT3, AL_RIGHT, g_set.pz_rated ? "your rating" : "unrated mode"); }

    // status card
    g_rrect(6, 58, 388, 62, 8, C_PANEL);
    const char *line1 = ""; const char *line2 = ""; u32 col = C_TEXT;
    char l1[80], l2[80];
    if (!g_has) {
        if (g_err[0] && !g_req_batch) { line1 = "Could not load puzzles"; line2 = g_err; col = C_ORANGE; }
        else { line1 = "Loading puzzles..."; line2 = "They are saved on the SD card for offline play"; }
    } else if (g_state == S_DONE && g_mode == 1) { line1 = "Time is up!"; col = C_ORANGE; snprintf(l2, sizeof l2, "Score %d  -  %d mistakes", g_storm_score, g_storm_fails); line2 = l2;
    } else if (g_state == S_DONE && g_mode == 2) { line1 = "Line complete!"; col = C_GREEN; line2 = "You know this line - try the next one";
    } else if (g_state == S_DONE) {
        if (g_gave_up) { line1 = "Solution shown"; col = C_ORANGE; }
        else if (g_failed) { line1 = "Solved - with mistakes"; col = C_ORANGE; }
        else { line1 = "Puzzle solved!"; col = C_GREEN; }
        if (g_diff_valid) { snprintf(l2, sizeof l2, "Rating %s%d", g_diff >= 0 ? "+" : "", g_diff); line2 = l2; }
        else if (g_daily) line2 = "The daily puzzle is not rated";
        else if (!g_set.pz_rated) line2 = "Rated mode is off";
        else if (g_req_solve) line2 = "Updating your rating...";
        else line2 = json_array_size(g_pending) ? "Result will be sent when online" : "";
    } else if (g_state == S_WRONG) {
        line1 = g_mode == 2 ? "Not the book move" : "That is not the move"; col = C_RED;
        line2 = g_mode == 1 ? "-10 seconds" : (g_mode == 2 ? "Try again - the piece to move is marked" : "Try again - the puzzle counts as failed");
    }
    else if (g_state == S_INTRO) { line1 = "Get ready..."; }
    else {
        snprintf(l1, sizeof l1, g_mode == 2 ? "Your turn - play the book move for %s" : "Your turn - find the best move for %s", P.my_color == WHITE ? "White" : "Black"); line1 = l1; col = C_GREEN_L;
        if (g_step > 0) line2 = "Correct! Keep going";
        else if (P.last_from >= 0) line2 = "Your opponent just moved (highlighted)";
    }
    g_text_fit(F_B16, 200, 80, col, AL_CENTER, 372, line1);
    g_text_fit(F_R13, 200, 102, C_TEXT3, AL_CENTER, 372, line2);

    // progress dots: one per move of yours
    if (g_has) {
        int mine = (P.nsol + 1) / 2, done = (g_step + 1) / 2;
        if (g_state == S_DONE) done = mine;
        float w = mine * 16.0f, x = 200 - w / 2 + 8;
        for (int i = 0; i < mine && i < 14; i++) g_disc(x + i * 16.0f, 134, 10, i < done ? C_GREEN : C_PANEL2);
    }
    // finished: details
    g_rrect(6, 148, 388, 86, 8, C_BG);
    if (g_mode == 1) {
        g_textf(F_R13, 200, 178, C_TEXT2, AL_CENTER, "Solved: %d     Mistakes: %d", g_storm_score, g_storm_fails);
        g_text(F_R11, 200, 204, C_TEXT3, AL_CENTER, "Solve as many puzzles as you can in 3 minutes");
    } else if (g_mode == 2) {
        g_textf(F_R13, 200, 178, C_TEXT2, AL_CENTER, "Line %d of %d", g_open_idx + 1, OPEN_LINE_COUNT);
        g_text(F_R11, 200, 204, C_TEXT3, AL_CENTER, "Play the moves of the main line from memory");
    } else if (g_has && g_state == S_DONE) {
        themes_line(164);
        g_textf(F_R11, 14, 216, C_TEXT3, AL_LEFT, "From a %s game: %s (%d) vs %s (%d)", P.perf, P.white, P.wr, P.black, P.br);
    } else {
        g_textf(F_R13, 200, 176, C_TEXT2, AL_CENTER, "Solved this session: %d     Streak: %d", g_solved_n, g_streak);
        g_textf(F_R11, 200, 200, C_TEXT3, AL_CENTER, "%s  -  %s%s", ANGLE_LABELS[g_set.pz_angle & 15], DIFF_LABELS[g_set.pz_diff % 5], json_array_size(g_q) ? "" : "");
        g_textf(F_R11, 200, 218, C_TEXT3, AL_CENTER, "%d puzzle%s saved", (int)json_array_size(g_q), json_array_size(g_q) == 1 ? "" : "s");
    }
}

// ------------------------------------------------------------------ bottom screen
static void draw_promo(void) {
    g_rect(0, 0, 320, 240, RGBA(0, 0, 0, 140));
    int ox = bv.ox;
    g_rrect(ox + 6, 82, 228, 76, 12, C_PANEL);
    g_text(F_B13, ox + 120, 96, C_TEXT2, AL_CENTER, "Promote to");
    static const int T[4] = {QUEEN, ROOK, BISHOP, KNIGHT};
    for (int i = 0; i < 4; i++) {
        Rect r = {ox + 14.0f + i * 54, 106, 48, 44};
        if (ui_button(r, NULL, F_B11, BS_TILE, -1)) {
            g_promo = false;
            Move mv[64]; int n = pos_moves_from(&g_pos, g_pp_from, mv);
            for (int k = 0; k < n; k++) if (mv[k].to == g_pp_to && mv[k].promo == T[i]) { try_move(mv[k]); break; }
        }
        g_piece(1, g_pos.turn, T[i], r.x + 5, r.y - 1, 0, 1.0f);
    }
}

static void draw_menu(void) {
    g_rect(0, 0, 320, 240, RGBA(0, 0, 0, 150));
    float cx = bv.ox + 120;
    g_rrect(cx - 104, 22, 208, 196, 12, C_PANEL);
    g_text(F_B16, cx, 40, C_TEXT, AL_CENTER, "Puzzles");
    float y = 54;
    if (g_mode == 1) {
        if (ui_button((Rect){cx - 92, y, 184, 30}, "Quit the storm", F_B13, BS_DANGER, ICON_X)) { g_menu = false; scene_goto(SC_LEARN); return; }
        y += 36;
        if (ui_button((Rect){cx - 92, y, 184, 30}, "Close", F_B13, BS_GHOST, -1)) g_menu = false;
        return;
    }
    if (g_mode == 2) {
        if (ui_button((Rect){cx - 92, y, 184, 30}, "Repeat this line", F_B13, BS_SECONDARY, ICON_ROTATE_CCW)) { g_menu = false; next_line(true); return; }
        y += 36;
        if (ui_button((Rect){cx - 92, y, 184, 30}, "Next line", F_B13, BS_SECONDARY, ICON_SKIP_FORWARD)) { g_menu = false; next_line(false); return; }
        y += 36;
        if (ui_button((Rect){cx - 92, y, 184, 30}, "Back to Learn", F_B13, BS_SECONDARY, ICON_ARROW_LEFT)) { g_menu = false; scene_goto(SC_LEARN); return; }
        y += 36;
        if (ui_button((Rect){cx - 92, y, 184, 30}, "Close", F_B13, BS_GHOST, -1)) g_menu = false;
        return;
    }
    if (ui_button((Rect){cx - 92, y, 184, 28}, "Skip to next puzzle", F_B13, BS_SECONDARY, ICON_SKIP_FORWARD)) { g_menu = false; next_puzzle(); return; }
    y += 33;
    if (ui_button((Rect){cx - 92, y, 184, 28}, "Show the solution", F_B13, BS_SECONDARY, ICON_EYE)) show_solution();
    y += 33;
    if (ui_button((Rect){cx - 92, y, 184, 28}, "Puzzle of the day", F_B13, BS_SECONDARY, ICON_CROWN)) fetch_daily();
    y += 33;
    if (ui_button((Rect){cx - 92, y, 184, 28}, "Theme & difficulty", F_B13, BS_SECONDARY, ICON_SETTINGS)) { g_menu = false; g_picker = true; }
    y += 33;
    if (ui_button((Rect){cx - 92, y, 88, 28}, "Home", F_B13, BS_SECONDARY, ICON_HOUSE)) { g_menu = false; scene_goto(SC_HOME); return; }
    if (ui_button((Rect){cx + 4, y, 88, 28}, "Close", F_B13, BS_GHOST, -1)) g_menu = false;
}

static void draw_picker(void) {
    g_rect(0, 0, 320, 240, RGBA(0, 0, 0, 170));
    g_rrect(4, 4, 312, 232, 12, C_PANEL);
    g_text(F_B13, 160, 18, C_TEXT, AL_CENTER, "Puzzle theme");
    bool changed = false;
    for (int i = 0; i < 16; i++) {
        Rect r = {10 + (i % 4) * 75.0f, 30 + (i / 4) * 31.0f, 71, 27};
        if (ui_button(r, NULL, F_B11, i == (g_set.pz_angle & 15) ? BS_TILE_SEL : BS_TILE, -1)) { if (g_set.pz_angle != i) changed = true; g_set.pz_angle = i; }
        g_text_fit(F_B11, r.x + r.w / 2, r.y + 13, C_TEXT, AL_CENTER, r.w - 6, ANGLE_LABELS[i]);
    }
    g_text(F_B13, 160, 162, C_TEXT, AL_CENTER, "Difficulty");
    for (int i = 0; i < 5; i++) {
        Rect r = {10 + i * 60.0f, 172, 56, 26};
        if (ui_button(r, NULL, F_B11, i == g_set.pz_diff ? BS_TILE_SEL : BS_TILE, -1)) { if (g_set.pz_diff != i) changed = true; g_set.pz_diff = i; }
        g_text_fit(F_B11, r.x + r.w / 2, r.y + 12, C_TEXT, AL_CENTER, r.w - 4, DIFF_LABELS[i]);
    }
    if (ui_button((Rect){10, 204, 150, 26}, g_set.pz_rated ? "Rated: on" : "Rated: off", F_B11, g_set.pz_rated ? BS_TILE_SEL : BS_SECONDARY, -1)) g_set.pz_rated = !g_set.pz_rated;
    if (ui_button((Rect){170, 204, 140, 26}, "Done", F_B13, BS_PRIMARY, ICON_CHECK)) {
        g_picker = false; settings_save();
        if (changed) { while (json_array_size(g_q)) json_array_remove(g_q, 0); next_puzzle(); }
    }
}

static void draw_over(void) {
    g_rect(0, 0, 320, 240, RGBA(0, 0, 0, 150));
    float cx = bv.ox + 120;
    if (g_mode == 1) {
        g_rrect(cx - 108, 34, 216, 168, 12, C_PANEL);
        g_icon(ICON_ZAP, cx, 58, 24, C_ORANGE);
        g_text(F_B21, cx, 86, C_TEXT, AL_CENTER, "Time is up!");
        g_textf(F_B34, cx, 122, C_GREEN_L, AL_CENTER, "%d", g_storm_score);
        g_textf(F_R13, cx, 146, g_storm_score >= g_storm_best && g_storm_score > 0 ? C_ORANGE : C_TEXT3, AL_CENTER, g_storm_score >= g_storm_best && g_storm_score > 0 ? "New best!" : "best %d", g_storm_best);
        if (ui_button((Rect){cx - 96, 158, 94, 32}, "Again", F_B13, BS_PRIMARY, ICON_REFRESH_CW)) { g_over = false; g_storm_left = 180.0f; g_storm_score = g_storm_fails = 0; g_storm_over = false; next_puzzle(); }
        if (ui_button((Rect){cx + 2, 158, 94, 32}, "Back", F_B13, BS_SECONDARY, ICON_ARROW_LEFT)) scene_goto(SC_LEARN);
        return;
    }
    if (g_mode == 2) {
        g_rrect(cx - 108, 34, 216, 168, 12, C_PANEL);
        g_icon(ICON_CROWN, cx, 60, 24, C_GREEN);
        g_text(F_B21, cx, 90, C_GREEN, AL_CENTER, "Line complete!");
        g_text_fit(F_R13, cx, 114, C_TEXT3, AL_CENTER, 200, OPEN_LINES[g_open_idx].name);
        if (ui_button((Rect){cx - 96, 128, 192, 32}, "Next line", F_B16, BS_PRIMARY, ICON_PLAY)) { g_over = false; next_line(false); return; }
        if (ui_button((Rect){cx - 96, 166, 62, 28}, "Repeat", F_B11, BS_SECONDARY, ICON_ROTATE_CCW)) { g_over = false; next_line(true); return; }
        if (ui_button((Rect){cx - 30, 166, 62, 28}, "Analyse", F_B11, BS_SECONDARY, ICON_TARGET)) {
            ag_reset(VAR_STANDARD, NULL);
            Pos t; pos_init(&t, VAR_STANDARD); (void)t;
            Puzzle q; if (learn_line_to_puzzle(&OPEN_LINES[g_open_idx], &q)) {
                if (q.has_last) { char u[8]; move_to_uci(&q.prev, q.last, u); ag_push_uci(u); }
                for (int i = 0; i < q.nsol; i++) ag_push_uci(q.sol[i]);
            }
            scene_analysis_open(0, SC_PUZZLE);
            return;
        }
        if (ui_button((Rect){cx + 36, 166, 60, 28}, "Back", F_B11, BS_SECONDARY, ICON_ARROW_LEFT)) scene_goto(SC_LEARN);
        return;
    }
    g_rrect(cx - 108, 34, 216, 168, 12, C_PANEL);
    bool good = !g_failed;
    g_icon(good ? ICON_TROPHY : ICON_FLAG, cx, 60, 24, good ? C_GREEN : C_ORANGE);
    g_text(F_B21, cx, 90, good ? C_GREEN : C_TEXT, AL_CENTER, good ? "Solved!" : (g_gave_up ? "Solution" : "Not quite"));
    if (g_diff_valid) g_textf(F_B16, cx, 114, g_diff >= 0 ? C_GREEN : C_RED, AL_CENTER, "%s%d", g_diff >= 0 ? "+" : "", g_diff);
    else if (g_req_solve) ui_spinner(cx, 114, 8, C_GREEN, g_wait_t);
    else if (g_rating) g_textf(F_R13, cx, 114, C_TEXT3, AL_CENTER, "rating %d", g_rating);
    if (ui_button((Rect){cx - 96, 128, 192, 32}, "Next puzzle", F_B16, BS_PRIMARY, ICON_PLAY)) { g_over = false; next_puzzle(); return; }
    if (ui_button((Rect){cx - 96, 166, 62, 28}, "Analyse", F_B11, BS_SECONDARY, ICON_TARGET)) {
        char fen[120]; pos_to_fen(&P.start, fen, sizeof fen);
        ag_reset(VAR_STANDARD, fen);
        for (int i = 0; i < P.nsol; i++) ag_push_uci(P.sol[i]);
        scene_analysis_open(0, SC_PUZZLE);
        return;
    }
    if (ui_button((Rect){cx - 30, 166, 62, 28}, "Board", F_B11, BS_GHOST, ICON_EYE)) g_over = false;
    if (ui_button((Rect){cx + 36, 166, 60, 28}, "Home", F_B11, BS_SECONDARY, ICON_HOUSE)) scene_goto(SC_HOME);
}

static void draw_side(void) {
    float x0 = g_set.board_left ? 240.0f : 0.0f;
    g_rect(x0, 0, 80, 240, C_BG);
    bool modal = g_menu || g_promo || g_picker || g_over;
    ui_set_enabled(!modal);
    if (ui_icon_button((Rect){x0 + 2, 6, 36, 34}, ICON_SEARCH, g_hint ? BS_PRIMARY : BS_SECONDARY) && my_move_now()) g_hint = true;
    if (ui_icon_button((Rect){x0 + 42, 6, 36, 34}, ICON_ARROW_DOWN_UP, BS_SECONDARY)) board_set_flip(&bv, !bv.flipped);
    if (ui_icon_button((Rect){x0 + 2, 44, 36, 34}, ICON_MENU, BS_SECONDARY)) { if (g_state == S_DONE) g_over = true; else g_menu = true; }
    if (g_mode != 1 && ui_icon_button((Rect){x0 + 42, 44, 36, 34}, ICON_SKIP_FORWARD, BS_SECONDARY)) next_puzzle();
    g_rrect(x0 + 3, 88, 74, 24, 12, C_PANEL2);
    g_disc(x0 + 17, 100, 12, P.my_color == WHITE ? RGB(240, 240, 240) : RGB(20, 20, 20));
    g_text(F_B11, x0 + 48, 100, C_TEXT2, AL_CENTER, g_has ? (P.my_color == WHITE ? "White" : "Black") : "-");
    if (g_has) {
        int mine = (P.nsol + 1) / 2, done = g_state == S_DONE ? mine : (g_step + 1) / 2;
        g_textf(F_R11, x0 + 40, 128, C_TEXT3, AL_CENTER, "Move %d of %d", done < mine ? done + 1 : mine, mine);
    }
    if (g_mode == 1) {
        g_textf(F_B21, x0 + 40, 160, C_TEXT, AL_CENTER, "%d", g_storm_score);
        g_text(F_R11, x0 + 40, 178, C_TEXT3, AL_CENTER, "score");
        g_textf(F_B16, x0 + 40, 202, g_storm_left < 20 ? C_RED : C_TEXT, AL_CENTER, "%d:%02d", (int)g_storm_left / 60, (int)g_storm_left % 60);
        g_text(F_R11, x0 + 40, 218, C_TEXT3, AL_CENTER, "left");
    } else if (g_mode == 0) {
        g_textf(F_B16, x0 + 40, 158, C_TEXT, AL_CENTER, "%d", g_solved_n);
        g_text(F_R11, x0 + 40, 174, C_TEXT3, AL_CENTER, "solved");
        g_textf(F_B16, x0 + 40, 198, g_streak ? C_ORANGE : C_TEXT, AL_CENTER, "%d", g_streak);
        g_text(F_R11, x0 + 40, 214, C_TEXT3, AL_CENTER, "streak");
    }
    ui_set_enabled(true);
}

static void bot_draw(void) {
    g_rect(0, 0, 320, 240, C_BG);
    bool modal = g_menu || g_promo || g_picker || g_over;
    ui_set_enabled(!modal);
    if (g_has) {
        board_draw(&bv, &g_pos);
        if (g_hint && my_move_now() && g_step < P.nsol) {                        // hint: mark the piece that has to move
            Move m;
            if (pos_parse_uci(&g_pos, P.sol[g_step], &m)) {
                float x, y; board_sq_center(&bv, m.from, &x, &y);
                float pulse = 0.5f + 0.5f * sinf(g_wait_t * 6.0f);
                g_rrect(x - 15, y - 15, 30, 30, 4, RGBA(255, 214, 0, (int)(70 + 70 * pulse)));
            }
        }
    } else {
        board_draw(&bv, &g_pos);
        g_rect(bv.ox, 0, 240, 240, RGBA(0, 0, 0, 120));
        if (g_err[0] && !g_req_batch) {
            g_text(F_B13, bv.ox + 120, 100, C_TEXT, AL_CENTER, "No puzzles available");
            g_text_fit(F_R11, bv.ox + 120, 120, C_TEXT3, AL_CENTER, 220, g_err);
            ui_set_enabled(true);
            if (ui_button((Rect){bv.ox + 60.0f, 140, 120, 30}, "Retry", F_B13, BS_PRIMARY, ICON_REFRESH_CW)) { g_net_retry = 0; g_err[0] = 0; fetch_more(); }
            ui_set_enabled(!modal);
        } else { ui_spinner(bv.ox + 120.0f, 112, 18, C_GREEN, g_wait_t); g_text(F_R13, bv.ox + 120, 150, C_TEXT2, AL_CENTER, "Loading puzzles..."); }
    }
    draw_side();
    ui_set_enabled(true);
    if (g_promo) draw_promo(); else if (g_menu) draw_menu(); else if (g_picker) draw_picker(); else if (g_over) draw_over();
    ui_set_enabled(true);
    ui_toast_draw((float)bv.ox + 120);
}

const Scene scene_puzzle = {"puzzle", pz_enter, pz_update, top_draw, bot_draw, pz_leave};
