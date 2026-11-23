// Boot (connect with the stored token) and Login scenes
#include "scenes.h"
#include "ui.h"

static float g_t;
static bool g_connecting;

static void boot_enter(void) {
    g_t = 0; g_connecting = false;
    if (g_set.token[0]) { api_connect(); g_connecting = true; }
}

static void boot_update(float dt) {
    g_t += dt;
    if (!g_connecting) { if (g_t > 0.7f) scene_goto(SC_LOGIN); return; }
    if (g_api.net == 2 && g_t > 0.7f) scene_goto(SC_HOME);
    if (g_api.token_rejected && g_t > 0.7f) scene_goto(SC_LOGIN);
}

static void logo_top(float y) {
    g_img(UI_LOGO_96, 200 - 48, y, 0, 1, 1);
}

static void boot_top(void) {
    ui_top_bg();
    logo_top(46);
    g_text(F_B21, 200, 160, C_TEXT, AL_CENTER, "Chess Quest 3DS");
    g_text(F_R13, 200, 184, C_TEXT3, AL_CENTER, "Lichess, FICS, bots, puzzles, learning");
}

static void boot_bot(void) {
    g_rect(0, 0, 320, 240, C_BG);
    if (g_connecting && g_api.net == -1 && !g_api.token_rejected) {
        g_icon(ICON_WIFI_OFF, 160, 60, 32, C_ORANGE);
        g_text_fit(F_B13, 160, 100, C_TEXT, AL_CENTER, 290, g_api.err[0] ? g_api.err : "Cannot reach Lichess");
        // certificate / TLS failures are almost always a wrong console clock (the certificate looks expired or not yet valid)
        int rc = g_api.err_rc; time_t now = time(NULL); struct tm *tm = localtime(&now);
        bool tls = rc == 35 || rc == 51 || rc == 58 || rc == 59 || rc == 60 || rc == 77 || rc == 83 || rc == 90;
        bool clock_bad = tm && tm->tm_year + 1900 < 2025;
        if (tls || clock_bad) {
            g_text(F_B11, 160, 116, C_ORANGE, AL_CENTER, "Check the date and time of your 3DS!");
            g_text(F_R11, 160, 131, C_TEXT3, AL_CENTER, "Home Menu > Settings > Other Settings > Date & Time");
            if (tm) g_textf(F_R11, 160, 146, C_TEXT2, AL_CENTER, "The console says: %04d-%02d-%02d  %02d:%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min);
            if (ui_button((Rect){16, 158, 92, 32}, "Retry", F_B11, BS_PRIMARY, ICON_REFRESH_CW)) { api_connect(); g_t = 0; }
            if (ui_button((Rect){114, 158, 92, 32}, "Token", F_B11, BS_SECONDARY, ICON_KEY_ROUND)) scene_goto(SC_LOGIN);
            if (ui_button((Rect){212, 158, 92, 32}, "Offline", F_B11, BS_SECONDARY, ICON_HOUSE)) scene_goto(SC_HOME);
            if (g_set.verify_tls) {
                if (ui_button((Rect){16, 196, 288, 34}, "Connect anyway (no certificate check)", F_B11, BS_SECONDARY, ICON_LOCK)) {
                    g_set.verify_tls = false; settings_save(); li_set_verify(false); api_connect(); g_t = 0;
                }
            } else g_text(F_R11, 160, 210, C_TEXT3, AL_CENTER, "Certificate check is off (Settings > Verify TLS)");
        } else {
            g_text(F_R11, 160, 118, C_TEXT3, AL_CENTER, "Check the Wi-Fi connection");
            if (ui_button((Rect){40, 134, 240, 36}, "Retry", F_B16, BS_PRIMARY, ICON_REFRESH_CW)) { api_connect(); g_t = 0; }
            if (ui_button((Rect){16, 178, 140, 34}, "Change token", F_B13, BS_SECONDARY, ICON_KEY_ROUND)) scene_goto(SC_LOGIN);
            if (ui_button((Rect){164, 178, 140, 34}, "Use offline", F_B13, BS_SECONDARY, ICON_HOUSE)) scene_goto(SC_HOME);
            g_text(F_R11, 160, 226, C_TEXT3, AL_CENTER, "Offline games, endgames, openings and analysis still work.");
        }
    } else {
        ui_spinner(160, 110, 22, C_GREEN, g_t);
        g_text(F_R13, 160, 160, C_TEXT2, AL_CENTER, g_connecting ? "Connecting to Lichess..." : "Starting...");
    }
}

const Scene scene_boot = {"boot", boot_enter, boot_update, boot_top, boot_bot, NULL};

// ------------------------------------------------------------------ login
static char g_msg[100];

static void login_enter(void) {
    g_msg[0] = 0;
    if (g_api.token_rejected) snprintf(g_msg, sizeof g_msg, "That token was rejected. Please check it.");
}

static void apply_token(const char *t) {
    snprintf(g_set.token, sizeof g_set.token, "%s", t);
    settings_save();
    g_api.token_rejected = false;
    scene_goto(SC_BOOT);
}

static void login_update(float dt) { (void)dt; }

static void login_top(void) {
    ui_top_bg();
    g_img(UI_LOGO_48, 24, 20, 0, 1, 1);
    g_text(F_B21, 84, 36, C_TEXT, AL_LEFT, "Sign in");
    g_text(F_R13, 84, 56, C_TEXT3, AL_LEFT, "A personal API token links your account");
    g_rrect(16, 84, 368, 132, 8, C_PANEL);
    g_text(F_B13, 30, 102, C_GREEN, AL_LEFT, "1");
    g_text(F_R13, 46, 102, C_TEXT, AL_LEFT, "On lichess.org open Preferences > API access tokens");
    g_text(F_B13, 30, 128, C_GREEN, AL_LEFT, "2");
    g_text(F_R13, 46, 128, C_TEXT, AL_LEFT, "Tick \"Play games with the board API\" and, for all");
    g_text(F_R11, 46, 143, C_TEXT2, AL_LEFT, "features: puzzles, follows, tournaments, challenges");
    g_text(F_B13, 30, 164, C_GREEN, AL_LEFT, "3");
    g_text(F_R13, 46, 164, C_TEXT, AL_LEFT, "Type it here, or save it as");
    g_text(F_R11, 46, 182, C_TEXT2, AL_LEFT, DATA_DIR "/token.txt");
    g_text(F_R11, 30, 198, C_TEXT3, AL_LEFT, "The token stays on this console and is only sent to Lichess servers.");
    g_text(F_R11, 30, 212, C_TEXT3, AL_LEFT, "No account needed for offline games, endgames, openings.");
}

static void login_bot(void) {
    g_rect(0, 0, 320, 240, C_BG);
    ui_header("Sign in", ICON_KEY_ROUND);
    if (g_msg[0]) g_text_fit(F_B11, 160, 52, C_ORANGE, AL_CENTER, 300, g_msg);
    if (ui_button((Rect){24, 62, 272, 44}, "Enter token", F_B16, BS_PRIMARY, ICON_KEY_ROUND)) {
        char t[96];
        if (ui_keyboard("lip_...", "", t, sizeof t) && t[0]) apply_token(t);
    }
    if (ui_button((Rect){24, 112, 272, 34}, "Load token.txt from SD card", F_B13, BS_SECONDARY, ICON_REFRESH_CW)) {
        size_t n; char *s = read_file(DATA_DIR "/token.txt", &n);
        if (s) {
            char *p = s; while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
            char *e = p + strlen(p); while (e > p && (e[-1] == ' ' || e[-1] == '\n' || e[-1] == '\r' || e[-1] == '\t')) *--e = 0;
            if (*p) apply_token(p); else snprintf(g_msg, sizeof g_msg, "token.txt is empty");
            free(s);
        } else snprintf(g_msg, sizeof g_msg, "No token.txt found in %s", DATA_DIR);
    }
    if (ui_button((Rect){24, 152, 132, 36}, "How to connect", F_B11, BS_SECONDARY, ICON_INFO)) guide_open(SC_LOGIN);
    if (ui_button((Rect){164, 152, 132, 36}, "Use without account", F_B11, BS_SECONDARY, ICON_HOUSE)) scene_goto(SC_HOME);
    g_text(F_R13, 28, 210, C_TEXT2, AL_LEFT, "Verify TLS certificates");
    if (ui_toggle((Rect){200, 196, 92, 28}, &g_set.verify_tls)) { settings_save(); li_set_verify(g_set.verify_tls); }
    g_text(F_R11, 160, 232, C_TEXT3, AL_CENTER, "Turn off only if connections fail (wrong clock).");
    ui_toast_draw(160);
}

const Scene scene_login = {"login", login_enter, login_update, login_top, login_bot, NULL};

// ------------------------------------------------------------------ connection guide (step by step)
typedef struct { const char *title; const char *l[8]; } GuidePage;
static const GuidePage GP[] = {
    {"1 / 5   What you need", {"Lichess does not use passwords inside apps.", "You create a personal token: a key that only", "allows the things you tick.", "You can delete it again on the website any time.", "", "You need a free Lichess account and a phone", "or computer (to create the token once).", ""}},
    {"2 / 5   Create the token", {"1. On a phone or PC open lichess.org and log in.", "2. Menu > Preferences > API access tokens.", "   Direct address: lichess.org/account/oauth/token", "3. Press the + button.", "4. Name it: 3DS", "", "Next page: which boxes to tick.", ""}},
    {"3 / 5   Tick these boxes", {"board:play  (needed to play games)", "challenge:read and challenge:write", "puzzle:read and puzzle:write", "follow:read and follow:write  (friends)", "tournament:write  (join arenas)", "", "The code is printed next to each box.", ""}},
    {"4 / 5   Get it onto the 3DS", {"Press Create. Lichess shows the token ONCE", "(it starts with lip_). Copy it now.", "", "On the 3DS: Enter token, type it with the keyboard.", "Or save it as a text file on the SD card:", "  sdmc:/lichess3ds/token.txt", "and press Load token.txt.", ""}},
    {"5 / 5   If it does not connect", {"- The 3DS must be connected to Wi-Fi.", "- Check Date & Time in the 3DS Settings:", "  a wrong clock breaks secure connections.", "- Token rejected: create a new one and check", "  the boxes from page 3.", "", "Without an account: offline games, endgames,", "openings and analysis all work."}},
};
static int g_gp;
static int g_guide_back = SC_LOGIN;

static void guide_enter(void) { g_gp = 0; }

static void guide_top(void) {
    ui_top_bg();
    g_rrect(8, 8, 384, 34, 8, C_PANEL);
    g_icon(ICON_INFO, 28, 25, 20, C_GREEN);
    g_text(F_B16, 46, 25, C_TEXT, AL_LEFT, GP[g_gp].title);
    for (int i = 0; i < 8; i++) if (GP[g_gp].l[i][0]) g_text_fit(F_R13, 20, 62 + i * 22, C_TEXT, AL_LEFT, 366, GP[g_gp].l[i]);
}

static void guide_bot(void) {
    g_rect(0, 0, 320, 240, C_BG);
    ui_header("Connect to Lichess", ICON_INFO);
    if (ui_icon_button((Rect){284, 3, 32, 28}, ICON_X, BS_GHOST)) scene_goto((SceneId)g_guide_back);
    int n = (int)(sizeof GP / sizeof GP[0]);
    for (int i = 0; i < n; i++) g_disc(160 + (i - (n - 1) / 2.0f) * 16, 64, 8, i == g_gp ? C_GREEN : C_PANEL3);
    if (g_gp > 0 && ui_button((Rect){12, 90, 140, 56}, "Back", F_B16, BS_SECONDARY, ICON_CHEVRON_LEFT)) g_gp--;
    if (g_gp < n - 1) { if (ui_button((Rect){168, 90, 140, 56}, "Next", F_B16, BS_PRIMARY, ICON_CHEVRON_RIGHT)) g_gp++; }
    else if (ui_button((Rect){168, 90, 140, 56}, "Enter token", F_B16, BS_PRIMARY, ICON_KEY_ROUND)) scene_goto(SC_LOGIN);
    if (ui_button((Rect){12, 160, 296, 34}, "Use the app without an account", F_B13, BS_SECONDARY, ICON_HOUSE)) scene_goto(SC_HOME);
    g_text(F_R11, 160, 214, C_TEXT3, AL_CENTER, "You can come back to this guide any time from");
    g_text(F_R11, 160, 228, C_TEXT3, AL_CENTER, "the Home screen: Connect.");
}
const Scene scene_guide = {"guide", guide_enter, NULL, guide_top, guide_bot, NULL};
void guide_open(SceneId back) { g_guide_back = back; scene_goto(SC_GUIDE); }
