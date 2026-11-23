// Runs the engine on a low priority thread: it only gets the CPU while the UI thread waits for the next frame.
#include "app.h"
#include "engine.h"

static Thread g_th;
static LightLock g_lk;
static volatile bool g_quit;
static volatile int g_abort;
// job
static bool g_have_job; static Pos g_job_pos; static uint64_t g_job_hist[160]; static int g_job_nhist; static EngLimits g_job_lim; static int g_job_id;
static int g_next_id = 1;
// results
static EngResult g_res; static bool g_res_new;
static volatile int g_running_id;

static uint64_t clk(void) { return now_ms(); }

static void progress(const EngResult *r, void *ud) {
    (void)ud;
    LightLock_Lock(&g_lk);
    if (r->has_move && g_job_id == g_running_id) { g_res = *r; g_res.job = g_running_id; g_res_new = true; }
    LightLock_Unlock(&g_lk);
}

static void worker(void *arg) {
    (void)arg;
    while (!g_quit) {
        LightLock_Lock(&g_lk);
        bool have = g_have_job; Pos p = g_job_pos; uint64_t hist[160]; int nh = g_job_nhist; EngLimits lim = g_job_lim; int id = g_job_id;
        if (have) { memcpy(hist, g_job_hist, sizeof(uint64_t) * (size_t)nh); g_have_job = false; g_running_id = id; g_abort = 0; }
        LightLock_Unlock(&g_lk);
        if (!have) { svcSleepThread(15 * 1000000LL); continue; }
        EngResult r;
        eng_search(&p, hist, nh, lim, clk, &g_abort, progress, NULL, &r);
        if (!g_abort && id == g_running_id) {                                   // a finished (not cancelled) job reports done
            LightLock_Lock(&g_lk);
            if (id == g_job_id) { g_res = r; g_res.job = id; g_res.done = true; g_res_new = true; }
            LightLock_Unlock(&g_lk);
        }
    }
}

void eng_async_init(void) {
    LightLock_Init(&g_lk);
    g_quit = false;
    g_th = threadCreate(worker, NULL, 768 * 1024, 0x3F, 0, false);
    if (!g_th) LOGF("engine thread failed");
}

void eng_async_exit(void) {
    if (!g_th) return;
    g_quit = true; g_abort = 1;
    threadJoin(g_th, U64_MAX); threadFree(g_th); g_th = NULL;
}

int eng_async_start(const Pos *p, const uint64_t *hist, int nhist, EngLimits lim) {
    LightLock_Lock(&g_lk);
    g_abort = 1;                                                               // stop whatever runs now
    g_job_pos = *p;
    if (nhist > 150) { hist += nhist - 150; nhist = 150; }
    for (int i = 0; i < nhist; i++) g_job_hist[i] = hist[i];
    g_job_nhist = nhist; g_job_lim = lim;
    g_job_id = g_next_id++;
    g_have_job = true; g_res_new = false;
    int id = g_job_id;
    LightLock_Unlock(&g_lk);
    return id;
}

void eng_async_cancel(void) {
    LightLock_Lock(&g_lk);
    g_abort = 1; g_have_job = false; g_job_id = g_next_id++; g_res_new = false;
    LightLock_Unlock(&g_lk);
}

bool eng_async_poll(EngResult *out) {
    bool n = false;
    LightLock_Lock(&g_lk);
    if (g_res_new) { *out = g_res; g_res_new = false; n = true; }
    LightLock_Unlock(&g_lk);
    return n;
}
