// Play: quick pairing, vs computer, challenge a friend
#include "scenes.h"
#include "ui.h"
#include "fics.h"

static const TimeCtl TCS[11] = {{60, 0}, {120, 1}, {180, 0}, {180, 2}, {300, 0}, {300, 3}, {600, 0}, {600, 5}, {900, 10}, {1800, 0}, {1800, 20}};
#define TC_CUSTOM 11
static const int CUSTOM_MIN_SEC[] = {15, 30, 45, 60, 90, 120, 180, 240, 300, 360, 420, 480, 600, 900, 1200, 1500, 1800, 2400, 2700, 3600, 5400, 7200, 10800};
#define N_CUSTOM_MIN ((int)(sizeof CUSTOM_MIN_SEC / sizeof CUSTOM_MIN_SEC[0]))
static const int CUSTOM_INC[] = {0, 1, 2, 3, 5, 10, 15, 20, 30, 45, 60};
#define N_CUSTOM_INC ((int)(sizeof CUSTOM_INC / sizeof CUSTOM_INC[0]))

static int g_mode;                 // 0 quick, 1 computer, 2 friend, 3 FICS, 4 offline
static const char *OFF_NAMES[8] = {"Beginner", "Casual", "Novice", "Club", "Solid", "Sharp", "Tough", "Maximum"};
static int g_cmin = 12, g_cinc = 0, g_cdays = 0; // custom indices (g_cdays: index into DAYS, 0 = real-time)
static const int DAYS[] = {0, 1, 2, 3, 5, 7, 10, 14};
#define N_DAYS ((int)(sizeof DAYS / sizeof DAYS[0]))
static bool g_pick_variant, g_pick_custom, g_pick_fics;
static bool g_fics_wait;           // FICS: connecting, the seek is posted once we are logged in
static float g_search_t;
static u64 g_ai_wait_until;
static bool g_autostart;

void scene_play_set_mode(int mode) { g_mode = mode; }  // (the tile selection is validated in play_enter)
void scene_play_autostart(void) { g_autostart = true; }

static TimeCtl cur_tc(void) {
    if (g_set.last_tc == TC_CUSTOM) return (TimeCtl){CUSTOM_MIN_SEC[g_cmin], CUSTOM_INC[g_cinc], (g_mode == 1 || g_mode == 4) ? 0 : DAYS[g_cdays]};
    return TCS[g_set.last_tc < 0 || g_set.last_tc > 10 ? 6 : g_set.last_tc];
}

static int tc_category(TimeCtl t) {                     // 0 bullet 1 blitz 2 rapid 3 classical (Lichess estimates 40 moves)
    if (t.days > 0) return 3;
    int est = t.limit_sec + 40 * t.inc_sec;
    return est < 180 ? 0 : (est < 480 ? 1 : (est < 1500 ? 2 : 3));
}
static const char *CAT_NAMES[4] = {"Bullet", "Blitz", "Rapid", "Classical"};
static const int CAT_ICONS[4] = {ICON_BOLT, ICON_ZAP, ICON_TIMER, ICON_TURTLE};

static void tc_label(TimeCtl t, char *out, size_t n) {
    if (t.days > 0) { snprintf(out, n, "%dd", t.days); return; }
    if (t.limit_sec < 60 || t.limit_sec % 60) snprintf(out, n, "%d:%02d+%d", t.limit_sec / 60, t.limit_sec % 60, t.inc_sec);
    else snprintf(out, n, "%d+%d", t.limit_sec / 60, t.inc_sec);
}

// Lichess only lets third-party apps pair real-time seeks of Rapid or slower (estimated >= 8 min). Verified against the live API.
static bool seek_blocked(TimeCtl t) { return t.days == 0 && t.limit_sec + 40 * t.inc_sec < 480; }

static bool searching(void) { return g_api.seeking || g_api.chal_out_wait || now_ms() < g_ai_wait_until || g_fics_wait || (g_mode == 3 && g_fics.seeking); }

static void start(void) {
    TimeCtl tc = cur_tc();
    Variant v = (Variant)g_set.variant;
    memset(&g_last_params, 0, sizeof g_last_params);
    g_last_params.tc = tc; g_last_params.variant = v; g_last_params.rated = g_set.rated && g_mode != 1; g_last_params.color = g_set.color;
    settings_save();
    g_search_t = 0;
    if (g_mode == 4) {                                        // offline: no network involved
        g_last_params.backend = 2; g_last_params.variant = VAR_STANDARD; g_last_params.rated = false; g_last_params.ai_level = g_set.off_level;
        scene_game_open_offline(g_set.off_level, !g_set.off_noclock, tc, g_set.color);
        return;
    }
    if (g_mode == 3) {                                        // FICS: connect (guest or account) and post a seek
        g_last_params.backend = 1; g_last_params.variant = VAR_STANDARD;
        bool guest = g_set.fics_guest || !g_set.fics_user[0];
        g_last_params.rated = g_set.rated && !guest;
        if (g_fics.state == FICS_ONLINE) fics_seek((tc.limit_sec + 30) / 60 > 0 ? (tc.limit_sec + 30) / 60 : 1, tc.inc_sec, g_last_params.rated, g_set.color);
        else { fics_connect(guest, g_set.fics_user, g_set.fics_pass); g_fics_wait = true; }
        return;
    }
    if (g_mode == 0) {
        if (seek_blocked(tc)) { ui_toast("Pairing needs Rapid or slower (Lichess rule)"); return; }
        api_seek(tc, g_set.rated, v, g_set.color);
    }
    else if (g_mode == 1) { g_last_params.ai_level = g_set.ai_level; api_challenge_ai(g_set.ai_level, tc, v, g_set.color); g_ai_wait_until = now_ms() + 12000; }
    else {
        if (!g_set.friend_name[0]) { ui_toast("Enter a username first"); return; }
        snprintf(g_last_params.opp, sizeof g_last_params.opp, "%.31s", g_set.friend_name);
        api_challenge_user(g_set.friend_name, tc, g_set.rated, v, g_set.color);
    }
}

static void cancel(void) { api_seek_cancel(); api_challenge_cancel(); g_ai_wait_until = 0; fics_unseek(); g_fics_wait = false; }

static void play_enter(void) {
    if (g_mode == 0 && g_set.last_tc != TC_CUSTOM && seek_blocked(cur_tc()) && !g_autostart) g_set.last_tc = 6;
    g_pick_variant = g_pick_custom = g_pick_fics = false;
    g_search_t = 0;
    if (g_autostart) { g_autostart = false; start(); }
}

static void play_update(float dt) {
    if (searching()) g_search_t += dt;
    if (g_fics_wait) {
        if (g_fics.state == FICS_ONLINE) {
            g_fics_wait = false;
            TimeCtl tc = cur_tc();
            int mn = (tc.limit_sec + 30) / 60; if (mn < 1) mn = 1;
            fics_seek(mn, tc.inc_sec, g_last_params.rated, g_set.color);
        } else if (g_fics.state == FICS_ERROR) { g_fics_wait = false; ui_toast(g_fics.err[0] ? g_fics.err : "Could not reach FICS"); }
        else if (g_fics.state == FICS_OFF) g_fics_wait = false;
    }
    if (g_api.notice_new) { g_api.notice_new = false; ui_toast(g_api.notice); g_ai_wait_until = 0; }
}

static void play_leave(void) { if (searching() && !g_api.start_pending && !g_fics.start_pending) cancel(); }

// ------------------------------------------------------------------ drawing
static void play_top(void) {
    ui_top_bg();
    TimeCtl tc = cur_tc(); char lbl[24]; tc_label(tc, lbl, sizeof lbl);
    int cat = tc_category(tc);
    static const char *MODES[5] = {"Quick pairing", "Play the computer", "Challenge a friend", "FICS - freechess.org", "Offline - built-in engine"};
    g_text(F_B13, 200, 26, C_TEXT3, AL_CENTER, MODES[g_mode]);
    g_rrect(100, 40, 200, 96, 12, C_PANEL);
    g_icon(CAT_ICONS[cat], 200, 62, 24, C_GREEN);
    g_text(F_B34, 200, 94, C_TEXT, AL_CENTER, lbl);
    g_text(F_B13, 200, 122, C_TEXT2, AL_CENTER, CAT_NAMES[cat]);
    char info[96];
    const char *vn = variant_name((Variant)g_set.variant);
    if (g_mode == 4) snprintf(info, sizeof info, "Standard  -  Level %d (%s)  -  %s", g_set.off_level, OFF_NAMES[(g_set.off_level - 1) & 7], g_set.off_noclock ? "no clock" : "with clock");
    else if (g_mode == 3) snprintf(info, sizeof info, "Standard  -  %s", (g_set.fics_guest || !g_set.fics_user[0]) ? "guest, unrated" : (g_set.rated ? "rated" : "unrated"));
    else if (g_mode == 1) snprintf(info, sizeof info, "%s  -  Stockfish level %d  -  casual", vn, g_set.ai_level);
    else snprintf(info, sizeof info, "%s  -  %s", vn, g_set.rated ? "rated" : "casual");
    g_text(F_R13, 200, 156, C_TEXT, AL_CENTER, info);
    static const char *COL[3] = {"Random colour", "Play as White", "Play as Black"};
    g_text(F_R13, 200, 176, C_TEXT3, AL_CENTER, COL[g_set.color % 3]);
    if (searching()) {
        g_rect(0, 196, 400, 44, RGBA(0, 0, 0, 90));
        ui_spinner(34, 218, 9, C_GREEN, g_search_t);
        char l[80];
        if (g_fics_wait && g_fics.state != FICS_ONLINE) snprintf(l, sizeof l, "Connecting to FICS...");
        else if (g_mode == 3) snprintf(l, sizeof l, "Searching on FICS...");
        else if (g_api.chal_out_wait) snprintf(l, sizeof l, "Waiting for %s...", g_api.chal_out_name);
        else if (g_api.seeking) snprintf(l, sizeof l, "Searching for an opponent...");
        else snprintf(l, sizeof l, "Starting game...");
        g_text_fit(F_B16, 58, 218, C_TEXT, AL_LEFT, 280, l);
        g_textf(F_R13, 384, 218, C_TEXT3, AL_RIGHT, "%d:%02d", (int)g_search_t / 60, (int)g_search_t % 60);
    }
}

static void variant_picker(void) {
    g_rect(0, 0, 320, 240, RGBA(0, 0, 0, 170));
    g_rrect(8, 12, 304, 216, 12, C_PANEL);
    g_text(F_B16, 160, 30, C_TEXT, AL_CENTER, "Variant");
    for (int i = 0; i < VAR_COUNT; i++) {
        Rect r = {16 + (i % 3) * 97.0f, 46 + (i / 3) * 52.0f, 93, 46};
        BtnStyle st = i == g_set.variant ? BS_TILE_SEL : BS_TILE;
        if (ui_button(r, NULL, F_B11, st, -1)) { g_set.variant = i; g_pick_variant = false; }
        g_text_fit(F_B11, r.x + r.w / 2, r.y + 20, C_TEXT, AL_CENTER, r.w - 6, variant_name((Variant)i));
    }
    if (ui_button((Rect){100, 196, 120, 26}, "Close", F_B13, BS_SECONDARY, -1)) g_pick_variant = false;
}

static void stepper(float y, const char *label, char *val, bool *dec, bool *inc) {
    g_text(F_R13, 24, y + 16, C_TEXT2, AL_LEFT, label);
    *dec = ui_icon_button((Rect){150, y, 40, 32}, ICON_MINUS, BS_SECONDARY);
    g_text(F_B16, 226, y + 16, C_TEXT, AL_CENTER, val);
    *inc = ui_icon_button((Rect){262, y, 40, 32}, ICON_PLUS, BS_SECONDARY);
}

static void custom_picker(void) {
    g_rect(0, 0, 320, 240, RGBA(0, 0, 0, 170));
    g_rrect(8, 24, 304, 196, 12, C_PANEL);
    g_text(F_B16, 160, 46, C_TEXT, AL_CENTER, "Custom time control");
    int sec = CUSTOM_MIN_SEC[g_cmin];
    char a[24], b[24];
    if (sec < 60) snprintf(a, sizeof a, "%d s", sec); else if (sec % 60) snprintf(a, sizeof a, "%.1f min", sec / 60.0); else snprintf(a, sizeof a, "%d min", sec / 60);
    snprintf(b, sizeof b, "%d s", CUSTOM_INC[g_cinc]);
    bool d, i;
    if (g_mode != 1 && g_mode != 4) {
        char dd[16]; if (DAYS[g_cdays]) snprintf(dd, sizeof dd, "%d day%s", DAYS[g_cdays], DAYS[g_cdays] > 1 ? "s" : ""); else snprintf(dd, sizeof dd, "off");
        stepper(104 + 42, "Correspondence", dd, &d, &i);
        if (d && g_cdays > 0) g_cdays--;
        if (i && g_cdays < N_DAYS - 1) g_cdays++;
    }
    stepper(62, "Time", a, &d, &i);
    if (d && g_cmin > 0) g_cmin--;
    if (i && g_cmin < N_CUSTOM_MIN - 1) g_cmin++;
    stepper(104, "Increment", b, &d, &i);
    if (d && g_cinc > 0) g_cinc--;
    if (i && g_cinc < N_CUSTOM_INC - 1) g_cinc++;
    TimeCtl tc = {CUSTOM_MIN_SEC[g_cmin], CUSTOM_INC[g_cinc], (g_mode == 1 || g_mode == 4) ? 0 : DAYS[g_cdays]};
    char l[24]; tc_label(tc, l, sizeof l);
    g_textf(F_R13, 160, 148, C_TEXT3, AL_CENTER, "%s  -  %s", l, tc.days ? "Correspondence" : CAT_NAMES[tc_category(tc)]);
    if (ui_button((Rect){60, 172, 200, 32}, "Done", F_B16, BS_PRIMARY, ICON_CHECK)) { g_set.last_tc = TC_CUSTOM; g_pick_custom = false; }
}

static void fics_login_flow(void) {
    char t[40], pw[48];
    if (!ui_keyboard("FICS username", g_set.fics_user, t, sizeof t)) return;
    if (!ui_keyboard_ex("FICS password", "", pw, sizeof pw, true)) return;
    snprintf(g_set.fics_user, sizeof g_set.fics_user, "%s", t);
    snprintf(g_set.fics_pass, sizeof g_set.fics_pass, "%s", pw);
    g_set.fics_guest = false;
    if (g_fics.state == FICS_ONLINE) fics_disconnect();           // reconnect with the new login next time
    settings_save();
}

static void fics_picker(void) {
    g_rect(0, 0, 320, 240, RGBA(0, 0, 0, 170));
    g_rrect(8, 14, 304, 212, 12, C_PANEL);
    g_text(F_B16, 160, 32, C_TEXT, AL_CENTER, "FICS account");
    g_text(F_R11, 160, 50, C_TEXT3, AL_CENTER, "freechess.org: free server, any time control (even 3+0)");
    bool guest = g_set.fics_guest || !g_set.fics_user[0];
    if (ui_button((Rect){20, 62, 280, 36}, "Play as guest (unrated)", F_B13, guest ? BS_TILE_SEL : BS_TILE, ICON_USER)) {
        g_set.fics_guest = true; settings_save(); if (g_fics.state == FICS_ONLINE && !g_fics.guest) fics_disconnect();
    }
    char l[64]; snprintf(l, sizeof l, "%s", g_set.fics_user[0] ? g_set.fics_user : "Sign in with a FICS account");
    if (ui_button((Rect){20, 104, 280, 36}, l, F_B13, !guest ? BS_TILE_SEL : BS_TILE, ICON_KEY_ROUND)) {
        if (g_set.fics_user[0]) { g_set.fics_guest = false; settings_save(); if (g_fics.state == FICS_ONLINE && g_fics.guest) fics_disconnect(); }
        else fics_login_flow();
    }
    if (g_set.fics_user[0] && ui_button((Rect){20, 146, 280, 28}, "Change account login", F_B11, BS_SECONDARY, -1)) fics_login_flow();
    g_text(F_R11, 160, 182, C_TEXT3, AL_CENTER, "Stored on this console's SD card.");
    g_text(F_R11, 160, 194, C_TEXT3, AL_CENTER, "FICS itself does not encrypt passwords.");
    if (ui_button((Rect){100, 202, 120, 22}, "Done", F_B13, BS_PRIMARY, ICON_CHECK)) g_pick_fics = false;
}

static void play_bot(void) {
    g_rect(0, 0, 320, 240, C_BG);
    bool ov = g_api.chal_in_pending, modal = g_pick_variant || g_pick_custom || g_pick_fics;
    ui_set_enabled(!ov && !modal);

    if (searching()) {
        ui_header("Play", ICON_SWORDS);
        ui_spinner(160, 96, 26, C_GREEN, g_search_t);
        g_text(F_B16, 160, 150, C_TEXT, AL_CENTER, (g_fics_wait && g_fics.state != FICS_ONLINE) ? "Connecting to FICS" : (g_mode == 2 ? "Waiting for your friend" : (g_mode == 1 ? "Starting game" : "Finding an opponent")));
        if (ui_button((Rect){70, 176, 180, 40}, "Cancel", F_B16, BS_DANGER, ICON_X)) cancel();
        if (ov) ui_challenge_overlay();
        ui_set_enabled(true);
        ui_toast_draw(160);
        return;
    }

    ui_header("Play", ICON_SWORDS);
    if (ui_icon_button((Rect){284, 3, 32, 28}, ICON_HOUSE, BS_GHOST)) scene_goto(SC_HOME);
    // mode tabs
    // tabs: Bots opens the offline bot ladder (no clock); Custom = Stockfish on Lichess or the engine on this console, with time controls
    static const char *TABS[5] = {"Quick", "Bots", "Friend", "FICS", "Custom"};
    for (int i = 0; i < 5; i++) {
        bool sel = i == 4 ? (g_mode == 1 || g_mode == 4) : (i != 1 && i == g_mode);
        if (ui_button((Rect){8 + i * 61.0f, 38, 58, 28}, TABS[i], F_B11, sel ? BS_TILE_SEL : BS_TILE, -1)) {
            if (i == 1) { scene_goto(SC_BOTS); return; }
            if (i == 4) { if (g_mode != 1 && g_mode != 4) g_mode = g_api.net == 2 ? 1 : 4; }
            else g_mode = i;
            if (g_mode == 0 && g_set.last_tc != TC_CUSTOM && seek_blocked(cur_tc())) g_set.last_tc = 6;
        }
    }
    // time control tiles 4 x 3
    for (int i = 0; i < 12; i++) {
        Rect r = {8 + (i % 4) * 78.0f, 72 + (i / 4) * 35.0f, 74, 31};
        char l[24];
        TimeCtl t = i == TC_CUSTOM ? cur_tc() : TCS[i];
        if (i == TC_CUSTOM) snprintf(l, sizeof l, "Custom"); else tc_label(t, l, sizeof l);
        BtnStyle st = g_set.last_tc == i ? BS_TILE_SEL : BS_TILE;
        int icon = i == TC_CUSTOM ? ICON_SETTINGS : CAT_ICONS[tc_category(t)];
        if (g_mode == 0 && i != TC_CUSTOM && seek_blocked(t)) { st = BS_DARK; icon = ICON_LOCK; }
        if (ui_button(r, l, F_B13, st, icon)) {
            if (i == TC_CUSTOM) g_pick_custom = true;
            else if (g_mode == 0 && seek_blocked(t)) ui_toast("Bullet/Blitz: play the computer or a friend");
            else g_set.last_tc = i;
        }
    }
    // options row
    bool fguest = g_set.fics_guest || !g_set.fics_user[0];
    if (g_mode == 4) { if (ui_button((Rect){8, 178, 108, 26}, g_set.off_noclock ? "No clock" : "With clock", F_B11, BS_SECONDARY, ICON_TIMER)) g_set.off_noclock = !g_set.off_noclock; }
    else if (g_mode == 3) { if (ui_button((Rect){8, 178, 108, 26}, fguest ? "Guest" : g_set.fics_user, F_B11, BS_SECONDARY, ICON_USER)) g_pick_fics = true; }
    else if (ui_button((Rect){8, 178, 108, 26}, variant_name((Variant)g_set.variant), F_B11, BS_SECONDARY, -1)) g_pick_variant = true;
    static const char *CN[3] = {"Random", "White", "Black"};
    if (ui_button((Rect){120, 178, 90, 26}, CN[g_set.color % 3], F_B11, BS_SECONDARY, -1)) g_set.color = (g_set.color + 1) % 3;
    if (g_mode == 4) {
        char l[24]; snprintf(l, sizeof l, "Level %d", g_set.off_level);
        if (ui_button((Rect){214, 178, 98, 26}, l, F_B11, BS_SECONDARY, ICON_BOT)) g_set.off_level = g_set.off_level % 8 + 1;
    } else if (g_mode == 1) {
        char l[24]; snprintf(l, sizeof l, "Level %d", g_set.ai_level);
        if (ui_button((Rect){214, 178, 98, 26}, l, F_B11, BS_SECONDARY, ICON_BOT)) g_set.ai_level = g_set.ai_level % 8 + 1;
    } else if (g_mode == 3 && fguest) {
        ui_button((Rect){214, 178, 98, 26}, "Unrated", F_B11, BS_DARK, -1);
    } else {
        if (ui_button((Rect){214, 178, 98, 26}, g_set.rated ? "Rated" : (g_mode == 3 ? "Unrated" : "Casual"), F_B11, g_set.rated ? BS_TILE_SEL : BS_SECONDARY, -1)) g_set.rated = !g_set.rated;
    }
    // start row
    if (g_mode == 2) {
        char l[60]; snprintf(l, sizeof l, "%s", g_set.friend_name[0] ? g_set.friend_name : "Username...");
        if (ui_button((Rect){8, 208, 140, 28}, l, F_B11, BS_DARK, ICON_USER)) {
            char t[40]; if (ui_keyboard("Lichess username", g_set.friend_name, t, sizeof t)) { snprintf(g_set.friend_name, sizeof g_set.friend_name, "%s", t); settings_save(); }
        }
        if (ui_button((Rect){154, 208, 158, 28}, "Challenge", F_B13, BS_PRIMARY, ICON_SWORDS)) start();
    } else if (g_mode == 3 && g_fics.in_game) {
        if (ui_button((Rect){8, 208, 304, 28}, "Resume FICS game", F_B13, BS_PRIMARY, ICON_PLAY)) scene_game_open_fics();
    } else {
        if (g_mode == 1 || g_mode == 4) {                     // custom: choose where the engine runs
            if (ui_button((Rect){8, 208, 150, 28}, g_mode == 1 ? "Stockfish (online)" : "Engine (this 3DS)", F_B11, BS_SECONDARY, ICON_BOT)) {
                if (g_mode == 4) { if (g_api.net == 2) g_mode = 1; else ui_toast("Stockfish needs a Lichess connection"); }
                else g_mode = 4;
            }
            if (ui_button((Rect){162, 208, 150, 28}, "Start game", F_B13, BS_PRIMARY, ICON_PLAY)) start();
        } else if (ui_button((Rect){8, 208, 304, 28}, (g_mode == 0 || g_mode == 3) ? "Start search" : "Start game", F_B13, BS_PRIMARY, ICON_PLAY)) start();
    }
    if (g_pick_custom) { ui_set_enabled(true); custom_picker(); }
    if (g_pick_variant) { ui_set_enabled(true); variant_picker(); }
    if (g_pick_fics) { ui_set_enabled(true); fics_picker(); }
    if (ov) ui_challenge_overlay();
    ui_set_enabled(true);
    ui_toast_draw(160);
}

const Scene scene_play = {"play", play_enter, play_update, play_top, play_bot, play_leave};
