// Scenario runner: reads sdmc:/lichess3ds/scenario.txt, one command per line, executed in order.
// A command that waits (wait / until / shot / drag) blocks the following ones; every wait has a timeout.
#include "scenario.h"
#include "gfx.h"
#include <ctype.h>

#define MAX_LINES 400
#define MAX_EVENTS 32

static char *g_script;
static char *g_lines[MAX_LINES];
static int g_nlines, g_cur;
static bool g_on, g_done, g_quit;
static ScnHooks g_hooks;
static u64 g_t0, g_wait_until, g_cmd_start, g_frames;
static bool g_waiting_shot;
static int g_logged = -1;

// drag state
static bool g_drag;
static int g_dx0, g_dy0, g_dx1, g_dy1;
static u64 g_drag_t0, g_drag_ms;

// events seen so far (until event X consumes one)
static char g_events[MAX_EVENTS][32];
static int g_nevents;

void scn_set_hooks(const ScnHooks *h) { g_hooks = *h; }
bool scn_active(void) { return g_on && !g_done; }
bool scn_quit_requested(void) { return g_quit; }

void scn_event(const char *name) {
    if (!g_on) return;
    if (g_nevents < MAX_EVENTS) snprintf(g_events[g_nevents++], 32, "%s", name);
    LOGF("[scn] event %s", name);
}

static bool take_event(const char *name) {
    for (int i = 0; i < g_nevents; i++)
        if (!strcmp(g_events[i], name)) { memmove(g_events[i], g_events[i + 1], (size_t)(g_nevents - i - 1) * 32); g_nevents--; return true; }
    return false;
}

void scn_init(void) {
    g_script = read_file(DATA_DIR "/scenario.txt", NULL);
    if (!g_script) return;
    for (char *p = strtok(g_script, "\n"); p && g_nlines < MAX_LINES; p = strtok(NULL, "\n")) {
        while (*p == ' ' || *p == '\t') p++;
        size_t n = strlen(p);
        while (n && (p[n - 1] == '\r' || p[n - 1] == ' ')) p[--n] = 0;
        if (!*p || *p == '#') continue;
        g_lines[g_nlines++] = p;
    }
    g_on = true;
    g_t0 = now_ms();
    LOGF("[scn] scenario loaded: %d commands", g_nlines);
}

static void fail(const char *why) {
    LOGF("[scn] FAIL: %s (line %d: %s)", why, g_cur + 1, g_cur < g_nlines ? g_lines[g_cur] : "?");
    g_done = true; g_quit = true;
}

static const char *args_of(const char *line) {
    while (*line && !isspace((unsigned char)*line)) line++;
    while (*line == ' ') line++;
    return line;
}

static u32 key_from_name(const char *n) {
    static const struct { const char *n; u32 k; } t[] = {
        {"a", KEY_A}, {"b", KEY_B}, {"x", KEY_X}, {"y", KEY_Y}, {"l", KEY_L}, {"r", KEY_R}, {"start", KEY_START},
        {"select", KEY_SELECT}, {"up", KEY_DUP}, {"down", KEY_DDOWN}, {"left", KEY_DLEFT}, {"right", KEY_DRIGHT},
        {"zl", KEY_ZL}, {"zr", KEY_ZR}};
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) if (!strcasecmp(n, t[i].n)) return t[i].k;
    return 0;
}

static u32 g_hold_key; static u64 g_hold_until;

// Executes the current command. Returns true when it has completed (advance to the next line).
static bool run_cmd(const char *line) {
    char name[32] = {0};
    sscanf(line, "%31s", name);
    const char *args = args_of(line);
    u64 now = now_ms();
    int a, b, c, d, ms;

    if (!strcmp(name, "wait")) {
        if (!g_cmd_start) { g_cmd_start = now; g_wait_until = now + (u64)atoi(args); }
        return now >= g_wait_until;
    }
    if (!strcmp(name, "log")) { LOGF("[scn] %s", args); return true; }
    if (!strcmp(name, "quit")) { LOGF("[scn] done: %llu ms, %llu frames", (unsigned long long)(now - g_t0), (unsigned long long)g_frames); g_done = true; g_quit = true; return true; }
    if (!strcmp(name, "shot")) {
        if (!g_cmd_start) {
            g_cmd_start = now; g_waiting_shot = true;
            char base[200]; snprintf(base, sizeof base, DATA_DIR "/shots/%s", args);
            mkdirs(DATA_DIR "/shots");
            gfx_request_capture(base);
            LOGF("[scn] shot %s", args);
            return false;
        }
        if (gfx_capture_done()) { g_waiting_shot = false; return true; }
        return false;
    }
    if (!strcmp(name, "key")) {            // key NAME [hold_ms]
        char kn[16]; int hold = 90; sscanf(args, "%15s %d", kn, &hold);
        if (!g_cmd_start) {
            g_cmd_start = now; g_hold_key = key_from_name(kn); g_hold_until = now + (u64)hold;
            if (!g_hold_key) { fail("unknown key"); return true; }
            input_inject_key(g_hold_key, true);
            return false;
        }
        if (now >= g_hold_until) { input_inject_key(g_hold_key, false); return true; }
        return false;
    }
    if (!strcmp(name, "tap")) {            // tap X Y   (bottom screen)
        if (sscanf(args, "%d %d", &a, &b) != 2) { fail("bad tap"); return true; }
        if (!g_cmd_start) { g_cmd_start = now; input_inject_touch(true, a, b); return false; }
        if (now - g_cmd_start >= 90) { input_inject_touch(false, a, b); return true; }
        return false;
    }
    if (!strcmp(name, "down") || !strcmp(name, "moveto")) {
        if (sscanf(args, "%d %d", &a, &b) != 2) { fail("bad coords"); return true; }
        input_inject_touch(true, a, b); return now - (g_cmd_start ? g_cmd_start : (g_cmd_start = now)) >= 50;
    }
    if (!strcmp(name, "up")) {
        if (!g_cmd_start) { g_cmd_start = now; input_inject_touch(false, 0, 0); return false; }
        return now - g_cmd_start >= 50;
    }
    if (!strcmp(name, "drag") || !strcmp(name, "dragsq") || !strcmp(name, "tapsq")) {
        if (!g_cmd_start) {
            g_cmd_start = now;
            if (!strcmp(name, "drag")) {
                ms = 260; c = d = 0;
                if (sscanf(args, "%d %d %d %d %d", &a, &b, &c, &d, &ms) < 4) { fail("bad drag"); return true; }
                g_dx0 = a; g_dy0 = b; g_dx1 = c; g_dy1 = d; g_drag_ms = (u64)ms;
            } else {
                char s1[8] = {0}, s2[8] = {0};
                int n = sscanf(args, "%7s %7s", s1, s2);
                int sq1 = sq_from_name(s1), sq2 = n > 1 ? sq_from_name(s2) : sq1;
                int x1, y1, x2, y2;
                if (sq1 < 0 || sq2 < 0 || !g_hooks.square_xy || !g_hooks.square_xy(sq1, &x1, &y1) || !g_hooks.square_xy(sq2, &x2, &y2)) { fail("bad square"); return true; }
                g_dx0 = x1; g_dy0 = y1; g_dx1 = x2; g_dy1 = y2;
                g_drag_ms = (!strcmp(name, "tapsq") || n < 2) ? 0 : 240;
            }
            g_drag = true; g_drag_t0 = now;
            input_inject_touch(true, g_dx0, g_dy0);
            return false;
        }
        u64 el = now - g_drag_t0;
        if (g_drag_ms == 0) {
            if (el >= 90) { input_inject_touch(false, g_dx0, g_dy0); g_drag = false; return true; }
            return false;
        }
        if (el < g_drag_ms + 80) {
            float t = el < 60 ? 0 : clampf((float)(el - 60) / (float)g_drag_ms, 0, 1);
            input_inject_touch(true, (int)lerpf((float)g_dx0, (float)g_dx1, t), (int)lerpf((float)g_dy0, (float)g_dy1, t));
            return false;
        }
        input_inject_touch(false, g_dx1, g_dy1); g_drag = false; return true;
    }
    if (!strcmp(name, "until")) {          // until COND [args] [timeout MS]
        char cond[32] = {0}; sscanf(args, "%31s", cond);
        const char *cargs = args_of(args);
        int timeout = 30000;
        const char *t = strstr(args, "timeout");
        char cbuf[96]; snprintf(cbuf, sizeof cbuf, "%s", cargs);
        if (t) { timeout = atoi(t + 7); char *tp = strstr(cbuf, "timeout"); if (tp) { *tp = 0; size_t n = strlen(cbuf); while (n && cbuf[n - 1] == ' ') cbuf[--n] = 0; } }
        if (!g_cmd_start) g_cmd_start = now;
        bool ok = false, known = true;
        if (!strcmp(cond, "event")) ok = take_event(cbuf);
        else if (!strcmp(cond, "frames")) ok = g_frames >= (u64)atoi(cbuf);
        else if (g_hooks.cond) { ok = g_hooks.cond(cond, cbuf, &known); if (!known) { fail("unknown condition"); return true; } }
        if (ok) return true;
        if (now - g_cmd_start > (u64)timeout) { char m[120]; snprintf(m, sizeof m, "timeout waiting for %s %s", cond, cbuf); fail(m); return true; }
        return false;
    }
    if (!strcmp(name, "cmd")) {
        char cn[32] = {0}; sscanf(args, "%31s", cn);
        if (g_hooks.cmd && g_hooks.cmd(cn, args_of(args))) return true;
        fail("unknown cmd"); return true;
    }
    fail("unknown command");
    return true;
}

void scn_tick(void) {
    if (!g_on) return;
    g_frames++;
    int guard = 0;
    while (!g_done && g_cur < g_nlines && guard++ < 8) {
        if (g_logged != g_cur) { g_logged = g_cur; LOGF("[scn] %llu ms f%llu: %s", (unsigned long long)(now_ms() - g_t0), (unsigned long long)g_frames, g_lines[g_cur]); }
        if (!run_cmd(g_lines[g_cur])) break;
        g_cur++; g_cmd_start = 0;
    }
    if (!g_done && g_cur >= g_nlines) { LOGF("[scn] done (end of script)"); g_done = true; g_quit = true; }
}
