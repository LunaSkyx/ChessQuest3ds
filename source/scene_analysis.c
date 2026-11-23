// Analysis board
#include "scenes.h"
#include "analysis.h"
#include "ui.h"
#include "board.h"
#include "audio.h"
#include "engine.h"
#include "lichess.h"
#include <jansson.h>
#include <math.h>

AGame g_ag;

#define A g_ag

// ------------------------------------------------------------------ game container
void ag_reset(Variant v, const char *fen) {
    memset(&A, 0, sizeof A);
    A.variant = v;
    if (!fen || !pos_from_fen(&A.start, fen, v)) pos_init(&A.start, v);
    snprintf(A.white, sizeof A.white, "White"); snprintf(A.black, sizeof A.black, "Black");
}

static Pos ag_pos_at(int ply) {
    Pos p = A.start;
    for (int i = 0; i < ply && i < A.n; i++) pos_do(&p, A.mv[i]);
    return p;
}

static bool ag_push_move(const Pos *p, Move m) {
    if (A.n >= AG_MAX) return false;
    move_to_uci(p, m, A.uci[A.n]);
    move_to_san(p, m, A.san[A.n], sizeof A.san[0]);
    A.mv[A.n] = m;
    A.n++;
    return true;
}

bool ag_push_uci(const char *uci) {
    Pos p = ag_pos_at(A.n); Move m;
    return pos_parse_uci(&p, uci, &m) && ag_push_move(&p, m);
}

bool ag_push_san(const char *san) {
    Pos p = ag_pos_at(A.n); Move m;
    return pos_parse_san(&p, san, &m) && ag_push_move(&p, m);
}

void ag_from_game(const Game *g) {
    ag_reset(g->variant, NULL);
    A.start = g->start;
    for (int c = 0; c < 2; c++) {
        char *dst = c == WHITE ? A.white : A.black; size_t dn = c == WHITE ? sizeof A.white : sizeof A.black;
        snprintf(dst, dn, "%s", g->pl[c].name);
    }
    A.wr = g->pl[WHITE].rating; A.br = g->pl[BLACK].rating;
    for (int i = 0; i < g->nply; i++) if (!ag_push_uci(g->uci[i])) break;
    char h[40], s[40]; game_result_text(g, h, sizeof h, s, sizeof s);
    const char *score = g->winner == WHITE ? "1-0" : (g->winner == BLACK ? "0-1" : "1/2");
    snprintf(A.result, sizeof A.result, "%s  %s", g->finished ? score : "*", g->finished ? s : "In progress");
    A.from_game = true;
}

// ------------------------------------------------------------------ scene state
static BoardView bv;
static int g_ply, g_back = SC_HOME;
static Pos g_pos;
static bool g_menu, g_promo;
static int g_pp_from, g_pp_to;
static u64 g_changed_at;               // last time the position changed (debounce for cloud requests)
static int g_req = 0, g_req_ply = -1;
static u64 g_backoff_until;
static char g_pv[96];                  // principal variation of the shown position (SAN)
static int g_depth;
static float g_wait_t, g_bar = 0.5f;   // animated eval bar fraction (white share)
static bool g_starting;
static int g_ex_req;                   // pending explorer request (defined below)
static bool g_rev; static int g_rev_i, g_rev_job, g_rev_total; static bool g_rev_busy;   // game review (engine pass over every ply)
static int g_loc_job; static bool g_loc_busy, g_pv_local;     // built-in engine fallback for positions the cloud does not know

static void refresh(void) {
    g_pos = ag_pos_at(g_ply);
    if (g_ply > 0) { Move m = A.mv[g_ply - 1]; bv.last_to = m.to; bv.last_from = (m.flags & MF_DROP) ? m.to : m.from; }
    else bv.last_from = bv.last_to = -1;
    bv.check_sq = pos_in_check(&g_pos, g_pos.turn) ? pos_king_sq(&g_pos, g_pos.turn) : -1;
    g_pv[0] = 0; g_depth = 0;
    g_changed_at = now_ms();
    if (g_req) { li_cancel(g_req); g_req = 0; }
    if (g_loc_busy) { if (!g_rev_busy) eng_async_cancel(); g_loc_busy = false; }
    g_pv_local = false;
    if (g_ex_req) { li_cancel(g_ex_req); g_ex_req = 0; }
}

static void clear_sel(void) { bv.sel = -1; bv.dests = bv.cap_dests = 0; bv.dragging = false; bv.drag_sq = -1; bv.drag_piece = 0; }

static void go(int p) {
    if (p < 0) p = 0;
    if (p > A.n) p = A.n;
    if (p == g_ply) return;
    g_ply = p; clear_sel(); refresh();
    audio_play(SND_CLICK);
}

void scene_analysis_open(int ply, SceneId back) {
    g_ply = ply < 0 ? 0 : (ply > A.n ? A.n : ply);
    g_back = back;
    scene_goto(SC_ANALYSIS);
}

// ------------------------------------------------------------------ opening explorer (explorer.lichess.ovh)
typedef struct { char uci[6], san[10]; long w, d, b; int avg; } ExMove;
typedef struct { uint64_t key; int n; ExMove m[8]; long tw, td, tb; char opening[48]; bool valid; } ExEntry;
static int g_ex_mode;                                   // 0 off, 1 Lichess games, 2 masters
static ExEntry g_ex[6]; static int g_ex_next;
static uint64_t g_ex_req_key; static u64 g_ex_backoff; static bool g_ex_fail;

static uint64_t ex_key(void) { return eng_hash(&g_pos) ^ ((uint64_t)g_ex_mode * 0x9E3779B97F4A7C15ULL); }
static const ExEntry *ex_current(void) { uint64_t k = ex_key(); for (int i = 0; i < 6; i++) if (g_ex[i].valid && g_ex[i].key == k) return &g_ex[i]; return NULL; }

static void request_explorer(void) {
    if (!g_ex_mode || g_ex_req || now_ms() < g_ex_backoff || now_ms() - g_changed_at < 450 || ex_current()) return;
    if (g_api.net != 2) return;
    if (g_ex_mode == 2 && A.variant != VAR_STANDARD) return;
    char fen[120], enc[300], path[560];
    pos_to_fen(&g_pos, fen, sizeof fen); li_url_encode(fen, enc, sizeof enc);
    if (g_ex_mode == 2) snprintf(path, sizeof path, "https://explorer.lichess.ovh/masters?fen=%s&moves=8&topGames=0", enc);
    else snprintf(path, sizeof path, "https://explorer.lichess.ovh/lichess?variant=%s&fen=%s&moves=8&topGames=0&recentGames=0", variant_key(A.variant), enc);
    g_ex_req = li_request(TAG_EXPL, "GET", path, NULL, false);
    g_ex_req_key = ex_key();
    g_ex_fail = false;
}

void analysis_net_explorer(int req, int curl_rc, long http, const char *body) {
    if (req != g_ex_req) return;
    g_ex_req = 0;
    if (curl_rc) { g_ex_backoff = now_ms() + 5000; g_ex_fail = true; return; }
    if (http == 401 || http == 403) { g_ex_backoff = now_ms() + 30000; g_ex_fail = true; ui_toast("Explorer needs a logged-in token"); return; }
    if (http != 200) { g_ex_backoff = now_ms() + (http == 429 ? 15000 : 6000); g_ex_fail = true; return; }
    json_error_t e; json_t *o = json_loads(body, 0, &e);
    if (!o) return;
    ExEntry *x = &g_ex[g_ex_next++ % 6]; memset(x, 0, sizeof *x);
    x->key = g_ex_req_key; x->valid = true;
    x->tw = (long)json_integer_value(json_object_get(o, "white")); x->td = (long)json_integer_value(json_object_get(o, "draws")); x->tb = (long)json_integer_value(json_object_get(o, "black"));
    const char *on = json_string_value(json_object_get(json_object_get(o, "opening"), "name"));
    snprintf(x->opening, sizeof x->opening, "%s", on ? on : "");
    json_t *mv = json_object_get(o, "moves"), *v; size_t i;
    if (json_is_array(mv)) json_array_foreach(mv, i, v) {
        if (x->n >= 8) break;
        ExMove *m = &x->m[x->n++];
        snprintf(m->uci, sizeof m->uci, "%.5s", json_string_value(json_object_get(v, "uci")) ? json_string_value(json_object_get(v, "uci")) : "");
        snprintf(m->san, sizeof m->san, "%.9s", json_string_value(json_object_get(v, "san")) ? json_string_value(json_object_get(v, "san")) : "");
        m->w = (long)json_integer_value(json_object_get(v, "white")); m->d = (long)json_integer_value(json_object_get(v, "draws")); m->b = (long)json_integer_value(json_object_get(v, "black"));
        m->avg = (int)json_integer_value(json_object_get(v, "averageRating"));
    }
    json_decref(o);
}

static void fmt_count(long n, char *out, size_t sz) {
    if (n >= 10000000) snprintf(out, sz, "%ldM", n / 1000000);
    else if (n >= 1000000) snprintf(out, sz, "%.1fM", n / 1e6);
    else if (n >= 10000) snprintf(out, sz, "%ldk", n / 1000);
    else if (n >= 1000) snprintf(out, sz, "%.1fk", n / 1e3);
    else snprintf(out, sz, "%ld", n);
}

// ------------------------------------------------------------------ cloud evaluation
static void score_text(int ply, char *out, size_t n) {
    if (A.evk[ply] == 1) snprintf(out, n, "%+.1f", A.ev[ply] / 100.0);
    else if (A.evk[ply] == 2) snprintf(out, n, "%sM%d", A.ev[ply] < 0 ? "-" : "+", abs(A.ev[ply]));
    else out[0] = 0;
}

static float score_share(int ply) {            // white's share of the eval bar 0..1
    if (A.evk[ply] == 1) return 1.0f / (1.0f + expf(-(float)A.ev[ply] * 0.0042f));
    if (A.evk[ply] == 2) return A.ev[ply] > 0 ? 1.0f : 0.0f;
    return 0.5f;
}

static void request_eval(void) {
    if (g_rev || g_req || A.evk[g_ply] || now_ms() < g_backoff_until || now_ms() - g_changed_at < 450) return;
    if (!g_api.have_account && g_api.net != 2) { /* works without a token too */ }
    char fen[120], enc[300], path[420];
    pos_to_fen(&g_pos, fen, sizeof fen);
    li_url_encode(fen, enc, sizeof enc);
    if (A.variant == VAR_STANDARD) snprintf(path, sizeof path, "/api/cloud-eval?fen=%s&multiPv=1", enc);
    else snprintf(path, sizeof path, "/api/cloud-eval?fen=%s&multiPv=1&variant=%s", enc, variant_key(A.variant));
    g_req = li_request(TAG_CLOUD, "GET", path, NULL, false);
    g_req_ply = g_ply;
}

void analysis_net_cloud(int req, int curl_rc, long http, const char *body) {
    if (req != g_req) return;                                  // stale (we moved on)
    g_req = 0;
    int ply = g_req_ply;
    if (ply < 0 || ply > A.n) return;
    if (curl_rc) { if (A.variant == VAR_STANDARD) A.evk[ply] = 3; else g_backoff_until = now_ms() + 4000; return; }   // offline: the built-in engine takes over
    if (http == 404) { A.evk[ply] = 3; return; }
    if (http == 429) { g_backoff_until = now_ms() + 8000; return; }
    if (http != 200) { g_backoff_until = now_ms() + 5000; return; }
    json_error_t e; json_t *o = json_loads(body, 0, &e);
    if (!o) return;
    json_t *pv = json_array_get(json_object_get(o, "pvs"), 0);
    if (pv) {
        json_t *cp = json_object_get(pv, "cp"), *mt = json_object_get(pv, "mate");
        if (json_is_integer(mt)) { A.evk[ply] = 2; A.ev[ply] = (short)json_integer_value(mt); }
        else if (json_is_integer(cp)) { A.evk[ply] = 1; long v = (long)json_integer_value(cp); A.ev[ply] = (short)(v > 30000 ? 30000 : (v < -30000 ? -30000 : v)); }
        const char *mv = json_string_value(json_object_get(pv, "moves"));
        if (mv && ply == g_ply) {
            g_depth = (int)json_integer_value(json_object_get(o, "depth"));
            // first move as the arrow, the first few as SAN text
            snprintf(A.best[ply], sizeof A.best[0], "%.5s", mv);
            Pos p = g_pos; char out[96] = ""; int k = 0; const char *s = mv;
            for (int i = 0; i < 4 && *s; i++) {
                char u[8] = {0}; int j = 0; while (*s && *s != ' ' && j < 7) u[j++] = *s++; while (*s == ' ') s++;
                Move m; if (!pos_parse_uci(&p, u, &m)) break;
                char sn[16]; move_to_san(&p, m, sn, sizeof sn);
                if (p.turn == WHITE) k += snprintf(out + k, sizeof out - (size_t)k, "%d. ", p.fullmove);
                else if (i == 0) k += snprintf(out + k, sizeof out - (size_t)k, "%d... ", p.fullmove);
                k += snprintf(out + k, sizeof out - (size_t)k, "%s ", sn);
                pos_do(&p, m);
            }
            snprintf(g_pv, sizeof g_pv, "%s", out);
        }
    }
    json_decref(o);
}

static float win_pct(int cp) { return 1.0f / (1.0f + expf(-0.00368208f * (float)cp)); }
static int ev_cp(int ply) { if (A.evk[ply] == 2) return A.ev[ply] > 0 ? 3000 : -3000; return A.ev[ply]; }

static void review_judge(void) {                          // from the evaluations after every ply: loss of winning chances of the side that moved
    for (int i = 1; i <= A.n; i++) {
        A.judg[i] = 0;
        if (A.evk[i - 1] != 1 && A.evk[i - 1] != 2) continue;
        if (A.evk[i] != 1 && A.evk[i] != 2) continue;
        int mover = ((int)A.start.turn + i - 1) & 1;
        float before = win_pct(ev_cp(i - 1)), after = win_pct(ev_cp(i));
        float loss = mover == WHITE ? before - after : after - before;
        A.judg[i] = loss >= 0.30f ? 3 : (loss >= 0.20f ? 2 : (loss >= 0.10f ? 1 : 0));
    }
}

static void review_update(void) {
    EngResult r;
    while (g_rev_busy && eng_async_poll(&r)) {
        if (r.job != g_rev_job || !r.done) continue;
        int ply = g_rev_i, mi;
        if (eng_is_mate(r.score, &mi)) { A.evk[ply] = 2; A.ev[ply] = (short)mi; }
        else { A.evk[ply] = 1; A.ev[ply] = (short)(r.score > 3000 ? 3000 : (r.score < -3000 ? -3000 : r.score)); }
        A.evsrc[ply] = 2;
        if (r.has_move) { Pos pp = ag_pos_at(ply); move_to_uci(&pp, r.best, A.best[ply]); }
        g_rev_busy = false; g_rev_i++;
    }
    if (g_rev_busy) return;
    while (g_rev_i <= A.n) {
        int i = g_rev_i; Pos p = ag_pos_at(i); int w;
        if (pos_outcome(&p, &w) != OUT_ONGOING) { A.evk[i] = 1; A.ev[i] = (short)(w == WHITE ? 3000 : (w == BLACK ? -3000 : 0)); g_rev_i++; continue; }
        static uint64_t hh[AG_MAX]; int n = 0; Pos q = A.start;
        for (int k = 0; k < i && n < AG_MAX; k++) { hh[n++] = eng_hash(&q); pos_do(&q, A.mv[k]); }
        EngLimits lim = {6, 450, 0};
        g_rev_job = eng_async_start(&p, hh, n, lim); g_rev_busy = true;
        return;
    }
    g_rev = false; review_judge();
    ui_toast("Review finished");
    refresh();
}

static void review_start(void) {
    if (A.variant != VAR_STANDARD) { ui_toast("Review works for standard chess"); return; }
    if (A.n < 2) { ui_toast("Play or load a game first"); return; }
    if (g_loc_busy) { eng_async_cancel(); g_loc_busy = false; }
    for (int i = 0; i <= A.n; i++) { A.evk[i] = 0; A.evsrc[i] = 0; A.judg[i] = 0; A.best[i][0] = 0; }
    g_rev = true; g_rev_i = 0; g_rev_busy = false; g_rev_total = A.n + 1; g_menu = false;
}

static void local_eval_update(void) {
    if (A.variant != VAR_STANDARD) return;
    if (g_rev) { review_update(); return; }
    EngResult r;
    while (g_loc_busy && eng_async_poll(&r)) {
        if (r.job != g_loc_job) continue;
        int ply = g_ply, mi;
        if (eng_is_mate(r.score, &mi)) { A.evk[ply] = 2; A.ev[ply] = (short)mi; }
        else { A.evk[ply] = 1; A.ev[ply] = (short)(r.score > 3000 ? 3000 : (r.score < -3000 ? -3000 : r.score)); }
        A.evsrc[ply] = r.done ? 2 : 1;
        if (r.has_move) move_to_uci(&g_pos, r.best, A.best[ply]);
        Pos p = g_pos; char out[96] = ""; int k = 0;
        for (int i = 0; i < r.npv && i < 4; i++) {
            char sn[16]; move_to_san(&p, r.pv[i], sn, sizeof sn);
            if (p.turn == WHITE) k += snprintf(out + k, sizeof out - (size_t)k, "%d. ", p.fullmove);
            else if (i == 0) k += snprintf(out + k, sizeof out - (size_t)k, "%d... ", p.fullmove);
            k += snprintf(out + k, sizeof out - (size_t)k, "%s ", sn);
            pos_do(&p, r.pv[i]);
        }
        snprintf(g_pv, sizeof g_pv, "%s", out); g_depth = r.depth; g_pv_local = true;
        if (r.done) g_loc_busy = false;
    }
    if (g_loc_busy || g_req || now_ms() - g_changed_at < 450) return;
    if (!(A.evk[g_ply] == 3 || A.evsrc[g_ply] == 1) || A.evsrc[g_ply] == 2) return;
    int w; if (pos_outcome(&g_pos, &w) != OUT_ONGOING) return;
    static uint64_t hh[AG_MAX]; int n = 0; Pos p = A.start;
    for (int i = 0; i < g_ply && n < AG_MAX; i++) { hh[n++] = eng_hash(&p); pos_do(&p, A.mv[i]); }
    EngLimits lim = {7, 2500, 0};
    g_loc_job = eng_async_start(&g_pos, hh, n, lim);
    g_loc_busy = true;
    A.evk[g_ply] = 0;                                         // shows the spinner until the first depth arrives
    A.evsrc[g_ply] = 1;
}

// ------------------------------------------------------------------ playing moves on the board
static void play_move(Move m) {
    Pos pre = g_pos;
    if (g_ply < A.n && move_eq(A.mv[g_ply], m)) { g_ply++; }
    else {                                                    // a new line: drop what came after
        A.n = g_ply;
        for (int i = g_ply; i <= AG_MAX; i++) { A.evk[i] = 0; A.evsrc[i] = 0; A.judg[i] = 0; A.best[i][0] = 0; }
        if (!ag_push_move(&pre, m)) return;
        g_ply++;
    }
    audio_play_for_move(&pre, m, &g_pos, true);
    clear_sel(); refresh();
    g_pos = ag_pos_at(g_ply);
    if (g_set.animations) {
        uint8_t pc = pre.b[m.from], victim = pre.b[m.to];
        if (!(m.flags & (MF_DROP | MF_CASTLE))) board_anim_move(&bv, m.from, m.to, m.promo ? MKPC(pre.turn, PAWN) : pc, (m.flags & MF_EP) ? 0 : victim, 0.12f);
    }
}

static void set_dests(int sq) {
    bv.sel = sq; bv.dests = bv.cap_dests = 0;
    Move mv[64]; int n = pos_moves_from(&g_pos, sq, mv);
    for (int i = 0; i < n; i++) {
        bv.dests |= 1ULL << mv[i].to;
        if (mv[i].flags & MF_CAPTURE) bv.cap_dests |= 1ULL << mv[i].to;
        if ((mv[i].flags & MF_CASTLE) && g_pos.variant == VAR_CHESS960) bv.dests |= 1ULL << castle_rook_sq(&g_pos, mv[i]);
    }
}

static void commit(int from, int to) {
    Move mv[64]; int n = pos_moves_from(&g_pos, from, mv), found = -1, cnt = 0;
    for (int i = 0; i < n; i++) {
        bool hit = mv[i].to == to || ((mv[i].flags & MF_CASTLE) && castle_rook_sq(&g_pos, mv[i]) == to);
        if (hit) { if (found < 0) found = i; cnt++; }
    }
    if (found < 0) { audio_play(SND_ILLEGAL); return; }
    if (cnt > 1 || mv[found].promo) {
        if (g_set.auto_queen) { for (int i = 0; i < n; i++) if (mv[i].to == to && mv[i].promo == QUEEN) { play_move(mv[i]); return; } }
        g_promo = true; g_pp_from = from; g_pp_to = to;
        return;
    }
    play_move(mv[found]);
}

static void on_press(int sq, bool touch, float px, float py) {
    if (sq < 0) { clear_sel(); return; }
    if (bv.sel >= 0 && ((bv.dests >> sq) & 1)) { int f = bv.sel; clear_sel(); commit(f, sq); return; }
    uint8_t pc = g_pos.b[sq];
    if (pc && PCOLOR(pc) == g_pos.turn) {
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
    if (g_menu || g_promo) { if (bv.dragging) clear_sel(); return; }
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

// ------------------------------------------------------------------ scene
static void an_enter(void) {
    gfx_set_piece_set(g_set.piece_set);
    board_reset(&bv);
    bv.ox = g_set.board_left ? 0 : 80;
    g_menu = g_promo = g_starting = false; g_rev = g_rev_busy = false;
    g_req = 0; g_ex_req = 0; g_backoff_until = 0; g_ex_backoff = 0; g_wait_t = 0;
    g_bar = 0.5f;
    refresh();
}

static void an_leave(void) { if (g_ex_req) { li_cancel(g_ex_req); g_ex_req = 0; } if (g_req) { li_cancel(g_req); g_req = 0; } if (g_loc_busy || g_rev_busy) { eng_async_cancel(); g_loc_busy = false; } g_rev = g_rev_busy = false; }

static void an_update(float dt) {
    g_wait_t += dt;
    board_update(&bv, dt);
    bv.ox = g_set.board_left ? 0 : 80;
    handle_input();
    if (g_kdown & KEY_L) go(g_ply - 1);
    if (g_kdown & KEY_R) go(g_ply + 1);
    if (g_kdown & KEY_X) board_set_flip(&bv, !bv.flipped);
    if (g_kdown & KEY_START) g_menu = !g_menu;
    request_eval();
    local_eval_update();
    request_explorer();
    float target = A.evk[g_ply] == 1 || A.evk[g_ply] == 2 ? score_share(g_ply) : 0.5f;
    g_bar += (target - g_bar) * clampf(dt * 8.0f, 0, 1);
    if (g_api.notice_new) { g_api.notice_new = false; ui_toast(g_api.notice); g_starting = false; }
}

// ------------------------------------------------------------------ top screen
static void card(float y, int color) {
    const char *name = color == WHITE ? A.white : A.black; int r = color == WHITE ? A.wr : A.br;
    g_disc(24, y + 14, 22, color == WHITE ? RGB(235, 235, 235) : RGB(24, 24, 24));
    g_disc(24, y + 14, 18, color == WHITE ? RGB(255, 255, 255) : RGB(62, 60, 58));
    g_text_fit(F_B13, 42, y + 8, C_TEXT, AL_LEFT, 120, name);
    if (r) g_textf(F_R11, 42, y + 22, C_TEXT3, AL_LEFT, "%d", r);
}

static void top_draw(void) {
    ui_top_bg();
    g_rrect(6, 6, 388, 46, 8, C_PANEL);
    if (A.from_game) {
        card(10, WHITE);
        char sc[16], why[40]; sc[0] = 0; why[0] = 0;
        sscanf(A.result, "%15s %39[^\n]", sc, why);
        g_text(F_B16, 200, 22, C_TEXT, AL_CENTER, sc);
        g_text_fit(F_R11, 200, 38, C_TEXT3, AL_CENTER, 100, why);
        // black card on the right (mirrored)
        g_disc(376, 24, 22, RGB(24, 24, 24)); g_disc(376, 24, 18, RGB(62, 60, 58));
        g_text_fit(F_B13, 358, 18, C_TEXT, AL_RIGHT, 120, A.black);
        if (A.br) g_textf(F_R11, 358, 32, C_TEXT3, AL_RIGHT, "%d", A.br);
    } else {
        g_icon(ICON_TARGET, 28, 29, 24, C_GREEN);
        g_text(F_B16, 48, 22, C_TEXT, AL_LEFT, "Analysis board");
        g_textf(F_R11, 48, 38, C_TEXT3, AL_LEFT, "%s  -  move pieces for both sides", variant_name(A.variant));
    }
    // evaluation bar
    g_rrect(6, 58, 388, 16, 8, RGB(40, 38, 36));
    float w = 388.0f * g_bar;
    if (w > 6) g_rrect(6, 58, w, 16, 8, RGB(236, 236, 232));
    char sc[16]; score_text(g_ply, sc, sizeof sc);
    if (sc[0]) {
        bool white_side = A.ev[g_ply] >= 0;
        if (white_side) g_text(F_B11, 14, 66, RGB(38, 36, 33), AL_LEFT, sc); else g_text(F_B11, 386, 66, C_TEXT, AL_RIGHT, sc);
    }
    // line under the bar
    char info[120];
    if (g_rev) snprintf(info, sizeof info, "Reviewing the game... %d / %d", g_rev_i < g_rev_total ? g_rev_i : g_rev_total, g_rev_total);
    else if (g_pv[0]) snprintf(info, sizeof info, "%s   (%sdepth %d)", g_pv, g_pv_local ? "local engine, " : "", g_depth);
    else if (A.evk[g_ply] == 1 || A.evk[g_ply] == 2) snprintf(info, sizeof info, "%s", A.judg[g_ply] ? "Computer analysis" : "Evaluation");
    else if (A.evk[g_ply] == 3) snprintf(info, sizeof info, A.variant == VAR_STANDARD ? "No evaluation (game is over)" : "No cloud evaluation for this position");
    else if (g_req || now_ms() - g_changed_at < 450) snprintf(info, sizeof info, "Evaluating...");
    else snprintf(info, sizeof info, "Cloud evaluation unavailable right now");
    g_text_fit(F_R13, 200, 88, g_pv[0] ? C_GREEN_L : C_TEXT3, AL_CENTER, 380, info);
    if (g_ex_mode) {                                           // opening explorer panel
        g_rrect(6, 100, 388, 134, 8, C_BG);
        const ExEntry *x = ex_current();
        g_text(F_B11, 14, 110, C_GREEN_L, AL_LEFT, g_ex_mode == 2 ? "OPENING EXPLORER - MASTERS" : "OPENING EXPLORER - LICHESS");
        if (!x) {
            if (g_ex_mode == 2 && A.variant != VAR_STANDARD) g_text(F_R13, 200, 170, C_TEXT3, AL_CENTER, "Masters games: standard chess only");
            else if (g_api.net != 2) g_text(F_R13, 200, 170, C_TEXT3, AL_CENTER, "Needs a connection to Lichess");
            else if (g_ex_fail) g_text(F_R13, 200, 170, C_TEXT3, AL_CENTER, "Explorer unavailable right now");
            else ui_spinner(200, 170, 10, C_GREEN, g_wait_t);
        } else {
            char t[16]; fmt_count(x->tw + x->td + x->tb, t, sizeof t);
            g_textf(F_R11, 386, 110, C_TEXT3, AL_RIGHT, "%s games", t);
            if (!x->n) g_text(F_R13, 200, 168, C_TEXT3, AL_CENTER, "No games reached this position");
            for (int i = 0; i < x->n; i++) {
                const ExMove *m = &x->m[i]; float y = 126 + i * 12.5f;
                long tot = m->w + m->d + m->b; if (tot < 1) tot = 1;
                if (i & 1) g_rect(10, y - 6, 380, 12, RGBA(255, 255, 255, 8));
                g_text(F_B11, 16, y, C_TEXT, AL_LEFT, m->san);
                char c[16]; fmt_count(tot, c, sizeof c); g_text(F_R11, 86, y, C_TEXT2, AL_RIGHT, c);
                float bx = 98, bw = 226, wf = (float)m->w / (float)tot, df = (float)m->d / (float)tot;
                g_rect(bx, y - 5, bw, 10, RGB(20, 19, 18));
                g_rect(bx, y - 5, bw * wf, 10, RGB(236, 236, 232));
                g_rect(bx + bw * wf, y - 5, bw * df, 10, RGB(130, 130, 128));
                if (wf > 0.18f) g_textf(F_B11, bx + 4, y, RGB(38, 36, 33), AL_LEFT, "%d%%", (int)(wf * 100 + 0.5f));
                float bf = 1.0f - wf - df; if (bf > 0.18f) g_textf(F_B11, bx + bw - 4, y, C_TEXT, AL_RIGHT, "%d%%", (int)(bf * 100 + 0.5f));
                if (m->avg) g_textf(F_R11, 386, y, C_TEXT3, AL_RIGHT, "%d", m->avg);
            }
            if (x->opening[0]) g_text_fit(F_R11, 200, 226, C_TEXT3, AL_CENTER, 372, x->opening);
        }
    } else {
    // move list around the current ply
    g_rrect(6, 100, 388, 134, 8, C_BG);
    g_rect(10, 211, 380, 1, RGBA(255, 255, 255, 14));
    if (!A.n) g_text(F_R13, 200, 167, C_TEXT3, AL_CENTER, "Play moves on the board");
    int rows = (A.n + 1) / 2, vis = 6, cur_row = g_ply > 0 ? (g_ply - 1) / 2 : 0;
    int first = cur_row - 2; if (first > rows - vis) first = rows - vis; if (first < 0) first = 0;
    for (int r = first; r < rows && r < first + vis; r++) {
        float y = 112 + (r - first) * 18.0f;
        if ((r & 1) == 0) g_rect(10, y - 9, 380, 18, RGBA(255, 255, 255, 8));
        g_textf(F_R13, 14, y, C_TEXT3, AL_LEFT, "%d.", r + 1);
        for (int c = 0; c < 2; c++) {
            int ply = r * 2 + c; if (ply >= A.n) break;
            float x = 52 + c * 150.0f;
            if (ply + 1 == g_ply) g_rrect(x - 6, y - 8, 96, 17, 4, C_GREEN_D);
            u32 col = ply + 1 == g_ply ? C_TEXT : C_TEXT2;
            g_text(F_B13, x, y, col, AL_LEFT, A.san[ply]);
            int j = A.judg[ply + 1];
            if (j) g_text(F_B13, x + 6 + g_text_w(F_B13, A.san[ply]), y, j == 1 ? C_ORANGE : (j == 2 ? RGB(240, 120, 40) : C_RED), AL_LEFT, j == 1 ? "?!" : (j == 2 ? "?" : "??"));
        }
    }
    if (g_ply > 0 && A.n) g_textf(F_R11, 386, 112, C_TEXT3, AL_RIGHT, "%d / %d", g_ply, A.n);
    if (A.opening[0]) g_text_fit(F_R11, 388, 223, C_TEXT3, AL_RIGHT, 300, A.opening);
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
            for (int k = 0; k < n; k++) if (mv[k].to == g_pp_to && mv[k].promo == T[i]) { play_move(mv[k]); break; }
        }
        g_piece(1, g_pos.turn, T[i], r.x + 5, r.y - 1, 0, 1.0f);
    }
}

static void start_from_here(void) {
    if (A.variant != VAR_STANDARD && A.variant != VAR_CHESS960) { ui_toast("Only standard / Chess960 positions"); return; }
    if (g_api.net != 2) { ui_toast("Not connected to Lichess"); return; }
    char fen[120]; pos_to_fen(&g_pos, fen, sizeof fen);
    memset(&g_last_params, 0, sizeof g_last_params);
    g_last_params.ai_level = g_set.ai_level; g_last_params.tc = (TimeCtl){600, 0}; g_last_params.variant = A.variant;
    g_last_params.color = g_pos.turn == WHITE ? 1 : 2;
    api_challenge_ai_fen(g_set.ai_level, (TimeCtl){600, 0}, A.variant, g_last_params.color, fen);
    g_starting = true; g_menu = false;
}

static void draw_menu(void) {
    g_rect(0, 0, 320, 240, RGBA(0, 0, 0, 150));
    float cx = bv.ox + 120;
    g_rrect(cx - 108, 6, 216, 228, 12, C_PANEL);
    g_text(F_B16, cx, 22, C_TEXT, AL_CENTER, "Analysis");
    float y = 34;
    char l[40]; snprintf(l, sizeof l, "Play from here (Stockfish %d)", g_set.ai_level);
    if (ui_button((Rect){cx - 96, y, 192, 30}, l, F_B11, BS_PRIMARY, ICON_BOT)) start_from_here();
    y += 34;
    if (ui_button((Rect){cx - 96, y, 192, 28}, "Review game (find mistakes)", F_B11, BS_SECONDARY, ICON_SEARCH)) review_start();
    y += 32;
    if (ui_button((Rect){cx - 96, y, 192, 28}, "Load a position (FEN)", F_B11, BS_SECONDARY, ICON_TARGET)) {
        char t[100]; char cur[100]; pos_to_fen(&g_pos, cur, sizeof cur);
        if (ui_keyboard("FEN", cur, t, sizeof t)) {
            Pos np;
            if (pos_from_fen(&np, t, A.variant)) { char keep[100]; snprintf(keep, sizeof keep, "%s", t); ag_reset(A.variant, keep); g_ply = 0; clear_sel(); refresh(); g_menu = false; }
            else ui_toast("That is not a valid FEN");
        }
    }
    y += 32;
    if (ui_button((Rect){cx - 96, y, 192, 28}, "Start position", F_B11, BS_SECONDARY, ICON_ROTATE_CCW)) { ag_reset(A.variant, NULL); g_ply = 0; clear_sel(); refresh(); g_menu = false; }
    y += 32;
    if (ui_button((Rect){cx - 96, y, 192, 28}, "Back to previous screen", F_B11, BS_SECONDARY, ICON_ARROW_LEFT)) { g_menu = false; scene_goto((SceneId)g_back); return; }
    y += 32;
    if (ui_button((Rect){cx - 96, y, 192, 26}, "Close", F_B13, BS_GHOST, -1)) g_menu = false;
    if (g_starting) { g_text(F_R11, cx, 228, C_TEXT3, AL_CENTER, "Starting game..."); }
}

static void draw_side(void) {
    float x0 = g_set.board_left ? 240.0f : 0.0f;
    g_rect(x0, 0, 80, 240, C_BG);
    bool modal = g_menu || g_promo;
    ui_set_enabled(!modal);
    if (ui_icon_button((Rect){x0 + 2, 6, 36, 34}, ICON_SKIP_BACK, BS_SECONDARY)) go(0);
    if (ui_icon_button((Rect){x0 + 42, 6, 36, 34}, ICON_SKIP_FORWARD, BS_SECONDARY)) go(A.n);
    if (ui_icon_button((Rect){x0 + 2, 44, 36, 34}, ICON_CHEVRON_LEFT, BS_SECONDARY)) go(g_ply - 1);
    if (ui_icon_button((Rect){x0 + 42, 44, 36, 34}, ICON_CHEVRON_RIGHT, BS_SECONDARY)) go(g_ply + 1);
    if (ui_icon_button((Rect){x0 + 2, 82, 36, 34}, ICON_ARROW_DOWN_UP, BS_SECONDARY)) board_set_flip(&bv, !bv.flipped);
    if (ui_icon_button((Rect){x0 + 42, 82, 36, 34}, ICON_MENU, BS_SECONDARY)) g_menu = true;
    // side to move + evaluation
    g_rrect(x0 + 3, 126, 74, 24, 12, C_PANEL2);
    g_disc(x0 + 17, 138, 12, g_pos.turn == WHITE ? RGB(240, 240, 240) : RGB(20, 20, 20));
    g_text(F_B11, x0 + 48, 138, C_TEXT2, AL_CENTER, g_pos.turn == WHITE ? "White" : "Black");
    static const char *BOOK[3] = {"Book", "Lichess", "Masters"};
    if (ui_button((Rect){x0 + 3, 154, 74, 22}, BOOK[g_ex_mode], F_B11, g_ex_mode ? BS_PRIMARY : BS_SECONDARY, -1)) {
        g_ex_mode = (g_ex_mode + 1) % 3; LOGF("analysis: book mode %d", g_ex_mode);
        if (g_ex_mode == 2 && A.variant != VAR_STANDARD) g_ex_mode = 0;
        g_changed_at = now_ms(); g_ex_backoff = 0;
    }
    char sc[16]; score_text(g_ply, sc, sizeof sc);
    if (sc[0]) {
        g_text(F_B21, x0 + 40, 192, C_TEXT, AL_CENTER, sc);
        if (A.judg[g_ply]) g_text(F_R11, x0 + 40, 207, A.judg[g_ply] == 3 ? C_RED : C_ORANGE, AL_CENTER, A.judg[g_ply] == 1 ? "Inaccuracy" : (A.judg[g_ply] == 2 ? "Mistake" : "Blunder"));
    } else if (A.evk[g_ply] == 3) g_text(F_R11, x0 + 40, 192, C_TEXT3, AL_CENTER, "no eval");
    else ui_spinner(x0 + 40, 192, 8, C_GREEN, g_wait_t);
    if (g_ply > 0) g_text_fit(F_B13, x0 + 40, 228, C_TEXT, AL_CENTER, 72, A.san[g_ply - 1]);
    ui_set_enabled(true);
}

static void bot_draw(void) {
    g_rect(0, 0, 320, 240, C_BG);
    bool modal = g_menu || g_promo;
    ui_set_enabled(!modal);
    board_draw(&bv, &g_pos);
    // best move arrow
    if (A.best[g_ply][0] && !bv.dragging) {
        Move m;
        if (pos_parse_uci(&g_pos, A.best[g_ply], &m)) {
            float x0, y0, x1, y1;
            int from = (m.flags & MF_DROP) ? m.to : m.from;
            board_sq_center(&bv, from, &x0, &y0); board_sq_center(&bv, m.to, &x1, &y1);
            g_arrow(x0, y0, x1, y1, 4.0f, RGBA(129, 182, 76, 190));
        }
    }
    if (g_ex_mode && !bv.dragging) {
        const ExEntry *x = ex_current(); Move m;
        if (x && x->n && pos_parse_uci(&g_pos, x->m[0].uci, &m)) {
            float x0, y0, x1, y1; int from = (m.flags & MF_DROP) ? m.to : m.from;
            board_sq_center(&bv, from, &x0, &y0); board_sq_center(&bv, m.to, &x1, &y1);
            g_arrow(x0, y0, x1, y1, 3.0f, RGBA(70, 130, 220, 170));
        }
    }
    draw_side();
    ui_set_enabled(true);
    if (g_promo) draw_promo(); else if (g_menu) draw_menu();
    ui_set_enabled(true);
    ui_toast_draw((float)bv.ox + 120);
}

const Scene scene_analysis = {"analysis", an_enter, an_update, top_draw, bot_draw, an_leave};
