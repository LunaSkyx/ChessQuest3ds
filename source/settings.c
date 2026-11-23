#include "app.h"
#include "gfx.h"
#include <jansson.h>

Settings g_set;

void settings_defaults(void) {
    memset(&g_set, 0, sizeof g_set);
    g_set.board_theme = 0;
    g_set.piece_set = 0;
    g_set.sound = true;
    g_set.volume = 80;
    g_set.coords = true;
    g_set.premoves = true;
    g_set.animations = true;
    g_set.legal_dots = true;
    g_set.auto_queen = false;
    g_set.board_left = true;
    g_set.verify_tls = true;
    g_set.rated = false;
    g_set.last_tc = 4;
    g_set.clock_min = 5; g_set.clock_inc = 0; g_set.ai_level = 3; g_set.off_level = 3; g_set.pz_diff = 2; g_set.pz_rated = true;
    g_set.variant = 0;
    g_set.color = 0;
    g_set.fics_guest = true;
}

static void get_int(json_t *o, const char *k, int *dst) { json_t *v = json_object_get(o, k); if (json_is_integer(v)) *dst = (int)json_integer_value(v); }
static void get_bool(json_t *o, const char *k, bool *dst) { json_t *v = json_object_get(o, k); if (json_is_boolean(v)) *dst = json_is_true(v); }
static void get_str(json_t *o, const char *k, char *dst, size_t n) { json_t *v = json_object_get(o, k); if (json_is_string(v)) snprintf(dst, n, "%s", json_string_value(v)); }

void settings_load(void) {
    settings_defaults();
    size_t len;
    char *txt = read_file(DATA_DIR "/settings.json", &len);
    if (txt) {
        json_error_t err;
        json_t *o = json_loads(txt, 0, &err);
        free(txt);
        if (o) {
            get_int(o, "board_theme", &g_set.board_theme); get_int(o, "piece_set", &g_set.piece_set); if (g_set.piece_set < 0 || g_set.piece_set >= PIECE_SET_COUNT) g_set.piece_set = 0;
            get_bool(o, "sound", &g_set.sound); get_int(o, "volume", &g_set.volume);
            get_bool(o, "coords", &g_set.coords); get_bool(o, "premoves", &g_set.premoves);
            get_bool(o, "animations", &g_set.animations); get_bool(o, "legal_dots", &g_set.legal_dots);
            get_bool(o, "auto_queen", &g_set.auto_queen); get_bool(o, "hide_draw", &g_set.hide_draw); get_bool(o, "board_left", &g_set.board_left);
            get_bool(o, "verify_tls", &g_set.verify_tls); get_bool(o, "rated", &g_set.rated);
            get_int(o, "last_tc", &g_set.last_tc); get_int(o, "ai_level", &g_set.ai_level); get_int(o, "off_level", &g_set.off_level); get_int(o, "pz_angle", &g_set.pz_angle); get_int(o, "pz_diff", &g_set.pz_diff); get_bool(o, "pz_rated", &g_set.pz_rated); get_bool(o, "off_noclock", &g_set.off_noclock); { int bb = 0; get_int(o, "bots_beaten", &bb); g_set.bots_beaten = (unsigned)bb; } get_int(o, "clock_min", &g_set.clock_min); get_int(o, "clock_inc", &g_set.clock_inc); get_bool(o, "clock_mode", &g_set.clock_mode); get_bool(o, "clock_swap", &g_set.clock_swap);
            get_int(o, "variant", &g_set.variant); get_int(o, "color", &g_set.color);
            get_str(o, "token", g_set.token, sizeof g_set.token);
            get_str(o, "friend_name", g_set.friend_name, sizeof g_set.friend_name);
            get_bool(o, "fics_guest", &g_set.fics_guest); get_str(o, "fics_user", g_set.fics_user, sizeof g_set.fics_user); get_str(o, "fics_pass", g_set.fics_pass, sizeof g_set.fics_pass);
            json_decref(o);
        }
    }
    // token.txt beats the settings file (easy to drop onto the SD card from a PC)
    char *t = read_file(DATA_DIR "/token.txt", &len);
    if (t) {
        for (char *p = t; *p; p++) if (*p == '\r' || *p == '\n' || *p == ' ') { *p = 0; break; }
        if (*t) snprintf(g_set.token, sizeof g_set.token, "%s", t);
        free(t);
    }
    g_set.volume = (int)clampf((float)g_set.volume, 0, 100);
}

void settings_save(void) {
    json_t *o = json_object();
    json_object_set_new(o, "board_theme", json_integer(g_set.board_theme));
    json_object_set_new(o, "piece_set", json_integer(g_set.piece_set));
    json_object_set_new(o, "sound", json_boolean(g_set.sound));
    json_object_set_new(o, "volume", json_integer(g_set.volume));
    json_object_set_new(o, "coords", json_boolean(g_set.coords));
    json_object_set_new(o, "premoves", json_boolean(g_set.premoves));
    json_object_set_new(o, "animations", json_boolean(g_set.animations));
    json_object_set_new(o, "legal_dots", json_boolean(g_set.legal_dots));
    json_object_set_new(o, "auto_queen", json_boolean(g_set.auto_queen));
    json_object_set_new(o, "hide_draw", json_boolean(g_set.hide_draw));
    json_object_set_new(o, "board_left", json_boolean(g_set.board_left));
    json_object_set_new(o, "verify_tls", json_boolean(g_set.verify_tls));
    json_object_set_new(o, "rated", json_boolean(g_set.rated));
    json_object_set_new(o, "last_tc", json_integer(g_set.last_tc));
    json_object_set_new(o, "ai_level", json_integer(g_set.ai_level));
    json_object_set_new(o, "off_level", json_integer(g_set.off_level));
    json_object_set_new(o, "pz_angle", json_integer(g_set.pz_angle));
    json_object_set_new(o, "pz_diff", json_integer(g_set.pz_diff));
    json_object_set_new(o, "pz_rated", json_boolean(g_set.pz_rated));
    json_object_set_new(o, "off_noclock", json_boolean(g_set.off_noclock));
    json_object_set_new(o, "clock_min", json_integer(g_set.clock_min)); json_object_set_new(o, "clock_inc", json_integer(g_set.clock_inc));
    json_object_set_new(o, "clock_mode", json_boolean(g_set.clock_mode)); json_object_set_new(o, "clock_swap", json_boolean(g_set.clock_swap));
    json_object_set_new(o, "bots_beaten", json_integer((json_int_t)g_set.bots_beaten));
    json_object_set_new(o, "variant", json_integer(g_set.variant));
    json_object_set_new(o, "color", json_integer(g_set.color));
    json_object_set_new(o, "friend_name", json_string(g_set.friend_name));
    json_object_set_new(o, "token", json_string(g_set.token));
    json_object_set_new(o, "fics_guest", json_boolean(g_set.fics_guest));
    json_object_set_new(o, "fics_user", json_string(g_set.fics_user));
    json_object_set_new(o, "fics_pass", json_string(g_set.fics_pass));
    char *s = json_dumps(o, JSON_INDENT(2));
    if (s) { write_file(DATA_DIR "/settings.json", s, strlen(s)); free(s); }
    json_decref(o);
}
