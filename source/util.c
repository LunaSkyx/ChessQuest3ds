#include "app.h"
#include <sys/stat.h>
#include <time.h>

static FILE *g_log;
static LightLock g_log_lock;

void log_init(void) {
    LightLock_Init(&g_log_lock);
    mkdirs(DATA_DIR);
    g_log = fopen(DATA_DIR "/log.txt", "w");
}

u64 now_ms(void) { return svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000); }

void LOGF(const char *fmt, ...) {
    if (!g_log) return;
    LightLock_Lock(&g_log_lock);
    fprintf(g_log, "%8llu ", (unsigned long long)now_ms());
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    LightLock_Unlock(&g_log_lock);
}

void mkdirs(const char *path) {
    char tmp[256];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 7; *p; p++)            // skip "sdmc:/"
        if (*p == '/') { *p = 0; mkdir(tmp, 0777); *p = '/'; }
    mkdir(tmp, 0777);
}

char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (!b) { fclose(f); return NULL; }
    size_t r = fread(b, 1, (size_t)n, f); b[r] = 0; fclose(f);
    if (len) *len = r;
    return b;
}

bool write_file(const char *path, const void *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    size_t w = fwrite(data, 1, len, f); fclose(f);
    return w == len;
}

float lerpf(float a, float b, float t) { return a + (b - a) * t; }
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float ease_out_cubic(float t) { t = 1.0f - clampf(t, 0, 1); return 1.0f - t * t * t; }

// 1:23 / 12:05 / 0:09.4 (tenths only below 10 s)
void fmt_clock(int ms, char *out, size_t n, bool tenths_ok) {
    if (ms < 0) ms = 0;
    if (ms < 10000 && tenths_ok) { snprintf(out, n, "0:%02d.%d", ms / 1000, (ms % 1000) / 100); return; }
    int s = (ms + 999) / 1000;
    if (s >= 3600) snprintf(out, n, "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
    else snprintf(out, n, "%d:%02d", s / 60, s % 60);
}
