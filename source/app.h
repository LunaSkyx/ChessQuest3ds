// Lichess3DS - shared declarations
#pragma once
#include <3ds.h>
#include <citro2d.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chess.h"
#include "ui_atlas.h"

#define TOP_W 400
#define BOT_W 320
#define SCR_H 240
#define APP_VERSION "0.9"
#define DATA_DIR "sdmc:/lichess3ds"

// colour: same byte order as C2D_Color32 (usable in constant initialisers)
#define RGBA(r, g, b, a) ((u32)(((a) << 24) | ((b) << 16) | ((g) << 8) | (r)))
#define RGB(r, g, b) RGBA(r, g, b, 255)
#define WITH_A(c, a) (((c) & 0x00FFFFFFu) | ((u32)(a) << 24))

// palette (dark, chess.com-like)
#define C_BG       RGB(38, 36, 33)
#define C_BG2      RGB(30, 29, 27)
#define C_PANEL    RGB(49, 46, 43)
#define C_PANEL2   RGB(60, 57, 53)
#define C_PANEL3   RGB(75, 72, 68)
#define C_GREEN    RGB(129, 182, 76)
#define C_GREEN_D  RGB(69, 117, 36)
#define C_GREEN_L  RGB(165, 214, 110)
#define C_TEXT     RGB(255, 255, 255)
#define C_TEXT2    RGB(196, 194, 190)
#define C_TEXT3    RGB(140, 138, 134)
#define C_RED      RGB(224, 68, 52)
#define C_ORANGE   RGB(246, 166, 35)
#define C_BLUE     RGB(88, 150, 220)
#define C_BLACK    RGB(0, 0, 0)

typedef struct { float x, y, w, h; } Rect;
static inline bool rect_hit(Rect r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

// ---- util.c
void log_init(void);
void LOGF(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
u64 now_ms(void);
void mkdirs(const char *path);
char *read_file(const char *path, size_t *len);          // malloc'd, NUL terminated
bool write_file(const char *path, const void *data, size_t len);
float lerpf(float a, float b, float t);
float clampf(float v, float lo, float hi);
float ease_out_cubic(float t);
void fmt_clock(int ms, char *out, size_t n, bool tenths_ok);

// ---- input.c
typedef struct { bool down, pressed, released, consumed; int x, y, sx, sy; } Touch;
extern Touch g_touch;                     // bottom-screen stylus
extern u32 g_kdown, g_kheld, g_kup;       // buttons
void input_update(void);
void input_inject_touch(bool down, int x, int y);
void input_inject_key(u32 key, bool down);
bool input_injecting(void);

// ---- settings.c
typedef struct {
    int board_theme;       // index into board theme list
    int piece_set;
    bool sound;
    int volume;            // 0..100
    bool coords;
    bool premoves;
    bool animations;
    bool legal_dots;
    bool auto_queen;
    bool hide_draw;        // do not show / sound opponent draw offers (bullet spam)
    bool board_left;       // board on the left of the bottom screen
    bool verify_tls;
    bool rated;
    int  last_tc;          // last chosen time control preset
    int  ai_level;
    int  pz_angle, pz_diff;  // puzzle theme index / difficulty (0 easiest .. 4 hardest, 2 normal)
    bool pz_rated;         // report puzzle results to Lichess (changes your puzzle rating)
    int  off_level;        // offline engine level 1..8
    bool off_noclock;      // offline game without a clock
    int clock_min, clock_inc; bool clock_mode, clock_swap;   // chess clock for real boards (mode: false increment, true delay)
    unsigned bots_beaten;  // bit i set = bot ladder opponent i has been beaten
    int  variant;
    int  color;            // 0 random, 1 white, 2 black
    char token[96];
    char friend_name[40];
    bool fics_guest;       // FICS: play as a guest (unrated) instead of with an account
    char fics_user[32], fics_pass[48];
} Settings;
extern Settings g_set;
void settings_defaults(void);
void settings_load(void);
void settings_save(void);
