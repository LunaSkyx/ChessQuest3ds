// Scripted, unattended runs of the real app (used with tools/az_run.ps1 in the Azahar emulator).
// Inert unless sdmc:/lichess3ds/scenario.txt exists.
#pragma once
#include "app.h"

typedef struct {
    // app specific commands:  cmd NAME args...   -> return false if unknown
    bool (*cmd)(const char *name, const char *args);
    // app specific wait conditions: until NAME args... -> true when satisfied; *known=false if unknown
    bool (*cond)(const char *name, const char *args, bool *known);
    // board square -> bottom-screen pixel centre
    bool (*square_xy)(int sq, int *x, int *y);
} ScnHooks;

void scn_init(void);
bool scn_active(void);
void scn_set_hooks(const ScnHooks *h);
void scn_tick(void);                       // once per frame, before the app updates
void scn_event(const char *name);          // the app reports things that happened (game_start, game_over, ...)
bool scn_quit_requested(void);
