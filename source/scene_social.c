// "More" hub, rating history, friends, Lichess TV and tournaments (arenas)
#include "scenes.h"
#include "ui.h"
#include "board.h"
#include "audio.h"
#include "analysis.h"
#include <jansson.h>
#include <math.h>

static const char *jstr(json_t *o, const char *k) { const char *s = json_string_value(json_object_get(o, k)); return s ? s : ""; }
static int jint(json_t *o, const char *k) { return (int)json_integer_value(json_object_get(o, k)); }

static int rq_rating, rq_follow, rq_status, rq_tv, rq_tlist, rq_tinfo, rq_tact, rq_fact;
static void social_toast_err(long http, int rc, const char *what) {
    char m[96];
    if (rc) snprintf(m, sizeof m, "%s: no connection", what);
    else if (http == 401 || http == 403) snprintf(m, sizeof m, "%s: token lacks the permission", what);
    else snprintf(m, sizeof m, "%s: Lichess said HTTP %ld", what, http);
    ui_toast(m);
}

void social_follow(const char *username) {
    char p[96]; snprintf(p, sizeof p, "/api/rel/follow/%.60s", username);
    rq_fact = li_request(TAG_SOC, "POST", p, NULL, false);
}

// ================================================================== More hub
static void more_top(void) {
    ui_top_bg();
    g_icon(ICON_GRID_3X3, 200, 52, 32, C_GREEN);
    g_text(F_B21, 200, 92, C_TEXT, AL_CENTER, "More");
    g_text(F_R13, 200, 124, C_TEXT2, AL_CENTER, "Arenas, friends, Lichess TV, rating history,");
    g_text(F_R13, 200, 144, C_TEXT2, AL_CENTER, "and training: openings, endgames, puzzle storm.");
    g_text(F_R11, 200, 200, C_TEXT3, AL_CENTER, "Everything runs on your Lichess account.");
}

static void more_bot(void) {
    g_rect(0, 0, 320, 240, C_BG);
    ui_header("More", ICON_GRID_3X3);
    if (ui_icon_button((Rect){284, 3, 32, 28}, ICON_HOUSE, BS_GHOST)) scene_goto(SC_HOME);
    static const struct { const char *l; int icon; int sc; } T[8] = {
        {"Bots", ICON_BOT, SC_BOTS}, {"Learn", ICON_CROWN, SC_LEARN}, {"Chess clock", ICON_TIMER, SC_CLOCK}, {"Analysis", ICON_TARGET, -1},
        {"Tournaments", ICON_TROPHY, SC_TOURNEY}, {"Friends", ICON_USERS, SC_FRIENDS}, {"Lichess TV", ICON_EYE, SC_TV},
        {"Ratings", ICON_SIGNAL, SC_RATING}};
    for (int i = 0; i < 8; i++) {
        Rect r = {8 + (i % 3) * 104.0f, 44 + (i / 3) * 64.0f, 100, 58};
        if (ui_button(r, NULL, F_B11, BS_SECONDARY, -1)) {
            if (T[i].sc >= 0 && g_api.net != 2 && (T[i].sc == SC_TOURNEY || T[i].sc == SC_FRIENDS || T[i].sc == SC_RATING)) ui_toast("Needs a Lichess connection");
            else if (T[i].sc >= 0) scene_goto((SceneId)T[i].sc);
            else { ag_reset(VAR_STANDARD, NULL); scene_analysis_open(0, SC_MORE); }
        }
        g_icon(T[i].icon, r.x + r.w / 2, r.y + 20, 24, C_GREEN);
        g_text_fit(F_B11, r.x + r.w / 2, r.y + 44, C_TEXT, AL_CENTER, r.w - 6, T[i].l);
    }
    ui_toast_draw(160);
}
const Scene scene_more = {"more", NULL, NULL, more_top, more_bot, NULL};

// ================================================================== rating history
#define RH_PERFS 16
#define RH_PTS 300
typedef struct { char name[20]; int n; int day[RH_PTS]; short rating[RH_PTS]; } RhPerf;
static RhPerf g_rh[RH_PERFS];
static int g_rh_n, g_rh_sel, g_rh_state;        // state: 0 idle, 1 loading, 2 ok, 3 failed

static int days_from_civil(int y, int m, int d) {
    y -= m <= 2; int era = (y >= 0 ? y : y - 399) / 400; unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1); unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int)doe - 719468;
}

static const char *perf_label(const char *k) {
    static const struct { const char *k, *v; } L[] = {{"ultraBullet", "UltraBullet"}, {"bullet", "Bullet"}, {"blitz", "Blitz"}, {"rapid", "Rapid"}, {"classical", "Classical"},
        {"correspondence", "Corresp."}, {"crazyhouse", "Crazyhouse"}, {"chess960", "Chess960"}, {"kingOfTheHill", "King of Hill"}, {"threeCheck", "3-check"},
        {"antichess", "Antichess"}, {"atomic", "Atomic"}, {"horde", "Horde"}, {"racingKings", "Racing K."}, {"puzzle", "Puzzles"}};
    for (size_t i = 0; i < sizeof L / sizeof L[0]; i++) if (!strcmp(L[i].k, k)) return L[i].v;
    return k;
}

static void rating_enter(void) {
    g_rh_state = 1; g_rh_n = 0; g_rh_sel = 0;
    if (!g_api.id[0]) { g_rh_state = 3; return; }
    char p[96]; snprintf(p, sizeof p, "/api/user/%s/rating-history", g_api.id);
    rq_rating = li_request(TAG_SOC, "GET", p, NULL, false);
}

static void rating_net(int rc, long http, const char *body) {
    if (rc || http != 200) { g_rh_state = 3; return; }
    json_error_t e; json_t *a = json_loads(body, 0, &e);
    if (!json_is_array(a)) { g_rh_state = 3; if (a) json_decref(a); return; }
    size_t i, k; json_t *v, *pt;
    g_rh_n = 0;
    json_array_foreach(a, i, v) {
        json_t *pts = json_object_get(v, "points");
        if (!json_is_array(pts) || !json_array_size(pts) || g_rh_n >= RH_PERFS) continue;
        RhPerf *p = &g_rh[g_rh_n++]; memset(p, 0, sizeof *p);
        snprintf(p->name, sizeof p->name, "%s", jstr(v, "name"));
        size_t tot = json_array_size(pts), from = tot > RH_PTS ? tot - RH_PTS : 0;
        p->n = 0;
        json_array_foreach(pts, k, pt) {
            if (k < from) continue;
            int y = (int)json_integer_value(json_array_get(pt, 0)), m = (int)json_integer_value(json_array_get(pt, 1)) + 1, d = (int)json_integer_value(json_array_get(pt, 2));
            p->day[p->n] = days_from_civil(y, m, d); p->rating[p->n] = (short)json_integer_value(json_array_get(pt, 3)); p->n++;
        }
    }
    json_decref(a);
    g_rh_sel = 0;
    for (int q = 1; q < g_rh_n; q++) if (g_rh[q].n > g_rh[g_rh_sel].n) g_rh_sel = q;        // start with the busiest rating
    g_rh_state = 2;
}

static void civil_from_days(int z, int *y, int *m, int *d) {
    z += 719468; int era = (z >= 0 ? z : z - 146096) / 146097; unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; int yy = (int)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100); unsigned mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1); *m = (int)(mp < 10 ? mp + 3 : mp - 9); *y = yy + (*m <= 2);
}

static void rating_top(void) {
    ui_top_bg();
    g_rrect(6, 6, 388, 40, 8, C_PANEL);
    g_icon(ICON_SIGNAL, 26, 26, 24, C_GREEN);
    if (g_rh_state != 2 || !g_rh_n) {
        g_text(F_B16, 46, 26, C_TEXT, AL_LEFT, "Rating history");
        g_text(F_R13, 200, 130, C_TEXT3, AL_CENTER, g_rh_state == 1 ? "Loading..." : (g_rh_state == 3 ? "Could not load your history" : "No rated games yet"));
        return;
    }
    RhPerf *p = &g_rh[g_rh_sel % g_rh_n];
    g_text(F_B16, 46, 26, C_TEXT, AL_LEFT, perf_label(p->name));
    int cur = p->rating[p->n - 1], lo = 99999, hi = 0;
    for (int i = 0; i < p->n; i++) { if (p->rating[i] < lo) lo = p->rating[i]; if (p->rating[i] > hi) hi = p->rating[i]; }
    g_textf(F_B21, 386, 20, C_TEXT, AL_RIGHT, "%d", cur);
    int diff = cur - p->rating[0];
    g_textf(F_R11, 386, 38, diff >= 0 ? C_GREEN_L : C_RED, AL_RIGHT, "%s%d since the first entry", diff >= 0 ? "+" : "", diff);
    // chart
    g_rrect(6, 52, 388, 182, 8, C_BG);
    if (p->n < 2) { g_text(F_R13, 200, 140, C_TEXT3, AL_CENTER, "Not enough data for a chart yet"); return; }
    float cx0 = 50, cx1 = 384, cy0 = 70, cy1 = 206;
    int span = hi - lo; if (span < 40) { int mid = (hi + lo) / 2; lo = mid - 20; hi = mid + 20; span = 40; }
    int pad = span / 8; lo -= pad; hi += pad; span = hi - lo;
    for (int g = 0; g <= 4; g++) {                                      // grid lines + labels
        float y = cy1 - (cy1 - cy0) * g / 4.0f;
        g_rect(cx0, y, cx1 - cx0, 1, RGBA(255, 255, 255, 18));
        g_textf(F_R11, 44, y, C_TEXT3, AL_RIGHT, "%d", lo + span * g / 4);
    }
    int d0 = p->day[0], d1 = p->day[p->n - 1]; if (d1 <= d0) d1 = d0 + 1;
    float px = 0, py = 0;
    for (int i = 0; i < p->n; i++) {
        float x = cx0 + (cx1 - cx0) * (float)(p->day[i] - d0) / (float)(d1 - d0);
        float y = cy1 - (cy1 - cy0) * (float)(p->rating[i] - lo) / (float)span;
        if (i) g_line(px, py, x, y, 2.0f, C_GREEN);
        px = x; py = y;
    }
    g_disc(px, py, 9, C_GREEN_L);
    int y0, m0, dd0, y1, m1, dd1; civil_from_days(d0, &y0, &m0, &dd0); civil_from_days(d1, &y1, &m1, &dd1);
    g_textf(F_R11, cx0, 222, C_TEXT3, AL_LEFT, "%04d-%02d-%02d", y0, m0, dd0);
    g_textf(F_R11, cx1, 222, C_TEXT3, AL_RIGHT, "%04d-%02d-%02d", y1, m1, dd1);
    g_textf(F_R11, 200, 222, C_TEXT3, AL_CENTER, "peak %d", p->rating[0] > hi ? p->rating[0] : (hi - pad));
}

static void rating_bot(void) {
    g_rect(0, 0, 320, 240, C_BG);
    ui_header("Rating history", ICON_SIGNAL);
    if (ui_icon_button((Rect){284, 3, 32, 28}, ICON_ARROW_LEFT, BS_GHOST)) scene_goto(SC_MORE);
    if (g_rh_state == 1) ui_spinner(160, 120, 18, C_GREEN, scene_age());
    if (g_rh_state == 3) { g_text(F_R13, 160, 110, C_TEXT3, AL_CENTER, "Could not load"); if (ui_button((Rect){100, 130, 120, 30}, "Retry", F_B13, BS_PRIMARY, ICON_REFRESH_CW)) rating_enter(); }
    for (int i = 0; i < g_rh_n; i++) {
        Rect r = {8 + (i % 3) * 103.0f, 44 + (i / 3) * 32.0f, 99, 28};
        if (ui_button(r, NULL, F_B11, i == g_rh_sel ? BS_TILE_SEL : BS_TILE, -1)) g_rh_sel = i;
        g_text_fit(F_B11, r.x + r.w / 2, r.y + 13, C_TEXT, AL_CENTER, r.w - 6, perf_label(g_rh[i].name));
    }
    ui_toast_draw(160);
}
const Scene scene_rating = {"rating", rating_enter, NULL, rating_top, rating_bot, NULL};

// ================================================================== friends
typedef struct { char name[28], id[28]; bool online, playing; int rating; } Friend;
static Friend g_fr[50];
static int g_fr_n; static int g_fr_state;       // 0 idle 1 loading 2 ok 3 failed
static Scroll g_fr_scroll;
static int g_fr_sel = -1;                        // friend whose challenge menu is open
static const char *FR_TC_L[4] = {"3+0", "5+3", "10+0", "15+10"};

static void friends_load(void) {
    g_fr_state = 1; g_fr_n = 0;
    rq_follow = li_request(TAG_SOC, "GET", "/api/rel/following", NULL, true);
}

static void friends_enter(void) { g_fr_sel = -1; memset(&g_fr_scroll, 0, sizeof g_fr_scroll); friends_load(); }

static void friends_status(void) {
    if (!g_fr_n) { g_fr_state = 2; return; }
    char ids[1200] = ""; size_t k = 0;
    for (int i = 0; i < g_fr_n; i++) k += (size_t)snprintf(ids + k, sizeof ids - k, "%s%s", i ? "," : "", g_fr[i].id);
    char path[1400]; snprintf(path, sizeof path, "/api/users/status?ids=%s&withGameIds=false", ids);
    rq_status = li_request(TAG_SOC, "GET", path, NULL, false);
}

static int fr_cmp(const void *a, const void *b) {
    const Friend *x = a, *y = b;
    int sx = x->playing ? 1 : (x->online ? 2 : 0), sy = y->playing ? 1 : (y->online ? 2 : 0);   // online first, then playing, then offline
    if (sx != sy) return sy - sx;
    return strcasecmp(x->name, y->name);
}

static void friends_top(void) {
    ui_top_bg();
    g_icon(ICON_USERS, 36, 40, 32, C_GREEN);
    g_text(F_B21, 62, 40, C_TEXT, AL_LEFT, "Friends");
    int on = 0; for (int i = 0; i < g_fr_n; i++) if (g_fr[i].online) on++;
    if (g_fr_state == 2) g_textf(F_R13, 36, 84, C_TEXT2, AL_LEFT, "%d following, %d online", g_fr_n, on);
    else if (g_fr_state == 1) g_text(F_R13, 36, 84, C_TEXT2, AL_LEFT, "Loading...");
    else g_text(F_R13, 36, 84, C_ORANGE, AL_LEFT, "Could not load (token needs follow:read)");
    g_text(F_R13, 36, 124, C_TEXT3, AL_LEFT, "Tap a friend to challenge them.");
    g_text(F_R13, 36, 146, C_TEXT3, AL_LEFT, "Use + to follow someone by username.");
}

static void friends_bot(void) {
    g_rect(0, 0, 320, 240, C_BG);
    bool modal = g_fr_sel >= 0;
    ui_set_enabled(!modal);
    Rect view = {0, 40, 320, 200};
    const float rh = 40;
    ui_scroll_update(&g_fr_scroll, view, 8 + g_fr_n * rh);
    if (g_fr_state == 1) ui_spinner(160, 130, 16, C_GREEN, scene_age());
    if (g_fr_state == 2 && !g_fr_n) { g_text(F_R13, 160, 120, C_TEXT3, AL_CENTER, "You do not follow anyone yet."); g_text(F_R13, 160, 142, C_TEXT3, AL_CENTER, "Tap + to add a friend."); }
    for (int i = 0; i < g_fr_n; i++) {
        const Friend *f = &g_fr[i];
        float y = view.y + 6 + i * rh - g_fr_scroll.off;
        if (y < 30 || y > 240) continue;
        Rect r = {10, y, 300, rh - 5};
        bool held = ui_held(r) && !ui_scroll_moved(&g_fr_scroll);
        g_rrect(r.x, r.y, r.w, r.h, 8, held ? C_PANEL3 : C_PANEL2);
        g_disc(r.x + 16, r.y + r.h / 2, 12, f->playing ? C_ORANGE : (f->online ? C_GREEN : RGB(90, 88, 84)));
        g_text_fit(F_B13, r.x + 34, r.y + 17, f->online ? C_TEXT : C_TEXT2, AL_LEFT, 170, f->name);
        g_text(F_R11, r.x + r.w - 10, r.y + 17, f->playing ? C_ORANGE : (f->online ? C_GREEN_L : C_TEXT3), AL_RIGHT, f->playing ? "playing" : (f->online ? "online" : "offline"));
        if (ui_clicked(r) && !ui_scroll_moved(&g_fr_scroll)) g_fr_sel = i;
    }
    g_rect(0, 0, 320, 40, C_BG);
    ui_header("Friends", ICON_USERS);
    if (ui_icon_button((Rect){216, 3, 32, 28}, ICON_PLUS, BS_GHOST)) {
        char t[40];
        if (ui_keyboard("Lichess username to follow", "", t, sizeof t) && t[0]) {
            char p[96]; snprintf(p, sizeof p, "/api/rel/follow/%s", t);
            rq_fact = li_request(TAG_SOC, "POST", p, NULL, false);
        }
    }
    if (ui_icon_button((Rect){250, 3, 32, 28}, ICON_REFRESH_CW, BS_GHOST)) friends_load();
    if (ui_icon_button((Rect){284, 3, 32, 28}, ICON_ARROW_LEFT, BS_GHOST)) scene_goto(SC_MORE);
    if (modal) {
        const Friend *f = &g_fr[g_fr_sel];
        ui_set_enabled(true);
        g_rect(0, 0, 320, 240, RGBA(0, 0, 0, 170));
        g_rrect(14, 30, 292, 180, 12, C_PANEL);
        g_text_fit(F_B21, 160, 54, C_TEXT, AL_CENTER, 270, f->name);
        g_text(F_R13, 160, 76, f->online ? C_GREEN_L : C_TEXT3, AL_CENTER, f->playing ? "is playing right now" : (f->online ? "is online" : "is offline - the challenge waits for them"));
        g_text(F_R11, 160, 98, C_TEXT3, AL_CENTER, "Challenge (casual):");
        for (int i = 0; i < 4; i++)
            if (ui_button((Rect){22 + i * 70.0f, 108, 66, 36}, FR_TC_L[i], F_B16, BS_TILE, ICON_SWORDS)) {
                memset(&g_last_params, 0, sizeof g_last_params);
                snprintf(g_set.friend_name, sizeof g_set.friend_name, "%s", f->name);
                g_set.rated = false;
                g_set.last_tc = i == 0 ? 2 : (i == 1 ? 5 : (i == 2 ? 6 : 8));      // 3+0, 5+3, 10+0, 15+10 in the play scene's table
                scene_play_set_mode(2);
                scene_goto(SC_PLAY);
                scene_play_autostart();
                g_fr_sel = -1;
            }
        if (ui_button((Rect){22, 156, 276, 30}, "Cancel", F_B13, BS_SECONDARY, -1)) g_fr_sel = -1;
    }
    ui_set_enabled(true);
    ui_toast_draw(160);
}

const Scene scene_friends = {"friends", friends_enter, NULL, friends_top, friends_bot, NULL};

// ================================================================== Lichess TV
typedef struct { const char *key, *label; } TvChan;
static const TvChan TVC[15] = {{"best", "Top"}, {"bullet", "Bullet"}, {"blitz", "Blitz"}, {"rapid", "Rapid"}, {"classical", "Classical"},
    {"ultraBullet", "Ultra"}, {"chess960", "Chess960"}, {"crazyhouse", "Crazyhouse"}, {"atomic", "Atomic"}, {"horde", "Horde"},
    {"kingOfTheHill", "KotH"}, {"threeCheck", "3-check"}, {"racingKings", "Racing K."}, {"antichess", "Antichess"}, {"computer", "Computer"}};
static int g_tv_chan;
static BoardView tvb;
static struct { bool have; char id[12]; char name[2][28], title[2][8]; int rating[2]; Pos pos; Variant var; int clk[2]; u64 stamp; int plies; } TV;
static u64 g_tv_retry;
static bool g_tv_want;

static Variant tv_variant(void) { Variant v = VAR_STANDARD; variant_from_key(TVC[g_tv_chan].key, &v); return v; }

static void tv_open(void) {
    if (rq_tv) { li_cancel(rq_tv); rq_tv = 0; }
    memset(&TV, 0, sizeof TV);
    board_anim_clear(&tvb);
    char p[96]; snprintf(p, sizeof p, "/api/tv/%s/feed", TVC[g_tv_chan].key);
    rq_tv = li_request(TAG_TV, "GET", p, NULL, true);
    g_tv_want = true;
}

static void tv_line(const char *line) {
    json_error_t e; json_t *o = json_loads(line, 0, &e);
    if (!o) return;
    const char *t = jstr(o, "t"); json_t *d = json_object_get(o, "d");
    if (!strcmp(t, "featured")) {
        memset(&TV, 0, sizeof TV);
        snprintf(TV.id, sizeof TV.id, "%s", jstr(d, "id"));
        TV.var = tv_variant();
        json_t *pl = json_object_get(d, "players"), *v; size_t i;
        json_array_foreach(pl, i, v) {
            int c = !strcmp(jstr(v, "color"), "white") ? WHITE : BLACK;
            json_t *u = json_object_get(v, "user");
            snprintf(TV.name[c], sizeof TV.name[0], "%s", jstr(u, "name")); snprintf(TV.title[c], sizeof TV.title[0], "%s", jstr(u, "title"));
            TV.rating[c] = jint(v, "rating"); TV.clk[c] = jint(v, "seconds");
        }
        board_set_flip(&tvb, !strcmp(jstr(d, "orientation"), "black"));
        board_anim_clear(&tvb);
        if (pos_from_fen(&TV.pos, jstr(d, "fen"), TV.var)) { TV.have = true; TV.stamp = now_ms(); }
        tvb.last_from = tvb.last_to = -1;
    } else if (!strcmp(t, "fen") && TV.have) {
        Pos np;
        if (pos_from_fen(&np, jstr(d, "fen"), TV.var)) {
            Pos pre = TV.pos; Move m;
            const char *lm = jstr(d, "lm");
            if (lm[0] && pos_parse_uci(&pre, lm, &m)) {
                tvb.last_from = (m.flags & MF_DROP) ? m.to : m.from; tvb.last_to = m.to;
                audio_play_for_move(&pre, m, &np, false);
                if (g_set.animations && !(m.flags & MF_DROP) && !(m.flags & MF_CASTLE)) board_anim_move(&tvb, m.from, m.to, m.promo ? MKPC(pre.turn, PAWN) : pre.b[m.from], (m.flags & MF_EP) ? 0 : pre.b[m.to], 0.2f);
            }
            TV.pos = np; TV.plies++;
            TV.clk[WHITE] = jint(d, "wc"); TV.clk[BLACK] = jint(d, "bc"); TV.stamp = now_ms();
            tvb.check_sq = pos_in_check(&np, np.turn) ? pos_king_sq(&np, np.turn) : -1;
        }
    }
    json_decref(o);
}

static void tv_enter(void) { board_reset(&tvb); tvb.ox = 6; tvb.oy = 0; g_tv_retry = 0; tv_open(); }
static void tv_leave(void) { g_tv_want = false; if (rq_tv) { li_cancel(rq_tv); rq_tv = 0; } }
static void tv_update(float dt) {
    board_update(&tvb, dt); tvb.ox = 6; tvb.oy = 0;
    if (g_tv_want && !rq_tv && now_ms() >= g_tv_retry && g_api.net == 2) tv_open();
}

static void tv_card(float y, int c) {
    g_rrect(252, y, 142, 56, 8, C_PANEL);
    g_disc(270, y + 28, 24, c == WHITE ? RGB(240, 240, 240) : RGB(24, 24, 24));
    g_disc(270, y + 28, 18, c == WHITE ? RGB(255, 255, 255) : RGB(62, 60, 58));
    float nx = 290;
    if (TV.title[c][0]) { g_rrect(nx, y + 8, 26, 14, 4, C_ORANGE); g_text(F_B11, nx + 13, y + 15, RGB(60, 40, 0), AL_CENTER, TV.title[c]); }
    g_text_fit(F_B13, TV.title[c][0] ? nx + 30 : nx, y + 15, C_TEXT, AL_LEFT, TV.title[c][0] ? 70 : 100, TV.name[c]);
    g_textf(F_R11, nx, y + 31, C_TEXT3, AL_LEFT, "%d", TV.rating[c]);
    int ms = TV.clk[c] * 1000;
    if (TV.have && TV.plies >= 0 && TV.pos.turn == c) { ms -= (int)(now_ms() - TV.stamp); if (ms < 0) ms = 0; }
    char t[16]; fmt_clock(ms, t, sizeof t, true);
    g_text(F_B16, 386, y + 44, TV.have && TV.pos.turn == c ? C_TEXT : C_TEXT2, AL_RIGHT, t);
}

static void tv_top(void) {
    g_rect(0, 0, 400, 240, C_BG2);
    gfx_set_piece_set(g_set.piece_set);
    if (!TV.have) {
        g_icon(ICON_EYE, 200, 100, 32, C_GREEN);
        g_text(F_B16, 200, 140, C_TEXT, AL_CENTER, g_api.net == 2 ? "Waiting for the featured game..." : "Not connected");
        return;
    }
    board_draw(&tvb, &TV.pos);
    int top_c = tvb.flipped ? WHITE : BLACK;
    tv_card(10, top_c); tv_card(174, top_c ^ 1);
    g_text(F_B11, 323, 100, C_GREEN_L, AL_CENTER, TVC[g_tv_chan].label);
    g_text(F_R11, 323, 118, C_TEXT3, AL_CENTER, "live from lichess.org/tv");
    g_textf(F_R11, 323, 138, C_TEXT3, AL_CENTER, "%s", variant_name(TV.var));
}

static void tv_bot(void) {
    g_rect(0, 0, 320, 240, C_BG);
    ui_header("Lichess TV", ICON_EYE);
    if (ui_icon_button((Rect){284, 3, 32, 28}, ICON_ARROW_LEFT, BS_GHOST)) scene_goto(SC_MORE);
    for (int i = 0; i < 15; i++) {
        Rect r = {8 + (i % 3) * 103.0f, 42 + (i / 3) * 31.0f, 99, 27};
        if (ui_button(r, NULL, F_B11, i == g_tv_chan ? BS_TILE_SEL : BS_TILE, -1)) { if (g_tv_chan != i) { g_tv_chan = i; tv_open(); } }
        g_text_fit(F_B11, r.x + r.w / 2, r.y + 12, C_TEXT, AL_CENTER, r.w - 6, TVC[i].label);
    }
    if (ui_button((Rect){8, 202, 100, 30}, "Flip", F_B13, BS_SECONDARY, ICON_ARROW_DOWN_UP)) board_set_flip(&tvb, !tvb.flipped);
    if (ui_button((Rect){112, 202, 200, 30}, "Analyse this position", F_B13, BS_SECONDARY, ICON_TARGET) && TV.have) {
        char fen[120]; pos_to_fen(&TV.pos, fen, sizeof fen);
        ag_reset(TV.var, fen);
        scene_analysis_open(0, SC_TV);
    }
    ui_toast_draw(160);
}
const Scene scene_tv = {"tv", tv_enter, tv_update, tv_top, tv_bot, tv_leave};

// ================================================================== tournaments (arenas)
typedef struct { char id[12], name[48]; int limit, inc, minutes, nb; bool started, rated; int secs; char var[16]; } TListItem;
static TListItem g_tl[24]; static int g_tl_n, g_tl_state;
static Scroll g_tl_scroll;
typedef struct { char id[12], name[48]; int nb, secs_finish, secs_start; bool started, finished, joined, has_me, eligible; int me_rank, me_score; int limit, inc; char verdict[80];
    int ns; struct { char name[24]; int rank, score, rating; } st[10]; } TDetail;
static TDetail g_td; static bool g_td_open; static u64 g_td_poll; static bool g_t_busy;

static void tl_load(void) { g_tl_state = 1; g_tl_n = 0; rq_tlist = li_request(TAG_SOC, "GET", "/api/tournament", NULL, false); }

static void td_load(const char *id) {
    char p[64]; snprintf(p, sizeof p, "/api/tournament/%s", id);
    rq_tinfo = li_request(TAG_SOC, "GET", p, NULL, false);
    g_td_poll = now_ms() + 7000;
}

static int tl_cmp(const void *a, const void *b) {            // standard chess first, then running before upcoming, then the busiest
    const TListItem *x = a, *y = b;
    bool sx = !strcmp(x->var, "Std"), sy = !strcmp(y->var, "Std");
    if (sx != sy) return sx ? -1 : 1;
    if (x->started != y->started) return x->started ? -1 : 1;
    return y->nb - x->nb;
}

static void tl_parse(const char *body) {
    json_error_t e; json_t *o = json_loads(body, 0, &e);
    if (!o) { g_tl_state = 3; return; }
    static const char *KEYS[2] = {"started", "created"};
    for (int k = 0; k < 2; k++) {
        json_t *a = json_object_get(o, KEYS[k]), *v; size_t i;
        json_array_foreach(a, i, v) {
            if (g_tl_n >= 24) break;
            if (strcmp(jstr(v, "system"), "arena")) continue;
            Variant var; if (!variant_from_key(jstr(json_object_get(v, "variant"), "key"), &var)) continue;
            TListItem *t = &g_tl[g_tl_n++]; memset(t, 0, sizeof *t);
            snprintf(t->id, sizeof t->id, "%s", jstr(v, "id")); snprintf(t->name, sizeof t->name, "%s", jstr(v, "fullName"));
            snprintf(t->var, sizeof t->var, "%.15s", jstr(json_object_get(v, "variant"), "short"));
            t->limit = jint(json_object_get(v, "clock"), "limit"); t->inc = jint(json_object_get(v, "clock"), "increment");
            t->minutes = jint(v, "minutes"); t->nb = jint(v, "nbPlayers"); t->started = k == 0; t->rated = json_is_true(json_object_get(v, "rated"));
            t->secs = k == 0 ? jint(v, "secondsToFinish") : jint(v, "secondsToStart");
            if (!t->secs) t->secs = (int)((json_integer_value(json_object_get(v, k == 0 ? "finishesAt" : "startsAt")) - (long long)time(NULL) * 1000LL) / 1000);
        }
    }
    json_decref(o);
    qsort(g_tl, (size_t)g_tl_n, sizeof g_tl[0], tl_cmp);
    g_tl_state = 2;
}

static void td_parse(const char *body) {
    json_error_t e; json_t *o = json_loads(body, 0, &e);
    if (!o) return;
    bool joined = g_td.joined; char keep[12]; snprintf(keep, sizeof keep, "%s", g_td.id);
    memset(&g_td, 0, sizeof g_td); g_td.joined = joined;
    snprintf(g_td.id, sizeof g_td.id, "%.11s", jstr(o, "id")[0] ? jstr(o, "id") : keep);
    snprintf(g_td.name, sizeof g_td.name, "%s", jstr(o, "fullName"));
    g_td.nb = jint(o, "nbPlayers"); g_td.secs_finish = jint(o, "secondsToFinish"); g_td.secs_start = jint(o, "secondsToStart");
    g_td.started = json_is_true(json_object_get(o, "isStarted")); g_td.finished = json_is_true(json_object_get(o, "isFinished"));
    g_td.limit = jint(json_object_get(o, "clock"), "limit"); g_td.inc = jint(json_object_get(o, "clock"), "increment");
    json_t *me = json_object_get(o, "me");
    if (json_is_object(me)) { g_td.has_me = true; g_td.me_rank = jint(me, "rank"); g_td.me_score = jint(me, "score"); g_td.joined = !json_is_true(json_object_get(me, "withdraw")); }
    else if (g_td.joined && !json_is_object(me)) { /* joined a moment ago, the server has not listed us yet */ }
    g_td.eligible = true;
    json_t *vd = json_object_get(o, "verdicts");
    if (json_is_object(vd)) {
        g_td.eligible = json_is_true(json_object_get(vd, "accepted"));
        json_t *list = json_object_get(vd, "list"), *v; size_t i;
        json_array_foreach(list, i, v) if (strcmp(jstr(v, "verdict"), "ok")) { snprintf(g_td.verdict, sizeof g_td.verdict, "%.79s", jstr(v, "verdict")); break; }
    }
    json_t *pl = json_object_get(json_object_get(o, "standing"), "players"), *p; size_t i;
    json_array_foreach(pl, i, p) { if (g_td.ns >= 10) break; int n = g_td.ns++; snprintf(g_td.st[n].name, sizeof g_td.st[n].name, "%s", jstr(p, "name")); g_td.st[n].rank = jint(p, "rank"); g_td.st[n].score = jint(p, "score"); g_td.st[n].rating = jint(p, "rating"); }
    json_decref(o);
    if (g_td.finished) g_api.tourney_active = false;
}

static void tourney_enter(void) {
    memset(&g_tl_scroll, 0, sizeof g_tl_scroll);
    if (g_api.tourney_active && g_td.id[0]) { g_td_open = true; td_load(g_td.id); }
    else { g_td_open = false; tl_load(); }
}

static void tourney_update(float dt) {
    (void)dt;
    if (g_td_open && g_td.id[0] && now_ms() >= g_td_poll && !rq_tinfo) td_load(g_td.id);
}

static void fmt_secs(int s, char *o, size_t n) {
    if (s < 0) s = 0;
    if (s >= 3600) snprintf(o, n, "%dh %02dm", s / 3600, s / 60 % 60); else snprintf(o, n, "%d:%02d", s / 60, s % 60);
}

static void tourney_top(void) {
    ui_top_bg();
    g_rrect(6, 6, 388, 40, 8, C_PANEL);
    g_icon(ICON_TROPHY, 26, 26, 24, C_ORANGE);
    if (!g_td_open) {
        g_text(F_B16, 46, 26, C_TEXT, AL_LEFT, "Arena tournaments");
        g_text(F_R13, 200, 120, C_TEXT2, AL_CENTER, "Join an arena: you are paired automatically");
        g_text(F_R13, 200, 142, C_TEXT2, AL_CENTER, "and each game opens by itself.");
        g_text(F_R11, 200, 196, C_TEXT3, AL_CENTER, "Needs the tournament:write token permission.");
        return;
    }
    g_text_fit(F_B16, 46, 20, C_TEXT, AL_LEFT, 250, g_td.name);
    char t[24];
    if (g_td.finished) snprintf(t, sizeof t, "finished");
    else if (g_td.started) { fmt_secs(g_td.secs_finish, t, sizeof t); }
    else { fmt_secs(g_td.secs_start, t, sizeof t); }
    g_textf(F_R11, 46, 36, C_TEXT3, AL_LEFT, "%d+%d  -  %d players", g_td.limit / 60, g_td.inc, g_td.nb);
    g_text(F_B13, 386, 26, g_td.started ? C_GREEN_L : C_ORANGE, AL_RIGHT, g_td.finished ? t : (g_td.started ? "ends in" : "starts in"));
    if (!g_td.finished) g_text(F_B16, 386, 42, C_TEXT, AL_RIGHT, t);
    if (g_td.has_me) {
        g_rrect(6, 52, 388, 28, 8, C_GREEN_D);
        g_textf(F_B13, 16, 66, C_TEXT, AL_LEFT, "You: rank %d   -   %d points", g_td.me_rank, g_td.me_score);
        if (g_api.tourney_active) g_text(F_R11, 384, 66, C_TEXT, AL_RIGHT, "waiting for pairing");
    }
    g_rrect(6, 86, 388, 148, 8, C_BG);
    for (int i = 0; i < g_td.ns; i++) {
        float y = 100 + i * 14.0f;
        if (i & 1) g_rect(10, y - 7, 380, 14, RGBA(255, 255, 255, 8));
        g_textf(F_R11, 16, y, C_TEXT3, AL_LEFT, "%d", g_td.st[i].rank);
        g_text_fit(F_B11, 44, y, C_TEXT, AL_LEFT, 220, g_td.st[i].name);
        g_textf(F_R11, 300, y, C_TEXT3, AL_RIGHT, "%d", g_td.st[i].rating);
        g_textf(F_B11, 380, y, C_GREEN_L, AL_RIGHT, "%d", g_td.st[i].score);
    }
}

static void tourney_bot(void) {
    g_rect(0, 0, 320, 240, C_BG);
    ui_header("Tournaments", ICON_TROPHY);
    if (!g_td_open) {
        if (ui_icon_button((Rect){250, 3, 32, 28}, ICON_REFRESH_CW, BS_GHOST)) tl_load();
        if (ui_icon_button((Rect){284, 3, 32, 28}, ICON_ARROW_LEFT, BS_GHOST)) scene_goto(SC_MORE);
        Rect view = {0, 40, 320, 200};
        const float rh = 50;
        ui_scroll_update(&g_tl_scroll, view, 8 + g_tl_n * rh);
        if (g_tl_state == 1) ui_spinner(160, 130, 16, C_GREEN, scene_age());
        else if (g_tl_state == 3) g_text(F_R13, 160, 130, C_TEXT3, AL_CENTER, "Could not load tournaments");
        else if (!g_tl_n) g_text(F_R13, 160, 130, C_TEXT3, AL_CENTER, "No arenas right now");
        for (int i = 0; i < g_tl_n; i++) {
            const TListItem *t = &g_tl[i];
            float y = view.y + 6 + i * rh - g_tl_scroll.off;
            if (y < 30 || y > 240) continue;
            Rect r = {10, y, 300, rh - 6};
            bool held = ui_held(r) && !ui_scroll_moved(&g_tl_scroll);
            g_rrect(r.x, r.y, r.w, r.h, 8, held ? C_PANEL3 : C_PANEL2);
            g_text_fit(F_B13, r.x + 10, r.y + 14, C_TEXT, AL_LEFT, 230, t->name);
            g_textf(F_R11, r.x + 10, r.y + 31, C_TEXT3, AL_LEFT, "%d+%d  -  %s  -  %d players", t->limit / 60, t->inc, t->var, t->nb);
            char s[24]; fmt_secs(t->secs, s, sizeof s);
            g_rrect(r.x + r.w - 72, r.y + 6, 64, 18, 9, t->started ? C_GREEN_D : C_PANEL3);
            g_text(F_B11, r.x + r.w - 40, r.y + 15, C_TEXT, AL_CENTER, t->started ? "live" : s);
            if (t->started) g_text(F_R11, r.x + r.w - 10, r.y + 34, C_TEXT3, AL_RIGHT, s);
            if (ui_clicked(r) && !ui_scroll_moved(&g_tl_scroll)) {
                memset(&g_td, 0, sizeof g_td);
                snprintf(g_td.id, sizeof g_td.id, "%s", t->id); snprintf(g_td.name, sizeof g_td.name, "%s", t->name);
                g_td.limit = t->limit; g_td.inc = t->inc; g_td.nb = t->nb; g_td.started = t->started;
                g_td.eligible = true;
                g_td_open = true; td_load(t->id);
            }
        }
        g_rect(0, 0, 320, 40, C_BG);
        ui_header("Tournaments", ICON_TROPHY);
        if (ui_icon_button((Rect){250, 3, 32, 28}, ICON_REFRESH_CW, BS_GHOST)) tl_load();
        if (ui_icon_button((Rect){284, 3, 32, 28}, ICON_ARROW_LEFT, BS_GHOST)) scene_goto(SC_MORE);
    } else {
        if (ui_icon_button((Rect){284, 3, 32, 28}, ICON_ARROW_LEFT, BS_GHOST)) { g_td_open = false; if (!g_api.tourney_active) tl_load(); }
        g_text_fit(F_B16, 160, 80, C_TEXT, AL_CENTER, 290, g_td.name);
        if (g_api.tourney_active || g_td.joined) {
            ui_spinner(160, 120, 14, C_GREEN, scene_age());
            g_text(F_R13, 160, 150, C_TEXT2, AL_CENTER, "You are in. Waiting for your next game...");
            if (ui_button((Rect){70, 176, 180, 36}, "Withdraw", F_B16, BS_DANGER, ICON_X) && !g_t_busy) {
                char p[64]; snprintf(p, sizeof p, "/api/tournament/%s/withdraw", g_td.id);
                rq_tact = li_request(TAG_SOC, "POST", p, NULL, false); g_t_busy = true;
            }
        } else if (g_td.finished) g_text(F_R13, 160, 130, C_TEXT3, AL_CENTER, "This tournament is over.");
        else {
            if (!g_td.eligible) g_text_fit(F_R11, 160, 124, C_ORANGE, AL_CENTER, 300, g_td.verdict[0] ? g_td.verdict : "You cannot join this tournament");
            else g_text(F_R13, 160, 124, C_TEXT2, AL_CENTER, g_td.started ? "Running - you can still join." : "Not started yet - join to be ready.");
            if (ui_button((Rect){70, 150, 180, 40}, "Join", F_B21, g_td.eligible ? BS_PRIMARY : BS_DARK, ICON_TROPHY) && !g_t_busy && g_td.eligible) {
                char p[64]; snprintf(p, sizeof p, "/api/tournament/%s/join", g_td.id);
                rq_tact = li_request(TAG_SOC, "POST", p, "", false); g_t_busy = true;
            }
        }
    }
    ui_toast_draw(160);
}

static void tourney_leave(void) { /* stay joined: the arena keeps pairing us */ }
const Scene scene_tourney = {"tourney", tourney_enter, tourney_update, tourney_top, tourney_bot, tourney_leave};

// ================================================================== network dispatch
void social_net(int tag, int req, int kind, int rc, long http, const char *data) {
    if (tag == TAG_TV) {
        if (req != rq_tv) return;
        if (kind == LIM_LINE) tv_line(data);
        else if (kind == LIM_END) { rq_tv = 0; g_tv_retry = now_ms() + 3000; }
        return;
    }
    if (req == rq_follow) {
        if (kind == LIM_LINE) {
            json_error_t e; json_t *o = json_loads(data, 0, &e);
            if (o && g_fr_n < 50) {
                Friend *f = &g_fr[g_fr_n++]; memset(f, 0, sizeof *f);
                snprintf(f->name, sizeof f->name, "%s", jstr(o, "username")); snprintf(f->id, sizeof f->id, "%s", jstr(o, "id"));
            }
            if (o) json_decref(o);
        } else if (kind == LIM_END) {
            rq_follow = 0;
            if (rc || http != 200) { g_fr_state = 3; return; }
            qsort(g_fr, (size_t)g_fr_n, sizeof g_fr[0], fr_cmp);
            friends_status();
        }
        return;
    }
    if (req == rq_status) {
        rq_status = 0;
        g_fr_state = 2;
        if (rc || http != 200) return;
        json_error_t e; json_t *a = json_loads(data, 0, &e), *v; size_t i;
        json_array_foreach(a, i, v) {
            for (int k = 0; k < g_fr_n; k++) if (!strcmp(g_fr[k].id, jstr(v, "id"))) { g_fr[k].online = json_is_true(json_object_get(v, "online")); g_fr[k].playing = json_is_true(json_object_get(v, "playing")); }
        }
        if (a) json_decref(a);
        qsort(g_fr, (size_t)g_fr_n, sizeof g_fr[0], fr_cmp);
        return;
    }
    if (req == rq_fact) {
        rq_fact = 0;
        if (rc || http != 200) social_toast_err(http, rc, "Follow"); else { ui_toast("Now following"); friends_load(); }
        return;
    }
    if (req == rq_rating) { rq_rating = 0; rating_net(rc, http, data); return; }
    if (req == rq_tlist) { rq_tlist = 0; if (rc || http != 200) g_tl_state = 3; else tl_parse(data); return; }
    if (req == rq_tinfo) { rq_tinfo = 0; if (!rc && http == 200) td_parse(data); return; }
    if (req == rq_tact) {
        rq_tact = 0; g_t_busy = false;
        if (rc || http >= 400) {
            char msg[100] = "";
            json_error_t e; json_t *o = json_loads(data, 0, &e);
            if (o) { snprintf(msg, sizeof msg, "%s", jstr(o, "error")); json_decref(o); }
            if (msg[0]) ui_toast(msg); else social_toast_err(http, rc, "Tournament");
            return;
        }
        if (g_td.joined || g_api.tourney_active) { g_api.tourney_active = false; g_td.joined = false; g_td.has_me = false; ui_toast("Left the tournament"); }
        else { g_api.tourney_active = true; g_td.joined = true; ui_toast("Joined! Waiting for pairing"); }
        td_load(g_td.id);
        return;
    }
}
