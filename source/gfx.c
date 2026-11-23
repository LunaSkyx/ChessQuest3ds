#include "gfx.h"
#include <math.h>

#define Z 0.5f

static C3D_RenderTarget *g_top, *g_bot;
static C2D_SpriteSheet g_ui_sheet, g_piece_sheet;
static int g_piece_set = -1;
typedef struct { u16 cp, x, y; u8 w, h; s8 xo, yo; u16 adv; } __attribute__((packed)) FGlyph;
typedef struct {
    C2D_SpriteSheet sheet; C3D_Tex *tex;
    int n, lh, asc, cap, tw, th;
    FGlyph *g; Tex3DS_SubTexture *sub;
    short ascii[128];
} BFont;
static BFont g_fonts[F_COUNT];
static C2D_SpriteSheet g_board_tex[3];
const char *PIECE_SET_NAMES[PIECE_SET_COUNT] = {"cburnett", "staunty", "cardinal", "chessnut", "merida", "rhosgfx"};
static const char *FONT_FILES[F_COUNT] = {"r11", "r13", "r16", "b11", "b13", "b16", "b21", "b34"};

// screenshot state
static char g_cap_base[200];
static bool g_cap_pending, g_cap_finished;
static int g_cap_delay;

static bool font_load(BFont *f, const char *name) {
    char p[80]; snprintf(p, sizeof p, "romfs:/fonts/%s.fnt", name);
    size_t len; u8 *d = (u8 *)read_file(p, &len);
    if (!d || len < 16 || memcmp(d, "LFNT", 4)) { free(d); return false; }
    u16 *h = (u16 *)(d + 4);
    f->lh = h[0]; f->asc = h[1]; f->cap = h[2]; f->n = h[3]; f->tw = h[4]; f->th = h[5];
    f->g = malloc(sizeof(FGlyph) * (size_t)f->n);
    memcpy(f->g, d + 16, sizeof(FGlyph) * (size_t)f->n);
    free(d);
    snprintf(p, sizeof p, "romfs:/fonts/%s.t3x", name);
    f->sheet = C2D_SpriteSheetLoad(p);
    if (!f->sheet) return false;
    C2D_Image im = C2D_SpriteSheetGetImage(f->sheet, 0);
    f->tex = im.tex;
    C3D_TexSetFilter(f->tex, GPU_NEAREST, GPU_NEAREST);
    f->sub = calloc((size_t)f->n, sizeof(Tex3DS_SubTexture));
    for (int i = 0; i < 128; i++) f->ascii[i] = -1;
    for (int i = 0; i < f->n; i++) {
        FGlyph *g = &f->g[i];
        Tex3DS_SubTexture *s = &f->sub[i];
        s->width = g->w; s->height = g->h;
        s->left = (float)g->x / (float)f->tw; s->right = (float)(g->x + g->w) / (float)f->tw;
        s->top = 1.0f - (float)g->y / (float)f->th; s->bottom = 1.0f - (float)(g->y + g->h) / (float)f->th;
        if (g->cp < 128) f->ascii[g->cp] = (short)i;
    }
    return true;
}

bool gfx_init(void) {
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(8192);
    C2D_Prepare();
    gfxSet3D(false);
    g_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    g_bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_ui_sheet = C2D_SpriteSheetLoad("romfs:/gfx/ui.t3x");
    if (!g_ui_sheet) { LOGF("ui.t3x failed to load"); return false; }
    C3D_TexSetFilter(C2D_SpriteSheetGetImage(g_ui_sheet, 0).tex, GPU_LINEAR, GPU_LINEAR);
    for (int i = 0; i < F_COUNT; i++) if (!font_load(&g_fonts[i], FONT_FILES[i])) { LOGF("font %s failed", FONT_FILES[i]); return false; }
    LOGF("gfx ready: ui images=%d", (int)C2D_SpriteSheetCount(g_ui_sheet));
    return true;
}

void gfx_exit(void) {
    if (g_piece_sheet) C2D_SpriteSheetFree(g_piece_sheet);
    for (int i = 0; i < 3; i++) if (g_board_tex[i]) C2D_SpriteSheetFree(g_board_tex[i]);
    if (g_ui_sheet) C2D_SpriteSheetFree(g_ui_sheet);
    for (int i = 0; i < F_COUNT; i++) { free(g_fonts[i].g); free(g_fonts[i].sub); if (g_fonts[i].sheet) C2D_SpriteSheetFree(g_fonts[i].sheet); }
    C2D_Fini(); C3D_Fini();
}

// ------------------------------------------------------------------ BMP capture
static void write_bmp(const char *path, const u8 *rgb, int w, int h) {   // rgb = top-down, 3 bytes/pixel
    FILE *f = fopen(path, "wb");
    if (!f) return;
    int row = (w * 3 + 3) & ~3, size = 54 + row * h;
    u8 hdr[54] = {'B', 'M'};
    *(u32 *)(hdr + 2) = (u32)size; *(u32 *)(hdr + 10) = 54; *(u32 *)(hdr + 14) = 40;
    *(s32 *)(hdr + 18) = w; *(s32 *)(hdr + 22) = h; *(u16 *)(hdr + 26) = 1; *(u16 *)(hdr + 28) = 24;
    *(u32 *)(hdr + 34) = (u32)(row * h);
    fwrite(hdr, 1, 54, f);
    u8 *line = calloc(1, (size_t)row);
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) { const u8 *p = rgb + ((size_t)y * w + x) * 3; line[x * 3] = p[2]; line[x * 3 + 1] = p[1]; line[x * 3 + 2] = p[0]; }
        fwrite(line, 1, (size_t)row, f);
    }
    free(line); fclose(f);
}

static void capture_target(C3D_RenderTarget *t, int sw, const char *path) {
    // read the finished display framebuffer (240 x sw, column major, bottom-left origin, 3 bytes per pixel)
    (void)t;
    u16 fw, fh;
    const u8 *fb = gfxGetFramebuffer(sw == 400 ? GFX_TOP : GFX_BOTTOM, GFX_LEFT, &fw, &fh);
    u8 *rgb = malloc((size_t)sw * 240 * 3);
    if (!rgb) return;
    for (int x = 0; x < sw; x++)
        for (int y = 0; y < 240; y++) {
            const u8 *p = fb + ((size_t)x * 240 + (239 - y)) * 3;      // memory order B G R
            u8 *o = rgb + ((size_t)y * sw + x) * 3;
            o[0] = p[2]; o[1] = p[1]; o[2] = p[0];
        }
    write_bmp(path, rgb, sw, 240);
    free(rgb);
}

void gfx_request_capture(const char *base) {
    snprintf(g_cap_base, sizeof g_cap_base, "%s", base);
    g_cap_pending = true; g_cap_finished = false; g_cap_delay = 4;
}
bool gfx_capture_done(void) { return g_cap_finished; }

void gfx_frame_start(void) {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);        // returns once the previous frame finished rendering
    if (g_cap_pending && g_cap_delay > 0) g_cap_delay--;
    else if (g_cap_pending) {
        char p[220];
        snprintf(p, sizeof p, "%s_top.bmp", g_cap_base); capture_target(g_top, 400, p);
        snprintf(p, sizeof p, "%s_bottom.bmp", g_cap_base); capture_target(g_bot, 320, p);
        g_cap_pending = false; g_cap_finished = true;
    }
}
void gfx_scene_top(u32 clear) { C2D_TargetClear(g_top, clear); C2D_SceneBegin(g_top); }
void gfx_scene_bottom(u32 clear) { C2D_TargetClear(g_bot, clear); C2D_SceneBegin(g_bot); }
void gfx_frame_end(void) { C3D_FrameEnd(0); }
void gfx_frame_pause(void) { C3D_FrameEnd(0); }
void gfx_frame_resume_bottom(void) { C3D_FrameBegin(C3D_FRAME_SYNCDRAW); C2D_SceneBegin(g_bot); }

// ------------------------------------------------------------------ primitives
void g_rect(float x, float y, float w, float h, u32 c) { if (w > 0 && h > 0) C2D_DrawRectSolid(x, y, Z, w, h, c); }
void g_grad(float x, float y, float w, float h, u32 t, u32 b) { C2D_DrawRectangle(x, y, Z, w, h, t, t, b, b); }

static inline C2D_Image ui_img(int idx) { return C2D_SpriteSheetGetImage(g_ui_sheet, (size_t)idx); }

void g_img(int idx, float x, float y, u32 tint, float sx, float sy) {
    C2D_Image im = ui_img(idx);
    if (tint) {
        C2D_ImageTint t; C2D_PlainImageTint(&t, tint, 1.0f);
        C2D_DrawImageAt(im, x, y, Z, &t, sx, sy);
    } else C2D_DrawImageAt(im, x, y, Z, NULL, sx, sy);
}

static int radius_slot(int r) {
    int best = 0, bd = 999;
    for (int i = 0; i < (int)(sizeof UI_RADII / sizeof UI_RADII[0]); i++) { int d = abs(UI_RADII[i] - r); if (d < bd) { bd = d; best = i; } }
    return best;
}

void g_rrect(float x, float y, float w, float h, int r, u32 c) {
    // snap to whole pixels: fractional positions make the linear-filtered corner sprites bleed (thin seam lines)
    float x1 = floorf(x + w + 0.5f), y1 = floorf(y + h + 0.5f);
    x = floorf(x + 0.5f); y = floorf(y + 0.5f); w = x1 - x; h = y1 - y;
    if (w <= 0 || h <= 0) return;
    int maxr = (int)(fminf(w, h) / 2);
    if (r > maxr) r = maxr;
    if (r < 3) { g_rect(x, y, w, h, c); return; }
    int slot = radius_slot(r);
    r = UI_RADII[slot];
    if (r * 2 > w || r * 2 > h) { g_rect(x, y, w, h, c); return; }
    int base = UI_CORNER_3_TL + slot * 4;                       // tl, tr, bl, br
    // the fills overlap the corner sprites by 1px: bilinear sampling at the sprite edges otherwise leaves faint seam lines
    // (so translucent fills would show a slightly denser line - panels are drawn opaque)
    g_rect(x + r - 1, y, w - 2 * r + 2, h, c);
    g_rect(x, y + r - 1, r, h - 2 * r + 2, c);
    g_rect(x + w - r, y + r - 1, r, h - 2 * r + 2, c);
    g_img(base + 0, x, y, c, 1, 1);
    g_img(base + 1, x + w - r, y, c, 1, 1);
    g_img(base + 2, x, y + h - r, c, 1, 1);
    g_img(base + 3, x + w - r, y + h - r, c, 1, 1);
}
void g_rrect_r(Rect r, int rad, u32 c) { g_rrect(r.x, r.y, r.w, r.h, rad, c); }

void g_shadow(float x, float y, float w, float h, int r, int spread) {
    for (int i = spread; i >= 1; i--)
        g_rrect(x - i, y - i + 2, w + 2 * i, h + 2 * i, r + i, RGBA(0, 0, 0, 18));
}

void g_disc(float cx, float cy, float d, u32 c) {
    int best = 0, bd = 999;
    for (int i = 0; i < (int)(sizeof UI_DISCS / sizeof UI_DISCS[0]); i++) { int dd = abs(UI_DISCS[i] - (int)d); if (dd < bd) { bd = dd; best = i; } }
    int size = UI_DISCS[best];
    float s = d / (float)size;
    g_img(UI_DISC_6 + best, cx - d / 2, cy - d / 2, c, s, s);
}

void g_icon(int icon, float cx, float cy, int size, u32 col) {
    int j = size <= 16 ? 0 : (size <= 24 ? 1 : 2);
    int px = j == 0 ? 16 : (j == 1 ? 24 : 32);
    g_img(UI_IC_MENU_16 + icon * 3 + j, cx - px / 2.0f, cy - px / 2.0f, col, 1, 1);
}

void g_line(float x0, float y0, float x1, float y1, float th, u32 c) { C2D_DrawLine(x0, y0, c, x1, y1, c, th, Z); }
void g_tri(float x0, float y0, float x1, float y1, float x2, float y2, u32 c) { C2D_DrawTriangle(x0, y0, c, x1, y1, c, x2, y2, c, Z); }
void g_arrow(float x0, float y0, float x1, float y1, float th, u32 c) {
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len < 1) return;
    float ux = dx / len, uy = dy / len, hl = th * 2.6f, hw = th * 1.7f;
    float bx = x1 - ux * hl, by = y1 - uy * hl;
    g_line(x0, y0, bx, by, th, c);
    g_tri(x1, y1, bx - uy * hw, by + ux * hw, bx + uy * hw, by - ux * hw, c);
}

// ------------------------------------------------------------------ text
static int glyph_index(const BFont *f, u32 cp) {
    if (cp < 128 && f->ascii[cp] >= 0) return f->ascii[cp];
    int lo = 0, hi = f->n - 1;
    while (lo <= hi) { int m = (lo + hi) / 2; if (f->g[m].cp == cp) return m; if (f->g[m].cp < cp) lo = m + 1; else hi = m - 1; }
    return f->ascii['?'];
}
static u32 utf8_next(const char **ps) {
    const unsigned char *s = (const unsigned char *)*ps;
    u32 c = *s++;
    if (c >= 0xF0) { c = ((c & 7) << 18) | ((u32)(s[0] & 63) << 12) | ((u32)(s[1] & 63) << 6) | (s[2] & 63); s += 3; }
    else if (c >= 0xE0) { c = ((c & 15) << 12) | ((u32)(s[0] & 63) << 6) | (s[1] & 63); s += 2; }
    else if (c >= 0xC0) { c = ((c & 31) << 6) | (s[0] & 63); s += 1; }
    *ps = (const char *)s;
    return c;
}

float g_text_w(FontId fid, const char *s) {
    if (!s) return 0;
    const BFont *f = &g_fonts[fid];
    float w = 0;
    while (*s) w += f->g[glyph_index(f, utf8_next(&s))].adv / 16.0f;
    return w;
}

float g_font_h(FontId f) { return (float)g_fonts[f].cap; }   // cap height

void g_text(FontId fid, float x, float y, u32 col, int align, const char *s) {
    if (!s || !*s) return;
    const BFont *f = &g_fonts[fid];
    float w = g_text_w(fid, s);
    float pen = align == AL_CENTER ? x - w / 2 : (align == AL_RIGHT ? x - w : x);
    pen = floorf(pen + 0.5f);
    float base = floorf(y + f->cap / 2.0f + 0.5f);
    C2D_ImageTint tint; C2D_PlainImageTint(&tint, col, 1.0f);
    while (*s) {
        const FGlyph *g = &f->g[glyph_index(f, utf8_next(&s))];
        if (g->w) {
            C2D_Image im = {f->tex, &f->sub[g - f->g]};
            C2D_DrawImageAt(im, floorf(pen + 0.5f) + g->xo, base + g->yo, Z, &tint, 1.0f, 1.0f);
        }
        pen += g->adv / 16.0f;
    }
}

void g_textf(FontId f, float x, float y, u32 col, int align, const char *fmt, ...) {
    char buf[256];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    g_text(f, x, y, col, align, buf);
}

void g_text_fit(FontId f, float x, float y, u32 col, int align, float maxw, const char *s) {
    if (g_text_w(f, s) <= maxw) { g_text(f, x, y, col, align, s); return; }
    char buf[96]; snprintf(buf, sizeof buf, "%s", s);
    int n = (int)strlen(buf);
    while (n > 1) {
        n--;
        while (n > 1 && ((unsigned char)buf[n] & 0xC0) == 0x80) n--;       // do not cut inside a UTF-8 sequence
        buf[n] = 0;
        char tmp[100]; snprintf(tmp, sizeof tmp, "%s\xE2\x80\xA6", buf);
        if (g_text_w(f, tmp) <= maxw) { g_text(f, x, y, col, align, tmp); return; }
    }
}

// ------------------------------------------------------------------ pieces / boards
bool gfx_set_piece_set(int set) {
    if (set < 0 || set >= PIECE_SET_COUNT) set = 0;
    if (set == g_piece_set && g_piece_sheet) return true;
    char p[64]; snprintf(p, sizeof p, "romfs:/gfx/pieces_%s.t3x", PIECE_SET_NAMES[set]);
    C2D_SpriteSheet s = C2D_SpriteSheetLoad(p);
    if (!s) { LOGF("piece set %s failed", p); return false; }
    C3D_TexSetFilter(C2D_SpriteSheetGetImage(s, 0).tex, GPU_LINEAR, GPU_LINEAR);
    if (g_piece_sheet) C2D_SpriteSheetFree(g_piece_sheet);
    g_piece_sheet = s; g_piece_set = set;
    return true;
}

C2D_Image gfx_piece(int size_idx, int color, int type) {
    return C2D_SpriteSheetGetImage(g_piece_sheet, (size_t)(size_idx * 12 + color * 6 + (type - 1)));
}

void g_piece(int size_idx, int color, int type, float x, float y, u32 alpha_tint, float scale) {
    if (!g_piece_sheet || type < PAWN || type > KING) return;
    C2D_Image im = gfx_piece(size_idx, color, type);
    if (alpha_tint) {
        C2D_ImageTint t; C2D_AlphaImageTint(&t, (alpha_tint >> 24) / 255.0f);
        C2D_DrawImageAt(im, x, y, Z, &t, scale, scale);
    } else C2D_DrawImageAt(im, x, y, Z, NULL, scale, scale);
}

C2D_Image gfx_board_texture(const char *name) {
    static const char *names[3] = {"wood", "walnut", "marble"};
    for (int i = 0; i < 3; i++) {
        if (strcmp(name, names[i])) continue;
        if (!g_board_tex[i]) {
            char p[64]; snprintf(p, sizeof p, "romfs:/gfx/board_%s.t3x", name);
            g_board_tex[i] = C2D_SpriteSheetLoad(p);
        }
        if (g_board_tex[i]) return C2D_SpriteSheetGetImage(g_board_tex[i], 0);
    }
    C2D_Image none = {0};
    return none;
}
