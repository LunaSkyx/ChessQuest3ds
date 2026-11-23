// Home menu, ongoing games list and the incoming-challenge overlay
#include "scenes.h"
#include "ui.h"

GameParams g_last_params;

// ------------------------------------------------------------------ challenge overlay
bool ui_challenge_overlay(void) {
    if (!g_api.chal_in_pending) return false;
    ui_set_enabled(true);
    g_rect(0, 0, 320, 240, RGBA(0, 0, 0, 150));
    g_rrect(16, 54, 288, 132, 12, C_PANEL);
    g_icon(ICON_SWORDS, 160, 78, 24, C_GREEN);
    g_text(F_B13, 160, 102, C_TEXT2, AL_CENTER, "Challenge from");
    g_text_fit(F_B16, 160, 122, C_TEXT, AL_CENTER, 270, g_api.chal_from);
    g_text_fit(F_R13, 160, 142, C_TEXT3, AL_CENTER, 270, g_api.chal_desc);
    if (ui_button((Rect){28, 154, 128, 28}, "Accept", F_B13, BS_PRIMARY, ICON_CHECK)) api_challenge_accept(g_api.chal_id);
    if (ui_button((Rect){164, 154, 128, 28}, "Decline", F_B13, BS_SECONDARY, ICON_X)) api_challenge_decline(g_api.chal_id);
    return true;
}

// ------------------------------------------------------------------ home
static const char *SPEED_NAMES[4] = {"Bullet", "Blitz", "Rapid", "Classical"};
static const int SPEED_ICONS[4] = {ICON_BOLT, ICON_ZAP, ICON_TIMER, ICON_TURTLE};
static float g_poll;

static void home_enter(void) { if (g_api.net == 2) api_refresh_ongoing(); g_poll = 0; }
static void home_update(float dt) {
    g_poll += dt;
    if (g_poll > 8.0f) { g_poll = 0; if (g_api.net == 2) api_refresh_ongoing(); }
}

static void home_top(void) {
    ui_top_bg();
    g_img(UI_LOGO_48, 340, 12, 0, 1, 1);
    // profile card
    g_rrect(16, 16, 308, 74, 8, C_PANEL);
    g_disc(52, 53, 48, C_GREEN_D);
    char ini[2] = {g_api.user[0] ? (char)((g_api.user[0] >= 'a' && g_api.user[0] <= 'z') ? g_api.user[0] - 32 : g_api.user[0]) : '?', 0};
    g_text(F_B21, 52, 53, C_TEXT, AL_CENTER, ini);
    float nx = 88;
    if (g_api.title[0]) { g_rrect(nx, 30, 30, 18, 4, C_ORANGE); g_text(F_B11, nx + 15, 39, RGB(60, 40, 0), AL_CENTER, g_api.title); nx += 36; }
    g_text_fit(F_B21, nx, 40, C_TEXT, AL_LEFT, 324 - nx - 8, g_api.user[0] ? g_api.user : (g_api.net == 1 ? "..." : "Offline mode"));
    bool on = g_api.net == 2;
    g_disc(95, 70, 10, on ? C_GREEN : C_ORANGE);
    g_text(F_R13, 106, 70, C_TEXT2, AL_LEFT, on ? "Online" : (g_api.net == 1 ? "Connecting..." : "Not connected to Lichess"));
    // ratings
    for (int i = 0; i < 4; i++) {
        float x = 16 + i * 78.0f;
        g_rrect(x, 102, 74, 58, 8, C_PANEL);
        g_icon(SPEED_ICONS[i], x + 37, 118, 16, C_GREEN);
        if (g_api.rating[i]) g_textf(F_B16, x + 37, 140, C_TEXT, AL_CENTER, "%d", g_api.rating[i]);
        else g_text(F_B16, x + 37, 140, C_TEXT3, AL_CENTER, "-");
        g_text(F_R11, x + 37, 153, C_TEXT3, AL_CENTER, SPEED_NAMES[i]);
    }
    g_text(F_R13, 200, 196, C_TEXT3, AL_CENTER, "Lichess is free and open source.");
    g_text(F_R11, 200, 214, C_TEXT3, AL_CENTER, "Chess Quest 3DS v" APP_VERSION "  -  unofficial Lichess client");
}

static int my_turn_count(void) {
    int n = 0;
    for (int i = 0; i < g_api.n_ongoing; i++) if (g_api.ongoing[i].my_turn) n++;
    return n;
}

static void quick(int tc_idx) {
    scene_play_set_mode(0);
    g_set.last_tc = tc_idx;
    scene_goto(SC_PLAY);
    scene_play_autostart();
}

static void home_bot(void) {
    g_rect(0, 0, 320, 240, C_BG);
    bool ov = g_api.chal_in_pending;
    ui_set_enabled(!ov);
    bool online = g_api.net == 2;
    if (ui_button((Rect){12, 12, 196, 58}, "Play", F_B21, BS_PRIMARY, ICON_SWORDS)) { if (online) { scene_play_set_mode(0); scene_goto(SC_PLAY); } else scene_goto(SC_BOTS); }
    if (ui_button((Rect){212, 12, 96, 58}, "Puzzles", F_B16, BS_SECONDARY, ICON_FLAME)) puzzle_open_normal();
    char lbl[40]; int n = g_api.n_ongoing;
    if (n) snprintf(lbl, sizeof lbl, "Games (%d)", n); else snprintf(lbl, sizeof lbl, "Games");
    if (n) snprintf(lbl, sizeof lbl, "Games");
    if (ui_button((Rect){12, 82, 71, 44}, lbl, F_B11, BS_SECONDARY, ICON_LIST)) { if (online) scene_goto(SC_GAMES); else ui_toast("Needs a Lichess connection"); }
    if (my_turn_count()) { g_disc(78, 90, 14, C_ORANGE); g_textf(F_B11, 78, 90, RGB(60, 40, 0), AL_CENTER, "%d", my_turn_count()); }
    if (ui_button((Rect){87, 82, 71, 44}, "History", F_B11, BS_SECONDARY, ICON_CLOCK)) scene_goto(SC_HISTORY);
    if (ui_button((Rect){162, 82, 71, 44}, "More", F_B11, BS_SECONDARY, ICON_GRID_3X3)) scene_goto(SC_MORE);
    if (ui_button((Rect){237, 82, 71, 44}, "Settings", F_B11, BS_SECONDARY, ICON_SETTINGS)) scene_goto(SC_SETTINGS);
    if (online) {
        g_text(F_B11, 12, 144, C_TEXT3, AL_LEFT, "QUICK PAIRING (RAPID)");
        static const struct { const char *l; int tc; int icon; } Q[3] = {{"10+0", 6, ICON_TIMER}, {"10+5", 7, ICON_TIMER}, {"15+10", 8, ICON_TIMER}};
        for (int i = 0; i < 3; i++)
            if (ui_button((Rect){12 + i * 100.0f, 154, 96, 36}, Q[i].l, F_B16, BS_TILE, Q[i].icon)) quick(Q[i].tc);
        if (g_set.token[0] && ui_button((Rect){12, 200, 100, 30}, "Log out", F_B13, BS_GHOST, ICON_LOG_OUT)) {
            api_disconnect(); g_set.token[0] = 0; settings_save(); li_set_token(""); scene_goto(SC_LOGIN);
        }
    } else {
        g_text(F_B11, 12, 144, C_TEXT3, AL_LEFT, "OFFLINE MODE: GAMES VS ENGINE, LEARN, ANALYSIS");
        bool has_tok = g_set.token[0] != 0;
        if (ui_button((Rect){12, 156, 146, 40}, has_tok ? (g_api.net == 1 ? "Connecting..." : "Connect") : "Connect", F_B13, BS_PRIMARY, ICON_REFRESH_CW)) {
            if (has_tok) { if (g_api.net != 1) { api_connect(); ui_toast("Connecting..."); } } else guide_open(SC_HOME);
        }
        if (ui_button((Rect){162, 156, 146, 40}, "How to connect", F_B13, BS_SECONDARY, ICON_INFO)) guide_open(SC_HOME);
        if (has_tok && ui_button((Rect){12, 202, 100, 28}, "Log out", F_B11, BS_GHOST, ICON_LOG_OUT)) {
            api_disconnect(); g_set.token[0] = 0; settings_save(); li_set_token(""); scene_goto(SC_LOGIN);
        }
    }
    g_text(F_R11, 308, 215, C_TEXT3, AL_RIGHT, g_api.user);
    if (ov) ui_challenge_overlay();
    ui_set_enabled(true);
    ui_toast_draw(160);
}

const Scene scene_home = {"home", home_enter, home_update, home_top, home_bot, NULL};

// ------------------------------------------------------------------ ongoing games
static Scroll g_scroll;
static float g_gpoll;

static void games_enter(void) { memset(&g_scroll, 0, sizeof g_scroll); api_refresh_ongoing(); g_gpoll = 0; }
static void games_update(float dt) { g_gpoll += dt; if (g_gpoll > 5.0f) { g_gpoll = 0; api_refresh_ongoing(); } }

static void games_top(void) {
    ui_top_bg();
    g_icon(ICON_LIST, 36, 40, 32, C_GREEN);
    g_text(F_B21, 62, 40, C_TEXT, AL_LEFT, "Ongoing games");
    g_text(F_R13, 36, 80, C_TEXT2, AL_LEFT, "Games you are playing right now. Tap one to resume it.");
    if (!g_api.n_ongoing) g_text(F_R13, 200, 150, C_TEXT3, AL_CENTER, g_api.ongoing_loaded ? "No games in progress." : "Loading...");
    else g_textf(F_B16, 200, 150, C_TEXT, AL_CENTER, "%d game%s, %d waiting for you", g_api.n_ongoing, g_api.n_ongoing == 1 ? "" : "s", my_turn_count());
}

static void games_bot(void) {
    g_rect(0, 0, 320, 240, C_BG);
    bool ov = g_api.chal_in_pending;
    ui_set_enabled(!ov);
    Rect view = {0, 40, 320, 200};
    const float rh = 50;
    ui_scroll_update(&g_scroll, view, 8 + g_api.n_ongoing * rh);
    for (int i = 0; i < g_api.n_ongoing; i++) {
        const OngoingGame *g = &g_api.ongoing[i];
        float y = view.y + 6 + i * rh - g_scroll.off;
        if (y < 30 || y > 240) continue;
        Rect r = {10, y, 300, rh - 6};
        bool held = ui_held(r) && !ui_scroll_moved(&g_scroll);
        g_rrect(r.x, r.y, r.w, r.h, 8, held ? C_PANEL3 : C_PANEL2);
        g_disc(r.x + 22, r.y + r.h / 2, 28, g->color == WHITE ? RGB(235, 235, 235) : RGB(20, 20, 20));
        g_disc(r.x + 22, r.y + r.h / 2, 20, g->color == WHITE ? RGB(255, 255, 255) : RGB(70, 70, 70));
        g_textf(F_B13, r.x + 46, r.y + 14, C_TEXT, AL_LEFT, "%s", g->opp);
        g_textf(F_R11, r.x + 46, r.y + 31, C_TEXT3, AL_LEFT, "%s%s%s", g->variant, g->speed[0] ? "  -  " : "", g->speed);
        if (g->my_turn) { g_rrect(r.x + r.w - 80, r.y + 8, 72, 24, 12, C_GREEN_D); g_text(F_B11, r.x + r.w - 44, r.y + 20, C_TEXT, AL_CENTER, "Your turn"); }
        else g_text(F_R11, r.x + r.w - 10, r.y + 20, C_TEXT3, AL_RIGHT, "Waiting");
        if (ui_clicked(r) && !ui_scroll_moved(&g_scroll)) scene_game_open(g->id);
    }
    g_rect(0, 0, 320, 40, C_BG);
    ui_header("Games", ICON_LIST);
    if (ui_icon_button((Rect){250, 3, 32, 28}, ICON_REFRESH_CW, BS_GHOST)) api_refresh_ongoing();
    if (ui_icon_button((Rect){284, 3, 32, 28}, ICON_HOUSE, BS_GHOST)) scene_goto(SC_HOME);
    if (ov) ui_challenge_overlay();
    ui_set_enabled(true);
}

const Scene scene_games = {"games", games_enter, games_update, games_top, games_bot, NULL};
