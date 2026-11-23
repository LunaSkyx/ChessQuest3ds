// Chess board view: themes, pieces, highlights, animations, drag
#pragma once
#include "gfx.h"

typedef struct { const char *name; u32 light, dark; const char *tex; } BoardTheme;
extern const BoardTheme BOARD_THEMES[];
extern const int BOARD_THEME_COUNT;

#define SQ_PX 30
#define BOARD_PX 240
#define MAX_PM 8

typedef struct {
    uint8_t piece, victim;      // piece colour|type being moved, piece being captured (0 = none)
    int8_t from, to;
    float t, dur;
    bool active;
} BoardAnim;

typedef struct {
    bool flipped;
    int ox, oy;                 // top-left of the board on the bottom screen
    int sel;                    // selected square or -1
    uint64_t dests, cap_dests;  // legal destination squares for the selection (and which of them capture)
    bool sel_premove;           // selection shows premove geometry (dots are red)
    bool dragging;
    int drag_sq;                // square the dragged piece came from (-1: dragging from a pocket)
    uint8_t drag_piece;         // piece being dragged
    float px, py;               // pointer position
    int last_from, last_to;     // last move highlight (-1 none)
    int check_sq;               // king in check (-1 none)
    int pm_from[MAX_PM], pm_to[MAX_PM], npm;   // queued premoves (for display)
    BoardAnim anim[6];
    int cursor;                 // d-pad cursor square or -1
    uint64_t hidden;            // squares whose piece is hidden (e.g. while a promotion dialog is open)
} BoardView;

void board_reset(BoardView *v);
void board_set_flip(BoardView *v, bool flipped);
void board_anim_move(BoardView *v, int from, int to, uint8_t piece, uint8_t victim, float dur);
void board_anim_clear(BoardView *v);
void board_update(BoardView *v, float dt);
bool board_animating(const BoardView *v);
void board_draw(const BoardView *v, const Pos *pos);
int  board_sq_at(const BoardView *v, int x, int y);                 // -1 when outside the board
void board_sq_center(const BoardView *v, int sq, float *cx, float *cy);
void board_draw_mini(int x, int y, int sq_px, int theme, int piece_set);   // preview used by settings
