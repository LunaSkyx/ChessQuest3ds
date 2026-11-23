// Learn hub: Puzzle Storm, opening trainer, endgame training
#include "scenes.h"
#include "ui.h"
#include "learn_data.h"

enum { V_MAIN, V_OPEN, V_END };
static int g_view;
static Scroll g_sc;

static void learn_enter(void) { g_view = V_MAIN; memset(&g_sc, 0, sizeof g_sc); }

static void learn_top(void) {
    ui_top_bg();
    g_icon(ICON_CROWN, 200, 56, 32, C_GREEN);
    g_text(F_B21, 200, 96, C_TEXT, AL_CENTER, "Learn");
    if (g_view == V_OPEN) {
        g_text(F_R13, 200, 130, C_TEXT2, AL_CENTER, "Opening trainer");
        g_text(F_R11, 200, 156, C_TEXT3, AL_CENTER, "Play the main line from memory.");
        g_text(F_R11, 200, 174, C_TEXT3, AL_CENTER, "The opponent answers automatically.");
    } else if (g_view == V_END) {
        g_text(F_R13, 200, 130, C_TEXT2, AL_CENTER, "Endgame training");
        g_text(F_R11, 200, 156, C_TEXT3, AL_CENTER, "You play the side to move against");
        g_text(F_R11, 200, 174, C_TEXT3, AL_CENTER, "the strongest engine level.");
    } else {
        g_text(F_R13, 200, 130, C_TEXT2, AL_CENTER, "Puzzle Storm, opening lines and endgames.");
        g_text(F_R11, 200, 160, C_TEXT3, AL_CENTER, "Game review: Analysis menu > Review game.");
        
    }
}

static void learn_bot(void) {
    g_rect(0, 0, 320, 240, C_BG);
    ui_header("Learn", ICON_CROWN);
    if (ui_icon_button((Rect){284, 3, 32, 28}, ICON_ARROW_LEFT, BS_GHOST)) { if (g_view == V_MAIN) scene_goto(SC_MORE); else g_view = V_MAIN; }
    if (g_view == V_MAIN) {
        if (ui_button((Rect){12, 46, 296, 52}, "Puzzle Storm  (3 min)", F_B16, BS_PRIMARY, ICON_ZAP)) puzzle_start_storm();
        if (ui_button((Rect){12, 106, 296, 52}, "Opening trainer", F_B16, BS_SECONDARY, ICON_CROWN)) { g_view = V_OPEN; memset(&g_sc, 0, sizeof g_sc); }
        if (ui_button((Rect){12, 166, 296, 52}, "Endgame training", F_B16, BS_SECONDARY, ICON_TARGET)) { g_view = V_END; memset(&g_sc, 0, sizeof g_sc); }
    } else {
        int n = g_view == V_OPEN ? OPEN_LINE_COUNT : ENDGAME_COUNT;
        Rect view = {0, 40, 320, 200};
        const float rh = 44;
        ui_scroll_update(&g_sc, view, 8 + n * rh);
        for (int i = 0; i < n; i++) {
            float y = view.y + 6 + i * rh - g_sc.off;
            if (y < 30 || y > 240) continue;
            Rect r = {10, y, 300, rh - 5};
            bool held = ui_held(r) && !ui_scroll_moved(&g_sc);
            g_rrect(r.x, r.y, r.w, r.h, 8, held ? C_PANEL3 : C_PANEL2);
            if (g_view == V_OPEN) {
                g_disc(r.x + 16, r.y + r.h / 2, 9, OPEN_LINES[i].color == WHITE ? RGB(240, 240, 240) : RGB(30, 30, 30));
                g_text_fit(F_B13, r.x + 34, r.y + 14, C_TEXT, AL_LEFT, 250, OPEN_LINES[i].name);
                g_text(F_R11, r.x + 34, r.y + 29, C_TEXT3, AL_LEFT, OPEN_LINES[i].color == WHITE ? "you play White" : "you play Black");
            } else {
                g_text_fit(F_B13, r.x + 12, r.y + 14, C_TEXT, AL_LEFT, 270, ENDGAMES[i].name);
                g_text_fit(F_R11, r.x + 12, r.y + 29, C_TEXT3, AL_LEFT, 270, ENDGAMES[i].goal);
            }
            if (ui_clicked(r) && !ui_scroll_moved(&g_sc)) {
                if (g_view == V_OPEN) puzzle_start_opening(i);
                else scene_game_open_offline_fen(8, ENDGAMES[i].fen);
            }
        }
        g_rect(0, 0, 320, 40, C_BG);
        ui_header(g_view == V_OPEN ? "Opening trainer" : "Endgames", ICON_CROWN);
        if (ui_icon_button((Rect){284, 3, 32, 28}, ICON_ARROW_LEFT, BS_GHOST)) g_view = V_MAIN;
    }
    ui_toast_draw(160);
}
const Scene scene_learn = {"learn", learn_enter, NULL, learn_top, learn_bot, NULL};
