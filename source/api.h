// High level Lichess API: account, events, seeks, challenges, game actions
#pragma once
#include "app.h"
#include "lichess.h"

enum { TAG_ACCOUNT = 1, TAG_EVENTS, TAG_GAME, TAG_SEEK, TAG_AI, TAG_CHAL, TAG_MOVE, TAG_ACTION, TAG_ONGOING, TAG_WARM, TAG_CANCEL, TAG_USER, TAG_HIST, TAG_CLOUD, TAG_PUZ, TAG_EXPL, TAG_SOC, TAG_TV, TAG_UPD };

typedef struct {
    char id[16];
    char opp[32];
    int opp_rating;
    int color;               // WHITE / BLACK
    bool my_turn;
    char speed[12];
    char variant[20];
    bool rated;
    int secs_left;
} OngoingGame;

typedef struct {
    // account
    bool have_account;
    char user[32], id[32], title[8];
    int puzzle_rating;           // 0 unknown
    int rating[4], games[4];     // bullet, blitz, rapid, classical
    // connection
    int net;                     // 0 idle, 1 connecting, 2 online, -1 error
    char err[120];
    int err_rc;                  // curl code of the last connect failure (0 = none)
    bool events_on;
    int req_account, req_events, req_seek, req_game, req_chal;
    u64 events_retry_at;
    bool token_rejected;
    // matchmaking state
    bool seeking;
    char seek_range[16];         // optional ratingRange for seeks (tests use an unmatchable range)
    u64 expect_until;            // gameStart events are only followed (auto-open) while we asked for a game
    int expect_mode;             // 1: a specific game id is expected (AI / challenge), 2: any NEW game (seek)
    char expect_id[16];          // the id we expect (known once the POST answered); a game that is not ours is never followed
    char early_id[16];           // a gameStart that arrived before the id was known
    u64 ev_open_at;              // when the event stream was (re)opened - it replays the games we are already in
    bool tourney_active;         // we joined an arena: the games it pairs us into are followed
    bool start_pending;          // a gameStart event arrived
    char start_id[16];
    bool chal_in_pending;        // incoming challenge
    char chal_id[16], chal_from[40], chal_desc[64];
    bool chal_out_wait;          // our own challenge is waiting to be accepted
    char chal_out_id[16], chal_out_name[32];
    char notice[120];            // one-shot message to show (declined, cancelled ...)
    bool notice_new;
    // ongoing games
    OngoingGame ongoing[16];
    int n_ongoing;
    bool ongoing_loaded;
    // callbacks installed by the game scene
    void (*on_game_line)(const char *line);
    void (*on_game_end)(int curl_rc, long http);
    void (*on_move_result)(bool ok, const char *body);
} Api;

extern Api g_api;

typedef struct { int limit_sec, inc_sec; int days; } TimeCtl;     // days > 0: correspondence

void api_init(void);
void api_connect(void);                      // validates the token (account) then opens the event stream
void api_disconnect(void);
void api_pump(void);                         // call every frame
void api_refresh_account(void);
void api_refresh_ongoing(void);

void api_seek(TimeCtl tc, bool rated, Variant v, int color);        // color: 0 random 1 white 2 black
void api_seek_cancel(void);
void api_challenge_ai(int level, TimeCtl tc, Variant v, int color);
void api_challenge_ai_fen(int level, TimeCtl tc, Variant v, int color, const char *fen);   // v: standard (-> fromPosition) or chess960
void api_challenge_user(const char *name, TimeCtl tc, bool rated, Variant v, int color);
void api_challenge_cancel(void);
void api_challenge_accept(const char *id);
void api_challenge_decline(const char *id);

void api_game_open(const char *id);          // starts the game stream
void api_game_close(void);
void api_warm(void);
void api_move(const char *id, const char *uci);
void api_resign(const char *id);
void api_abort(const char *id);
void api_draw(const char *id, bool yes);
void api_takeback(const char *id, bool yes);
void api_claim_victory(const char *id);
void hist_net_line(const char *line);                 // (scene_history.c) one game of /api/games/user
void hist_net_end(int curl_rc, long http);
void analysis_net_cloud(int req, int curl_rc, long http, const char *body);
void analysis_net_explorer(int req, int curl_rc, long http, const char *body);   // (scene_analysis.c) opening explorer answer
void puzzle_net(int req, int curl_rc, long http, const char *body);
void social_net(int tag, int req, int kind, int curl_rc, long http, const char *data);   // (scene_social.c) friends / rating history / TV / tournaments              // (scene_puzzle.c) puzzle batch / daily / solve answers   // (scene_analysis.c) cloud evaluation answer
void api_notice(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// update check (update.c): asks GitHub for the newest release once a day; every failure is silent
void update_tick(float dt);                                   // call every frame
void update_net(int req, int curl_rc, long http, const char *body);
bool update_available(char *ver, size_t n);                   // true + "0.9.2" when a newer release is known
