// Game history: recent Lichess games (from the API) and FICS games (stored on the SD card); tap one to analyse it
#include "scenes.h"
#include "analysis.h"
#include "ui.h"
#include <jansson.h>
#include <time.h>

#define HMAX 40
#define HFILE DATA_DIR "/history.json"

typedef struct {
    char id[12];
    char white[32], black[32];
    int wr, br, wd, bd;               // ratings, rating changes
    int me;                           // my colour (WHITE / BLACK) or -1
    char speed[12], variant[20];
    bool rated;
    char status[16]; int winner;
    long t;                           // unix seconds
    char opening[48], why[40];
    int init_s, inc_s;
    char *moves; bool uci;            // space separated SAN (Lichess) or UCI (FICS)
    char *fen;                        // start position when not the standard one
    short *ev; uint8_t *evk, *judg; int nev;
} HGame;

static HGame g_h[2][HMAX];
static int g_hn[2];
static int g_tab;                     // 0 Lichess, 1 FICS
static bool g_loading, g_fail;
static u64 g_loaded_at;
static int g_req;
static float g_t;
static Scroll g_scroll;

static void free_game(HGame *h) { free(h->moves); free(h->fen); free(h->ev); free(h->evk); free(h->judg); memset(h, 0, sizeof *h); }
static void free_list(int tab) { for (int i = 0; i < g_hn[tab]; i++) free_game(&g_h[tab][i]); g_hn[tab] = 0; }

static const char *jstr(json_t *o, const char *k) { json_t *v = json_object_get(o, k); return json_is_string(v) ? json_string_value(v) : NULL; }
static int jint(json_t *o, const char *k, int d) { json_t *v = json_object_get(o, k); return json_is_integer(v) ? (int)json_integer_value(v) : d; }

// ------------------------------------------------------------------ Lichess list
static void parse_side(json_t *p, char *name, size_t n, int *rating, int *diff) {
    json_t *u = json_object_get(p, "user");
    const char *nm = jstr(u, "name");
    if (nm) snprintf(name, n, "%s", nm);
    else if (json_is_integer(json_object_get(p, "aiLevel"))) snprintf(name, n, "Stockfish %d", jint(p, "aiLevel", 1));
    else snprintf(name, n, "Anonymous");
    *rating = jint(p, "rating", 0); *diff = jint(p, "ratingDiff", 0);
}

void hist_net_line(const char *line) {
    if (g_hn[0] >= HMAX) return;
    json_error_t e; json_t *o = json_loads(line, 0, &e);
    if (!o) return;
    HGame *h = &g_h[0][g_hn[0]];
    memset(h, 0, sizeof *h);
    const char *id = jstr(o, "id"); snprintf(h->id, sizeof h->id, "%s", id ? id : "");
    json_t *pl = json_object_get(o, "players");
    parse_side(json_object_get(pl, "white"), h->white, sizeof h->white, &h->wr, &h->wd);
    parse_side(json_object_get(pl, "black"), h->black, sizeof h->black, &h->br, &h->bd);
    h->me = -1;
    if (!strcasecmp(h->white, g_api.user)) h->me = WHITE; else if (!strcasecmp(h->black, g_api.user)) h->me = BLACK;
    const char *sp = jstr(o, "speed"); snprintf(h->speed, sizeof h->speed, "%s", sp ? sp : "");
    const char *vr = jstr(o, "variant"); snprintf(h->variant, sizeof h->variant, "%s", vr ? vr : "standard");
    h->rated = json_is_true(json_object_get(o, "rated"));
    const char *st = jstr(o, "status"); snprintf(h->status, sizeof h->status, "%s", st ? st : "");
    const char *w = jstr(o, "winner"); h->winner = w ? (!strcmp(w, "white") ? WHITE : BLACK) : -1;
    h->t = (long)(json_integer_value(json_object_get(o, "createdAt")) / 1000);
    const char *on = jstr(json_object_get(o, "opening"), "name"); snprintf(h->opening, sizeof h->opening, "%s", on ? on : "");
    json_t *ck = json_object_get(o, "clock"); h->init_s = jint(ck, "initial", 0); h->inc_s = jint(ck, "increment", 0);
    const char *mv = jstr(o, "moves"); h->moves = strdup(mv ? mv : ""); h->uci = false;
    const char *fen = jstr(o, "initialFen"); if (fen && strcmp(fen, "startpos")) h->fen = strdup(fen);
    json_t *an = json_object_get(o, "analysis");
    if (json_is_array(an) && json_array_size(an)) {
        int n = (int)json_array_size(an);
        h->nev = n; h->ev = calloc((size_t)n, sizeof *h->ev); h->evk = calloc((size_t)n, 1); h->judg = calloc((size_t)n, 1);
        for (int i = 0; i < n; i++) {
            json_t *a = json_array_get(an, (size_t)i);
            if (json_is_integer(json_object_get(a, "mate"))) { h->evk[i] = 2; h->ev[i] = (short)jint(a, "mate", 0); }
            else if (json_is_integer(json_object_get(a, "eval"))) { h->evk[i] = 1; int v = jint(a, "eval", 0); h->ev[i] = (short)(v > 30000 ? 30000 : (v < -30000 ? -30000 : v)); }
            const char *jn = jstr(json_object_get(a, "judgment"), "name");
            if (jn) h->judg[i] = !strcmp(jn, "Inaccuracy") ? 1 : (!strcmp(jn, "Mistake") ? 2 : (!strcmp(jn, "Blunder") ? 3 : 0));
        }
    }
    json_decref(o);
    g_hn[0]++;
}

void hist_net_end(int curl_rc, long http) {
    g_loading = false; g_req = 0;
    if (curl_rc == -1) return;
    g_fail = curl_rc != 0 || http != 200;
    g_loaded_at = now_ms();
    LOGF("history: %d games (rc=%d http=%ld)", g_hn[0], curl_rc, http);
}

static void fetch_lichess(void) {
    if (g_req) li_cancel(g_req);
    free_list(0);
    char path[240];
    snprintf(path, sizeof path, "/api/games/user/%s?max=%d&moves=true&opening=true&evals=true&clocks=false&tags=false&lastFen=false", g_api.id[0] ? g_api.id : g_api.user, HMAX);
    g_req = li_request(TAG_HIST, "GET", path, NULL, true);
    g_loading = true; g_fail = false;
    memset(&g_scroll, 0, sizeof g_scroll);
}

// ------------------------------------------------------------------ local (FICS) list
static void load_local(void) {
    free_list(1);
    size_t len; char *txt = read_file(HFILE, &len);
    if (!txt) return;
    json_error_t e; json_t *arr = json_loads(txt, 0, &e); free(txt);
    size_t i; json_t *o;
    json_array_foreach(arr, i, o) {
        if (g_hn[1] >= HMAX) break;
        HGame *h = &g_h[1][g_hn[1]++];
        memset(h, 0, sizeof *h);
        snprintf(h->white, sizeof h->white, "%s", jstr(o, "w") ? jstr(o, "w") : "?");
        snprintf(h->black, sizeof h->black, "%s", jstr(o, "b") ? jstr(o, "b") : "?");
        h->wr = jint(o, "wr", 0); h->br = jint(o, "br", 0); h->me = jint(o, "me", -1);
        snprintf(h->status, sizeof h->status, "%s", jstr(o, "st") ? jstr(o, "st") : "");
        snprintf(h->why, sizeof h->why, "%s", jstr(o, "why") ? jstr(o, "why") : "");
        snprintf(h->speed, sizeof h->speed, "%s", jstr(o, "speed") ? jstr(o, "speed") : "");
        snprintf(h->variant, sizeof h->variant, "standard");
        h->winner = jint(o, "win", -1); h->t = (long)json_integer_value(json_object_get(o, "t"));
        h->rated = json_is_true(json_object_get(o, "rated"));
        h->init_s = jint(o, "init", 0); h->inc_s = jint(o, "inc", 0);
        h->moves = strdup(jstr(o, "mv") ? jstr(o, "mv") : ""); h->uci = true;
        if (jstr(o, "fen")) h->fen = strdup(jstr(o, "fen"));
    }
    json_decref(arr);
}

void history_save_game(const Game *g) {
    if (g->backend != 1 || !g->loaded || g->nply < 2) return;           // FICS games only (Lichess keeps its own history); skip aborted starts
    size_t len; char *txt = read_file(HFILE, &len);
    json_t *arr = NULL;
    if (txt) { json_error_t e; arr = json_loads(txt, 0, &e); free(txt); }
    if (!json_is_array(arr)) { if (arr) json_decref(arr); arr = json_array(); }
    json_t *o = json_object();
    json_object_set_new(o, "w", json_string(g->pl[WHITE].name)); json_object_set_new(o, "b", json_string(g->pl[BLACK].name));
    json_object_set_new(o, "wr", json_integer(g->pl[WHITE].rating)); json_object_set_new(o, "br", json_integer(g->pl[BLACK].rating));
    json_object_set_new(o, "me", json_integer(g->my_color));
    json_object_set_new(o, "st", json_string(g->status)); json_object_set_new(o, "why", json_string(g->why));
    json_object_set_new(o, "win", json_integer(g->winner)); json_object_set_new(o, "t", json_integer((json_int_t)time(NULL)));
    json_object_set_new(o, "speed", json_string(g->speed)); json_object_set_new(o, "rated", json_boolean(g->rated));
    json_object_set_new(o, "init", json_integer(g->init_ms / 1000)); json_object_set_new(o, "inc", json_integer(g->inc_ms / 1000));
    Pos std; pos_init(&std, VAR_STANDARD);
    if (memcmp(std.b, g->start.b, 64) || g->start.turn != std.turn) { char fen[100]; pos_to_fen(&g->start, fen, sizeof fen); json_object_set_new(o, "fen", json_string(fen)); }
    static char mv[MAX_PLIES * 6]; mv[0] = 0; size_t k = 0;
    for (int i = 0; i < g->nply && k + 8 < sizeof mv; i++) k += (size_t)snprintf(mv + k, sizeof mv - k, "%s%s", i ? " " : "", g->uci[i]);
    json_object_set_new(o, "mv", json_string(mv));
    json_array_insert_new(arr, 0, o);
    while (json_array_size(arr) > 50) json_array_remove(arr, json_array_size(arr) - 1);
    char *s = json_dumps(arr, 0);
    if (s) { write_file(HFILE, s, strlen(s)); free(s); }
    json_decref(arr);
    LOGF("history: saved FICS game (%d plies)", g->nply);
}

// ------------------------------------------------------------------ opening a game in the analysis board
static const char *status_text(const HGame *h) {
    const char *s = h->status;
    if (h->why[0]) return h->why;
    if (!strcmp(s, "mate")) return "Checkmate";
    if (!strcmp(s, "resign")) return "Resignation";
    if (!strcmp(s, "outoftime") || !strcmp(s, "timeout")) return "Time out";
    if (!strcmp(s, "stalemate")) return "Stalemate";
    if (!strcmp(s, "draw")) return "Draw";
    if (!strcmp(s, "aborted") || !strcmp(s, "noStart")) return "Aborted";
    if (!strcmp(s, "variantEnd")) return "Variant ending";
    return s[0] ? s : "Game over";
}

static void open_game(const HGame *h) {
    Variant v = VAR_STANDARD; variant_from_key(h->variant, &v);
    ag_reset(v, h->fen);
    snprintf(g_ag.white, sizeof g_ag.white, "%s", h->white); snprintf(g_ag.black, sizeof g_ag.black, "%s", h->black);
    g_ag.wr = h->wr; g_ag.br = h->br;
    snprintf(g_ag.opening, sizeof g_ag.opening, "%s", h->opening);
    const char *score = !strcmp(h->status, "aborted") || !strcmp(h->status, "noStart") ? "*" : (h->winner == WHITE ? "1-0" : (h->winner == BLACK ? "0-1" : "1/2"));
    snprintf(g_ag.result, sizeof g_ag.result, "%s  %s", score, status_text(h));
    g_ag.from_game = true;
    char tok[16]; const char *s = h->moves;
    while (*s) {
        while (*s == ' ') s++;
        int k = 0; while (*s && *s != ' ' && k < 15) tok[k++] = *s++;
        tok[k] = 0;
        if (!k) break;
        if (!(h->uci ? ag_push_uci(tok) : ag_push_san(tok))) break;
    }
    for (int i = 0; i < h->nev && i < g_ag.n; i++) { g_ag.ev[i + 1] = h->ev[i]; g_ag.evk[i + 1] = h->evk[i]; g_ag.judg[i + 1] = h->judg[i]; }
    scene_analysis_open(0, SC_HISTORY);
}

// ------------------------------------------------------------------ scene
static void h_enter(void) {
    g_t = 0; g_tab = 0;
    memset(&g_scroll, 0, sizeof g_scroll);
    load_local();
    if (g_api.net == 2 && (!g_hn[0] || now_ms() - g_loaded_at > 60000)) fetch_lichess();
    if (g_tab == 0 && g_api.net != 2) g_fail = true;
}
static void h_leave(void) { if (g_req) { li_cancel(g_req); g_req = 0; g_loading = false; } }
static void h_update(float dt) { g_t += dt; }

static void fmt_date(long t, char *out, size_t n) {
    if (!t) { snprintf(out, n, ""); return; }
    time_t tt = (time_t)t; struct tm *tm = gmtime(&tt);
    if (tm) strftime(out, n, "%b %d", tm); else out[0] = 0;
}

static void h_top(void) {
    ui_top_bg();
    g_icon(ICON_LIST, 40, 40, 32, C_GREEN);
    g_text(F_B21, 66, 40, C_TEXT, AL_LEFT, "Game history");
    g_text(F_R13, 36, 78, C_TEXT2, AL_LEFT, g_tab == 0 ? "Your recent Lichess games." : "Your FICS games are saved on this console.");
    g_text(F_R13, 36, 98, C_TEXT3, AL_LEFT, "Tap a game to replay it on the analysis board.");
    int w = 0, d = 0, l = 0;
    for (int i = 0; i < g_hn[g_tab]; i++) {
        const HGame *h = &g_h[g_tab][i];
        if (h->me < 0 || !strcmp(h->status, "aborted") || !strcmp(h->status, "noStart")) continue;
        if (h->winner < 0) d++; else if (h->winner == h->me) w++; else l++;
    }
    if (g_hn[g_tab]) {
        g_rrect(36, 130, 328, 64, 10, C_PANEL);
        g_textf(F_B34, 100, 160, C_GREEN, AL_CENTER, "%d", w); g_text(F_R11, 100, 182, C_TEXT3, AL_CENTER, "wins");
        g_textf(F_B34, 200, 160, C_TEXT2, AL_CENTER, "%d", d); g_text(F_R11, 200, 182, C_TEXT3, AL_CENTER, "draws");
        g_textf(F_B34, 300, 160, C_RED, AL_CENTER, "%d", l); g_text(F_R11, 300, 182, C_TEXT3, AL_CENTER, "losses");
        g_textf(F_R11, 200, 212, C_TEXT3, AL_CENTER, "last %d games", g_hn[g_tab]);
    }
}

static void h_bot(void) {
    g_rect(0, 0, 320, 240, C_BG);
    bool ov = g_api.chal_in_pending;
    ui_set_enabled(!ov);
    Rect view = {0, 70, 320, 170};
    const float rh = 48;
    int n = g_hn[g_tab];
    ui_scroll_update(&g_scroll, view, 8 + n * rh);
    for (int i = 0; i < n; i++) {
        const HGame *h = &g_h[g_tab][i];
        float y = view.y + 4 + i * rh - g_scroll.off;
        if (y < 62 || y > 240) continue;
        Rect r = {8, y, 304, rh - 4};
        bool held = ui_held(r) && !ui_scroll_moved(&g_scroll);
        g_rrect(r.x, r.y, r.w, r.h, 8, held ? C_PANEL3 : C_PANEL2);
        bool aborted = !strcmp(h->status, "aborted") || !strcmp(h->status, "noStart");
        int res = (h->me < 0 || aborted) ? 3 : (h->winner < 0 ? 2 : (h->winner == h->me ? 0 : 1));    // 0 win 1 loss 2 draw 3 other
        static const char *LET[4] = {"W", "L", "D", "-"};
        u32 col = res == 0 ? C_GREEN_D : (res == 1 ? RGB(150, 44, 36) : C_PANEL3);
        g_disc(r.x + 22, r.y + r.h / 2, 28, col);
        g_text(F_B16, r.x + 22, r.y + r.h / 2, C_TEXT, AL_CENTER, LET[res]);
        const char *opp = h->me == WHITE ? h->black : (h->me == BLACK ? h->white : h->white);
        int orat = h->me == WHITE ? h->br : (h->me == BLACK ? h->wr : h->wr);
        char head[64]; if (orat) snprintf(head, sizeof head, "%s (%d)", opp, orat); else snprintf(head, sizeof head, "%s", opp);
        g_text_fit(F_B13, r.x + 44, r.y + 14, C_TEXT, AL_LEFT, 186, head);
        char date[16]; fmt_date(h->t, date, sizeof date);
        char tc[16] = ""; if (h->init_s || h->inc_s) snprintf(tc, sizeof tc, "%d+%d", h->init_s / 60, h->inc_s);
        char sub[80]; snprintf(sub, sizeof sub, "%s%s%s  %s  %s", h->speed, tc[0] ? "  " : "", tc, h->rated ? "rated" : (g_tab == 1 ? "unrated" : "casual"), date);
        g_text_fit(F_R11, r.x + 44, r.y + 31, C_TEXT3, AL_LEFT, 190, sub);
        int diff = h->me == WHITE ? h->wd : (h->me == BLACK ? h->bd : 0);
        if (diff) g_textf(F_B13, r.x + r.w - 12, r.y + 14, diff > 0 ? C_GREEN : C_RED, AL_RIGHT, "%+d", diff);
        if (g_tab == 0 && h->variant[0] && strcmp(h->variant, "standard")) g_text_fit(F_R11, r.x + r.w - 12, r.y + 31, C_TEXT3, AL_RIGHT, 70, h->variant);
        if (ui_clicked(r) && !ui_scroll_moved(&g_scroll)) open_game(h);
    }
    if (!n) {
        if (g_tab == 0 && g_loading) { ui_spinner(160, 140, 16, C_GREEN, g_t); g_text(F_R13, 160, 176, C_TEXT3, AL_CENTER, "Loading games..."); }
        else if (g_tab == 0 && g_fail) g_text_fit(F_R13, 160, 140, C_ORANGE, AL_CENTER, 280, g_api.net == 2 ? "Could not load your games" : "Connect to Lichess to see your games");
        else if (g_tab == 0) g_text(F_R13, 160, 140, C_TEXT3, AL_CENTER, "No games yet.");
        else { g_text(F_R13, 160, 132, C_TEXT3, AL_CENTER, "No FICS games saved yet."); g_text(F_R11, 160, 152, C_TEXT3, AL_CENTER, "They appear here after you play one."); }
    }
    // header + tabs drawn over the list
    g_rect(0, 0, 320, 70, C_BG);
    ui_header("History", ICON_LIST);
    if (ui_icon_button((Rect){216, 3, 32, 28}, ICON_TARGET, BS_GHOST)) { ag_reset(VAR_STANDARD, NULL); scene_analysis_open(0, SC_HISTORY); }
    if (ui_icon_button((Rect){250, 3, 32, 28}, ICON_REFRESH_CW, BS_GHOST)) { if (g_tab == 0) { if (g_api.net == 2) fetch_lichess(); } else load_local(); }
    if (ui_icon_button((Rect){284, 3, 32, 28}, ICON_HOUSE, BS_GHOST)) scene_goto(SC_HOME);
    static const char *TABS[2] = {"Lichess", "FICS"};
    for (int i = 0; i < 2; i++)
        if (ui_button((Rect){8 + i * 156.0f, 38, 148, 28}, TABS[i], F_B13, i == g_tab ? BS_TILE_SEL : BS_TILE, i == 0 ? ICON_SWORDS : ICON_GAMEPAD_2)) { g_tab = i; memset(&g_scroll, 0, sizeof g_scroll); }
    if (ov) ui_challenge_overlay();
    ui_set_enabled(true);
    ui_toast_draw(160);
}

const Scene scene_history = {"history", h_enter, h_update, h_top, h_bot, h_leave};
