// Immediate-mode UI helpers for the touch (bottom) screen
#pragma once
#include "gfx.h"

typedef enum { BS_PRIMARY, BS_SECONDARY, BS_DANGER, BS_TILE, BS_TILE_SEL, BS_GHOST, BS_DARK } BtnStyle;

bool ui_button(Rect r, const char *label, FontId f, BtnStyle st, int icon);   // true when clicked
bool ui_icon_button(Rect r, int icon, BtnStyle st);
void ui_set_enabled(bool e);                                                   // false: widgets draw but ignore input (under a modal)
bool ui_held(Rect r);                                                          // stylus is down inside r
bool ui_clicked(Rect r);                                                       // released inside r (press started inside too)
bool ui_toggle(Rect r, bool *v);                                               // switch; true when changed
bool ui_slider(Rect r, int *v, int lo, int hi);                                // true while dragging
void ui_card(Rect r, u32 col);
void ui_header(const char *title, int icon);                                   // bottom-screen title bar (36px)

typedef struct { float off, vel; bool drag; float last_y; } Scroll;
void ui_scroll_update(Scroll *s, Rect view, float content_h);
bool ui_scroll_moved(const Scroll *s);                                         // true if the gesture was a drag (suppress clicks)

void ui_toast(const char *msg);
void ui_toast_draw(float x_center);                                            // call at the end of a screen's drawing
bool ui_keyboard(const char *hint, const char *initial, char *out, size_t n);  // software keyboard
bool ui_keyboard_ex(const char *hint, const char *initial, char *out, size_t n, bool password);

void ui_top_bg(void);                                                          // shared backdrop of the top screen (non-game scenes)
void ui_spinner(float cx, float cy, float r, u32 col, float t);
bool ui_challenge_overlay(void);                                               // incoming challenge card (bottom screen); true when shown
