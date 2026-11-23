#include "ui.h"
#include "audio.h"
#include <math.h>

static bool g_enabled = true;
void ui_set_enabled(bool e) { g_enabled = e; }
static bool inside_both(Rect r) { return g_enabled && !g_touch.consumed && rect_hit(r, g_touch.sx, g_touch.sy) && rect_hit(r, g_touch.x, g_touch.y); }
bool ui_held(Rect r) { return g_touch.down && inside_both(r); }
bool ui_clicked(Rect r) { return g_touch.released && inside_both(r); }

static u32 shade(u32 c, int d) {
    int r = (int)(c & 255) + d, g = (int)((c >> 8) & 255) + d, b = (int)((c >> 16) & 255) + d;
    r = r < 0 ? 0 : (r > 255 ? 255 : r); g = g < 0 ? 0 : (g > 255 ? 255 : g); b = b < 0 ? 0 : (b > 255 ? 255 : b);
    return RGBA(r, g, b, c >> 24);
}

void ui_card(Rect r, u32 col) { g_rrect_r(r, 8, col); }

bool ui_button(Rect r, const char *label, FontId f, BtnStyle st, int icon) {
    bool held = ui_held(r);
    u32 face, lip, fg = C_TEXT;
    switch (st) {
    case BS_PRIMARY:   face = C_GREEN; lip = C_GREEN_D; break;
    case BS_DANGER:    face = C_RED; lip = RGB(150, 40, 32); break;
    case BS_TILE:      face = C_PANEL2; lip = C_PANEL; fg = C_TEXT; break;
    case BS_TILE_SEL:  face = C_GREEN_D; lip = RGB(40, 75, 20); break;
    case BS_DARK:      face = C_BG2; lip = RGB(20, 19, 18); break;
    case BS_GHOST:     face = 0; lip = 0; fg = C_TEXT2; break;
    default:           face = C_PANEL3; lip = C_PANEL2; break;
    }
    float dy = held ? 2.0f : 0.0f;
    if (st != BS_GHOST) {
        g_rrect(r.x, r.y + 3, r.w, r.h - 1, 8, lip);
        g_rrect(r.x, r.y + dy, r.w, r.h - 3, 8, held ? shade(face, 14) : face);
    } else if (held) g_rrect(r.x, r.y, r.w, r.h, 8, RGBA(255, 255, 255, 25));
    float cy = r.y + dy + (r.h - 3) / 2.0f;
    float tw = (label && *label) ? g_text_w(f, label) : 0;
    int isz = (r.h < 36 && tw > 0) ? 16 : 24;
    float iw = icon >= 0 ? (float)isz : 0, gap = (icon >= 0 && tw > 0) ? 5.0f : 0;
    float x0 = r.x + r.w / 2.0f - (iw + gap + tw) / 2.0f;
    if (icon >= 0) g_icon(icon, x0 + iw / 2, cy, isz, fg);
    if (tw > 0) g_text(f, x0 + iw + gap, cy, fg, AL_LEFT, label);
    if (ui_clicked(r) && fabsf((float)(g_touch.x - g_touch.sx)) < 14 && fabsf((float)(g_touch.y - g_touch.sy)) < 14) { audio_play(SND_CLICK); g_touch.consumed = true; return true; }
    return false;
}

bool ui_icon_button(Rect r, int icon, BtnStyle st) { return ui_button(r, NULL, F_B16, st, icon); }

bool ui_toggle(Rect r, bool *v) {
    float w = 44, h = 24, x = r.x + r.w - w, y = r.y + (r.h - h) / 2;
    bool on = *v;
    g_rrect(x, y, w, h, 12, on ? C_GREEN : C_PANEL3);
    g_disc(x + (on ? w - 12 : 12), y + h / 2, 18, C_TEXT);
    if (ui_clicked(r)) { *v = !*v; audio_play(SND_CLICK); g_touch.consumed = true; return true; }
    return false;
}

bool ui_slider(Rect r, int *v, int lo, int hi) {
    float ty = r.y + r.h / 2 - 3, tw = r.w - 16, tx = r.x + 8;
    float k = (float)(*v - lo) / (float)(hi - lo);
    g_rrect(tx, ty, tw, 6, 3, C_PANEL3);
    g_rrect(tx, ty, tw * k, 6, 3, C_GREEN);
    g_disc(tx + tw * k, ty + 3, 20, C_TEXT);
    if (g_enabled && g_touch.down && rect_hit(r, g_touch.sx, g_touch.sy)) {
        float nk = clampf(((float)g_touch.x - tx) / tw, 0, 1);
        int nv = lo + (int)(nk * (float)(hi - lo) + 0.5f);
        if (nv != *v) { *v = nv; return true; }
    }
    return false;
}

void ui_header(const char *title, int icon) {
    g_grad(0, 0, 320, 36, C_PANEL, C_BG);
    g_rect(0, 35, 320, 1, RGBA(0, 0, 0, 90));
    float x = 12;
    if (icon >= 0) { g_icon(icon, 22, 18, 24, C_GREEN); x = 40; }
    g_text(F_B16, x, 18, C_TEXT, AL_LEFT, title);
}

// ------------------------------------------------------------------ scrolling
static bool g_scroll_moved;
void ui_scroll_update(Scroll *s, Rect view, float content_h) {
    float max_off = content_h > view.h ? content_h - view.h : 0;
    if (g_enabled && g_touch.pressed && rect_hit(view, g_touch.x, g_touch.y)) { s->drag = true; s->last_y = (float)g_touch.y; s->vel = 0; g_scroll_moved = false; }
    if (s->drag && g_touch.down) {
        float dy = (float)g_touch.y - s->last_y;
        if (fabsf((float)(g_touch.y - g_touch.sy)) > 8) g_scroll_moved = true;
        s->off -= dy; s->vel = -dy * 60.0f; s->last_y = (float)g_touch.y;
    } else {
        s->drag = false;
        s->off += s->vel / 60.0f; s->vel *= 0.92f;
        if (fabsf(s->vel) < 3) s->vel = 0;
    }
    if (s->off < 0) { s->off = 0; s->vel = 0; }
    if (s->off > max_off) { s->off = max_off; s->vel = 0; }
}
bool ui_scroll_moved(const Scroll *s) { (void)s; return g_scroll_moved; }

// ------------------------------------------------------------------ toast
static char g_toast[120];
static u64 g_toast_t0;
void ui_toast(const char *msg) { snprintf(g_toast, sizeof g_toast, "%s", msg); g_toast_t0 = now_ms(); }
void ui_toast_draw(float xc) {
    if (!g_toast[0]) return;
    float age = (float)(now_ms() - g_toast_t0) / 1000.0f;
    if (age > 3.2f) { g_toast[0] = 0; return; }
    float a = age < 0.15f ? age / 0.15f : (age > 2.8f ? (3.2f - age) / 0.4f : 1.0f);
    float maxw = (xc < 200 ? xc * 2 : 400) - 12;
    float w = clampf(g_text_w(F_B13, g_toast) + 28, 120, maxw);
    float y = 8 + (1.0f - a) * -10;
    g_rrect(xc - w / 2, y, w, 28, 8, RGBA(20, 19, 18, (int)(235 * a)));
    g_text_fit(F_B13, xc, y + 14, WITH_A(C_TEXT, (int)(255 * a)), AL_CENTER, w - 20, g_toast);
}

bool ui_keyboard(const char *hint, const char *initial, char *out, size_t n) { return ui_keyboard_ex(hint, initial, out, n, false); }

bool ui_keyboard_ex(const char *hint, const char *initial, char *out, size_t n, bool password) {
    SwkbdState kb;
    swkbdInit(&kb, SWKBD_TYPE_WESTERN, 2, (int)n - 1);
    if (password) swkbdSetPasswordMode(&kb, SWKBD_PASSWORD_HIDE_DELAY);
    swkbdSetHintText(&kb, hint);
    if (initial && *initial) swkbdSetInitialText(&kb, initial);
    swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    char tmp[128] = {0};
    // The keyboard is a system applet: it must not be launched in the middle of a rendered frame (the console froze
    // on return), so the frame in progress is finished first and a new one is started afterwards.
    LOGF("keyboard: open (%s)", hint);
    gfx_frame_pause();
    SwkbdButton b = swkbdInputText(&kb, tmp, sizeof tmp);
    gfx_frame_resume_bottom();
    LOGF("keyboard: closed (button %d)", (int)b);
    if (b != SWKBD_BUTTON_CONFIRM) return false;
    snprintf(out, n, "%s", tmp);
    return true;
}

void ui_top_bg(void) {
    g_grad(0, 0, 400, 240, C_BG2, C_BG);
    for (int r = 0; r < 6; r++)
        for (int f = 0; f < 10; f++)
            if ((r + f) & 1) g_rect((float)(f * 40), (float)(r * 40), 40, 40, RGBA(255, 255, 255, 5));
}

void ui_spinner(float cx, float cy, float r, u32 col, float t) {
    for (int i = 0; i < 8; i++) {
        float a = (float)i * 0.785398f - t * 6.0f;
        float k = (float)((i + (int)(t * 8)) & 7) / 7.0f;
        g_disc(cx + cosf(a) * r, cy + sinf(a) * r, 6, WITH_A(col, 60 + (int)(195 * k)));
    }
}
