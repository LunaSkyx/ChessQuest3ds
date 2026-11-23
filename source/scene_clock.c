// Chess clock for games on a real board: two big touch zones, increment or delay, low-time beeps
#include "scenes.h"
#include "ui.h"
#include "audio.h"

typedef enum { CS_SETUP, CS_READY, CS_RUN, CS_PAUSED, CS_OVER } ClockState;

static ClockState g_cs;
static int g_t[2];                 // remaining ms (white = 0, black = 1) as of the last press
static int g_moves[2];
static int g_turn;                 // side whose clock runs
static u64 g_stamp;                // when the current side's clock started
static int g_flag = -1;            // side that ran out of time
static int g_last_beep = -1;
static float g_reset_arm;          // double-tap confirmation for Reset
static const int PRE_MIN[8] = {1, 3, 5, 5, 10, 10, 15, 30};
static const int PRE_INC[8] = {0, 2, 0, 3, 0, 5, 10, 0};

static int base_ms(void) { return g_set.clock_min * 60000; }
static int inc_ms(void) { return g_set.clock_inc * 1000; }
static int left_side(void) { return g_set.clock_swap ? 1 : 0; }

static int remain(int side) {
    if (g_cs != CS_RUN || side != g_turn) return g_t[side];
    int eff = (int)(now_ms() - g_stamp);
    if (g_set.clock_mode == 1) { eff -= inc_ms(); if (eff < 0) eff = 0; }      // delay: the first seconds of each move are free
    return g_t[side] - eff;
}

static void clock_enter(void) {
    if (g_set.clock_min < 1) { g_set.clock_min = 5; g_set.clock_inc = 0; }
    g_cs = CS_SETUP; g_flag = -1; g_reset_arm = 0;
}

static void begin_ready(void) {
    g_t[0] = g_t[1] = base_ms(); g_moves[0] = g_moves[1] = 0;
    g_turn = 0; g_flag = -1; g_last_beep = -1; g_cs = CS_READY;
    settings_save();
}

static void start_clock(void) { g_turn = 0; g_stamp = now_ms(); g_cs = CS_RUN; audio_play(SND_START); }

static void press(int side) {
    if (g_cs != CS_RUN || side != g_turn) return;
    int r = remain(side);
    if (r <= 0) return;
    g_t[side] = r + (g_set.clock_mode == 0 ? inc_ms() : 0);
    g_moves[side]++;
    g_turn ^= 1; g_stamp = now_ms(); g_last_beep = -1;
    audio_play(SND_MOVE_SELF);
}

static void clock_update(float dt) {
    if (g_reset_arm > 0) g_reset_arm -= dt;
    if (g_cs == CS_RUN) {
        // physical keys so nobody has to hammer the screen: D-pad (and L) = left timer, A/B/X/Y (and R) = right timer
        if (g_kdown & (KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT | KEY_L)) press(left_side());
        if (g_kdown & (KEY_A | KEY_B | KEY_X | KEY_Y | KEY_R)) press(left_side() ^ 1);
        if (g_touch.pressed && g_touch.y < 196) press(g_touch.x < 160 ? left_side() : left_side() ^ 1);
        int r = remain(g_turn);
        if (r <= 0) { g_t[g_turn] = 0; g_flag = g_turn; g_cs = CS_OVER; audio_play(SND_LOSE); return; }
        if (r <= 10000) { int sec = (r + 999) / 1000; if (sec != g_last_beep) { g_last_beep = sec; audio_play(SND_LOW); } }
        if (g_kdown & KEY_START) { g_t[g_turn] = r; g_cs = CS_PAUSED; }
    } else if (g_cs == CS_PAUSED) {
        if (g_kdown & KEY_START) { g_stamp = now_ms(); g_cs = CS_RUN; }
    } else if (g_cs == CS_READY) {
        if (g_kdown & KEY_START) start_clock();
    }
}

static void tstr(int ms, char *o, size_t n) { fmt_clock(ms, o, n, true); }

static void side_panel(float x, float y, float w, float h, int side, bool big) {
    bool run = g_cs == CS_RUN && g_turn == side, flagged = g_cs == CS_OVER && g_flag == side;
    bool white = side == 0;
    u32 bg = flagged ? RGB(200, 60, 50) : (white ? RGB(238, 236, 232) : RGB(52, 50, 46));
    u32 fg = flagged ? RGB(255, 255, 255) : (white ? RGB(38, 36, 33) : RGB(245, 245, 240));
    if (run) g_rrect(x - 4, y - 4, w + 8, h + 8, 12, C_GREEN);
    g_rrect(x, y, w, h, 12, bg);
    int r = remain(side);
    if (flagged) r = 0;
    char t[24]; tstr(r, t, sizeof t);
    u32 tc = (!flagged && r < 20000 && g_cs != CS_SETUP && g_cs != CS_READY) ? (white ? RGB(190, 40, 30) : RGB(255, 120, 100)) : fg;
    g_text(F_B34, x + w / 2, y + h * 0.46f, tc, AL_CENTER, t);
    g_text(F_B13, x + w / 2, y + (big ? 22 : 16), fg, AL_CENTER, white ? "White" : "Black");
    g_textf(F_R11, x + w / 2, y + h - 16, fg, AL_CENTER, "%d move%s", g_moves[side], g_moves[side] == 1 ? "" : "s");
    if (flagged) g_text(F_B16, x + w / 2, y + h - 36, fg, AL_CENTER, "Time!");
}

static void clock_top(void) {
    ui_top_bg();
    if (g_cs == CS_SETUP) {
        g_icon(ICON_TIMER, 200, 62, 32, C_GREEN);
        g_text(F_B21, 200, 100, C_TEXT, AL_CENTER, "Chess clock");
        g_text(F_R13, 200, 130, C_TEXT2, AL_CENTER, "For games on a real board.");
        g_text(F_R11, 200, 160, C_TEXT3, AL_CENTER, "Put the console between the players.");
        g_text(F_R11, 200, 176, C_TEXT3, AL_CENTER, "Tap your half, or press: D-pad = left clock,");
        g_text(F_R11, 200, 192, C_TEXT3, AL_CENTER, "A / B / X / Y = right clock. START = pause.");
        return;
    }
    int ls = left_side();
    side_panel(10, 20, 186, 150, ls, true);
    side_panel(204, 20, 186, 150, ls ^ 1, true);
    char l[64]; snprintf(l, sizeof l, "%d min %s %d s", g_set.clock_min, g_set.clock_mode ? "delay" : "+", g_set.clock_inc);
    const char *st = g_cs == CS_READY ? "Press START - White's clock runs first" : (g_cs == CS_PAUSED ? "Paused" : (g_cs == CS_OVER ? (g_flag == 0 ? "White ran out of time" : "Black ran out of time") : (g_turn == 0 ? "White to move" : "Black to move")));
    g_text(F_B13, 200, 192, g_cs == CS_OVER ? C_ORANGE : C_TEXT, AL_CENTER, st);
    g_text(F_R11, 200, 212, C_TEXT3, AL_CENTER, l);
}

static void stepper(float y, const char *label, int *v, int lo, int hi) {
    g_text(F_B13, 12, y + 14, C_TEXT, AL_LEFT, label);
    int step = *v < 10 ? 1 : (*v < 60 ? 5 : 10);
    if (ui_icon_button((Rect){158, y, 44, 28}, ICON_MINUS, BS_SECONDARY)) { *v -= (*v <= 10 ? 1 : (*v <= 60 ? 5 : 10)); if (*v < lo) *v = lo; settings_save(); }
    g_textf(F_B16, 236, y + 14, C_TEXT, AL_CENTER, "%d", *v);
    if (ui_icon_button((Rect){268, y, 44, 28}, ICON_PLUS, BS_SECONDARY)) { *v += step; if (*v > hi) *v = hi; settings_save(); }
}

static void setup_bottom(void) {
    ui_header("Chess clock", ICON_TIMER);
    if (ui_icon_button((Rect){284, 3, 32, 28}, ICON_ARROW_LEFT, BS_GHOST)) scene_goto(SC_MORE);
    for (int i = 0; i < 8; i++) {
        Rect r = {8 + (i % 4) * 78.0f, 40 + (i / 4) * 34.0f, 74, 30};
        char l[16]; snprintf(l, sizeof l, "%d+%d", PRE_MIN[i], PRE_INC[i]);
        bool sel = g_set.clock_min == PRE_MIN[i] && g_set.clock_inc == PRE_INC[i];
        if (ui_button(r, l, F_B13, sel ? BS_TILE_SEL : BS_TILE, -1)) { g_set.clock_min = PRE_MIN[i]; g_set.clock_inc = PRE_INC[i]; settings_save(); }
    }
    stepper(112, "Minutes", &g_set.clock_min, 1, 180);
    stepper(144, g_set.clock_mode ? "Delay (s)" : "Increment (s)", &g_set.clock_inc, 0, 60);
    if (ui_button((Rect){8, 176, 150, 26}, g_set.clock_mode ? "Mode: Delay" : "Mode: Increment", F_B11, BS_SECONDARY, -1)) { g_set.clock_mode = !g_set.clock_mode; settings_save(); }
    if (ui_button((Rect){162, 176, 150, 26}, g_set.clock_swap ? "White on the right" : "White on the left", F_B11, BS_SECONDARY, ICON_ARROW_DOWN_UP)) { g_set.clock_swap = !g_set.clock_swap; settings_save(); }
    if (ui_button((Rect){8, 206, 304, 30}, "Set clock", F_B16, BS_PRIMARY, ICON_CHECK)) begin_ready();
}

static void run_bottom(void) {
    int ls = left_side();
    side_panel(4, 4, 154, 188, ls, false);
    side_panel(162, 4, 154, 188, ls ^ 1, false);
    if (g_cs == CS_READY) {
        g_rect(0, 0, 320, 196, RGBA(0, 0, 0, 120));
        if (ui_button((Rect){70, 70, 180, 56}, "START", F_B21, BS_PRIMARY, ICON_PLAY)) start_clock();
        g_text(F_R11, 160, 142, RGB(255, 255, 255), AL_CENTER, "White's clock runs first");
    } else if (g_cs == CS_PAUSED) {
        g_rect(0, 0, 320, 196, RGBA(0, 0, 0, 205));
        g_text(F_B21, 160, 74, RGB(255, 255, 255), AL_CENTER, "Paused");
        if (ui_button((Rect){90, 110, 140, 40}, "Resume", F_B16, BS_PRIMARY, ICON_PLAY)) { g_stamp = now_ms(); g_cs = CS_RUN; }
    }
    // control strip (below the touch zones, so a press on the clock never hits it)
    if (g_cs == CS_RUN) { if (ui_button((Rect){4, 200, 100, 36}, "Pause", F_B11, BS_SECONDARY, ICON_TIMER)) { g_t[g_turn] = remain(g_turn); g_cs = CS_PAUSED; } }
    else ui_button((Rect){4, 200, 100, 36}, g_cs == CS_OVER ? "Finished" : "Pause", F_B11, BS_DARK, ICON_TIMER);
    if (ui_button((Rect){110, 200, 100, 36}, g_reset_arm > 0 ? "Sure?" : "Restart", F_B11, g_reset_arm > 0 ? BS_DANGER : BS_SECONDARY, ICON_ROTATE_CCW)) {
        if (g_reset_arm > 0 || g_cs == CS_OVER || g_cs == CS_READY) { begin_ready(); g_reset_arm = 0; } else g_reset_arm = 2.0f;
    }
    if (ui_button((Rect){216, 200, 100, 36}, "Setup", F_B11, BS_SECONDARY, ICON_SETTINGS)) g_cs = CS_SETUP;
}

static void clock_bot(void) {
    g_rect(0, 0, 320, 240, C_BG);
    if (g_cs == CS_SETUP) setup_bottom(); else run_bottom();
    ui_toast_draw(160);
}

const Scene scene_clock = {"clock", clock_enter, clock_update, clock_top, clock_bot, NULL};
