// Settings: board theme / piece set preview on top, scrolling option list on the bottom
#include "scenes.h"
#include "ui.h"
#include "board.h"
#include "audio.h"

static Scroll g_scroll;
static bool g_dirty, g_about;

static void set_enter(void) { memset(&g_scroll, 0, sizeof g_scroll); g_dirty = false; g_about = false; gfx_set_piece_set(g_set.piece_set); }
static void set_leave(void) { if (g_dirty) settings_save(); }
static void set_update(float dt) { (void)dt; }

static void preview(void) {
    ui_top_bg();
    int sq = 28, x0 = 24, y0 = 8;
    board_draw_mini(x0, y0, sq, g_set.board_theme, g_set.piece_set);
    // sample position: a few pieces, last-move tint and a legal-move dot, so the theme can be judged
    g_rect((float)(x0 + 4 * sq), (float)(y0 + 4 * sq), (float)sq, (float)sq, RGBA(246, 240, 90, 150));
    g_rect((float)(x0 + 4 * sq), (float)(y0 + 3 * sq), (float)sq, (float)sq, RGBA(246, 240, 90, 150));
    static const struct { int f, r, c, t; } P[] = {
        {0, 0, WHITE, ROOK}, {4, 0, WHITE, KING}, {3, 1, WHITE, PAWN}, {4, 3, WHITE, PAWN}, {2, 2, WHITE, KNIGHT}, {5, 2, WHITE, BISHOP}, {3, 0, WHITE, QUEEN},
        {4, 7, BLACK, KING}, {3, 7, BLACK, QUEEN}, {1, 6, BLACK, PAWN}, {4, 4, BLACK, PAWN}, {5, 5, BLACK, KNIGHT}, {7, 7, BLACK, ROOK}, {2, 5, BLACK, BISHOP}};
    for (size_t i = 0; i < sizeof P / sizeof P[0]; i++)
        g_piece(0, P[i].c, P[i].t, (float)(x0 + P[i].f * sq - 1), (float)(y0 + (7 - P[i].r) * sq - 1), 0, (float)sq / 30.0f + 0.07f);
    g_disc((float)(x0 + 6 * sq + sq / 2), (float)(y0 + 5 * sq + sq / 2), 9, RGBA(20, 20, 20, 70));
    g_rrect(264, 14, 124, 100, 8, C_PANEL);
    g_text(F_B11, 326, 28, C_TEXT3, AL_CENTER, "BOARD");
    g_text_fit(F_B16, 326, 48, C_TEXT, AL_CENTER, 112, BOARD_THEMES[g_set.board_theme % BOARD_THEME_COUNT].name);
    g_text(F_B11, 326, 76, C_TEXT3, AL_CENTER, "PIECES");
    g_text_fit(F_B16, 326, 96, C_TEXT, AL_CENTER, 112, PIECE_SET_NAMES[g_set.piece_set % PIECE_SET_COUNT]);
}

typedef enum { R_CYCLE_BOARD, R_CYCLE_PIECE, R_TOGGLE, R_SLIDER, R_BUTTON } RowKind;
typedef struct { const char *label; RowKind kind; bool *flag; int *val; } Row;

static void about_overlay(void) {
    g_rect(0, 0, 320, 240, RGBA(0, 0, 0, 190));
    g_rrect(8, 8, 304, 224, 12, C_PANEL);
    g_text(F_B16, 160, 26, C_TEXT, AL_CENTER, "About");
    static const char *L[] = {
        "Chess Quest 3DS v" APP_VERSION, "Unofficial client for lichess.org and", "FICS (not affiliated with either).", "",
        "Chess pieces: free sets from lila", "(Cburnett, Merida, Chessnut, RhosGFX).",
        "Icons: Lucide (ISC). Lichess is free", "and open source software (AGPLv3).", "",
        "Your API token is only stored on this", "console and only sent to Lichess servers."};
    for (int i = 0; i < (int)(sizeof L / sizeof L[0]); i++) g_text(F_R13, 160, 48 + i * 15.0f, C_TEXT2, AL_CENTER, L[i]);
    if (ui_button((Rect){100, 196, 120, 28}, "Close", F_B13, BS_PRIMARY, -1)) g_about = false;
}

static void set_bot(void) {
    g_rect(0, 0, 320, 240, C_BG);
    bool ov = g_api.chal_in_pending;
    ui_set_enabled(!ov && !g_about);
    Row rows[] = {
        {"Board", R_CYCLE_BOARD, NULL, &g_set.board_theme},
        {"Pieces", R_CYCLE_PIECE, NULL, &g_set.piece_set},
        {"Sound", R_TOGGLE, &g_set.sound, NULL},
        {"Volume", R_SLIDER, NULL, &g_set.volume},
        {"Coordinates", R_TOGGLE, &g_set.coords, NULL},
        {"Premoves", R_TOGGLE, &g_set.premoves, NULL},
        {"Move animations", R_TOGGLE, &g_set.animations, NULL},
        {"Legal move dots", R_TOGGLE, &g_set.legal_dots, NULL},
        {"Auto-queen", R_TOGGLE, &g_set.auto_queen, NULL},
        {"Ignore draw offers", R_TOGGLE, &g_set.hide_draw, NULL},
        {"Board on left", R_TOGGLE, &g_set.board_left, NULL},
        {"Verify TLS", R_TOGGLE, &g_set.verify_tls, NULL},
        {"Check for updates", R_TOGGLE, &g_set.check_updates, NULL},
        {"About", R_BUTTON, NULL, NULL},
    };
    const int n = (int)(sizeof rows / sizeof rows[0]);
    const float rh = 38;
    Rect view = {0, 40, 320, 200};
    ui_scroll_update(&g_scroll, view, 8 + n * rh);
    bool moved = ui_scroll_moved(&g_scroll);
    for (int i = 0; i < n; i++) {
        float y = view.y + 4 + i * rh - g_scroll.off;
        if (y < 28 || y > 240) continue;
        Row *r = &rows[i];
        Rect rr = {8, y, 304, rh - 4};
        g_rrect(rr.x, rr.y, rr.w, rr.h, 8, C_PANEL);
        g_text(F_B13, 20, y + (rh - 4) / 2, C_TEXT, AL_LEFT, r->label);
        float cy = y + (rh - 4) / 2;
        switch (r->kind) {
        case R_CYCLE_BOARD: case R_CYCLE_PIECE: {
            int cnt = r->kind == R_CYCLE_BOARD ? BOARD_THEME_COUNT : PIECE_SET_COUNT;
            const char *nm = r->kind == R_CYCLE_BOARD ? BOARD_THEMES[*r->val % cnt].name : PIECE_SET_NAMES[*r->val % cnt];
            bool l = ui_icon_button((Rect){112, y + 3, 30, rh - 10}, ICON_CHEVRON_LEFT, BS_SECONDARY);
            bool rt = ui_icon_button((Rect){276, y + 3, 30, rh - 10}, ICON_CHEVRON_RIGHT, BS_SECONDARY);
            g_text_fit(F_B13, 209, cy, C_TEXT, AL_CENTER, 128, nm);
            if ((l || rt) && !moved) {
                *r->val = (*r->val + (rt ? 1 : cnt - 1)) % cnt;
                if (r->kind == R_CYCLE_PIECE) gfx_set_piece_set(*r->val);
                g_dirty = true;
            }
            break;
        }
        case R_TOGGLE:
            if (ui_toggle((Rect){rr.x + 150, y, 146, rh - 4}, r->flag) && !moved) { g_dirty = true; if (r->flag == &g_set.verify_tls) li_set_verify(g_set.verify_tls); if (r->flag == &g_set.sound && g_set.sound) audio_play(SND_MOVE_SELF); }
            break;
        case R_SLIDER: {
            int old = *r->val;
            if (ui_slider((Rect){150, y, 152, rh - 4}, r->val, 0, 100) && old != *r->val) g_dirty = true;
            if (g_touch.released && g_touch.sx >= 150 && g_touch.sy >= y && g_touch.sy < y + rh) audio_play(SND_CAPTURE);
            break;
        }
        case R_BUTTON:
            if (ui_button((Rect){228, y + 3, 76, rh - 10}, "Open", F_B11, BS_SECONDARY, ICON_INFO) && !moved) g_about = true;
            break;
        }
    }
    g_rect(0, 0, 320, 40, C_BG);
    ui_header("Settings", ICON_SETTINGS);
    if (ui_icon_button((Rect){284, 3, 32, 28}, ICON_HOUSE, BS_GHOST)) scene_goto(SC_HOME);
    if (g_about) { ui_set_enabled(true); about_overlay(); }
    if (ov) ui_challenge_overlay();
    ui_set_enabled(true);
}

const Scene scene_settings = {"settings", set_enter, set_update, preview, set_bot, set_leave};
