// Bot ladder: pick a named opponent, beat it to unlock the next one (all offline, built-in engine)
#include "scenes.h"
#include "ui.h"
#include "bots.h"

static int g_sel;
static Scroll g_sc;

static void bots_enter(void) {
    memset(&g_sc, 0, sizeof g_sc);
    g_sel = bot_next_target(g_set.bots_beaten);
    g_sc.off = (g_sel > 2 ? (float)(g_sel - 2) * 44.0f : 0.0f);
}

static u32 col_of(const Bot *b, bool locked) {
    if (locked) return RGB(70, 68, 64);
    return RGB((b->color >> 16) & 255, (b->color >> 8) & 255, b->color & 255);
}

static void avatar(float x, float y, float r, const Bot *b, bool locked, bool beaten) {
    g_disc(x, y, r * 2 + 4, locked ? RGB(50, 48, 45) : RGB(255, 255, 255));
    g_disc(x, y, r * 2, col_of(b, locked));
    g_icon(locked ? ICON_LOCK : ICON_BOT, x, y, r * 1.2f, locked ? C_TEXT3 : RGB(255, 255, 255));
    if (beaten) { g_disc(x + r * 0.9f, y + r * 0.9f, r * 0.9f, C_GREEN_D); g_icon(ICON_CHECK, x + r * 0.9f, y + r * 0.9f, r * 0.7f, RGB(255, 255, 255)); }
}

static void stars(float x, float y, int n, bool left) {
    for (int i = 0; i < 5; i++) g_disc((left ? x : x - 4 * 14) + i * 14 + 5, y, 8, i < n ? C_ORANGE : RGB(70, 68, 64));
}

static void bots_top(void) {
    ui_top_bg();
    uint32_t m = g_set.bots_beaten;
    const Bot *b = &BOTS[g_sel];
    bool locked = !bot_is_unlocked(m, g_sel), beaten = bot_is_beaten(m, g_sel);
    g_rrect(8, 8, 384, 224, 10, C_PANEL);
    avatar(66, 78, 34, b, locked, beaten);
    g_text_fit(F_B21, 124, 54, C_TEXT, AL_LEFT, 260, locked ? "???" : b->name);
    g_text_fit(F_R13, 124, 80, C_TEXT2, AL_LEFT, 262, locked ? "Locked" : b->tag);
    stars(124, 106, locked ? 0 : b->stars, true);
    g_text(F_R11, 124, 126, C_TEXT3, AL_LEFT, locked ? "Beat the previous bot to unlock." : (beaten ? "Beaten - play again for fun." : "Ready to play."));
    // progress
    int n = bot_beaten_count(m);
    g_textf(F_B13, 24, 168, C_TEXT, AL_LEFT, "Ladder: %d / %d beaten", n, BOT_COUNT);
    g_rrect(24, 182, 352, 12, 6, C_PANEL3);
    if (n) g_rrect(24, 182, 352.0f * (float)n / BOT_COUNT, 12, 6, C_GREEN);
    g_text(F_R11, 24, 208, C_TEXT3, AL_LEFT, "No takebacks. Win to unlock the next bot.");
    g_text(F_R11, 24, 222, C_TEXT3, AL_LEFT, "Bots get stronger and think longer as you climb.");
}

static void bots_bot(void) {
    g_rect(0, 0, 320, 240, C_BG);
    Rect view = {0, 38, 320, 162};
    const float rh = 44;
    ui_scroll_update(&g_sc, view, 6 + BOT_COUNT * rh);
    uint32_t m = g_set.bots_beaten;
    for (int i = 0; i < BOT_COUNT; i++) {
        const Bot *b = &BOTS[i];
        float y = view.y + 4 + i * rh - g_sc.off;
        if (y < 28 || y > 200) continue;
        bool locked = !bot_is_unlocked(m, i), beaten = bot_is_beaten(m, i);
        Rect r = {8, y, 304, rh - 4};
        bool held = ui_held(r) && !ui_scroll_moved(&g_sc);
        g_rrect(r.x, r.y, r.w, r.h, 8, i == g_sel ? C_GREEN_D : (held ? C_PANEL3 : C_PANEL2));
        avatar(r.x + 22, r.y + r.h / 2, 12, b, locked, beaten);
        g_text_fit(F_B13, r.x + 48, r.y + 13, locked ? C_TEXT3 : C_TEXT, AL_LEFT, 170, locked ? "Locked" : b->name);
        g_text_fit(F_R11, r.x + 48, r.y + 28, C_TEXT3, AL_LEFT, 170, locked ? "Beat the bot above" : b->tag);
        if (!locked) stars(r.x + r.w - 10, r.y + r.h / 2, b->stars, false);
        if (ui_clicked(r) && !ui_scroll_moved(&g_sc)) g_sel = i;
    }
    g_rect(0, 0, 320, 38, C_BG);
    g_rect(0, 200, 320, 40, C_BG);
    ui_header("Bot ladder", ICON_BOT);
    if (ui_icon_button((Rect){284, 3, 32, 28}, ICON_ARROW_LEFT, BS_GHOST)) scene_goto(SC_HOME);
    if (ui_button((Rect){182, 4, 96, 28}, "Custom", F_B11, BS_SECONDARY, ICON_SETTINGS)) { scene_play_set_mode(g_api.net == 2 ? 1 : 4); scene_goto(SC_PLAY); }
    static const char *CN[3] = {"Random", "White", "Black"};
    if (ui_button((Rect){8, 206, 92, 30}, CN[g_set.color % 3], F_B11, BS_SECONDARY, -1)) g_set.color = (g_set.color + 1) % 3;
    bool locked = !bot_is_unlocked(m, g_sel);
    char l[48]; snprintf(l, sizeof l, "Play %s", locked ? "(locked)" : BOTS[g_sel].name);
    if (ui_button((Rect){106, 206, 206, 30}, l, F_B13, locked ? BS_DARK : BS_PRIMARY, ICON_PLAY)) {
        if (locked) ui_toast("Beat the bot above first");
        else { settings_save(); scene_game_open_bot(g_sel, g_set.color); }
    }
    ui_toast_draw(160);
}

const Scene scene_bots = {"bots", bots_enter, NULL, bots_top, bots_bot, NULL};
