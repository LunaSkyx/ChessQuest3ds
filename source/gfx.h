// Rendering helpers on top of citro2d
#pragma once
#include "app.h"

// pixel em sizes: R11 R13 R16 (regular), B11 B13 B16 B21 B34 (bold)
typedef enum { F_R11, F_R13, F_R16, F_B11, F_B13, F_B16, F_B21, F_B34, F_COUNT } FontId;
enum { AL_LEFT = 0, AL_CENTER = 1, AL_RIGHT = 2 };

bool gfx_init(void);
void gfx_exit(void);
void gfx_frame_start(void);                    // begins the frame (and services pending screenshots)
void gfx_scene_top(u32 clear);
void gfx_scene_bottom(u32 clear);
void gfx_frame_end(void);
void gfx_frame_pause(void);                 // finish the frame in progress (needed before launching a system applet such as the keyboard)
void gfx_frame_resume_bottom(void);         // start a fresh frame and continue drawing on the bottom screen
void gfx_request_capture(const char *base);    // saves <base>_top.bmp / <base>_bottom.bmp on the next frame
bool gfx_capture_done(void);                   // true once the requested capture is written

// primitives (coordinates in the current screen's pixels)
void g_rect(float x, float y, float w, float h, u32 c);
void g_grad(float x, float y, float w, float h, u32 top, u32 bottom);
void g_rrect(float x, float y, float w, float h, int r, u32 c);
void g_rrect_r(Rect r, int rad, u32 c);
void g_shadow(float x, float y, float w, float h, int r, int spread);
void g_disc(float cx, float cy, float d, u32 c);
void g_img(int ui_idx, float x, float y, u32 tint, float sx, float sy);   // tint 0 = draw as is
void g_icon(int icon, float cx, float cy, int size, u32 col);             // size 16 / 24 / 32
void g_line(float x0, float y0, float x1, float y1, float th, u32 c);
void g_tri(float x0, float y0, float x1, float y1, float x2, float y2, u32 c);
void g_arrow(float x0, float y0, float x1, float y1, float th, u32 c);

// text: y is the vertical CENTRE of the capital letters / digits (so boxes centre easily)
void g_text(FontId f, float x, float y, u32 col, int align, const char *s);
void g_textf(FontId f, float x, float y, u32 col, int align, const char *fmt, ...) __attribute__((format(printf, 6, 7)));
void g_text_fit(FontId f, float x, float y, u32 col, int align, float maxw, const char *s);
float g_text_w(FontId f, const char *s);
float g_font_h(FontId f);

// piece sprites (set loaded on demand): size_idx 0 = 30px, 1 = 38px, 2 = 16px
bool gfx_set_piece_set(int set);
C2D_Image gfx_piece(int size_idx, int color, int type);
void g_piece(int size_idx, int color, int type, float x, float y, u32 alpha_tint, float scale);
extern const char *PIECE_SET_NAMES[];
#define PIECE_SET_COUNT 4

// textured boards
C2D_Image gfx_board_texture(const char *name);   // "wood" / "walnut" / "marble"
