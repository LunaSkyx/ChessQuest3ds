#include "fics.h"
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#define FICS_HOST "freechess.org"
#define FICS_PORT "5000"
#define LINE_MAX_LEN 400
#define QN 256

Fics g_fics;

// ------------------------------------------------------------------ thread + queues
static LightLock g_lk;
static char g_in[QN][LINE_MAX_LEN]; static int g_in_h, g_in_t;       // worker -> UI
static char g_out[QN][LINE_MAX_LEN]; static int g_out_h, g_out_t;    // UI -> worker
static Thread g_th;
static volatile bool g_quit;
static volatile int g_req;                                           // 1 connect, 2 disconnect
static volatile int g_sock = -1;
static volatile int g_stage;                                         // 0 idle, 1 resolving, 2 connecting, 3 connected (waiting for the server), 4 receiving
static volatile int g_rx_total, g_last_errno;                        // diagnostics shown when the login times out

static void push_in(const char *s) {
    LightLock_Lock(&g_lk);
    int nt = (g_in_t + 1) % QN;
    if (nt != g_in_h) { snprintf(g_in[g_in_t], LINE_MAX_LEN, "%s", s); g_in_t = nt; }
    LightLock_Unlock(&g_lk);
}
static bool pop_in(char *dst) {
    bool ok = false;
    LightLock_Lock(&g_lk);
    if (g_in_h != g_in_t) { snprintf(dst, LINE_MAX_LEN, "%s", g_in[g_in_h]); g_in_h = (g_in_h + 1) % QN; ok = true; }
    LightLock_Unlock(&g_lk);
    return ok;
}
static void push_out(const char *s) {
    LightLock_Lock(&g_lk);
    int nt = (g_out_t + 1) % QN;
    if (nt != g_out_h) { snprintf(g_out[g_out_t], LINE_MAX_LEN, "%s", s); g_out_t = nt; }
    LightLock_Unlock(&g_lk);
}
static bool pop_out(char *dst) {
    bool ok = false;
    LightLock_Lock(&g_lk);
    if (g_out_h != g_out_t) { snprintf(dst, LINE_MAX_LEN, "%s", g_out[g_out_h]); g_out_h = (g_out_h + 1) % QN; ok = true; }
    LightLock_Unlock(&g_lk);
    return ok;
}
static void clear_queues(void) { LightLock_Lock(&g_lk); g_in_h = g_in_t = g_out_h = g_out_t = 0; LightLock_Unlock(&g_lk); }

static bool ends_with(const char *s, size_t n, const char *suf) { size_t k = strlen(suf); return n >= k && !memcmp(s + n - k, suf, k); }

static void session(void) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints); hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    g_stage = 1; g_rx_total = 0; g_last_errno = 0;
    int rc = getaddrinfo(FICS_HOST, FICS_PORT, &hints, &res);
    if (rc || !res) { push_in("\x01" "ERR Cannot find freechess.org (DNS)"); return; }
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); push_in("\x01" "ERR No socket"); return; }
    // plain blocking connect (the 3DS socket layer reports non-blocking connect results unreliably: SO_ERROR gave -26 on hardware)
    g_stage = 2;
    int cr = connect(fd, res->ai_addr, res->ai_addrlen);
    int ce = errno;
    freeaddrinfo(res);
    if (cr < 0) { g_last_errno = ce; close(fd); char m[80]; snprintf(m, sizeof m, "\x01" "ERR Cannot connect to FICS (errno %d)", ce); push_in(m); return; }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    int one = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    g_stage = 3;
    g_sock = fd;
    push_in("\x01" "CONNECTED");
    char line[LINE_MAX_LEN]; size_t ll = 0; int iac = 0; u64 last_rx = now_ms();
    char rx[1024];
    while (!g_quit && g_req == 0) {
        struct pollfd pf = {fd, POLLIN, 0};
        int pr = poll(&pf, 1, 25);
        if (pr > 0 && (pf.revents & (POLLIN | POLLHUP | POLLERR))) {
            ssize_t n = recv(fd, rx, sizeof rx, 0);
            if (n == 0) { push_in("\x01" "CLOSED"); break; }
            if (n < 0) { if (errno != EAGAIN && errno != EWOULDBLOCK) { push_in("\x01" "CLOSED"); break; } }
            else {
                last_rx = now_ms(); g_stage = 4; g_rx_total += (int)n;
                for (ssize_t i = 0; i < n; i++) {
                    unsigned char c = (unsigned char)rx[i];
                    if (iac) { iac--; continue; }                       // telnet negotiation: IAC <cmd> <opt>
                    if (c == 255) { iac = 2; continue; }
                    if (c == '\r') continue;
                    if (c == '\n') { line[ll] = 0; push_in(line); ll = 0; continue; }
                    if (ll < LINE_MAX_LEN - 1) line[ll++] = (char)c;
                }
            }
        }
        // prompts have no newline: hand them over once the server went quiet
        if (ll && now_ms() - last_rx > 120) {
            line[ll] = 0;
            if (ends_with(line, ll, "login: ") || ends_with(line, ll, "password: ") || ends_with(line, ll, "% ")) { push_in(line); ll = 0; }
        }
        char o[LINE_MAX_LEN];
        while (pop_out(o)) {
            size_t k = strlen(o); o[k++] = '\n';
            size_t sent = 0;
            while (sent < k) {
                ssize_t w = send(fd, o + sent, k - sent, 0);
                if (w > 0) sent += (size_t)w;
                else if (errno == EAGAIN || errno == EWOULDBLOCK) svcSleepThread(2 * 1000000LL);
                else { push_in("\x01" "CLOSED"); goto out; }
            }
        }
    }
out:
    g_sock = -1;
    close(fd);
}

static volatile int g_alive;                                         // set by the worker when it runs (diagnostics)
static void worker(void *arg) {
    (void)arg;
    g_alive = 1;
    while (!g_quit) {
        if (g_req == 1) { g_req = 0; session(); }
        else { if (g_req == 2) g_req = 0; svcSleepThread(20 * 1000000LL); }
    }
}

void fics_start(void) {
    LightLock_Init(&g_lk);
    memset(&g_fics, 0, sizeof g_fics);
    g_fics.guest = true;
    g_quit = false;
    // On real hardware this thread never got to run when it shared the system core with the HTTP worker, so it lives on the
    // application core (the main loop sleeps for vblank every frame, leaving it plenty of time) at a slightly lower priority.
    g_th = threadCreate(worker, NULL, 96 * 1024, 0x31, -2, false);
    if (!g_th) g_th = threadCreate(worker, NULL, 96 * 1024, 0x31, 1, false);
    LOGF("fics: worker thread %s", g_th ? "created" : "FAILED");
}

void fics_stop(void) {
    if (!g_th) return;
    g_quit = true;
    threadJoin(g_th, U64_MAX); threadFree(g_th); g_th = NULL;
}

// ------------------------------------------------------------------ protocol state machine (UI thread)
static u64 g_t_conn;
static bool g_sent_user, g_sent_pass, g_sent_return;

void fics_send(const char *fmt, ...) {
    char b[LINE_MAX_LEN];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    LOGF("fics >> %s", strstr(b, "pass") ? "(hidden)" : b);
    push_out(b);
}

void fics_connect(bool guest, const char *user, const char *pass) {
    if (g_fics.state == FICS_CONNECTING || g_fics.state == FICS_LOGIN) return;
    if (g_fics.state == FICS_ONLINE) return;
    g_fics.guest = guest;
    snprintf(g_fics.user, sizeof g_fics.user, "%s", user ? user : "");
    snprintf(g_fics.pass, sizeof g_fics.pass, "%s", pass ? pass : "");
    g_fics.state = FICS_CONNECTING; g_fics.err[0] = 0; g_fics.seeking = false;
    g_t_conn = now_ms(); g_sent_user = g_sent_pass = g_sent_return = false;
    clear_queues();
    g_req = 1;
    LOGF("fics: connecting (%s)", guest ? "guest" : g_fics.user);
}

void fics_disconnect(void) {
    if (g_fics.state != FICS_OFF && g_fics.state != FICS_ERROR) { fics_send("quit"); svcSleepThread(30 * 1000000LL); }
    g_req = 2;
    g_fics.state = FICS_OFF; g_fics.seeking = false;
}

static void fail(const char *msg) {
    snprintf(g_fics.err, sizeof g_fics.err, "%s", msg);
    g_fics.state = FICS_ERROR; g_fics.seeking = false;
    g_req = 2;
    LOGF("fics: error: %s", msg);
}

void fics_seek(int minutes, int inc, bool rated, int color) {
    fics_send("seek %d %d %s%s", minutes, inc, rated ? "rated" : "unrated", color == 1 ? " white" : (color == 2 ? " black" : ""));
    g_fics.seeking = true;
}
void fics_unseek(void) { if (g_fics.state == FICS_ONLINE) fics_send("unseek"); g_fics.seeking = false; }

void fics_send_move(const Pos *pre, Move m) {
    char s[16]; fics_move_string(pre, m, s, sizeof s);
    fics_send("%s", s);
}

static void init_session(void) {
    static const char *cmds[] = {"set style 12", "iset ms 1", "iset gameinfo 1", "set shout 0", "set cshout 0", "set seek 0", "set pin 0",
                                 "set gin 0", "set bell 0", "set open 1", "set silence 1", "set tell 0", "set kibitz 0", "set height 200"};
    for (unsigned i = 0; i < sizeof cmds / sizeof *cmds; i++) fics_send("%s", cmds[i]);
}

static void deliver_result(int winner, const char *status, const char *why) {
    g_fics.in_game = false;
    if (g_fics.on_result) g_fics.on_result(winner, status, why);
    else { g_fics.result_valid = true; g_fics.res_winner = winner; snprintf(g_fics.res_status, sizeof g_fics.res_status, "%s", status); snprintf(g_fics.res_why, sizeof g_fics.res_why, "%s", why); }
}

static void on_game_line(const char *l) {
    Style12 s;
    if (!strncmp(l, "<12>", 4)) {
        if (!fics_parse_style12(l, &s)) return;
        if (s.relation != 1 && s.relation != -1) return;                   // we only play, never observe
        g_fics.last = s;
        if (!g_fics.in_game || s.gnum != g_fics.gnum) {
            g_fics.in_game = true; g_fics.gnum = s.gnum; g_fics.seeking = false;
            g_fics.first = s; g_fics.start_pending = true; g_fics.result_valid = false;
            LOGF("fics: game %d started: %s vs %s (%d+%d)", s.gnum, s.white, s.black, s.init_min, s.inc);
        } else if (g_fics.on_s12) g_fics.on_s12(&s);
        return;
    }
    if (!strncmp(l, "<g1>", 4)) {                                           // <g1> 40 p=0 t=blitz r=0 u=1,1 it=240,3 i=240,3 pt=0 rt=1500E,1600E ...
        const char *r = strstr(l, " r="); g_fics.gi_rated = r && r[3] == '1';
        const char *rt = strstr(l, "rt=");
        g_fics.gi_rating[0] = g_fics.gi_rating[1] = 0;
        if (rt) { rt += 3; g_fics.gi_rating[0] = atoi(rt); const char *c = strchr(rt, ','); if (c) g_fics.gi_rating[1] = atoi(c + 1); }
        return;
    }
    int gnum, winner; char st[16], why[40];
    if (l[0] == '{' && fics_parse_result(l, &gnum, &winner, st, sizeof st, why, sizeof why)) {
        LOGF("fics: result game %d: %s (%s)", gnum, st, why);
        if (g_fics.in_game && gnum == g_fics.gnum) deliver_result(winner, st, why);
        return;
    }
    int ev = 0;
    if (strstr(l, "offers you a draw")) ev = FE_DRAW_OFFER;
    else if (strstr(l, "declines your draw request")) ev = FE_DRAW_DECLINED;
    else if (strstr(l, "would like to take back")) ev = FE_TAKEBACK_OFFER;
    else if (strstr(l, "declines the takeback request")) ev = FE_TAKEBACK_DECLINED;
    else if (!strncmp(l, "Illegal move", 12)) ev = FE_ILLEGAL;
    else if (strstr(l, "would like to abort the game")) ev = FE_ABORT_OFFER;
    if (ev) { LOGF("fics: event %d: %s", ev, l); if (g_fics.on_event) g_fics.on_event(ev, l); return; }
    if (!strncmp(l, "Your seek has been posted", 25)) { g_fics.seeking = true; return; }
    if (!strncmp(l, "Your seeks have been removed", 28) || !strncmp(l, "You have no seeks", 17)) { g_fics.seeking = false; return; }
}

static void handle_line(char *raw) {
    char *l = raw;
    while (!strncmp(l, "fics% ", 6)) l += 6;
    if (l[0] == 1) {                                                        // worker markers
        if (!strncmp(l + 1, "CONNECTED", 9)) { g_fics.state = FICS_LOGIN; LOGF("fics: socket connected"); }
        else if (!strncmp(l + 1, "ERR ", 4)) fail(l + 5);
        else if (!strncmp(l + 1, "CLOSED", 6)) {
            if (g_fics.state != FICS_OFF) { fail(g_fics.state == FICS_ONLINE ? "Connection to FICS lost" : "FICS closed the connection"); }
        }
        return;
    }
    if (g_fics.state == FICS_LOGIN) {
        if (!strncmp(l, "login:", 6) && !g_sent_user) { g_sent_user = true; fics_send("%s", g_fics.guest || !g_fics.user[0] ? "guest" : g_fics.user); return; }
        if (!strncmp(l, "password:", 9) && !g_sent_pass) {
            g_sent_pass = true;
            push_out(g_fics.pass);                                          // (not logged)
            return;
        }
        if (strstr(l, "Invalid password")) { fail("Wrong FICS password"); return; }
        if (strstr(l, "is not a registered name") && !g_fics.guest) { fail("That FICS name is not registered"); return; }
        if (strstr(l, "Press return to enter the server") && !g_sent_return) { g_sent_return = true; fics_send("%s", ""); return; }
        const char *st = strstr(l, "Starting FICS session as ");
        if (st) {
            st += 25;
            snprintf(g_fics.name, sizeof g_fics.name, "%s", st);
            char *e = strstr(g_fics.name, " ****"); if (e) *e = 0;
            char *u = strstr(g_fics.name, "(U)"); if (u) *u = 0;
            g_fics.state = FICS_ONLINE;
            LOGF("fics: online as %s", g_fics.name);
            init_session();
        }
        return;
    }
    if (g_fics.state == FICS_ONLINE && l[0]) on_game_line(l);
}

void fics_pump(void) {
    char l[LINE_MAX_LEN]; int guard = 0;
    while (guard++ < 96 && pop_in(l)) handle_line(l);
    if ((g_fics.state == FICS_CONNECTING || g_fics.state == FICS_LOGIN) && now_ms() - g_t_conn > 30000) {
        static const char *ST[] = {"not started", "DNS lookup", "TCP connect", "connected, silent server", "server talks, no login prompt"};
        char m[100]; snprintf(m, sizeof m, "FICS login timed out (%s%s, %d bytes)", g_alive ? "" : "thread not running, ", ST[g_stage < 0 || g_stage > 4 ? 0 : g_stage], g_rx_total);
        fail(m);
    }
}
