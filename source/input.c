#include "app.h"

Touch g_touch;
u32 g_kdown, g_kheld, g_kup;

static bool inj_touch;           // scripted stylus is "down"
static int inj_x, inj_y;
static u32 inj_keys;             // scripted held keys
static bool inj_active;

void input_inject_touch(bool down, int x, int y) { inj_touch = down; inj_x = x; inj_y = y; inj_active = true; }
void input_inject_key(u32 key, bool down) { if (down) inj_keys |= key; else inj_keys &= ~key; inj_active = true; }
bool input_injecting(void) { return inj_active; }

void input_update(void) {
    g_touch.consumed = false;
    hidScanInput();
    u32 kd = hidKeysDown(), kh = hidKeysHeld(), ku = hidKeysUp();
    static u32 prev_inj;
    u32 inj_down = inj_keys & ~prev_inj, inj_up = prev_inj & ~inj_keys;
    prev_inj = inj_keys;
    g_kdown = kd | inj_down; g_kheld = kh | inj_keys; g_kup = ku | inj_up;

    bool was = g_touch.down;
    bool down = false; int x = 0, y = 0;
    if (kh & KEY_TOUCH) {
        touchPosition tp; hidTouchRead(&tp);
        down = true; x = tp.px; y = tp.py;
    }
    if (inj_touch) { down = true; x = inj_x; y = inj_y; }
    g_touch.pressed = down && !was;
    g_touch.released = !down && was;
    g_touch.down = down;
    if (down) {
        g_touch.x = x; g_touch.y = y;
        if (g_touch.pressed) { g_touch.sx = x; g_touch.sy = y; }
    }
}
