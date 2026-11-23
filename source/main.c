#include "app.h"
#include "gfx.h"
#include "scenario.h"
#include "scenes.h"
#include "audio.h"
#include "game.h"
#include "lichess.h"
#include "fics.h"
#include "engine.h"
#include "analysis.h"

// ------------------------------------------------------------------ scenario hooks (inert unless a scenario is running)
static bool g_scenes_name_eq(int i, const char *n);
static bool g_fics_pending_seek; static int g_fics_seek_args[4]; static char g_fics_raw[100];
static bool app_cmd(const char *name, const char *args) {
    if (!strcmp(name, "goto")) {
        for (int i = 1; i < SC_COUNT; i++) if (g_scenes_name_eq(i, args)) { scene_goto((SceneId)i); return true; }
        return false;
    }
    if (!strcmp(name, "ai")) {                       // ai LEVEL LIMIT_SEC INC_SEC [variantKey] [color 0/1/2]
        int lvl = 1, lim = 600, inc = 0, col = 0; char var[32] = "standard";
        sscanf(args ? args : "", "%d %d %d %31s %d", &lvl, &lim, &inc, var, &col);
        Variant v = VAR_STANDARD; variant_from_key(var, &v);
        memset(&g_last_params, 0, sizeof g_last_params);
        g_last_params.ai_level = lvl; g_last_params.tc = (TimeCtl){lim, inc}; g_last_params.variant = v; g_last_params.color = col;
        api_challenge_ai(lvl, (TimeCtl){lim, inc}, v, col);
        return true;
    }
    if (!strcmp(name, "offline")) {                  // offline LEVEL LIMIT_SEC INC_SEC [color 0/1/2]: built-in engine game
        int lvl = 3, lim = 0, inc = 0, col = 1;
        sscanf(args ? args : "", "%d %d %d %d", &lvl, &lim, &inc, &col);
        scene_game_open_offline(lvl, lim > 0 || inc > 0, (TimeCtl){lim, inc}, col);
        return true;
    }
    if (!strcmp(name, "analysisfen")) {              // analysisfen FEN...: analysis board on a position
        ag_reset(VAR_STANDARD, args);
        scene_analysis_open(0, SC_HOME);
        return true;
    }
    if (!strcmp(name, "analysispgn")) {              // analysispgn SAN SAN ...: analysis board with a played line
        ag_reset(VAR_STANDARD, NULL);
        char buf[600]; snprintf(buf, sizeof buf, "%s", args ? args : ""); char *sv = NULL;
        for (char *t = strtok_r(buf, " ", &sv); t; t = strtok_r(NULL, " ", &sv)) ag_push_san(t);
        scene_analysis_open(0, SC_HOME);
        return true;
    }
    if (!strcmp(name, "neterr")) {                   // neterr RC: pretend the connect failed with this curl code (boot screen test)
        g_api.net = -1; g_api.err_rc = args ? atoi(args) : 60; snprintf(g_api.err, sizeof g_api.err, "Network error: SSL peer certificate was not OK");
        return true;
    }
    if (!strcmp(name, "demoname")) { snprintf(g_api.user, sizeof g_api.user, "%s", args ? args : "Player"); return true; }   // promo video: show a neutral name
    if (!strcmp(name, "botmask")) { g_set.bots_beaten = args ? (unsigned)atoi(args) : 0; return true; }
    if (!strcmp(name, "botfen")) {                   // botfen IDX FEN...: ladder game from a position (test)
        int idx = 0, used = 0; sscanf(args ? args : "", "%d %n", &idx, &used);
        scene_game_open_bot_fen(idx, args ? args + used : "");
        return true;
    }
    if (!strcmp(name, "pz")) { puzzle_test(args ? args : ""); return true; }
    if (!strcmp(name, "pzrated")) { g_set.pz_rated = args && *args == '1'; return true; }
    if (!strcmp(name, "offer")) { g_game.draw_offer[g_game.my_color ^ 1] = true; return true; }   // test: pretend the opponent offers a draw
    if (!strcmp(name, "aifen")) {                    // aifen LEVEL LIMIT INC COLOR VARIANTKEY FEN...   (test positions)
        int lvl = 1, lim = 600, inc = 0, col = 0, used = 0; char var[32] = "standard";
        sscanf(args ? args : "", "%d %d %d %d %31s %n", &lvl, &lim, &inc, &col, var, &used);
        Variant v = VAR_STANDARD; variant_from_key(var, &v);
        const char *fen = args ? args + used : "";
        memset(&g_last_params, 0, sizeof g_last_params);
        g_last_params.ai_level = lvl; g_last_params.tc = (TimeCtl){lim, inc}; g_last_params.variant = v; g_last_params.color = col;
        api_challenge_ai_fen(lvl, (TimeCtl){lim, inc}, v, col, fen);
        return true;
    }
    if (!strcmp(name, "set")) {                      // set KEY VALUE
        char k[32] = "", v[32] = ""; sscanf(args ? args : "", "%31s %31s", k, v);
        int n = atoi(v);
        if (!strcmp(k, "board")) g_set.board_theme = n; else if (!strcmp(k, "pieces")) g_set.piece_set = n;
        else if (!strcmp(k, "sound")) g_set.sound = n; else if (!strcmp(k, "left")) g_set.board_left = n;
        else if (!strcmp(k, "premoves")) g_set.premoves = n; else if (!strcmp(k, "autoqueen")) g_set.auto_queen = n;
        else if (!strcmp(k, "anim")) g_set.animations = n; else if (!strcmp(k, "coords")) g_set.coords = n;
        else if (!strcmp(k, "verify")) { g_set.verify_tls = n; li_set_verify(n); }
        else return false;
        return true;
    }
    if (!strcmp(name, "fics")) {                     // fics MIN INC [rated 0/1] [color 0/1/2]: connect as guest and seek
        int mn = 3, inc = 0, rated = 0, col = 0; sscanf(args ? args : "", "%d %d %d %d", &mn, &inc, &rated, &col);
        fics_connect(true, "", ""); g_fics_pending_seek = true; g_fics_seek_args[0] = mn; g_fics_seek_args[1] = inc; g_fics_seek_args[2] = rated; g_fics_seek_args[3] = col;
        return true;
    }
    if (!strcmp(name, "ficsraw")) { fics_connect(true, "", ""); snprintf(g_fics_raw, sizeof g_fics_raw, "%s", args ? args : ""); return true; }   // connect as guest, then type a command
    if (!strcmp(name, "seekrange")) { snprintf(g_api.seek_range, sizeof g_api.seek_range, "%s", args ? args : ""); return true; }
    if (!strcmp(name, "challenge")) { api_challenge_user(args ? args : "", (TimeCtl){300, 0}, false, VAR_STANDARD, 0); return true; }
    return scene_game_cmd(name, args);
}

static bool app_cond(const char *name, const char *args, bool *known) {
    *known = true;
    if (!strcmp(name, "online")) return g_api.net == 2;
    if (!strcmp(name, "ficsonline")) return g_fics.state == FICS_ONLINE;
    if (!strcmp(name, "scene")) return args && !strcmp(scene_name(scene_current()), args);
    if (!strcmp(name, "pzstate")) return args && puzzle_test("state") == atoi(args);   // 3 = your move, 7 = finished
    return scene_game_cond(name, args, known);
}

// ------------------------------------------------------------------ scene manager
static SceneId g_cur = SC_NONE;
static u64 g_scene_t0;

static const Scene *g_scenes[SC_COUNT];
static bool g_scenes_name_eq(int i, const char *n) { return g_scenes[i] && n && !strcmp(g_scenes[i]->name, n); }
const char *scene_name(SceneId id) { return g_scenes[id] ? g_scenes[id]->name : "none"; }
SceneId scene_current(void) { return g_cur; }
float scene_age(void) { return (float)(now_ms() - g_scene_t0) / 1000.0f; }
float scene_fade(void) { float a = scene_age(); return a < 0.18f ? 1.0f - a / 0.18f : 0.0f; }

void scene_goto(SceneId id) {
    if (g_cur != SC_NONE && g_scenes[g_cur] && g_scenes[g_cur]->leave) g_scenes[g_cur]->leave();
    g_cur = id;
    g_scene_t0 = now_ms();
    LOGF("scene -> %s", scene_name(id));
    if (g_scenes[id] && g_scenes[id]->enter) g_scenes[id]->enter();
    scn_event(scene_name(id));
}
void scene_frame(float dt) { if (g_scenes[g_cur] && g_scenes[g_cur]->update) g_scenes[g_cur]->update(dt); }
void scene_draw_top(void) { if (g_scenes[g_cur] && g_scenes[g_cur]->draw_top) g_scenes[g_cur]->draw_top(); }
void scene_draw_bottom(void) { if (g_scenes[g_cur] && g_scenes[g_cur]->draw_bot) g_scenes[g_cur]->draw_bot(); }

// ------------------------------------------------------------------ main
int main(void) {
    gfxInitDefault();
    romfsInit();
    log_init();
    LOGF("Chess Quest 3DS %s starting", APP_VERSION);
    settings_load();
    if (!gfx_init()) { LOGF("gfx init failed"); romfsExit(); gfxExit(); return 1; }
    scn_init();
    audio_init();
    li_start();
    fics_start();
    eng_async_init();
    api_init();
    static const ScnHooks hooks = {app_cmd, app_cond, scene_game_square_xy};
    scn_set_hooks(&hooks);

    g_scenes[SC_BOOT] = &scene_boot; g_scenes[SC_LOGIN] = &scene_login; g_scenes[SC_HOME] = &scene_home;
    g_scenes[SC_PLAY] = &scene_play; g_scenes[SC_GAME] = &scene_game; g_scenes[SC_GAMES] = &scene_games;
    g_scenes[SC_SETTINGS] = &scene_settings; g_scenes[SC_HISTORY] = &scene_history; g_scenes[SC_ANALYSIS] = &scene_analysis; g_scenes[SC_PUZZLE] = &scene_puzzle; g_scenes[SC_MORE] = &scene_more; g_scenes[SC_RATING] = &scene_rating; g_scenes[SC_FRIENDS] = &scene_friends; g_scenes[SC_TV] = &scene_tv; g_scenes[SC_TOURNEY] = &scene_tourney; g_scenes[SC_LEARN] = &scene_learn; g_scenes[SC_GUIDE] = &scene_guide; g_scenes[SC_BOTS] = &scene_bots; g_scenes[SC_CLOCK] = &scene_clock;
    scene_goto(SC_BOOT);

    u64 last = now_ms();
    while (aptMainLoop()) {
        u64 now = now_ms();
        float dt = clampf((float)(now - last) / 1000.0f, 0.0f, 0.1f);
        last = now;
        input_update();
        scn_tick();
        if (scn_quit_requested()) break;
        api_pump();
        fics_pump();
        if (g_fics_raw[0] && g_fics.state == FICS_ONLINE) { fics_send("%s", g_fics_raw); g_fics_raw[0] = 0; }
        if (g_fics_pending_seek && g_fics.state == FICS_ONLINE) { g_fics_pending_seek = false; fics_seek(g_fics_seek_args[0], g_fics_seek_args[1], g_fics_seek_args[2] != 0, g_fics_seek_args[3]); }
        if (g_fics.start_pending) {
            g_fics.start_pending = false;
            bool running = scene_current() == SC_GAME && g_game.active && !g_game.finished && g_game.backend == 1;
            if (!running) scene_game_open_fics();
        }
        if (g_api.start_pending) {
            g_api.start_pending = false;
            bool running = scene_current() == SC_GAME && g_game.active && !g_game.finished;
            if (!running) scene_game_open(g_api.start_id);
            else if (strcmp(g_game.id, g_api.start_id)) api_notice("Another game started - see Games");
        }
        scene_frame(dt);

        gfx_frame_start();
        gfx_scene_top(C_BG);
        scene_draw_top();
        float fade = scene_fade();
        if (fade > 0) g_rect(0, 0, 400, 240, RGBA(38, 36, 33, (int)(fade * 255)));
        gfx_scene_bottom(C_BG);
        scene_draw_bottom();
        if (fade > 0) g_rect(0, 0, 320, 240, RGBA(38, 36, 33, (int)(fade * 255)));
        gfx_frame_end();
    }
    LOGF("exiting");
    settings_save();
    api_disconnect();
    fics_disconnect();
    eng_async_exit();
    fics_stop();
    li_stop();
    audio_exit();
    gfx_exit();
    romfsExit();
    gfxExit();
    return 0;
}
