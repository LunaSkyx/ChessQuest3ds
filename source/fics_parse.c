#include "fics_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

bool fics_parse_style12(const char *line, Style12 *s) {
    memset(s, 0, sizeof *s);
    char buf[512];
    snprintf(buf, sizeof buf, "%s", line);
    char *tok[48]; int n = 0;
    for (char *p = strtok(buf, " "); p && n < 48; p = strtok(NULL, " ")) tok[n++] = p;
    if (n < 30 || strcmp(tok[0], "<12>")) return false;
    for (int r = 0; r < 8; r++) {
        if (strlen(tok[1 + r]) != 8) return false;
        memcpy(s->board[r], tok[1 + r], 8); s->board[r][8] = 0;
    }
    s->white_turn = tok[9][0] == 'W';
    s->ep_file = atoi(tok[10]);
    for (int i = 0; i < 4; i++) s->castle[i] = atoi(tok[11 + i]) != 0;
    s->irrev = atoi(tok[15]);
    s->gnum = atoi(tok[16]);
    snprintf(s->white, sizeof s->white, "%s", tok[17]);
    snprintf(s->black, sizeof s->black, "%s", tok[18]);
    s->relation = atoi(tok[19]);
    s->init_min = atoi(tok[20]); s->inc = atoi(tok[21]);
    s->wt_ms = atoi(tok[24]); s->bt_ms = atoi(tok[25]);
    s->movenum = atoi(tok[26]);
    snprintf(s->verbose, sizeof s->verbose, "%s", tok[27]);
    snprintf(s->san, sizeof s->san, "%s", tok[29]);
    s->ticking = n > 31 ? atoi(tok[31]) != 0 : true;
    return true;
}

static bool has(const char *hay, const char *needle) { return strstr(hay, needle) != NULL; }

bool fics_parse_result(const char *line, int *gnum, int *winner, char *status, size_t sn, char *why, size_t wn) {
    if (strncmp(line, "{Game ", 6)) return false;
    const char *close = strrchr(line, '}');
    const char *open = strstr(line, ") ");
    if (!close || !open || open > close) return false;
    const char *res = close + 1;
    while (*res == ' ') res++;
    int w;
    if (!strncmp(res, "1-0", 3)) w = WHITE; else if (!strncmp(res, "0-1", 3)) w = BLACK;
    else if (!strncmp(res, "1/2-1/2", 7) || *res == '*') w = -1;
    else return false;                                         // e.g. "Creating unrated blitz match." has no result token
    char text[160];
    size_t n = (size_t)(close - (open + 2)); if (n >= sizeof text) n = sizeof text - 1;
    memcpy(text, open + 2, n); text[n] = 0;
    *gnum = atoi(line + 6);
    *winner = w;
    const char *st = "draw", *wy = "Draw";
    if (has(text, "resigns")) { st = "resign"; wy = "Resignation"; }
    else if (has(text, "checkmated")) { st = "mate"; wy = "Checkmate"; }
    else if (has(text, "forfeits on time")) { st = "outoftime"; wy = "Time out"; }
    else if (has(text, "forfeits by disconnection")) { st = "resign"; wy = "Disconnection"; }
    else if (has(text, "ran out of time and")) { st = "draw"; wy = "Time out vs bare king"; }
    else if (has(text, "both players ran out of time")) { st = "draw"; wy = "Both ran out of time"; }
    else if (has(text, "stalemate")) { st = "stalemate"; wy = "Stalemate"; }
    else if (has(text, "repetition")) { st = "draw"; wy = "Repetition"; }
    else if (has(text, "50 move")) { st = "draw"; wy = "50-move rule"; }
    else if (has(text, "insufficient material")) { st = "draw"; wy = "Insufficient material"; }
    else if (has(text, "mutual agreement") && has(text, "aborted")) { st = "aborted"; wy = "Aborted by agreement"; }
    else if (has(text, "drawn by mutual agreement")) { st = "draw"; wy = "Agreement"; }
    else if (has(text, "aborted")) { st = "aborted"; wy = "Game aborted"; }
    else if (has(text, "adjourned")) { st = "aborted"; wy = "Adjourned"; }
    else if (w >= 0) { st = "resign"; wy = "Game over"; }
    snprintf(status, sn, "%s", st);
    snprintf(why, wn, "%s", wy);
    return true;
}

bool fics_s12_to_pos(const Style12 *s, Pos *p) {
    char fen[128]; int k = 0;
    for (int r = 0; r < 8; r++) {
        int e = 0;
        for (int f = 0; f < 8; f++) {
            char c = s->board[r][f];
            if (c == '-') e++;
            else { if (e) { fen[k++] = (char)('0' + e); e = 0; } fen[k++] = c; }
        }
        if (e) fen[k++] = (char)('0' + e);
        if (r < 7) fen[k++] = '/';
    }
    fen[k++] = ' '; fen[k++] = s->white_turn ? 'w' : 'b'; fen[k++] = ' ';
    int c0 = k;
    if (s->castle[0]) fen[k++] = 'K';
    if (s->castle[1]) fen[k++] = 'Q';
    if (s->castle[2]) fen[k++] = 'k';
    if (s->castle[3]) fen[k++] = 'q';
    if (k == c0) fen[k++] = '-';
    fen[k++] = ' ';
    if (s->ep_file >= 0 && s->ep_file < 8) { fen[k++] = (char)('a' + s->ep_file); fen[k++] = s->white_turn ? '6' : '3'; } else fen[k++] = '-';
    snprintf(fen + k, sizeof fen - (size_t)k, " %d %d", s->irrev, s->movenum > 0 ? s->movenum : 1);
    return pos_from_fen(p, fen, VAR_STANDARD);
}

bool fics_verbose_to_move(const Pos *p, const char *v, Move *out) {
    if (!strcasecmp(v, "o-o") || !strcasecmp(v, "o-o-o")) {
        bool king = !strcasecmp(v, "o-o");
        Move mv[MAX_MOVES]; int n = pos_legal_moves(p, mv);
        for (int i = 0; i < n; i++)
            if ((mv[i].flags & MF_CASTLE) && ((mv[i].to > mv[i].from) == king)) { *out = mv[i]; return true; }
        return false;
    }
    const char *sl = strchr(v, '/');
    if (!sl || strlen(sl) < 6 || sl[3] != '-') return false;
    char uci[8] = {sl[1], sl[2], sl[4], sl[5], 0, 0};
    const char *eq = strchr(v, '=');
    if (eq && eq[1]) uci[4] = (char)((eq[1] >= 'A' && eq[1] <= 'Z') ? eq[1] + 32 : eq[1]);
    return pos_parse_uci(p, uci, out);
}

void fics_move_string(const Pos *pre, Move m, char *out, size_t n) {
    (void)pre;
    if (m.flags & MF_CASTLE) { snprintf(out, n, "%s", m.to > m.from ? "o-o" : "o-o-o"); return; }
    char u[8]; move_to_uci(pre, m, u);
    if (strlen(u) == 5) snprintf(out, n, "%c%c%c%c=%c", u[0], u[1], u[2], u[3], u[4] >= 'a' ? u[4] - 32 : u[4]);
    else snprintf(out, n, "%s", u);
}
