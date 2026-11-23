#include "board.h"
#include <math.h>

const BoardTheme BOARD_THEMES[] = {
    {"Green",    RGB(238, 238, 210), RGB(118, 150, 86), NULL},
    {"Brown",    RGB(240, 217, 181), RGB(181, 136, 99), NULL},
    {"Blue",     RGB(222, 227, 230), RGB(140, 162, 173), NULL},
    {"Purple",   RGB(239, 239, 239), RGB(136, 119, 183), NULL},
    {"Ice",      RGB(232, 241, 247), RGB(125, 158, 189), NULL},
    {"Coral",    RGB(244, 225, 205), RGB(196, 114, 98), NULL},
    {"Gray",     RGB(228, 229, 231), RGB(123, 127, 133), NULL},
    {"Midnight", RGB(165, 165, 181), RGB(72, 76, 102), NULL},
    {"Wood",     RGB(236, 208, 164), RGB(176, 124, 82), "wood"},
    {"Walnut",   RGB(196, 150, 104), RGB(120, 78, 52), "walnut"},
    {"Marble",   RGB(232, 232, 236), RGB(120, 128, 140), "marble"},
};
const int BOARD_THEME_COUNT = sizeof BOARD_THEMES / sizeof BOARD_THEMES[0];

#define C_LAST      RGBA(246, 240, 90, 150)
#define C_SELECT    RGBA(246, 240, 90, 190)
#define C_PREMOVE   RGBA(214, 62, 58, 150)
#define C_HOVER     RGBA(255, 255, 255, 90)

void board_reset(BoardView *v) {
    memset(v, 0, sizeof *v);
    v->sel = -1; v->last_from = v->last_to = -1; v->check_sq = -1; v->drag_sq = -1; v->cursor = -1;
}
void board_set_flip(BoardView *v, bool f) { v->flipped = f; }

void board_anim_move(BoardView *v, int from, int to, uint8_t piece, uint8_t victim, float dur) {
    for (int i = 0; i < 6; i++)
        if (!v->anim[i].active) {
            v->anim[i] = (BoardAnim){piece, victim, (int8_t)from, (int8_t)to, 0, dur, true};
            return;
        }
}
void board_anim_clear(BoardView *v) { for (int i = 0; i < 6; i++) v->anim[i].active = false; }
bool board_animating(const BoardView *v) { for (int i = 0; i < 6; i++) if (v->anim[i].active) return true; return false; }

void board_update(BoardView *v, float dt) {
    for (int i = 0; i < 6; i++) {
        if (!v->anim[i].active) continue;
        v->anim[i].t += dt;
        if (v->anim[i].t >= v->anim[i].dur) v->anim[i].active = false;
    }
}

static void sq_topleft(const BoardView *v, int sq, float *x, float *y) {
    int f = FILE_OF(sq), r = RANK_OF(sq);
    int col = v->flipped ? 7 - f : f, row = v->flipped ? r : 7 - r;
    *x = (float)(v->ox + col * SQ_PX); *y = (float)(v->oy + row * SQ_PX);
}
void board_sq_center(const BoardView *v, int sq, float *cx, float *cy) {
    float x, y; sq_topleft(v, sq, &x, &y); *cx = x + SQ_PX / 2.0f; *cy = y + SQ_PX / 2.0f;
}
int board_sq_at(const BoardView *v, int px, int py) {
    int x = px - v->ox, y = py - v->oy;
    if (x < 0 || y < 0 || x >= BOARD_PX || y >= BOARD_PX) return -1;
    int col = x / SQ_PX, row = y / SQ_PX;
    int f = v->flipped ? 7 - col : col, r = v->flipped ? row : 7 - row;
    return SQ(f, r);
}

static bool light_square(int sq) { return ((FILE_OF(sq) + RANK_OF(sq)) & 1) != 0; }

static void draw_squares(const BoardView *v, const BoardTheme *th) {
    C2D_Image tex = {0};
    if (th->tex) tex = gfx_board_texture(th->tex);
    if (tex.tex) {
        C2D_DrawImageAt(tex, (float)v->ox, (float)v->oy, 0.5f, NULL, 1.0f, 1.0f);
    } else {
        g_rect((float)v->ox, (float)v->oy, BOARD_PX, BOARD_PX, th->light);
        for (int sq = 0; sq < 64; sq++) {
            if (light_square(sq)) continue;
            float x, y; sq_topleft(v, sq, &x, &y);
            g_rect(x, y, SQ_PX, SQ_PX, th->dark);
        }
    }
}

static void tint_square(const BoardView *v, int sq, u32 col) {
    float x, y; sq_topleft(v, sq, &x, &y);
    g_rect(x, y, SQ_PX, SQ_PX, col);
}

static void draw_coords(const BoardView *v, const BoardTheme *th) {
    for (int i = 0; i < 8; i++) {
        int f = v->flipped ? 7 - i : i;          // file shown in column i
        int r = v->flipped ? i : 7 - i;          // rank shown in row i
        // files along the bottom row, ranks down the left column; colour = opposite of the square colour
        int sqb = SQ(f, v->flipped ? 7 : 0);
        char s[2] = {(char)('a' + f), 0};
        g_text(F_B11, v->ox + i * SQ_PX + SQ_PX - 4, v->oy + BOARD_PX - 9, light_square(sqb) ? th->dark : th->light, AL_RIGHT, s);
        int sql = SQ(v->flipped ? 7 : 0, r);
        char t[2] = {(char)('1' + r), 0};
        g_text(F_B11, v->ox + 3, v->oy + i * SQ_PX + 7, light_square(sql) ? th->dark : th->light, AL_LEFT, t);
    }
}

static bool anim_covers(const BoardView *v, int sq) {
    for (int i = 0; i < 6; i++) if (v->anim[i].active && v->anim[i].to == sq) return true;
    return false;
}

void board_draw(const BoardView *v, const Pos *pos) {
    const BoardTheme *th = &BOARD_THEMES[g_set.board_theme % BOARD_THEME_COUNT];
    draw_squares(v, th);

    // highlights
    if (v->last_from >= 0) tint_square(v, v->last_from, C_LAST);
    if (v->last_to >= 0) tint_square(v, v->last_to, C_LAST);
    for (int i = 0; i < v->npm; i++) { tint_square(v, v->pm_from[i], C_PREMOVE); tint_square(v, v->pm_to[i], C_PREMOVE); }
    if (v->sel >= 0) tint_square(v, v->sel, C_SELECT);
    if (v->check_sq >= 0) {
        float x, y; sq_topleft(v, v->check_sq, &x, &y);
        g_img(UI_GLOW_30, x, y, RGBA(230, 40, 30, 230), 1, 1);
    }
    if (g_set.coords) draw_coords(v, th);

    // legal destination markers
    if (v->sel >= 0 || v->drag_sq >= 0 || v->drag_piece) {
        u32 mcol = v->sel_premove ? RGBA(200, 50, 50, 120) : RGBA(20, 20, 20, 70);
        for (int sq = 0; sq < 64; sq++) {
            if (!((v->dests >> sq) & 1)) continue;
            float x, y; sq_topleft(v, sq, &x, &y);
            if ((v->cap_dests >> sq) & 1) g_img(UI_RING_30, x, y, mcol, 1, 1);
            else if (g_set.legal_dots) g_disc(x + SQ_PX / 2.0f, y + SQ_PX / 2.0f, 10, mcol);
        }
    }
    // drag hover
    if (v->dragging) {
        int hs = board_sq_at(v, (int)v->px, (int)v->py);
        if (hs >= 0 && ((v->dests >> hs) & 1)) tint_square(v, hs, C_HOVER);
    }
    if (v->cursor >= 0) {
        float x, y; sq_topleft(v, v->cursor, &x, &y);
        g_rect(x, y, SQ_PX, 2, C_BLUE); g_rect(x, y + SQ_PX - 2, SQ_PX, 2, C_BLUE);
        g_rect(x, y, 2, SQ_PX, C_BLUE); g_rect(x + SQ_PX - 2, y, 2, SQ_PX, C_BLUE);
    }

    // pieces
    for (int sq = 0; sq < 64; sq++) {
        uint8_t pc = pos->b[sq];
        if (!pc) continue;
        if (v->dragging && sq == v->drag_sq) continue;
        if ((v->hidden >> sq) & 1) continue;
        if (anim_covers(v, sq)) continue;
        float x, y; sq_topleft(v, sq, &x, &y);
        g_piece(0, PCOLOR(pc), PTYPE(pc), x, y, 0, 1.0f);
    }
    // premove arrows
    for (int i = 0; i < v->npm; i++) {
        float x0, y0, x1, y1;
        board_sq_center(v, v->pm_from[i], &x0, &y0); board_sq_center(v, v->pm_to[i], &x1, &y1);
        g_arrow(x0, y0, x1, y1, 4.0f, RGBA(214, 62, 58, 200));
    }
    // animated pieces (and fading victims)
    for (int i = 0; i < 6; i++) {
        const BoardAnim *a = &v->anim[i];
        if (!a->active) continue;
        float k = ease_out_cubic(a->t / a->dur);
        float x0, y0, x1, y1;
        sq_topleft(v, a->from, &x0, &y0); sq_topleft(v, a->to, &x1, &y1);
        if (a->victim) {
            int alpha = (int)(255 * (1.0f - clampf(k * 1.6f, 0, 1)));
            if (alpha > 8) g_piece(0, PCOLOR(a->victim), PTYPE(a->victim), x1, y1, RGBA(255, 255, 255, alpha), 1.0f);
        }
        g_piece(0, PCOLOR(a->piece), PTYPE(a->piece), floorf(lerpf(x0, x1, k) + 0.5f), floorf(lerpf(y0, y1, k) + 0.5f), 0, 1.0f);
    }
    // dragged piece, lifted above the stylus
    if (v->dragging && v->drag_piece) {
        float cx = v->px, cy = v->py - 12;
        g_img(UI_SHADOW_38, cx - 19, cy + 10, RGBA(0, 0, 0, 110), 1, 1);
        g_piece(1, PCOLOR(v->drag_piece), PTYPE(v->drag_piece), floorf(cx - 19), floorf(cy - 19), 0, 1.0f);
    }
}

void board_draw_mini(int x, int y, int sq_px, int theme, int piece_set) {
    const BoardTheme *th = &BOARD_THEMES[theme % BOARD_THEME_COUNT];
    (void)piece_set;
    C2D_Image tex = {0};
    if (th->tex) tex = gfx_board_texture(th->tex);
    if (tex.tex) {
        float s = (float)(sq_px * 8) / 240.0f;
        C2D_DrawImageAt(tex, (float)x, (float)y, 0.5f, NULL, s, s);
        return;
    }
    g_rect((float)x, (float)y, (float)(sq_px * 8), (float)(sq_px * 8), th->light);
    for (int r = 0; r < 8; r++)
        for (int f = 0; f < 8; f++)
            if (((r + f) & 1) == 1) g_rect((float)(x + f * sq_px), (float)(y + r * sq_px), (float)sq_px, (float)sq_px, th->dark);
}
