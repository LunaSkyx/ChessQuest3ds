#include "api.h"
#include "chess.h"
#include <jansson.h>

Api g_api;

static const char *jstr(json_t *o, const char *k) { json_t *v = json_object_get(o, k); return json_is_string(v) ? json_string_value(v) : NULL; }
static int jint(json_t *o, const char *k, int def) { json_t *v = json_object_get(o, k); return json_is_integer(v) ? (int)json_integer_value(v) : def; }

void api_notice(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(g_api.notice, sizeof g_api.notice, fmt, ap); va_end(ap);
    g_api.notice_new = true;
    LOGF("notice: %s", g_api.notice);
}

// Best effort: pull a human readable message out of a Lichess error body.
static void extract_error(const char *body, char *out, size_t n) {
    out[0] = 0;
    if (!body || !*body) return;
    json_error_t e; json_t *o = json_loads(body, 0, &e);
    if (o) {
        const char *s = NULL;
        if (json_is_object(o)) {
            s = jstr(o, "error");
            if (!s) {
                const char *key; json_t *val;
                json_object_foreach(o, key, val) {
                    if (json_is_string(val)) { s = json_string_value(val); break; }
                    if (json_is_array(val) && json_array_size(val) && json_is_string(json_array_get(val, 0))) { s = json_string_value(json_array_get(val, 0)); break; }
                    if (json_is_object(val)) {
                        const char *k2; json_t *v2;
                        json_object_foreach(val, k2, v2) if (!s) {
                            if (json_is_string(v2)) s = json_string_value(v2);
                            else if (json_is_array(v2) && json_array_size(v2) && json_is_string(json_array_get(v2, 0))) s = json_string_value(json_array_get(v2, 0));
                        }
                        if (s) break;
                    }
                }
            }
        }
        if (s) snprintf(out, n, "%s", s);
        json_decref(o);
    }
    if (!out[0]) snprintf(out, n, "%.100s", body);
}

void api_init(void) {
    memset(&g_api, 0, sizeof g_api);
    li_set_token(g_set.token);
    li_set_verify(g_set.verify_tls);
}

void api_refresh_account(void) {
    g_api.req_account = li_request(TAG_ACCOUNT, "GET", "/api/account", NULL, false);
}

void api_connect(void) {
    g_api.net = 1; g_api.err[0] = 0; g_api.token_rejected = false;
    li_set_token(g_set.token);
    li_set_verify(g_set.verify_tls);
    api_refresh_account();
}

static bool ongoing_has(const char *id) {
    for (int i = 0; i < g_api.n_ongoing; i++) if (!strcmp(g_api.ongoing[i].id, id)) return true;
    return false;
}

static void follow_start(const char *id) {
    snprintf(g_api.start_id, sizeof g_api.start_id, "%s", id);
    g_api.start_pending = true; g_api.seeking = false; g_api.chal_out_wait = false; g_api.expect_until = 0;
    g_api.expect_mode = 0; g_api.expect_id[0] = g_api.early_id[0] = 0;
    LOGF("event: gameStart %s (expected)", id);
}

static void expect_game(const char *id) {            // the id of the game we asked for became known
    snprintf(g_api.expect_id, sizeof g_api.expect_id, "%s", id);
    if (g_api.early_id[0] && !strcmp(g_api.early_id, id)) follow_start(id);
}

static void open_events(void) {
    g_api.ev_open_at = now_ms();
    g_api.req_events = li_request(TAG_EVENTS, "GET", "/api/stream/event", NULL, true);
    g_api.events_on = true;
}

void api_disconnect(void) {
    if (g_api.req_events) li_cancel(g_api.req_events);
    api_seek_cancel(); api_game_close();
    g_api.events_on = false; g_api.have_account = false; g_api.net = 0;
}

static void parse_account(const char *body) {
    json_error_t e; json_t *o = json_loads(body, 0, &e);
    if (!o) return;
    const char *u = jstr(o, "username"), *id = jstr(o, "id"), *t = jstr(o, "title");
    if (u) snprintf(g_api.user, sizeof g_api.user, "%s", u);
    if (id) snprintf(g_api.id, sizeof g_api.id, "%s", id);
    snprintf(g_api.title, sizeof g_api.title, "%s", t ? t : "");
    static const char *perfs[4] = {"bullet", "blitz", "rapid", "classical"};
    json_t *pf = json_object_get(o, "perfs");
    for (int i = 0; i < 4; i++) {
        json_t *p = json_object_get(pf, perfs[i]);
        g_api.rating[i] = jint(p, "rating", 0); g_api.games[i] = jint(p, "games", 0);
    }
    { json_t *pp = json_object_get(pf, "puzzle"); g_api.puzzle_rating = jint(pp, "rating", 0); }
    g_api.have_account = true;
    json_decref(o);
}

static void on_event(json_t *o) {
    const char *type = jstr(o, "type");
    if (!type) return;
    if (!strcmp(type, "gameStart")) {
        json_t *g = json_object_get(o, "game");
        const char *id = jstr(g, "gameId"); if (!id) id = jstr(g, "id");
        if (id) {
            bool asked = now_ms() < g_api.expect_until || g_api.seeking || g_api.chal_out_wait;
            // Never take over a game we did not ask for: the event stream replays the games we are already playing
            // (e.g. one started on the website), and those must stay untouched.
            if (asked && g_api.expect_mode == 1 && g_api.expect_id[0]) {
                if (!strcmp(id, g_api.expect_id)) follow_start(id);
                else { LOGF("event: gameStart %s (not the game we asked for)", id); api_refresh_ongoing(); }
            } else if (asked && g_api.expect_mode == 1) {
                snprintf(g_api.early_id, sizeof g_api.early_id, "%s", id);        // decided when the POST answers with the id
                LOGF("event: gameStart %s (early, waiting for our game id)", id);
            } else if (asked && g_api.expect_mode == 2 && !ongoing_has(id) && now_ms() - g_api.ev_open_at > 5000) {
                follow_start(id);
            } else if (g_api.tourney_active && !ongoing_has(id) && now_ms() - g_api.ev_open_at > 5000) {
                follow_start(id);                                          // an arena pairing
                g_api.tourney_active = true;
            } else {
                LOGF("event: gameStart %s (already running)", id);
                api_refresh_ongoing();
            }
        }
    } else if (!strcmp(type, "challenge")) {
        json_t *c = json_object_get(o, "challenge");
        const char *id = jstr(c, "id");
        json_t *ch = json_object_get(c, "challenger");
        const char *from = jstr(ch, "name"); if (!from) from = jstr(ch, "id");
        if (id && from && strcasecmp(from, g_api.user)) {          // ignore the echo of our own challenges
            snprintf(g_api.chal_id, sizeof g_api.chal_id, "%s", id);
            snprintf(g_api.chal_from, sizeof g_api.chal_from, "%s (%d)", from, jint(ch, "rating", 0));
            json_t *tc = json_object_get(c, "timeControl");
            const char *show = jstr(tc, "show");
            snprintf(g_api.chal_desc, sizeof g_api.chal_desc, "%s %s%s", show ? show : "?",
                     jstr(json_object_get(c, "variant"), "name") ? jstr(json_object_get(c, "variant"), "name") : "Standard",
                     json_is_true(json_object_get(c, "rated")) ? " rated" : " casual");
            g_api.chal_in_pending = true;
        }
    } else if (!strcmp(type, "challengeCanceled")) {
        const char *id = jstr(json_object_get(o, "challenge"), "id");
        if (id && !strcmp(id, g_api.chal_id)) g_api.chal_in_pending = false;
    } else if (!strcmp(type, "challengeDeclined")) {
        json_t *c = json_object_get(o, "challenge");
        const char *id = jstr(c, "id");
        if (id && g_api.chal_out_wait && !strcmp(id, g_api.chal_out_id)) {
            g_api.chal_out_wait = false;
            const char *why = jstr(c, "declineReason");
            api_notice("%s declined%s%s", g_api.chal_out_name, why ? ": " : "", why ? why : "");
        }
    }
}

static void parse_ongoing(const char *body) {
    json_error_t e; json_t *o = json_loads(body, 0, &e);
    if (!o) return;
    json_t *arr = json_object_get(o, "nowPlaying");
    g_api.n_ongoing = 0;
    size_t i; json_t *g;
    json_array_foreach(arr, i, g) {
        if (g_api.n_ongoing >= 16) break;
        OngoingGame *og = &g_api.ongoing[g_api.n_ongoing++];
        memset(og, 0, sizeof *og);
        const char *id = jstr(g, "gameId"); snprintf(og->id, sizeof og->id, "%s", id ? id : "");
        json_t *opp = json_object_get(g, "opponent");
        const char *nm = jstr(opp, "username"); snprintf(og->opp, sizeof og->opp, "%s", nm ? nm : (jint(opp, "ai", 0) ? "Stockfish" : "Anonymous"));
        if (jstr(opp, "id") == NULL && json_object_get(opp, "ai")) snprintf(og->opp, sizeof og->opp, "Stockfish %d", jint(opp, "ai", 0));
        og->opp_rating = jint(opp, "rating", 0);
        const char *col = jstr(g, "color"); og->color = (col && !strcmp(col, "black")) ? BLACK : WHITE;
        og->my_turn = json_is_true(json_object_get(g, "isMyTurn"));
        const char *sp = jstr(g, "speed"); snprintf(og->speed, sizeof og->speed, "%s", sp ? sp : "");
        const char *vn = jstr(json_object_get(g, "variant"), "name"); snprintf(og->variant, sizeof og->variant, "%s", vn ? vn : "Standard");
        og->rated = json_is_true(json_object_get(g, "rated"));
        og->secs_left = jint(g, "secondsLeft", -1);
    }
    g_api.ongoing_loaded = true;
    json_decref(o);
}

void api_refresh_ongoing(void) { li_request(TAG_ONGOING, "GET", "/api/account/playing", NULL, false); }

static void handle(LiMsg *m) {
    switch (m->tag) {
    case TAG_ACCOUNT:
        if (m->curl_rc) {
            g_api.net = -1; g_api.err_rc = m->curl_rc;
            snprintf(g_api.err, sizeof g_api.err, "Network error: %s", m->err);
        } else if (m->http == 200) {
            parse_account(m->data);
            if (g_api.net != 2) { g_api.net = 2; g_api.err[0] = 0; g_api.err_rc = 0; }
            if (!g_api.events_on) open_events();
            LOGF("account: %s (blitz %d)", g_api.user, g_api.rating[1]);
        } else if (m->http == 401) {
            g_api.net = -1; g_api.token_rejected = true;
            snprintf(g_api.err, sizeof g_api.err, "Token rejected by Lichess");
        } else {
            g_api.net = -1;
            snprintf(g_api.err, sizeof g_api.err, "Lichess error %ld", m->http);
        }
        break;
    case TAG_EVENTS:
        if (m->kind == LIM_LINE) {
            json_error_t e; json_t *o = json_loads(m->data, 0, &e);
            if (o) { on_event(o); json_decref(o); }
        } else if (m->kind == LIM_END) {
            LOGF("events stream ended rc=%d http=%ld", m->curl_rc, m->http);
            g_api.events_on = false;
            g_api.events_retry_at = now_ms() + (m->http == 429 ? 20000 : 2500);
        }
        break;
    case TAG_HIST:
        if (m->kind == LIM_LINE) hist_net_line(m->data); else if (m->kind == LIM_END) hist_net_end(m->curl_rc, m->http);
        break;
    case TAG_CLOUD:
        analysis_net_cloud(m->req, m->curl_rc, m->http, m->data ? m->data : "");
        break;
    case TAG_EXPL:
        analysis_net_explorer(m->req, m->curl_rc, m->http, m->data ? m->data : "");
        break;
    case TAG_PUZ:
        puzzle_net(m->req, m->curl_rc, m->http, m->data ? m->data : "");
        break;
    case TAG_SOC:
    case TAG_TV:
        social_net(m->tag, m->req, m->kind, m->curl_rc, m->http, m->data ? m->data : "");
        break;
    case TAG_ONGOING:
        if (m->kind == LIM_RESPONSE && m->http == 200) parse_ongoing(m->data);
        break;
    case TAG_SEEK:
        if (m->kind == LIM_LINE) {
            char msg[100]; extract_error(m->data, msg, sizeof msg);
            if (g_api.seeking && strcmp(msg, "")) api_notice("Seek: %s", msg);
        } else if (m->kind == LIM_END) {
            if (m->curl_rc && m->curl_rc != -1) api_notice("Seek failed: %s", m->err);
            else if (m->http >= 400) api_notice("Seek rejected (HTTP %ld)", m->http);
            if (m->req == g_api.req_seek) { g_api.seeking = false; g_api.req_seek = 0; }
        }
        break;
    case TAG_AI:
        if (m->http >= 400 || m->curl_rc) { char msg[100]; if (m->curl_rc) snprintf(msg, sizeof msg, "%s", m->err); else extract_error(m->data, msg, sizeof msg); api_notice("Could not start game: %s", msg); g_api.chal_out_wait = false; g_api.expect_mode = 0; }
        else {
            json_error_t e; json_t *o = json_loads(m->data, 0, &e);
            const char *id = o ? jstr(o, "id") : NULL;
            if (id) expect_game(id);
            if (o) json_decref(o);
        }
        break;
    case TAG_CHAL:
        if (m->curl_rc) { api_notice("Challenge failed: %s", m->err); g_api.chal_out_wait = false; }
        else if (m->http >= 400) { char msg[100]; extract_error(m->data, msg, sizeof msg); api_notice("Challenge failed: %s", msg); g_api.chal_out_wait = false; }
        else {
            json_error_t e; json_t *o = json_loads(m->data, 0, &e);
            if (o) {
                json_t *c = json_object_get(o, "challenge"); if (!c) c = o;
                const char *id = jstr(c, "id"); if (id) { snprintf(g_api.chal_out_id, sizeof g_api.chal_out_id, "%s", id); expect_game(id); }
                json_decref(o);
            }
        }
        break;
    case TAG_GAME:
        if (m->req != g_api.req_game) break;                       // stale message of a stream we already replaced / closed
        if (m->kind == LIM_LINE) { if (g_api.on_game_line) g_api.on_game_line(m->data); }
        else if (m->kind == LIM_END) { g_api.req_game = 0; if (m->curl_rc != -1 && g_api.on_game_end) g_api.on_game_end(m->curl_rc, m->http); }
        break;
    case TAG_MOVE:
        if (g_api.on_move_result) {
            if (m->curl_rc) g_api.on_move_result(false, m->err);
            else if (m->http >= 400) { char msg[100]; extract_error(m->data, msg, sizeof msg); g_api.on_move_result(false, msg); }
            else g_api.on_move_result(true, "");
        }
        break;
    default: break;
    }
}

void api_pump(void) {
    LiMsg *m;
    int guard = 0;
    while ((m = li_poll()) && guard++ < 64) { handle(m); li_msg_free(m); }
    if (g_api.have_account && !g_api.events_on && now_ms() > g_api.events_retry_at) open_events();
}

// ------------------------------------------------------------------ actions
static const char *color_str(int c) { return c == 1 ? "white" : (c == 2 ? "black" : "random"); }

static void time_str(int sec, char *out, size_t n) {
    if (sec % 60 == 0) snprintf(out, n, "%d", sec / 60); else snprintf(out, n, "%.2f", sec / 60.0);
}

void api_seek(TimeCtl tc, bool rated, Variant v, int color) {
    char t[16], body[240]; time_str(tc.limit_sec, t, sizeof t);
    if (tc.days > 0) snprintf(body, sizeof body, "days=%d&rated=%s&variant=%s&color=%s", tc.days, rated ? "true" : "false", variant_key(v), color_str(color));
    else snprintf(body, sizeof body, "time=%s&increment=%d&rated=%s&variant=%s&color=%s", t, tc.inc_sec, rated ? "true" : "false", variant_key(v), color_str(color));
    if (g_api.seek_range[0]) { size_t n = strlen(body); snprintf(body + n, sizeof body - n, "&ratingRange=%s", g_api.seek_range); }
    if (g_api.req_seek) li_cancel(g_api.req_seek);
    g_api.req_seek = li_request(TAG_SEEK, "POST", "/api/board/seek", body, true);
    g_api.seeking = true;
    g_api.expect_until = now_ms() + 900000;
    g_api.expect_mode = 2; g_api.expect_id[0] = g_api.early_id[0] = 0;
}

void api_seek_cancel(void) {
    if (g_api.req_seek) li_cancel(g_api.req_seek);
    g_api.req_seek = 0; g_api.seeking = false; g_api.expect_until = 0; g_api.expect_mode = 0;
}

void api_challenge_ai_fen(int level, TimeCtl tc, Variant v, int color, const char *fen) {
    char enc[200], body[420];
    li_url_encode(fen, enc, sizeof enc);
    snprintf(body, sizeof body, "level=%d&clock.limit=%d&clock.increment=%d&variant=%s&color=%s&fen=%s", level, tc.limit_sec, tc.inc_sec,
             v == VAR_STANDARD ? "fromPosition" : variant_key(v), color_str(color), enc);
    g_api.expect_until = now_ms() + 20000;
    g_api.expect_mode = 1; g_api.expect_id[0] = g_api.early_id[0] = 0;
    li_request(TAG_AI, "POST", "/api/challenge/ai", body, false);
}

void api_challenge_ai(int level, TimeCtl tc, Variant v, int color) {
    char body[200];
    snprintf(body, sizeof body, "level=%d&clock.limit=%d&clock.increment=%d&variant=%s&color=%s", level, tc.limit_sec, tc.inc_sec, variant_key(v), color_str(color));
    g_api.expect_until = now_ms() + 20000;
    g_api.expect_mode = 1; g_api.expect_id[0] = g_api.early_id[0] = 0;
    li_request(TAG_AI, "POST", "/api/challenge/ai", body, false);
}

void api_challenge_user(const char *name, TimeCtl tc, bool rated, Variant v, int color) {
    char enc[100], path[160], body[200];
    li_url_encode(name, enc, sizeof enc);
    snprintf(path, sizeof path, "/api/challenge/%s", enc);
    if (tc.days > 0) snprintf(body, sizeof body, "rated=%s&days=%d&variant=%s&color=%s", rated ? "true" : "false", tc.days, variant_key(v), color_str(color));
    else snprintf(body, sizeof body, "rated=%s&clock.limit=%d&clock.increment=%d&variant=%s&color=%s", rated ? "true" : "false", tc.limit_sec, tc.inc_sec, variant_key(v), color_str(color));
    snprintf(g_api.chal_out_name, sizeof g_api.chal_out_name, "%s", name);
    g_api.chal_out_id[0] = 0; g_api.chal_out_wait = true;
    g_api.expect_mode = 1; g_api.expect_id[0] = g_api.early_id[0] = 0;
    g_api.req_chal = li_request(TAG_CHAL, "POST", path, body, false);
}

void api_challenge_cancel(void) {
    if (g_api.chal_out_id[0]) { char p[80]; snprintf(p, sizeof p, "/api/challenge/%s/cancel", g_api.chal_out_id); li_request(TAG_CANCEL, "POST", p, NULL, false); }
    g_api.chal_out_wait = false; g_api.expect_until = 0;
}

void api_challenge_accept(const char *id) { g_api.expect_until = now_ms() + 20000; g_api.expect_mode = 1; g_api.early_id[0] = 0; snprintf(g_api.expect_id, sizeof g_api.expect_id, "%s", id); char p[80]; snprintf(p, sizeof p, "/api/challenge/%s/accept", id); li_request(TAG_ACTION, "POST", p, NULL, false); g_api.chal_in_pending = false; }
void api_challenge_decline(const char *id) { char p[80]; snprintf(p, sizeof p, "/api/challenge/%s/decline", id); li_request(TAG_ACTION, "POST", p, NULL, false); g_api.chal_in_pending = false; }

void api_game_open(const char *id) {
    char p[80]; snprintf(p, sizeof p, "/api/board/game/stream/%s", id);
    if (g_api.req_game) li_cancel(g_api.req_game);
    g_api.req_game = li_request(TAG_GAME, "GET", p, NULL, true);
}
void api_game_close(void) {
    if (g_api.req_game) { li_cancel(g_api.req_game); g_api.req_game = 0; }
}
void api_warm(void) { li_request(TAG_WARM, "GET", "/api/account", NULL, false); }

void api_move(const char *id, const char *uci) {
    char p[120]; snprintf(p, sizeof p, "/api/board/game/%s/move/%s", id, uci);
    li_request(TAG_MOVE, "POST", p, NULL, false);
}
void api_resign(const char *id) { char p[80]; snprintf(p, sizeof p, "/api/board/game/%s/resign", id); li_request(TAG_ACTION, "POST", p, NULL, false); }
void api_abort(const char *id) { char p[80]; snprintf(p, sizeof p, "/api/board/game/%s/abort", id); li_request(TAG_ACTION, "POST", p, NULL, false); }
void api_draw(const char *id, bool yes) { char p[96]; snprintf(p, sizeof p, "/api/board/game/%s/draw/%s", id, yes ? "yes" : "no"); li_request(TAG_ACTION, "POST", p, NULL, false); }
void api_takeback(const char *id, bool yes) { char p[96]; snprintf(p, sizeof p, "/api/board/game/%s/takeback/%s", id, yes ? "yes" : "no"); li_request(TAG_ACTION, "POST", p, NULL, false); }
void api_claim_victory(const char *id) { char p[96]; snprintf(p, sizeof p, "/api/board/game/%s/claim-victory", id); li_request(TAG_ACTION, "POST", p, NULL, false); }
