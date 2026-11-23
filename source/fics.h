// FICS (freechess.org) client: raw TCP text protocol on its own thread, state machine pumped from the UI thread.
#pragma once
#include "app.h"
#include "fics_parse.h"

enum { FICS_OFF, FICS_CONNECTING, FICS_LOGIN, FICS_ONLINE, FICS_ERROR };
enum { FE_DRAW_OFFER = 1, FE_DRAW_DECLINED, FE_TAKEBACK_OFFER, FE_TAKEBACK_DECLINED, FE_ILLEGAL, FE_ABORT_OFFER };

typedef struct {
    int state;
    char err[100];
    char user[32], pass[48]; bool guest;     // credentials used for (re)connecting
    char name[32];                           // name we are logged in as
    bool seeking;
    // current game
    bool in_game; int gnum;
    bool start_pending;                      // a new game started: the main loop opens the game scene
    Style12 first, last;                     // first / most recent board of the game
    int gi_rating[2]; bool gi_rated;         // from the <g1> game info line
    bool result_valid; int res_winner; char res_status[16], res_why[40];    // a result that arrived while no scene listened
    // callbacks installed by the game scene
    void (*on_s12)(const Style12 *s);
    void (*on_result)(int winner, const char *status, const char *why);
    void (*on_event)(int evt, const char *text);
} Fics;

extern Fics g_fics;

void fics_start(void);                       // starts the socket thread (needs the soc service from li_start)
void fics_stop(void);
void fics_connect(bool guest, const char *user, const char *pass);
void fics_disconnect(void);
void fics_pump(void);                        // call every frame
void fics_send(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void fics_seek(int minutes, int inc, bool rated, int color);       // color 0 random 1 white 2 black
void fics_unseek(void);
void fics_send_move(const Pos *pre, Move m);
