// Update check: once a day (and only if enabled in Settings) ask the public GitHub API for the newest release of this
// project and remember its version. Nothing is downloaded or installed. Every failure (no Wi-Fi, wrong clock, GitHub down,
// odd answer) is silent: only a line in the log.
#include "app.h"
#include "api.h"
#include <jansson.h>
#include <time.h>

#define UPD_URL "https://api.github.com/repos/LunaSkyx/ChessQuest3ds/releases/latest"
#define UPD_DELAY 6.0f                                        // seconds after start, so the first screens are not slowed down

static int g_req;
static float g_uptime;
static bool g_done;                                           // asked (or decided not to ask) in this run

static int today(void) { return (int)((long long)time(NULL) / 86400); }

// "v0.9.2" / "0.9.2-beta" -> {0,9,2}; returns false when it does not start like a version number
static bool parse_ver(const char *s, int v[3]) {
    v[0] = v[1] = v[2] = 0;
    if (*s == 'v' || *s == 'V') s++;
    if (*s < '0' || *s > '9') return false;
    for (int i = 0; i < 3; i++) {
        int n = 0, digits = 0;
        while (*s >= '0' && *s <= '9' && digits < 4) { n = n * 10 + (*s - '0'); s++; digits++; }
        v[i] = n;
        if (*s != '.') break;
        s++;
    }
    return true;
}

static int ver_cmp(const int a[3], const int b[3]) {
    for (int i = 0; i < 3; i++) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

bool update_available(char *ver, size_t n) {
    if (!g_set.check_updates || !g_set.upd_latest[0]) return false;
    int latest[3], mine[3];
    if (!parse_ver(g_set.upd_latest, latest) || !parse_ver(APP_VERSION, mine)) return false;
    if (ver_cmp(mine, latest) >= 0) return false;
    if (ver) snprintf(ver, n, "%s", g_set.upd_latest);
    return true;
}

void update_tick(float dt) {
    if (g_done) return;
    g_uptime += dt;
    if (g_uptime < UPD_DELAY) return;
    g_done = true;
    if (!g_set.check_updates) return;
    int d = today(), age = d - g_set.upd_day;
    if (g_set.upd_day > 0 && age >= 0 && age < 1) { LOGF("update: checked today, latest known '%s'", g_set.upd_latest); return; }
    g_req = li_request(TAG_UPD, "GET", UPD_URL, NULL, false);
}

void update_net(int req, int curl_rc, long http, const char *body) {
    if (req != g_req) return;
    g_req = 0;
    if (curl_rc || http != 200) { LOGF("update: check failed (curl %d, http %ld) - ignored", curl_rc, http); return; }
    json_error_t e;
    json_t *o = json_loads(body, 0, &e);
    if (!o) { LOGF("update: unreadable answer - ignored"); return; }
    const char *tag = json_string_value(json_object_get(o, "tag_name"));
    int v[3];
    if (tag && parse_ver(tag, v)) {
        snprintf(g_set.upd_latest, sizeof g_set.upd_latest, "%d.%d.%d", v[0], v[1], v[2]);   // only digits and dots are kept
        g_set.upd_day = today();
        settings_save();
        LOGF("update: latest release %s (this is %s)", g_set.upd_latest, APP_VERSION);
    } else LOGF("update: no usable tag - ignored");
    json_decref(o);
}
