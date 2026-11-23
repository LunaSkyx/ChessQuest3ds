// The game screen: touch board with drag & tap moves, multi-premoves, clocks, move list, menus
#include "scenes.h"
#include "bots.h"
#include "ui.h"
#include "board.h"
#include "audio.h"
#include "game.h"
#include "scenario.h"
#include "fics.h"
#include "analysis.h"
#include "engine.h"
#include <jansson.h>
#include <math.h>

#define G g_game

static char g_open_id[16];
static int g_open_backend;                // 0 Lichess, 1 FICS, 2 offline
static int g_off_bot = -1;                 // bot ladder opponent of the running offline game (-1 = plain engine)
static char g_bot_msg[80];                 // shown on the game-over card after a ladder win
static int g_off_level, g_off_color; static bool g_off_clock; static TimeCtl g_off_tc;   // offline game setup
static bool g_off_check;                 // an offline move was made: test for the end of the game
static bool g_eng_busy; static int g_eng_job, g_eng_ply, g_eng_score; static u64 g_eng_t0; static EngResult g_eng_res; static bool g_eng_have;
static BoardView bv;
static int view_ply = -1;                 // -1 live, else number of plies shown
static bool g_menu, g_confirm_resign, g_over_open, g_press_was_sel;
static bool g_promo_open, g_promo_pre;
static int g_pp_from, g_pp_to, g_pp_drop;
static int pocket_sel;                    // selected pocket piece type (crazyhouse) or 0
static float g_over_timer;
static bool g_over_done, g_over_shown;
static u64 g_reconnect_at;
static bool g_low_warned;
static float g_wait_t;
static u64 g_rematch_until;
static char g_gone[48];

// ------------------------------------------------------------------ helpers
static bool premove_mode(void) {
    return g_set.premoves && view_ply == -1 && !G.finished && G.loaded && G.my_color >= 0 && !game_my_turn(&G) && G.nply >= 0;
}
static bool can_interact(void) {
    return view_ply == -1 && !G.finished && G.loaded && G.my_color >= 0 && !g_menu && !g_promo_open && !g_over_open;
}
static int bottom_color(void) { return bv.flipped ? BLACK : WHITE; }

static void view_position(Pos *out) {
    if (view_ply >= 0 && view_ply < G.nply) { game_pos_at(&G, view_ply, out); return; }
    if (G.pre.n && G.my_color >= 0) { pm_virtual(&G.pos, &G.pre, G.my_color, out); return; }
    *out = G.pos;
}

static void clear_sel(void) {
    bv.sel = -1; bv.dests = bv.cap_dests = 0; bv.dragging = false; bv.drag_sq = -1; bv.drag_piece = 0; bv.sel_premove = false;
    pocket_sel = 0;
}

static void refresh_highlights(void) {
    int ply = view_ply < 0 ? G.nply : view_ply;
    if (ply > 0 && ply <= G.nply) {
        Move m = G.mv[ply - 1];
        bv.last_to = m.to;
        bv.last_from = (m.flags & MF_DROP) ? m.to : m.from;
    } else bv.last_from = bv.last_to = -1;
    Pos p; game_pos_at(&G, ply, &p);
    bv.check_sq = (G.loaded && pos_in_check(&p, p.turn)) ? pos_king_sq(&p, p.turn) : -1;
    bv.npm = 0;
    if (view_ply < 0)
        for (int i = 0; i < G.pre.n && i < MAX_PM; i++) {
            bv.pm_to[bv.npm] = G.pre.q[i].to;
            bv.pm_from[bv.npm] = G.pre.q[i].drop ? G.pre.q[i].to : G.pre.q[i].from;
            bv.npm++;
        }
}

static void set_dests_for(int sq) {
    bv.sel = sq; bv.dests = bv.cap_dests = 0;
    if (premove_mode()) {
        Pos vp; pm_virtual(&G.pos, &G.pre, G.my_color, &vp);
        bv.sel_premove = true;
        bv.dests = pm_dests(&vp, G.my_color, sq);
        for (int s = 0; s < 64; s++)
            if (((bv.dests >> s) & 1) && vp.b[s]) bv.cap_dests |= 1ULL << s;
        return;
    }
    bv.sel_premove = false;
    Move mv[64]; int n = pos_moves_from(&G.pos, sq, mv);
    for (int i = 0; i < n; i++) {
        bv.dests |= 1ULL << mv[i].to;
        if (mv[i].flags & MF_CAPTURE) bv.cap_dests |= 1ULL << mv[i].to;
        if ((mv[i].flags & MF_CASTLE) && G.pos.variant == VAR_CHESS960) bv.dests |= 1ULL << castle_rook_sq(&G.pos, mv[i]);
    }
}

static void set_dests_for_drop(int type) {
    pocket_sel = type; bv.sel = -1; bv.dests = bv.cap_dests = 0;
    if (premove_mode()) {
        Pos vp; pm_virtual(&G.pos, &G.pre, G.my_color, &vp);
        bv.dests = pm_drop_dests(&vp, G.my_color, type); bv.sel_premove = true;
        return;
    }
    bv.sel_premove = false;
    Move mv[128]; int n = pos_drops(&G.pos, type, mv);
    for (int i = 0; i < n; i++) bv.dests |= 1ULL << mv[i].to;
}

// ------------------------------------------------------------------ backend dispatch (Lichess API or FICS)
static bool fics_game(void) { return G.backend == 1; }
static bool off_game(void) { return G.backend == 2; }
static void off_finish(const char *status, int winner, const char *why);
static void off_takeback(void);
static void off_offer_draw(void);
static void be_move(const Pos *pre, Move m, const char *uci) { if (off_game()) { (void)pre; (void)m; (void)uci; g_off_check = true; } else if (fics_game()) fics_send_move(pre, m); else api_move(G.id, uci); }
static void be_resign(void) { if (off_game()) off_finish("resign", G.my_color ^ 1, ""); else if (fics_game()) fics_send("resign"); else api_resign(G.id); }
static void be_abort(void) { if (off_game()) off_finish("aborted", -1, ""); else if (fics_game()) fics_send("abort"); else api_abort(G.id); }
static void be_draw(bool yes) { if (off_game()) { if (yes) off_offer_draw(); } else if (fics_game()) fics_send(yes ? "draw" : "decline"); else api_draw(G.id, yes); }
static void be_takeback(bool yes) { if (off_game()) { (void)yes; off_takeback(); } else if (fics_game()) { if (yes) fics_send("accept"); else fics_send("decline"); } else api_takeback(G.id, yes); }
static void be_resync(void) { if (off_game()) return; if (fics_game()) fics_send("refresh"); else api_game_open(G.id); }

// ------------------------------------------------------------------ making moves
// A piece the user is holding / has selected stays in hand when the opponent moves: only recompute where it can go
// (premove squares become real legal moves), and drop the selection only if the piece is gone.
static void revalidate_selection(void) {
    if (G.finished || !G.loaded) return;
    if (pocket_sel) {
        if (premove_mode() || G.pos.pocket[G.my_color][pocket_sel - 1] > 0) set_dests_for_drop(pocket_sel); else clear_sel();
        return;
    }
    if (bv.sel < 0) return;
    Pos vp; if (premove_mode()) pm_virtual(&G.pos, &G.pre, G.my_color, &vp); else vp = G.pos;
    uint8_t pc = vp.b[bv.sel];
    if (pc && PCOLOR(pc) == G.my_color) { bv.drag_piece = bv.dragging ? pc : bv.drag_piece; set_dests_for(bv.sel); }
    else clear_sel();
}

static void animate_move(const Pos *pre, Move m, float dur) {
    if (!g_set.animations) return;
    int color = pre->turn;
    if (m.flags & MF_DROP) { board_anim_move(&bv, m.to, m.to, MKPC(color, m.from), 0, dur); return; }
    uint8_t pc = pre->b[m.from], victim = pre->b[m.to];
    if (m.flags & MF_CASTLE) {
        int rs = castle_rook_sq(pre, m), rank = RANK_OF(m.from);
        int rd = SQ(FILE_OF(m.to) > FILE_OF(m.from) ? 5 : 3, rank);
        board_anim_move(&bv, m.from, m.to, pc, 0, dur);
        board_anim_move(&bv, rs, rd, MKPC(color, ROOK), 0, dur);
        return;
    }
    if (m.promo) pc = MKPC(color, PAWN);
    board_anim_move(&bv, m.from, m.to, pc, (m.flags & MF_EP) ? 0 : victim, dur);
}

static void do_move(Move m, bool animate) {
    Pos pre = G.pos;
    char u[8]; move_to_uci(&pre, m, u);
    if (!game_local_move(&G, m)) return;
    be_move(&pre, m, u);
    audio_play_for_move(&pre, m, &G.pos, true);
    if (animate) animate_move(&pre, m, 0.14f);
    refresh_highlights();
    g_low_warned = false;
    LOGF("move: %s (ply %d)", u, G.nply);
}

static void exec_premove(void) {
    if (!G.pre.n || !game_my_turn(&G) || G.finished) return;
    PreMove pm = G.pre.q[0];
    memmove(&G.pre.q[0], &G.pre.q[1], sizeof(PreMove) * (size_t)(G.pre.n - 1));
    G.pre.n--;
    Move m;
    if (pm_execute(&G.pos, pm, &m)) { do_move(m, false); LOGF("premove executed, %d left", G.pre.n); }
    else { pm_clear(&G.pre); audio_play(SND_ILLEGAL); ui_toast("Premove cancelled"); LOGF("premove illegal -> cleared"); }
    refresh_highlights();
}

static void push_premove(PreMove pm) {
    if (pm_push(&G.pre, pm)) { audio_play(SND_PREMOVE); LOGF("premove queued (%d)", G.pre.n); }
    else ui_toast("Premove queue is full");
    refresh_highlights();
}

static void open_promo(int from, int to, bool pre) {
    g_promo_open = true; g_promo_pre = pre; g_pp_from = from; g_pp_to = to; g_pp_drop = 0;
}

static void finish_promo(int piece) {
    g_promo_open = false;
    if (g_promo_pre) { push_premove((PreMove){(uint8_t)g_pp_from, (uint8_t)g_pp_to, (uint8_t)piece, 0}); return; }
    Move mv[64]; int n = pos_moves_from(&G.pos, g_pp_from, mv);
    for (int i = 0; i < n; i++) if (mv[i].to == g_pp_to && mv[i].promo == piece) { do_move(mv[i], true); return; }
}

static void commit(int from, int to, bool animate) {
    Pos vp;
    if (premove_mode()) {
        pm_virtual(&G.pos, &G.pre, G.my_color, &vp);
        if (pm_is_promotion(&vp, G.my_color, from, to)) {
            if (g_set.auto_queen) push_premove((PreMove){(uint8_t)from, (uint8_t)to, QUEEN, 0});
            else open_promo(from, to, true);
        } else push_premove((PreMove){(uint8_t)from, (uint8_t)to, 0, 0});
        return;
    }
    if (!game_my_turn(&G)) return;
    Move mv[64]; int n = pos_moves_from(&G.pos, from, mv), found = -1, cnt = 0;
    for (int i = 0; i < n; i++) {
        bool hit = mv[i].to == to || ((mv[i].flags & MF_CASTLE) && castle_rook_sq(&G.pos, mv[i]) == to);
        if (hit) { if (found < 0) found = i; cnt++; }
    }
    if (found < 0) { audio_play(SND_ILLEGAL); return; }
    if (cnt > 1 || mv[found].promo) {                      // promotion choice
        if (g_set.auto_queen) { for (int i = 0; i < n; i++) if (mv[i].to == to && mv[i].promo == QUEEN) { do_move(mv[i], animate); return; } }
        open_promo(from, to, false);
        return;
    }
    do_move(mv[found], animate);
}

static void commit_drop(int type, int to) {
    if (premove_mode()) { push_premove((PreMove){0, (uint8_t)to, 0, (uint8_t)type}); return; }
    Move mv[128]; int n = pos_drops(&G.pos, type, mv);
    for (int i = 0; i < n; i++) if (mv[i].to == to) { do_move(mv[i], false); return; }
    audio_play(SND_ILLEGAL);
}

// ------------------------------------------------------------------ pointer handling
static void on_press(int sq, bool touch, float px, float py) {
    if (sq < 0) { clear_sel(); return; }
    if (pocket_sel && ((bv.dests >> sq) & 1)) { int t = pocket_sel; clear_sel(); commit_drop(t, sq); return; }
    if (bv.sel >= 0 && ((bv.dests >> sq) & 1)) { int f = bv.sel; clear_sel(); commit(f, sq, true); return; }
    Pos vp; if (premove_mode()) pm_virtual(&G.pos, &G.pre, G.my_color, &vp); else vp = G.pos;
    uint8_t pc = vp.b[sq];
    if (pc && PCOLOR(pc) == G.my_color) {
        g_press_was_sel = bv.sel == sq;
        pocket_sel = 0;
        set_dests_for(sq);
        if (touch) { bv.dragging = true; bv.drag_sq = sq; bv.drag_piece = pc; bv.px = px; bv.py = py; }
    } else clear_sel();
}

static void on_release(int sq) {
    if (!bv.dragging) return;
    bv.dragging = false;
    int from = bv.drag_sq;
    bv.drag_piece = 0; bv.drag_sq = -1;
    if (pocket_sel) {                                      // dragged from a pocket
        if (sq >= 0 && ((bv.dests >> sq) & 1)) { int t = pocket_sel; clear_sel(); commit_drop(t, sq); }
        return;                                            // otherwise keep the pocket piece selected (tap mode)
    }
    if (sq == from) { if (g_press_was_sel) clear_sel(); return; }
    if (sq >= 0 && ((bv.dests >> sq) & 1)) { int f = bv.sel; clear_sel(); commit(f, sq, false); }
    else clear_sel();
}

static Rect pocket_rect(int i, int x0) { return (Rect){x0 + 2.0f + (i % 3) * 26.0f, 150.0f + (i / 3) * 26.0f, 26, 26}; }
static const int POCKET_ORDER[5] = {PAWN, KNIGHT, BISHOP, ROOK, QUEEN};

static void history_step(int d) {
    if (G.nply == 0) return;
    int cur = view_ply < 0 ? G.nply : view_ply;
    cur += d;
    if (cur < 0) cur = 0;
    view_ply = cur >= G.nply ? -1 : cur;
    clear_sel(); refresh_highlights();
    if (view_ply >= 0 || d) audio_play(SND_CLICK);
}

static void handle_input(void) {
    if (!can_interact()) { if (bv.dragging) clear_sel(); return; }
    int side_x = g_set.board_left ? 240 : 0;
    if (g_touch.pressed) {
        int sq = board_sq_at(&bv, g_touch.x, g_touch.y);
        if (sq >= 0) on_press(sq, true, (float)g_touch.x, (float)g_touch.y);
        else if (G.pos.variant == VAR_CRAZYHOUSE) {
            for (int i = 0; i < 5; i++) {
                if (!rect_hit(pocket_rect(i, side_x), g_touch.x, g_touch.y)) continue;
                int t = POCKET_ORDER[i];
                if (G.pos.pocket[G.my_color][t - 1] <= 0 && !premove_mode()) break;
                clear_sel(); set_dests_for_drop(t);
                bv.dragging = true; bv.drag_sq = -1; bv.drag_piece = MKPC(G.my_color, t); bv.px = (float)g_touch.x; bv.py = (float)g_touch.y;
            }
        }
    }
    if (bv.dragging && g_touch.down) { bv.px = (float)g_touch.x; bv.py = (float)g_touch.y; }
    if (g_touch.released && bv.dragging) on_release(board_sq_at(&bv, g_touch.x, g_touch.y));

    // buttons: d-pad cursor, A = tap, B = cancel
    if (g_kdown & (KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT)) {
        if (bv.cursor < 0) bv.cursor = bv.flipped ? SQ(3, 6) : SQ(4, 1);
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

// ------------------------------------------------------------------ server messages
static void on_full(void) {
    if (G.my_color == BLACK) board_set_flip(&bv, true);
    else if (G.my_color == WHITE) board_set_flip(&bv, false);
    // remember the game for "rematch"
    g_last_params.tc = (TimeCtl){G.init_ms / 1000, G.inc_ms / 1000};
    g_last_params.variant = G.variant; g_last_params.rated = G.rated; g_last_params.backend = G.backend;
    int opp = G.my_color == WHITE ? BLACK : WHITE;
    if (G.my_color >= 0) {
        g_last_params.ai_level = G.pl[opp].ai_level;
        snprintf(g_last_params.opp, sizeof g_last_params.opp, "%s", G.pl[opp].ai_level ? "" : G.pl[opp].name);
        g_last_params.color = G.my_color == WHITE ? 2 : 1;       // swap colours for a rematch
    }
    refresh_highlights();
    LOGF("game: loaded %s, me=%s, %s vs %s, %d plies", G.id, G.my_color == WHITE ? "white" : (G.my_color == BLACK ? "black" : "none"), G.pl[WHITE].name, G.pl[BLACK].name, G.nply);
    audio_play(SND_START);
    scn_event("game_start");
}

static void on_game_over(void) {
    if (g_over_done) return;
    g_over_done = true; g_over_timer = 0;
    clear_sel(); pm_clear(&G.pre); g_menu = false; g_promo_open = false;
    if (!strcmp(G.status, "aborted") || !strcmp(G.status, "noStart")) audio_play(SND_DRAW);
    else if (G.winner < 0) audio_play(SND_DRAW);
    else audio_play(G.winner == G.my_color ? SND_WIN : SND_LOSE);
    LOGF("game: over status=%s winner=%d", G.status, G.winner);
    history_save_game(&G);
    scn_event("game_over");
}

static void on_state(int appended, bool diverged) {
    if (diverged) { board_anim_clear(&bv); clear_sel(); ui_toast("Position resynchronised"); }
    if (appended > 0) {
        Pos pre; game_pos_at(&G, G.nply - 1, &pre);
        Move m = G.mv[G.nply - 1];
        if (appended == 1) animate_move(&pre, m, 0.16f);
        audio_play_for_move(&pre, m, &G.pos, false);
        g_low_warned = false;
        LOGF("opp move: %s (ply %d)", G.uci[G.nply - 1], G.nply);
    }
    refresh_highlights();
    if (G.finished) on_game_over(); else exec_premove();
    revalidate_selection();
    if (G.my_color >= 0 && G.draw_offer[G.my_color == WHITE ? BLACK : WHITE] && !g_set.hide_draw) audio_play(SND_NOTIFY);
}

static void on_line(const char *line) {
    json_error_t e; json_t *o = json_loads(line, 0, &e);
    if (!o) return;
    const char *type = json_string_value(json_object_get(o, "type"));
    if (type && !strcmp(type, "gameFull")) {
        bool first = !G.loaded;
        game_parse_full(&G, o, g_api.id);
        LOGF("game: gameFull received (%s, %d plies)", first ? "first" : "resync", G.nply);
        if (first) on_full(); else { refresh_highlights(); }
        if (G.finished) on_game_over(); else if (!first) exec_premove();
    } else if (type && !strcmp(type, "gameState")) {
        bool dv; int app = game_parse_state(&G, o, &dv);
        on_state(app, dv);
    } else if (type && !strcmp(type, "opponentGone")) {
        if (json_is_true(json_object_get(o, "gone"))) {
            int s = (int)json_integer_value(json_object_get(o, "claimWinInSeconds"));
            snprintf(g_gone, sizeof g_gone, "Opponent left%s", s > 0 ? " - you can claim the win soon" : "");
        } else g_gone[0] = 0;
    }
    json_decref(o);
}

static void on_end(int rc, long http) {
    if (G.finished || scene_current() != SC_GAME) return;
    LOGF("game stream ended rc=%d http=%ld -> reconnect", rc, http);
    if (http == 404) { ui_toast("Game not found"); return; }
    g_reconnect_at = now_ms() + 1200;
}

static void on_move_result(bool ok, const char *msg) {
    if (ok) return;
    ui_toast(msg[0] ? msg : "Move rejected");
    LOGF("move rejected: %s -> resync", msg);
    audio_play(SND_ILLEGAL);
    pm_clear(&G.pre);
    be_resync();                                           // the server's board resets our line
}

// FICS ---------------------------------------------------------------
static void on_fics_s12(const Style12 *s) {
    bool first = !G.loaded;
    int app; bool dv;
    if (!game_fics_update(&G, s, &app, &dv)) return;
    if (first) {
        G.pl[WHITE].rating = g_fics.gi_rating[0]; G.pl[BLACK].rating = g_fics.gi_rating[1];
        G.rated = g_fics.gi_rated;
        snprintf(G.id, sizeof G.id, "fics%d", s->gnum);
        on_full();
        g_last_params.tc = (TimeCtl){s->init_min * 60, s->inc};
    } else on_state(app, dv);
}

static void on_fics_result(int winner, const char *status, const char *why) {
    if (!G.loaded) return;
    snprintf(G.status, sizeof G.status, "%s", status);
    snprintf(G.why, sizeof G.why, "%s", why);
    G.winner = winner; G.finished = true; G.t_turn = -1;
    G.draw_offer[WHITE] = G.draw_offer[BLACK] = G.takeback_offer[WHITE] = G.takeback_offer[BLACK] = false;
    pm_clear(&G.pre);
    refresh_highlights();
    on_game_over();
}

static void on_fics_event(int evt, const char *text) {
    (void)text;
    int opp = G.my_color == WHITE ? BLACK : WHITE;
    if (G.my_color < 0) return;
    switch (evt) {
    case FE_DRAW_OFFER: G.draw_offer[opp] = true; if (!g_set.hide_draw) audio_play(SND_NOTIFY); break;
    case FE_DRAW_DECLINED: ui_toast("Draw declined"); break;
    case FE_TAKEBACK_OFFER: G.takeback_offer[opp] = true; audio_play(SND_NOTIFY); break;
    case FE_TAKEBACK_DECLINED: ui_toast("Takeback declined"); break;
    case FE_ABORT_OFFER: ui_toast("Opponent asks to abort (menu > Abort)"); audio_play(SND_NOTIFY); break;
    case FE_ILLEGAL: on_move_result(false, "Illegal move"); break;
    }
}

// ------------------------------------------------------------------ offline game (built-in engine)
static void off_finish(const char *status, int winner, const char *why) {
    if (G.finished) return;
    eng_async_cancel(); g_eng_busy = false;
    snprintf(G.status, sizeof G.status, "%s", status);
    snprintf(G.why, sizeof G.why, "%s", why);
    G.winner = winner; G.finished = true; G.t_turn = -1;
    g_bot_msg[0] = 0;
    if (g_off_bot >= 0 && winner >= 0 && winner == G.my_color) {          // ladder win: unlock the next bot
        bool first = !bot_is_beaten(g_set.bots_beaten, g_off_bot);
        g_set.bots_beaten |= 1u << g_off_bot; settings_save();
        if (first && g_off_bot + 1 < BOT_COUNT) snprintf(g_bot_msg, sizeof g_bot_msg, "Unlocked: %s!", BOTS[g_off_bot + 1].name);
        else if (first) snprintf(g_bot_msg, sizeof g_bot_msg, "You beat the whole ladder!");
        else snprintf(g_bot_msg, sizeof g_bot_msg, "You beat %s again", BOTS[g_off_bot].name);
    }
    pm_clear(&G.pre);
    refresh_highlights();
    on_game_over();
}

static int off_hashes(uint64_t *h) {                       // hashes of every earlier position of the game (not the current one)
    Pos p = G.start; int k = 0;
    for (int i = 0; i < G.nply && k < MAX_PLIES; i++) { h[k++] = eng_hash(&p); pos_do(&p, G.mv[i]); }
    return k;
}

static bool off_check_end(void) {                          // sets the result when the game is over
    int w; OutKind o = pos_outcome(&G.pos, &w);
    if (o == OUT_CHECKMATE) { off_finish("mate", w, ""); return true; }
    if (o == OUT_STALEMATE) { off_finish("stalemate", -1, ""); return true; }
    if (o != OUT_ONGOING) { off_finish("draw", -1, "Insufficient material"); return true; }
    if (G.pos.halfmove >= 100) { off_finish("draw", -1, "50-move rule"); return true; }
    static uint64_t hh[MAX_PLIES]; int n = off_hashes(hh); uint64_t cur = eng_hash(&G.pos); int rep = 1;
    for (int i = 0; i < n; i++) if (hh[i] == cur) rep++;
    if (rep >= 3) { off_finish("draw", -1, "Threefold repetition"); return true; }
    return false;
}

static void off_offer_draw(void) {
    // the engine accepts when it does not think it is better (and the opening is over)
    if (G.nply >= 20 && (!g_eng_have || g_eng_score <= 25)) off_finish("draw", -1, "Draw agreed");
    else ui_toast("The engine declined the draw");
}

static void off_takeback(void) {
    if (G.finished || G.nply == 0) return;
    eng_async_cancel(); g_eng_busy = false; g_eng_have = false;
    int target = G.pos.turn == G.my_color ? G.nply - 2 : G.nply - 1;
    if (target < 0) target = 0;
    if (target >= G.nply) return;
    game_truncate(&G, target);
    G.t_stamp = now_ms(); G.t_turn = (G.has_clock && G.nply >= 2) ? G.pos.turn : -1;
    pm_clear(&G.pre); clear_sel(); view_ply = -1; board_anim_clear(&bv);
    refresh_highlights();
    audio_play(SND_CLICK);
    ui_toast("Move taken back");
}

static void off_engine_play(Move m) {
    if (!game_local_move(&G, m)) return;
    off_check_end();                                       // may finish the game (on_state then reports it)
    G.draw_offer[WHITE] = G.draw_offer[BLACK] = false;
    on_state(1, false);
}

static void off_update(void) {
    if (!G.loaded || G.finished) return;
    if (g_off_check) { g_off_check = false; if (off_check_end()) return; }
    if (G.has_clock && G.t_turn >= 0 && game_clock(&G, G.t_turn) <= 0) {     // flag fell
        int loser = G.t_turn;
        off_finish("outoftime", loser ^ 1, "");
        return;
    }
    if (G.pos.turn == G.my_color) return;                  // the engine thinks only on its own turn
    if (!g_eng_busy) {
        EngLimits lim = g_off_bot >= 0 ? BOTS[g_off_bot].lim : eng_level(g_off_level);
        if (G.has_clock) {                                 // never burn more than a slice of the remaining time
            int rem = game_clock(&G, G.my_color ^ 1), slice = rem / 25 + G.inc_ms * 3 / 4;
            if (slice < 60) slice = 60;
            if (slice < lim.time_ms) lim.time_ms = slice;
        }
        static uint64_t hh[MAX_PLIES]; int n = off_hashes(hh);
        g_eng_job = eng_async_start(&G.pos, hh, n, lim);
        g_eng_busy = true; g_eng_ply = G.nply; g_eng_t0 = now_ms(); g_eng_have = false;
        LOGF("engine: thinking (level %d, depth<=%d, %d ms, ply %d)", g_off_level, lim.max_depth, lim.time_ms, G.nply);
        return;
    }
    EngResult r;
    while (eng_async_poll(&r)) {
        if (r.job != g_eng_job) continue;
        g_eng_res = r; g_eng_have = true;
        g_eng_score = G.pos.turn == WHITE ? r.score : -r.score;     // from the engine's point of view
    }
    bool hurry = G.has_clock && game_clock(&G, G.my_color ^ 1) < 8000;
    if (g_eng_have && g_eng_res.done && (hurry || now_ms() >= g_eng_t0 + 450)) {
        g_eng_busy = false;
        if (G.nply != g_eng_ply) return;                   // the line changed (take-back): think again
        LOGF("engine: plays depth %d score %d (%ld nodes, %llu ms)", g_eng_res.depth, g_eng_score, g_eng_res.nodes, (unsigned long long)(now_ms() - g_eng_t0));
        off_engine_play(g_eng_res.best);
    }
}

// ------------------------------------------------------------------ scene
void scene_game_open(const char *id) {
    snprintf(g_open_id, sizeof g_open_id, "%s", id);
    g_open_backend = 0;
    scene_goto(SC_GAME);
}

static char g_off_fen[100];          // custom start position (endgame training), "" = normal start
void scene_game_open_offline_fen(int level, const char *fen) {
    g_off_bot = -1;
    snprintf(g_off_fen, sizeof g_off_fen, "%s", fen);
    g_open_backend = 2; g_open_id[0] = 0;
    g_off_level = level < 1 ? 1 : (level > 8 ? 8 : level); g_off_clock = false; g_off_tc = (TimeCtl){0, 0}; g_off_color = 0;
    scene_goto(SC_GAME);
}

void scene_game_open_bot(int bot, int color) {
    if (bot < 0 || bot >= BOT_COUNT) return;
    g_off_bot = bot; g_off_fen[0] = 0;
    g_open_backend = 2; g_open_id[0] = 0;
    g_off_level = 1 + bot * 7 / (BOT_COUNT - 1);
    g_off_clock = false; g_off_tc = (TimeCtl){0, 0}; g_off_color = color;
    scene_goto(SC_GAME);
}

void scene_game_open_bot_fen(int bot, const char *fen) {          // (test hook) ladder game from a given position
    if (bot < 0 || bot >= BOT_COUNT) return;
    g_off_bot = bot; snprintf(g_off_fen, sizeof g_off_fen, "%s", fen);
    g_open_backend = 2; g_open_id[0] = 0;
    g_off_level = 1 + bot * 7 / (BOT_COUNT - 1); g_off_clock = false; g_off_tc = (TimeCtl){0, 0}; g_off_color = 0;
    scene_goto(SC_GAME);
}

void scene_game_open_offline(int level, bool clock, TimeCtl tc, int color) {
    g_off_bot = -1;
    g_off_fen[0] = 0;
    g_open_backend = 2; g_open_id[0] = 0;
    g_off_level = level < 1 ? 1 : (level > 8 ? 8 : level); g_off_clock = clock; g_off_tc = tc; g_off_color = color;
    scene_goto(SC_GAME);
}

void scene_game_open_fics(void) {
    g_open_backend = 1;
    g_open_id[0] = 0;
    scene_goto(SC_GAME);
}

static void game_enter(void) {
    gfx_set_piece_set(g_set.piece_set);
    game_reset(&G, g_open_id);
    board_reset(&bv);
    bv.ox = g_set.board_left ? 0 : 80;
    view_ply = -1; g_menu = g_confirm_resign = g_over_open = g_promo_open = false;
    g_over_done = false; g_over_shown = false; g_over_timer = 0; g_wait_t = 0; g_reconnect_at = 0; g_low_warned = false; g_gone[0] = 0;
    pocket_sel = 0; g_rematch_until = 0;
    if (g_open_backend == 2) {
        eng_async_cancel(); g_eng_busy = false; g_eng_have = false; g_off_check = false;
        snprintf(G.id, sizeof G.id, "offline");
        G.backend = 2; G.variant = VAR_STANDARD; pos_init(&G.start, VAR_STANDARD);
        if (g_off_fen[0] && !pos_from_fen(&G.start, g_off_fen, VAR_STANDARD)) { g_off_fen[0] = 0; pos_init(&G.start, VAR_STANDARD); }
        G.pos = G.start; G.nply = 0;
        int me = g_off_fen[0] ? (int)G.start.turn : g_off_color == 1 ? WHITE : (g_off_color == 2 ? BLACK : (int)(now_ms() / 7 & 1));
        G.my_color = me;
        snprintf(G.pl[me].name, sizeof G.pl[me].name, "You");
        if (g_off_bot >= 0) snprintf(G.pl[me ^ 1].name, sizeof G.pl[me ^ 1].name, "%s", BOTS[g_off_bot].name);
        else snprintf(G.pl[me ^ 1].name, sizeof G.pl[me ^ 1].name, "Engine level %d", g_off_level);
        g_bot_msg[0] = 0;
        G.pl[me ^ 1].ai_level = g_off_level;
        G.has_clock = g_off_clock && (g_off_tc.limit_sec > 0 || g_off_tc.inc_sec > 0);
        G.init_ms = G.has_clock ? g_off_tc.limit_sec * 1000 : 0; G.inc_ms = G.has_clock ? g_off_tc.inc_sec * 1000 : 0;
        G.t_ms[WHITE] = G.t_ms[BLACK] = G.init_ms; G.t_turn = -1; G.t_stamp = now_ms();
        int est = g_off_tc.limit_sec + 40 * g_off_tc.inc_sec;
        snprintf(G.speed, sizeof G.speed, "%s", !G.has_clock ? "casual" : (est < 180 ? "bullet" : (est < 480 ? "blitz" : (est < 1500 ? "rapid" : "classical"))));
        snprintf(G.status, sizeof G.status, "started");
        G.loaded = true;
        on_full();
        g_last_params.backend = 2; g_last_params.ai_level = g_off_level;
        g_last_params.tc = G.has_clock ? g_off_tc : (TimeCtl){0, 0};
        return;
    }
    if (g_open_backend == 1) {
        G.backend = 1;
        g_fics.on_s12 = on_fics_s12; g_fics.on_result = on_fics_result; g_fics.on_event = on_fics_event;
        g_fics.start_pending = false;
        if (g_fics.in_game) on_fics_s12(&g_fics.last);
        if (g_fics.result_valid) { g_fics.result_valid = false; on_fics_result(g_fics.res_winner, g_fics.res_status, g_fics.res_why); }
        return;
    }
    g_api.on_game_line = on_line; g_api.on_game_end = on_end; g_api.on_move_result = on_move_result;
    api_game_open(G.id);
    api_warm();
}

static void game_leave(void) {
    if (off_game()) { eng_async_cancel(); g_eng_busy = false; }
    g_fics.on_s12 = NULL; g_fics.on_result = NULL; g_fics.on_event = NULL;
    api_game_close();
    g_api.on_game_line = NULL; g_api.on_game_end = NULL; g_api.on_move_result = NULL;
    G.active = false;
}

static void game_update(float dt) {
    g_wait_t += dt;
    board_update(&bv, dt);
    bv.ox = g_set.board_left ? 0 : 80;
    if (off_game()) off_update();
    if (g_reconnect_at && now_ms() >= g_reconnect_at && !fics_game() && !off_game()) { g_reconnect_at = 0; if (!G.finished) api_game_open(G.id); }
    if (fics_game() && !G.finished && g_fics.state != FICS_ONLINE && (g_fics.state == FICS_ERROR || g_fics.state == FICS_OFF)) {
        // the server keeps the game running while we are away: log back in and it sends the board again
        if (!g_reconnect_at) g_reconnect_at = now_ms() + 2500;
        else if (now_ms() >= g_reconnect_at) { g_reconnect_at = 0; fics_connect(g_fics.guest, g_fics.user, g_fics.pass); }
    }
    if (g_api.notice_new) { g_api.notice_new = false; ui_toast(g_api.notice); g_rematch_until = 0; }
    handle_input();
    if (g_kdown & KEY_X) { board_set_flip(&bv, !bv.flipped); }
    if (g_kdown & KEY_Y) { pm_clear(&G.pre); refresh_highlights(); }
    if (g_kdown & KEY_L) history_step(-1);
    if (g_kdown & KEY_R) history_step(+1);
    if (g_kdown & KEY_START) { if (G.finished) g_over_open = true; else g_menu = !g_menu; }
    if (G.finished && g_over_done && !g_over_shown) { g_over_timer += dt; if (g_over_timer > 0.9f) { g_over_open = true; g_over_shown = true; } }
    // low time warning
    if (G.loaded && !G.finished && game_my_turn(&G) && G.has_clock && G.nply >= 2) {
        int ms = game_clock(&G, G.my_color);
        if (ms < 10000 && !g_low_warned) { g_low_warned = true; audio_play(SND_LOW); }
    }
    refresh_highlights();
}

// ------------------------------------------------------------------ top screen
static void draw_clock_box(float x, float y, float w, float h, int color, bool top) {
    (void)top;
    int ms = game_clock(&G, color);
    bool active = G.t_turn == color && !G.finished;
    bool low = G.has_clock && ms < 20000 && G.nply >= 2;
    g_rrect(x, y, w, h, 8, active ? (low ? RGB(196, 58, 48) : RGB(236, 236, 232)) : C_BG2);
    char t[16];
    if (!G.has_clock) snprintf(t, sizeof t, "--:--"); else fmt_clock(ms, t, sizeof t, true);
    g_text(F_B21, x + w / 2, y + h / 2, active ? (low ? C_TEXT : RGB(38, 36, 33)) : C_TEXT2, AL_CENTER, t);
}

static void draw_card(float y, int color) {
    bool turn = !G.finished && G.loaded && G.pos.turn == color;
    g_rrect(6, y, 388, 52, 8, C_PANEL);
    if (turn) g_rrect(6, y + 8, 4, 36, 2, C_GREEN);
    g_disc(40, y + 26, 34, color == WHITE ? RGB(230, 230, 230) : RGB(24, 24, 24));
    g_disc(40, y + 26, 28, color == WHITE ? RGB(255, 255, 255) : RGB(62, 60, 58));
    const Player *p = &G.pl[color];
    g_icon(p->ai_level ? ICON_BOT : ICON_USER, 40, y + 26, 16, color == WHITE ? RGB(60, 60, 60) : RGB(200, 200, 200));
    float nx = 64;
    if (p->title[0]) { g_rrect(nx, y + 9, 28, 16, 4, C_ORANGE); g_text(F_B11, nx + 14, y + 17, RGB(60, 40, 0), AL_CENTER, p->title); nx += 33; }
    char nm[48]; snprintf(nm, sizeof nm, "%s", G.loaded ? p->name : "...");
    g_text_fit(F_B16, nx, y + 17, C_TEXT, AL_LEFT, 150 - (nx - 64), nm);
    if (p->rating) g_textf(F_R13, 226 - 2, y + 17, C_TEXT3, AL_RIGHT, "%d%s", p->rating, p->prov ? "?" : "");
    // captured pieces: pieces of the other colour that are missing from the board
    if (G.loaded) {
        Pos cur = G.pos;
        int miss[7]; game_captured(&cur, color == WHITE ? BLACK : WHITE, miss);
        float x = 64;
        static const int ORDER[5] = {QUEEN, ROOK, BISHOP, KNIGHT, PAWN};
        for (int k = 0; k < 5; k++)
            for (int c = 0; c < miss[ORDER[k]]; c++) {
                g_piece(2, color == WHITE ? BLACK : WHITE, ORDER[k], x, y + 29, 0, 1.0f);
                x += (ORDER[k] == PAWN) ? 7 : 12;
            }
        int d = game_material_diff(&cur); if (color == BLACK) d = -d;
        if (d > 0) g_textf(F_B11, x + 6, y + 37, C_TEXT2, AL_LEFT, "+%d", d);
        if (G.pos.variant == VAR_CRAZYHOUSE) {
            float px = 226; static const int PO[5] = {PAWN, KNIGHT, BISHOP, ROOK, QUEEN};
            for (int k = 0; k < 5; k++) { int n = G.pos.pocket[color][PO[k] - 1]; if (!n) continue; g_piece(2, color, PO[k], px, y + 29, 0, 1.0f); g_textf(F_B11, px + 17, y + 37, C_TEXT2, AL_LEFT, "%d", n); px += 28; }
        }
        if (G.pos.variant == VAR_3CHECK) g_textf(F_B11, 270, y + 37, C_ORANGE, AL_RIGHT, "Checks %d/3", G.pos.checks[color]);
    }
    draw_clock_box(280, y + 7, 106, 38, color, false);
}

static void game_top(void) {
    g_rect(0, 0, 400, 240, C_BG2);
    int top_c = bottom_color() == WHITE ? BLACK : WHITE;
    draw_card(6, top_c);
    draw_card(182, bottom_color());

    // status line
    char st[96]; u32 col = C_TEXT2;
    if (!G.loaded) snprintf(st, sizeof st, "Connecting to game...");
    else if (G.finished) { char h[40], s[40]; game_result_text(&G, h, sizeof h, s, sizeof s); snprintf(st, sizeof st, "%s  -  %s", h, s); col = C_TEXT; }
    else if (view_ply >= 0) { snprintf(st, sizeof st, "Move %d of %d", view_ply, G.nply); col = C_BLUE; }
    else if (G.my_color < 0) snprintf(st, sizeof st, "%s to move", G.pos.turn == WHITE ? "White" : "Black");
    else if (game_my_turn(&G)) { snprintf(st, sizeof st, "Your turn"); col = C_GREEN; }
    else if (G.pre.n) { snprintf(st, sizeof st, "Premove queued (%d)", G.pre.n); col = C_RED; }
    else if (g_gone[0]) { snprintf(st, sizeof st, "%s", g_gone); col = C_ORANGE; }
    else snprintf(st, sizeof st, "Opponent's turn");
    g_text_fit(F_B13, 200, 69, col, AL_CENTER, 380, st);

    // move list (two moves per row, newest rows at the bottom)
    g_rrect(6, 80, 388, 98, 8, C_BG);
    if (G.loaded && G.nply == 0) g_text(F_R13, 200, 129, C_TEXT3, AL_CENTER, G.my_color == WHITE ? "You play White - make the first move" : "No moves yet");
    int rows = (G.nply + 1) / 2, vis = 5, first = rows > vis ? rows - vis : 0;
    int shown = view_ply < 0 ? G.nply : view_ply;
    for (int r = first; r < rows; r++) {
        float y = 92 + (r - first) * 18.0f;
        if ((r & 1) == 0) g_rect(10, y - 9, 380 - 4, 18, RGBA(255, 255, 255, 8));
        g_textf(F_R13, 14, y, C_TEXT3, AL_LEFT, "%d.", r + 1);
        for (int c = 0; c < 2; c++) {
            int ply = r * 2 + c;
            if (ply >= G.nply) break;
            float x = 52 + c * 120.0f;
            if (ply + 1 == shown) g_rrect(x - 6, y - 8, 76, 17, 4, C_GREEN_D);
            g_text(F_B13, x, y, ply + 1 == shown ? C_TEXT : C_TEXT2, AL_LEFT, G.san[ply]);
        }
    }
    // opening info: mode line
    char info[64]; snprintf(info, sizeof info, "%s%s  %s", fics_game() ? "FICS  " : "", variant_name(G.variant), G.rated ? "rated" : (fics_game() ? "unrated" : "casual"));
    g_text(F_R11, 388, 92, C_TEXT3, AL_RIGHT, info);
    if (G.has_clock) { char tl[24]; if (G.init_ms % 60000) snprintf(tl, sizeof tl, "%d:%02d+%d", G.init_ms / 60000, G.init_ms / 1000 % 60, G.inc_ms / 1000); else snprintf(tl, sizeof tl, "%d+%d", G.init_ms / 60000, G.inc_ms / 1000); g_text(F_B11, 388, 106, C_TEXT3, AL_RIGHT, tl); }
}

// ------------------------------------------------------------------ bottom screen
static void side_clock(float x, float y, int color) {
    int ms = game_clock(&G, color);
    bool active = G.t_turn == color && !G.finished;
    bool low = G.has_clock && ms < 20000 && G.nply >= 2;
    g_rrect(x + 3, y, 74, 28, 6, active ? (low ? RGB(196, 58, 48) : RGB(236, 236, 232)) : C_BG2);
    char t[16]; if (!G.has_clock) snprintf(t, sizeof t, "--:--"); else fmt_clock(ms, t, sizeof t, true);
    g_text(F_B16, x + 40, y + 14, active ? (low ? C_TEXT : RGB(38, 36, 33)) : C_TEXT2, AL_CENTER, t);
}

static void overlay_dim(void) { g_rect(0, 0, 320, 240, RGBA(0, 0, 0, 140)); }

static void draw_promo(void) {
    overlay_dim();
    int ox = bv.ox;
    g_rrect(ox + 6, 82, 228, 76, 12, C_PANEL);
    g_text(F_B13, ox + 120, 96, C_TEXT2, AL_CENTER, "Promote to");
    static const int T[4] = {QUEEN, ROOK, BISHOP, KNIGHT};
    int color = G.my_color;
    for (int i = 0; i < 4; i++) {
        Rect r = {ox + 14.0f + i * 54, 106, 48, 44};
        if (ui_button(r, NULL, F_B11, BS_TILE, -1)) finish_promo(T[i]);
        g_piece(1, color, T[i], r.x + 5, r.y - 1, 0, 1.0f);
    }
}

static void draw_menu(void) {
    overlay_dim();
    float cx = bv.ox + 120;
    g_rrect(cx - 104, 28, 208, 190, 12, C_PANEL);
    g_text(F_B16, cx, 46, C_TEXT, AL_CENTER, "Game menu");
    float y = 62;
    bool human_ok = !G.finished && G.my_color >= 0;
    if (human_ok && G.nply >= 2) {
        if (ui_button((Rect){cx - 92, y, 184, 28}, "Offer draw", F_B13, BS_SECONDARY, ICON_HANDSHAKE)) { be_draw(true); g_menu = false; ui_toast("Draw offered"); }
        y += 34;
        if (ui_button((Rect){cx - 92, y, 184, 28}, "Ask takeback", F_B13, BS_SECONDARY, ICON_UNDO_2)) { if (off_game() && g_off_bot >= 0) { ui_toast("No takebacks against ladder bots"); g_menu = false; } else if (off_game()) { off_takeback(); g_menu = false; } else { if (fics_game()) fics_send("takeback 2"); else api_takeback(G.id, true); g_menu = false; ui_toast("Takeback requested"); } }
        y += 34;
        if (ui_button((Rect){cx - 92, y, 184, 28}, g_confirm_resign ? "Tap again to resign" : "Resign", F_B13, BS_DANGER, ICON_FLAG)) {
            if (g_confirm_resign) { be_resign(); g_menu = false; g_confirm_resign = false; } else g_confirm_resign = true;
        }
        y += 34;
    } else if (human_ok) {
        if (ui_button((Rect){cx - 92, y, 184, 28}, "Abort game", F_B13, BS_DANGER, ICON_X)) { be_abort(); g_menu = false; }
        y += 34;
    }
    if (ui_button((Rect){cx - 92, y, 184, 28}, "Leave to home", F_B13, BS_SECONDARY, ICON_HOUSE)) { g_menu = false; scene_goto(SC_HOME); return; }
    y += 34;
    if (ui_button((Rect){cx - 92, y, 184, 26}, "Close", F_B13, BS_GHOST, -1)) { g_menu = false; g_confirm_resign = false; }
}

static void rematch(void) {
    if (off_game() && g_off_bot >= 0) { scene_game_open_bot(g_off_bot, g_set.color); return; }
    if (off_game() && g_off_fen[0]) { scene_game_open_offline_fen(g_last_params.ai_level, g_off_fen); return; }
    if (off_game()) { scene_game_open_offline(g_last_params.ai_level, g_last_params.tc.limit_sec > 0 || g_last_params.tc.inc_sec > 0, g_last_params.tc, g_last_params.color); return; }
    if (fics_game()) { fics_send("rematch"); g_rematch_until = now_ms() + 15000; return; }
    TimeCtl tc = g_last_params.tc;
    if (g_last_params.ai_level > 0) { api_challenge_ai(g_last_params.ai_level, tc, g_last_params.variant, g_last_params.color); g_rematch_until = now_ms() + 12000; }
    else if (g_last_params.opp[0]) { api_challenge_user(g_last_params.opp, tc, g_last_params.rated, g_last_params.variant, g_last_params.color); g_rematch_until = now_ms() + 600000; }
    else { api_seek(tc, g_last_params.rated, g_last_params.variant, 0); g_rematch_until = now_ms() + 600000; }
}

static void draw_over(void) {
    overlay_dim();
    float cx = bv.ox + 120;
    g_rrect(cx - 108, 26, 216, 192, 12, C_PANEL);
    char h[40], s[40]; game_result_text(&G, h, sizeof h, s, sizeof s);
    bool win = G.winner >= 0 && G.winner == G.my_color;
    g_icon(win ? ICON_TROPHY : (G.winner < 0 ? ICON_HANDSHAKE : ICON_FLAG), cx, 52, 24, win ? C_GREEN : C_TEXT2);
    g_text(F_B21, cx, 80, win ? C_GREEN : C_TEXT, AL_CENTER, h);
    if (g_bot_msg[0] && off_game()) g_text_fit(F_B13, cx, 100, C_GREEN_L, AL_CENTER, 200, g_bot_msg); else g_text(F_R13, cx, 100, C_TEXT3, AL_CENTER, s);
    bool waiting = g_api.seeking || g_api.chal_out_wait || now_ms() < g_rematch_until;
    if (waiting) {
        ui_spinner(cx, 138, 12, C_GREEN, g_wait_t);
        if (ui_button((Rect){cx - 80, 164, 160, 30}, "Cancel", F_B13, BS_DANGER, ICON_X)) { api_seek_cancel(); api_challenge_cancel(); g_rematch_until = 0; }
        return;
    }
    if (G.my_color >= 0) {
        const char *rl = (fics_game() || off_game()) ? "Rematch" : (g_last_params.ai_level ? "Rematch" : (g_last_params.opp[0] ? "Rematch" : "New opponent"));
        if (g_api.tourney_active && !fics_game() && !off_game()) { if (ui_button((Rect){cx - 96, 116, 192, 32}, "Arena standings", F_B16, BS_PRIMARY, ICON_TROPHY)) scene_goto(SC_TOURNEY); }
        else if (ui_button((Rect){cx - 96, 116, 192, 32}, rl, F_B16, BS_PRIMARY, ICON_SWORDS)) rematch();
    }
    if (ui_button((Rect){cx - 96, 154, 94, 28}, "Analyse", F_B11, BS_SECONDARY, ICON_TARGET)) { ag_from_game(&G); scene_analysis_open(0, SC_HOME); return; }
    if (ui_button((Rect){cx + 2, 154, 94, 28}, "Home", F_B11, BS_SECONDARY, ICON_HOUSE)) scene_goto(SC_HOME);
    int opp = G.my_color >= 0 ? (G.my_color ^ 1) : -1;
    bool can_follow = opp >= 0 && !fics_game() && !off_game() && !G.pl[opp].ai_level && G.pl[opp].name[0] && strcmp(G.pl[opp].name, "AI") != 0;
    if (can_follow) {
        static char followed[32];
        bool done = !strcmp(followed, G.pl[opp].name);
        if (ui_button((Rect){cx - 96, 186, 62, 26}, done ? "Followed" : "Follow", F_B11, done ? BS_GHOST : BS_SECONDARY, ICON_USERS) && !done) { social_follow(G.pl[opp].name); snprintf(followed, sizeof followed, "%s", G.pl[opp].name); }
        if (ui_button((Rect){cx - 30, 186, 62, 26}, "Board", F_B11, BS_GHOST, ICON_EYE)) g_over_open = false;
        if (ui_button((Rect){cx + 36, 186, 60, 26}, "New", F_B11, BS_GHOST, ICON_SETTINGS)) scene_goto(SC_PLAY);
        return;
    }
    if (ui_button((Rect){cx - 96, 186, 94, 26}, "View board", F_B11, BS_GHOST, ICON_EYE)) g_over_open = false;
    if (ui_button((Rect){cx + 2, 186, 94, 26}, "New game", F_B11, BS_GHOST, ICON_SETTINGS)) { if (off_game() && g_off_bot >= 0) { scene_goto(SC_BOTS); return; } if (fics_game()) scene_play_set_mode(3); if (off_game()) scene_play_set_mode(4); scene_goto(SC_PLAY); }
}

// A pending draw/takeback offer: shown in the side column (never over the board) and ignorable in the settings
static const char *pending_offer(bool *is_draw) {
    if (G.my_color < 0 || G.finished) return NULL;
    int opp = G.my_color == WHITE ? BLACK : WHITE;
    if (G.draw_offer[opp] && !g_set.hide_draw) { *is_draw = true; return "Draw?"; }
    if (G.takeback_offer[opp]) { *is_draw = false; return "Takeback?"; }
    return NULL;
}

static void draw_side(void) {
    float x0 = g_set.board_left ? 240.0f : 0.0f;
    g_rect(x0, 0, 80, 240, C_BG);
    int bc = bottom_color(), tc = bc == WHITE ? BLACK : WHITE;
    side_clock(x0, 4, tc);
    side_clock(x0, 208, bc);
    bool modal = g_menu || g_over_open || g_promo_open;
    if (ui_icon_button((Rect){x0 + 2, 38, 36, 34}, ICON_MENU, BS_SECONDARY) && !modal) { if (G.finished) g_over_open = true; else g_menu = true; }
    bool offer_draw = false; const char *offer = pending_offer(&offer_draw);
    if (offer) g_disc(x0 + 36, 41, 10, C_RED);
    if (ui_icon_button((Rect){x0 + 42, 38, 36, 34}, ICON_ARROW_DOWN_UP, BS_SECONDARY)) board_set_flip(&bv, !bv.flipped);
    if (ui_icon_button((Rect){x0 + 2, 76, 36, 34}, ICON_CHEVRON_LEFT, BS_SECONDARY) && !modal) history_step(-1);
    if (ui_icon_button((Rect){x0 + 42, 76, 36, 34}, ICON_CHEVRON_RIGHT, view_ply >= 0 ? BS_PRIMARY : BS_SECONDARY) && !modal) history_step(+1);
    if (G.pre.n) {
        char l[24]; snprintf(l, sizeof l, "Clear %d", G.pre.n);
        if (ui_button((Rect){x0 + 2, 114, 76, 28}, l, F_B11, BS_DANGER, -1)) { pm_clear(&G.pre); refresh_highlights(); }
    } else if (view_ply >= 0) {
        if (ui_button((Rect){x0 + 2, 114, 76, 28}, "Live", F_B11, BS_PRIMARY, -1)) { view_ply = -1; refresh_highlights(); }
    }
    if (G.loaded && G.pos.variant != VAR_CRAZYHOUSE && offer) {   // offer buttons take the place of the turn indicator
        g_rrect(x0 + 2, 146, 76, 36, 8, C_PANEL2);
        g_text(F_B11, x0 + 40, 154, C_ORANGE, AL_CENTER, offer);
        if (ui_icon_button((Rect){x0 + 2, 160, 36, 26}, ICON_CHECK, BS_PRIMARY) && !modal) { if (offer_draw) be_draw(true); else be_takeback(true); }
        if (ui_icon_button((Rect){x0 + 42, 160, 36, 26}, ICON_X, BS_DANGER) && !modal) { if (offer_draw) be_draw(false); else be_takeback(false); }
    } else if (G.loaded && G.pos.variant != VAR_CRAZYHOUSE) {          // turn indicator + last move at a glance
        const char *txt; u32 bg, fg = C_TEXT;
        if (G.finished) { txt = "Game over"; bg = C_PANEL3; }
        else if (G.my_color < 0) { txt = G.pos.turn == WHITE ? "White" : "Black"; bg = C_PANEL3; }
        else if (game_my_turn(&G)) { txt = "Your move"; bg = C_GREEN_D; }
        else if (G.pre.n) { txt = "Premoving"; bg = RGB(150, 44, 36); }
        else { txt = "Waiting"; bg = C_PANEL2; fg = C_TEXT2; }
        g_rrect(x0 + 3, 152, 74, 24, 12, bg);
        g_text(F_B11, x0 + 40, 164, fg, AL_CENTER, txt);
        int shown = view_ply < 0 ? G.nply : view_ply;
        if (shown > 0) {
            g_textf(F_R11, x0 + 40, 188, C_TEXT3, AL_CENTER, "%d%s", (shown + 1) / 2, shown & 1 ? "." : "...");
            g_text_fit(F_B16, x0 + 40, 204 - 4, C_TEXT, AL_CENTER, 72, G.san[shown - 1]);
        }
    }
    if (G.loaded && G.pos.variant == VAR_CRAZYHOUSE && G.my_color >= 0) {
        for (int i = 0; i < 5; i++) {
            Rect r = pocket_rect(i, (int)x0);
            int n = G.pos.pocket[G.my_color][POCKET_ORDER[i] - 1];
            bool sel = pocket_sel == POCKET_ORDER[i];
            g_rrect(r.x, r.y, r.w - 1, r.h - 1, 4, sel ? C_GREEN_D : C_PANEL2);
            g_piece(0, G.my_color, POCKET_ORDER[i], r.x - 2, r.y - 2, n ? 0 : RGBA(255, 255, 255, 70), 0.9f);
            if (n > 1) g_textf(F_B11, r.x + r.w - 5, r.y + r.h - 7, C_TEXT, AL_RIGHT, "%d", n);
        }
    }
}

static void game_bot(void) {
    g_rect(0, 0, 320, 240, C_BG);
    bool modal = g_menu || g_over_open || g_promo_open;
    ui_set_enabled(!modal);
    Pos vp; view_position(&vp);
    board_draw(&bv, &vp);
    draw_side();
    if (!G.loaded) {
        g_rect((float)bv.ox, 0, 240, 240, RGBA(0, 0, 0, 120));
        ui_spinner(bv.ox + 120.0f, 112, 18, C_GREEN, g_wait_t);
        g_text(F_B13, bv.ox + 120.0f, 150, C_TEXT, AL_CENTER, g_reconnect_at ? "Reconnecting..." : "Loading game...");
        if (g_wait_t > 6.0f && ui_button((Rect){bv.ox + 70.0f, 176, 100, 28}, "Home", F_B13, BS_SECONDARY, ICON_HOUSE)) scene_goto(SC_HOME);
    }
    ui_set_enabled(true);
    if (g_promo_open) draw_promo();
    else if (g_menu) draw_menu();
    else if (g_over_open) draw_over();
    ui_set_enabled(true);
    ui_toast_draw((float)bv.ox + 120);
}

// ------------------------------------------------------------------ scenario hooks
bool scene_game_square_xy(int sq, int *x, int *y) {
    if (scene_current() != SC_GAME || sq < 0 || sq > 63) return false;
    float cx, cy; board_sq_center(&bv, sq, &cx, &cy);
    *x = (int)cx; *y = (int)cy;
    return true;
}

bool scene_game_cond(const char *name, const char *args, bool *known) {
    *known = true;
    int n = args ? atoi(args) : 0;
    if (!strcmp(name, "loaded")) return G.loaded;
    if (!strcmp(name, "myturn")) return G.loaded && game_my_turn(&G);
    if (!strcmp(name, "oppturn")) return G.loaded && !G.finished && !game_my_turn(&G);
    if (!strcmp(name, "gameover")) return G.loaded && G.finished;
    if (!strcmp(name, "plies")) return G.nply >= n;
    if (!strcmp(name, "npre")) return G.pre.n == n;
    *known = false;
    return false;
}

bool scene_game_cmd(const char *name, const char *args) {
    (void)args;
    if (!strcmp(name, "resign")) { be_resign(); return true; }
    if (!strcmp(name, "abort")) { be_abort(); return true; }
    if (!strcmp(name, "flip")) { board_set_flip(&bv, !bv.flipped); return true; }
    if (!strcmp(name, "menu")) { g_menu = true; return true; }
    if (!strcmp(name, "closemenu")) { g_menu = false; g_over_open = false; return true; }
    if (!strcmp(name, "dropstream")) { api_game_close(); on_end(56, 0); return true; }            // simulate a broken connection
    if (!strcmp(name, "sendbad")) { if (fics_game()) fics_send("a1a8"); else api_move(G.id, "a1a8"); return true; }                       // server must reject -> resync path
    if (!strcmp(name, "prev")) { history_step(-1); return true; }
    if (!strcmp(name, "next")) { history_step(+1); return true; }
    if (!strcmp(name, "clearpre")) { pm_clear(&G.pre); refresh_highlights(); return true; }
    return false;
}

const Scene scene_game = {"game", game_enter, game_update, game_top, game_bot, game_leave};
