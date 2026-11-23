#include "game.h"

Game g_game;

void game_reset(Game *g, const char *id) {
    memset(g, 0, sizeof *g);
    snprintf(g->id, sizeof g->id, "%s", id ? id : "");
    g->active = true;
    g->my_color = -1; g->winner = -1; g->t_turn = -1;
    snprintf(g->status, sizeof g->status, "created");
}

static void parse_player(Player *p, json_t *o) {
    memset(p, 0, sizeof *p);
    if (!json_is_object(o)) { snprintf(p->name, sizeof p->name, "?"); return; }
    json_t *lvl = json_object_get(o, "aiLevel");
    if (json_is_integer(lvl)) {
        p->ai_level = (int)json_integer_value(lvl);
        snprintf(p->name, sizeof p->name, "Stockfish %d", p->ai_level);
        return;
    }
    const char *n = json_string_value(json_object_get(o, "name"));
    snprintf(p->name, sizeof p->name, "%s", n ? n : "Anonymous");
    const char *t = json_string_value(json_object_get(o, "title"));
    snprintf(p->title, sizeof p->title, "%s", t ? t : "");
    json_t *r = json_object_get(o, "rating");
    p->rating = json_is_integer(r) ? (int)json_integer_value(r) : 0;
    p->prov = json_is_true(json_object_get(o, "provisional"));
}

bool game_parse_full(Game *g, json_t *o, const char *my_id) {
    const char *id = json_string_value(json_object_get(o, "id"));
    if (id) snprintf(g->id, sizeof g->id, "%s", id);
    json_t *var = json_object_get(o, "variant");
    const char *vk = json_string_value(json_object_get(var, "key"));
    g->variant = VAR_STANDARD;
    if (vk) variant_from_key(vk, &g->variant);
    parse_player(&g->pl[WHITE], json_object_get(o, "white"));
    parse_player(&g->pl[BLACK], json_object_get(o, "black"));
    g->rated = json_is_true(json_object_get(o, "rated"));
    const char *sp = json_string_value(json_object_get(o, "speed"));
    snprintf(g->speed, sizeof g->speed, "%s", sp ? sp : "");
    json_t *clk = json_object_get(o, "clock");
    g->has_clock = json_is_object(clk);
    if (g->has_clock) {
        g->init_ms = (int)json_integer_value(json_object_get(clk, "initial"));
        g->inc_ms = (int)json_integer_value(json_object_get(clk, "increment"));
    }
    // my colour: compare lower-cased ids
    g->my_color = -1;
    if (my_id && *my_id) {
        for (int c = 0; c < 2; c++) {
            const char *pid = json_string_value(json_object_get(json_object_get(o, c == WHITE ? "white" : "black"), "id"));
            if (pid && !strcasecmp(pid, my_id)) g->my_color = c;
        }
    }
    const char *fen = json_string_value(json_object_get(o, "initialFen"));
    if (!fen || !strcmp(fen, "startpos")) pos_init(&g->start, g->variant);
    else if (!pos_from_fen(&g->start, fen, g->variant)) pos_init(&g->start, g->variant);
    g->pos = g->start;
    g->nply = 0; g->server_nply = 0;
    g->loaded = true;
    bool dv; game_parse_state(g, json_object_get(o, "state"), &dv);
    return true;
}

static int split_moves(const char *s, char out[][8], int max) {
    int n = 0;
    while (*s && n < max) {
        while (*s == ' ') s++;
        if (!*s) break;
        int k = 0;
        while (*s && *s != ' ' && k < 7) out[n][k++] = *s++;
        out[n][k] = 0;
        while (*s && *s != ' ') s++;
        n++;
    }
    return n;
}

static bool append_ply(Game *g, const char *uci) {
    Move m;
    if (g->nply >= MAX_PLIES || !pos_parse_uci(&g->pos, uci, &m)) return false;
    snprintf(g->uci[g->nply], 8, "%s", uci);
    move_to_san(&g->pos, m, g->san[g->nply], sizeof g->san[0]);
    g->mv[g->nply] = m;
    pos_do(&g->pos, m);
    g->nply++;
    return true;
}

static void rebuild_to(Game *g, int ply) {          // truncate local history to `ply` plies
    Pos p = g->start;
    for (int i = 0; i < ply; i++) pos_do(&p, g->mv[i]);
    g->pos = p; g->nply = ply;
}

void game_truncate(Game *g, int ply) { if (ply < 0) ply = 0; if (ply < g->nply) rebuild_to(g, ply); }

int game_parse_state(Game *g, json_t *st, bool *diverged) {
    if (diverged) *diverged = false;
    if (!json_is_object(st)) return 0;
    const char *mvs = json_string_value(json_object_get(st, "moves"));
    static char sv[MAX_PLIES][8];
    int m = split_moves(mvs ? mvs : "", sv, MAX_PLIES);
    int appended = 0;
    // 1. common prefix with the local line
    // (castling can be spelled e1g1 or e1h1 depending on the variant / start position: compare moves, not strings)
    int i = 0;
    while (i < g->nply && i < m) {
        if (strcmp(g->uci[i], sv[i])) {
            Pos before; game_pos_at(g, i, &before);
            Move sm;
            if (!pos_parse_uci(&before, sv[i], &sm) || !move_eq(sm, g->mv[i])) break;
            snprintf(g->uci[i], 8, "%s", sv[i]);
        }
        i++;
    }
    if (i < g->nply && i < m) {                       // the server contradicts us: roll back to the common prefix
        if (diverged) *diverged = true;
        rebuild_to(g, i);
        pm_clear(&g->pre);
    }
    // 2. append what we do not have yet (opponent moves, or moves made from another device)
    for (int k = g->nply; k < m; k++) {
        if (!append_ply(g, sv[k])) { LOGF("game: cannot apply server move %s at ply %d", sv[k], k); break; }
        appended++;
    }
    g->server_nply = m;
    // 3. clocks + status
    json_t *w = json_object_get(st, "wtime"), *b = json_object_get(st, "btime");
    if (json_is_integer(w)) g->t_ms[WHITE] = (int)json_integer_value(w);
    if (json_is_integer(b)) g->t_ms[BLACK] = (int)json_integer_value(b);
    g->t_stamp = now_ms();
    const char *s = json_string_value(json_object_get(st, "status"));
    if (s) snprintf(g->status, sizeof g->status, "%s", s);
    g->finished = strcmp(g->status, "created") && strcmp(g->status, "started");
    json_t *win = json_object_get(st, "winner");
    const char *ws = json_string_value(win);
    g->winner = ws ? (!strcmp(ws, "white") ? WHITE : BLACK) : -1;
    g->t_turn = (!g->finished && g->has_clock && g->nply >= 2) ? g->pos.turn : -1;
    g->draw_offer[WHITE] = json_is_true(json_object_get(st, "wdraw"));
    g->draw_offer[BLACK] = json_is_true(json_object_get(st, "bdraw"));
    g->takeback_offer[WHITE] = json_is_true(json_object_get(st, "wtakeback"));
    g->takeback_offer[BLACK] = json_is_true(json_object_get(st, "btakeback"));
    return appended;
}

static bool pos_same(const Pos *a, const Pos *b) { return !memcmp(a->b, b->b, 64) && a->turn == b->turn; }

bool game_fics_update(Game *g, const Style12 *s, int *appended, bool *diverged) {
    *appended = 0; *diverged = false;
    Pos target;
    if (!fics_s12_to_pos(s, &target)) return false;
    int turn = s->white_turn ? WHITE : BLACK;
    if (!g->loaded) {
        g->backend = 1; g->variant = VAR_STANDARD;
        snprintf(g->pl[WHITE].name, sizeof g->pl[WHITE].name, "%s", s->white);
        snprintf(g->pl[BLACK].name, sizeof g->pl[BLACK].name, "%s", s->black);
        g->my_color = s->relation == 1 ? turn : 1 - turn;
        g->has_clock = s->init_min > 0 || s->inc > 0;
        g->init_ms = s->init_min * 60000; g->inc_ms = s->inc * 1000;
        int est = s->init_min * 60 + 40 * s->inc;
        snprintf(g->speed, sizeof g->speed, "%s", est < 180 ? "bullet" : (est < 480 ? "blitz" : (est < 1500 ? "rapid" : "classical")));
        if (s->movenum <= 1 && !strcmp(s->verbose, "none")) pos_init(&g->start, VAR_STANDARD); else g->start = target;
        g->pos = g->start; g->nply = 0; g->loaded = true;
    } else if (!pos_same(&g->pos, &target)) {
        Move m; Pos t2 = g->pos; bool ok = fics_verbose_to_move(&g->pos, s->verbose, &m);
        if (ok) { pos_do(&t2, m); ok = pos_same(&t2, &target); }
        if (ok) {
            char u[8]; move_to_uci(&g->pos, m, u);
            append_ply(g, u); *appended = 1;
        } else {                                       // take-back or a desync: find the position in our own history first
            int found = -1;
            for (int i = g->nply - 1; i >= 0 && found < 0; i--) { Pos q; game_pos_at(g, i, &q); if (pos_same(&q, &target)) found = i; }
            if (found >= 0) rebuild_to(g, found); else { g->start = target; g->pos = target; g->nply = 0; }
            *diverged = true;
            pm_clear(&g->pre);
        }
    }
    if (*appended || *diverged) { g->draw_offer[WHITE] = g->draw_offer[BLACK] = g->takeback_offer[WHITE] = g->takeback_offer[BLACK] = false; }
    g->server_nply = g->nply;
    g->t_ms[WHITE] = s->wt_ms; g->t_ms[BLACK] = s->bt_ms;
    g->t_stamp = now_ms();
    g->t_turn = (s->ticking && g->has_clock && !g->finished) ? turn : -1;
    return true;
}

bool game_local_move(Game *g, Move m) {
    char u[8]; move_to_uci(&g->pos, m, u);
    if (g->nply >= MAX_PLIES) return false;
    u64 now = now_ms();
    int mine = g->pos.turn;
    if (g->t_turn == mine) {                           // stop my clock, add the increment
        g->t_ms[mine] -= (int)(now - g->t_stamp);
        if (g->t_ms[mine] < 0) g->t_ms[mine] = 0;
        g->t_ms[mine] += g->inc_ms;
    }
    snprintf(g->uci[g->nply], 8, "%s", u);
    move_to_san(&g->pos, m, g->san[g->nply], sizeof g->san[0]);
    g->mv[g->nply] = m;
    pos_do(&g->pos, m);
    g->nply++;
    g->t_stamp = now;
    g->t_turn = (g->has_clock && g->nply >= 2 && !g->finished) ? g->pos.turn : -1;
    return true;
}

int game_clock(const Game *g, int color) {
    int ms = g->t_ms[color];
    if (g->t_turn == color) ms -= (int)(now_ms() - g->t_stamp);
    return ms < 0 ? 0 : ms;
}

void game_pos_at(const Game *g, int ply, Pos *out) {
    if (ply >= g->nply) { *out = g->pos; return; }
    *out = g->start;
    for (int i = 0; i < ply; i++) pos_do(out, g->mv[i]);
}

bool game_my_turn(const Game *g) { return g->my_color >= 0 && !g->finished && g->pos.turn == g->my_color; }

void game_result_text(const Game *g, char *head, size_t hn, char *sub, size_t sn) {
    const char *s = g->status;
    const char *why = "Game over";
    if (g->why[0]) why = g->why;
    else if (!strcmp(s, "mate")) why = "Checkmate";
    else if (!strcmp(s, "resign")) why = g->winner == g->my_color ? "Opponent resigned" : "Resignation";
    else if (!strcmp(s, "stalemate")) why = "Stalemate";
    else if (!strcmp(s, "timeout") || !strcmp(s, "outoftime")) why = "Time out";
    else if (!strcmp(s, "draw")) why = "Draw";
    else if (!strcmp(s, "aborted")) why = "Game aborted";
    else if (!strcmp(s, "noStart")) why = "Game not started";
    else if (!strcmp(s, "cheat")) why = "Cheat detected";
    else if (!strcmp(s, "variantEnd")) why = "Variant ending";
    if (!strcmp(s, "aborted") || !strcmp(s, "noStart")) snprintf(head, hn, "Game aborted");
    else if (g->winner < 0) snprintf(head, hn, "Draw");
    else if (g->my_color < 0) snprintf(head, hn, "%s wins", g->winner == WHITE ? "White" : "Black");
    else snprintf(head, hn, g->winner == g->my_color ? "You won!" : "You lost");
    snprintf(sub, sn, "%s", why);
}

int game_material_diff(const Pos *p) {
    int d = 0;
    for (int i = 0; i < 64; i++) {
        int t = PTYPE(p->b[i]); if (!t || t == KING) continue;
        d += (PCOLOR(p->b[i]) == WHITE ? 1 : -1) * material_value(t);
    }
    return d;
}

void game_captured(const Pos *p, int color, int counts[7]) {
    static const int full[7] = {0, 8, 2, 2, 2, 1, 1};
    int have[7] = {0};
    for (int i = 0; i < 64; i++) if (p->b[i] && PCOLOR(p->b[i]) == color) have[PTYPE(p->b[i])]++;
    for (int t = PAWN; t <= QUEEN; t++) { int miss = full[t] - have[t]; counts[t] = miss > 0 ? miss : 0; }
    counts[KING] = 0;
}

void game_player_label(const Game *g, int color, char *out, size_t n) {
    const Player *p = &g->pl[color];
    if (p->title[0]) snprintf(out, n, "%s %s", p->title, p->name); else snprintf(out, n, "%s", p->name);
}
