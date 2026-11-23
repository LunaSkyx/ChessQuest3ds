#include "lichess.h"
#include <curl/curl.h>
#include <malloc.h>

#define HOST "https://lichess.org"
#define EXPLORER_HOST "https://explorer.lichess.ovh"
#define MAX_BODY (1 << 20)
#define STREAM_IDLE_MS 45000

typedef struct Req {
    struct Req *next;
    int id, tag;
    bool stream;
    char method[8];
    char *path, *body;
    CURL *easy;
    struct curl_slist *hdr;
    char *buf; size_t len, cap;      // line buffer (stream) or body buffer
    u64 last_data;
    char errbuf[CURL_ERROR_SIZE];
} Req;

static LightLock g_lock;
static Req *g_pend, *g_pend_tail;                 // submitted by the UI thread, not yet started
static int g_cancel[32]; static int g_ncancel;
static LiMsg *g_out, *g_out_tail;
static Req *g_active;                              // owned by the worker
static int g_next_id = 1, g_nactive;
static char g_token[128];
static bool g_verify = true;
static volatile bool g_quit;
static Thread g_thread;
static CURLM *g_multi;
static u32 *g_soc;

void li_set_token(const char *t) { LightLock_Lock(&g_lock); snprintf(g_token, sizeof g_token, "%s", t ? t : ""); LightLock_Unlock(&g_lock); }
void li_set_verify(bool v) { g_verify = v; }
int li_active_count(void) { return g_nactive; }

void li_url_encode(const char *in, char *out, size_t n) {
    size_t k = 0;
    for (; *in && k + 4 < n; in++) {
        unsigned char c = (unsigned char)*in;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out[k++] = (char)c;
        else k += (size_t)snprintf(out + k, n - k, "%%%02X", c);
    }
    out[k] = 0;
}

static void push_out(LiMsg *m) {
    m->next = NULL;
    LightLock_Lock(&g_lock);
    if (g_out_tail) g_out_tail->next = m; else g_out = m;
    g_out_tail = m;
    LightLock_Unlock(&g_lock);
}

static LiMsg *new_msg(int kind, const Req *r) {
    LiMsg *m = calloc(1, sizeof *m);
    m->kind = kind; m->req = r->id; m->tag = r->tag;
    return m;
}

static void emit_line(Req *r, const char *s, size_t n) {
    while (n && (s[n - 1] == '\r' || s[n - 1] == ' ')) n--;
    if (!n) return;                                   // keep-alive newline
    LiMsg *m = new_msg(LIM_LINE, r);
    m->data = malloc(n + 1); memcpy(m->data, s, n); m->data[n] = 0; m->len = n;
    push_out(m);
}

static size_t write_cb(char *p, size_t sz, size_t nm, void *ud) {
    Req *r = ud; size_t n = sz * nm;
    r->last_data = now_ms();
    if (r->len + n + 1 > r->cap) {
        if (!r->stream && r->len + n > MAX_BODY) return 0;
        size_t nc = r->cap ? r->cap : 1024;
        while (nc < r->len + n + 1) nc *= 2;
        char *nb = realloc(r->buf, nc);
        if (!nb) return 0;
        r->buf = nb; r->cap = nc;
    }
    memcpy(r->buf + r->len, p, n); r->len += n; r->buf[r->len] = 0;
    if (r->stream) {
        size_t start = 0;
        for (size_t i = 0; i < r->len; i++)
            if (r->buf[i] == '\n') { emit_line(r, r->buf + start, i - start); start = i + 1; }
        if (start) { memmove(r->buf, r->buf + start, r->len - start); r->len -= start; r->buf[r->len] = 0; }
    }
    return n;
}

static void start_req(Req *r) {
    r->easy = curl_easy_init();
    char url[1500];
    if (!strncmp(r->path, EXPLORER_HOST "/", sizeof EXPLORER_HOST)) snprintf(url, sizeof url, "%s", r->path);   // the official opening explorer (same token)
    else snprintf(url, sizeof url, HOST "%s", r->path);
    char auth[200];
    LightLock_Lock(&g_lock); snprintf(auth, sizeof auth, "Authorization: Bearer %s", g_token); LightLock_Unlock(&g_lock);
    if (g_token[0]) r->hdr = curl_slist_append(r->hdr, auth);
    r->hdr = curl_slist_append(r->hdr, r->stream ? "Accept: application/x-ndjson" : "Accept: application/json");
    CURL *c = r->easy;
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, r->hdr);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, r);
    curl_easy_setopt(c, CURLOPT_PRIVATE, r);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, r->errbuf);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "ChessQuest3DS/" APP_VERSION);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(c, CURLOPT_TCP_KEEPIDLE, 30L);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    if (g_verify) {
        curl_easy_setopt(c, CURLOPT_CAINFO, "romfs:/cacert.pem");
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    } else {
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    if (!r->stream) curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    if (!strcmp(r->method, "POST")) {
        if (r->body && r->body[0] == '{') r->hdr = curl_slist_append(r->hdr, "Content-Type: application/json");
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, r->hdr);
        curl_easy_setopt(c, CURLOPT_POST, 1L);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, r->body ? r->body : "");
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)(r->body ? strlen(r->body) : 0));
    } else if (!strcmp(r->method, "DELETE")) {
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "DELETE");
    }
    r->last_data = now_ms();
    curl_multi_add_handle(g_multi, c);
    r->next = g_active; g_active = r; g_nactive++;
    LOGF("net: start #%d %s %s%s", r->id, r->method, r->path, r->stream ? " (stream)" : "");
}

static void free_req(Req *r) {
    if (r->easy) { curl_multi_remove_handle(g_multi, r->easy); curl_easy_cleanup(r->easy); }
    if (r->hdr) curl_slist_free_all(r->hdr);
    free(r->path); free(r->body); free(r->buf); free(r);
}

static void unlink_req(Req *r) {
    for (Req **pp = &g_active; *pp; pp = &(*pp)->next)
        if (*pp == r) { *pp = r->next; g_nactive--; break; }
}

static void finish_req(Req *r, int rc, long http) {
    if (r->stream && r->len) emit_line(r, r->buf, r->len);
    LiMsg *m = new_msg(r->stream ? LIM_END : LIM_RESPONSE, r);
    m->http = http; m->curl_rc = rc;
    if (rc) snprintf(m->err, sizeof m->err, "%s", r->errbuf[0] ? r->errbuf : curl_easy_strerror((CURLcode)rc));
    if (!r->stream) { m->data = r->buf ? r->buf : strdup(""); m->len = r->len; r->buf = NULL; }
    push_out(m);
    double tc = 0, tt = 0, ts = 0; long nc = 0;
    curl_easy_getinfo(r->easy, CURLINFO_CONNECT_TIME, &tc); curl_easy_getinfo(r->easy, CURLINFO_TOTAL_TIME, &tt);
    curl_easy_getinfo(r->easy, CURLINFO_STARTTRANSFER_TIME, &ts); curl_easy_getinfo(r->easy, CURLINFO_NUM_CONNECTS, &nc);
    LOGF("net: done #%d rc=%d http=%ld  (new conn %ld, connect %.0f ms, first byte %.0f ms, total %.0f ms)", r->id, rc, http, nc, tc * 1000, ts * 1000, tt * 1000);
    unlink_req(r);
    free_req(r);
}

static void worker(void *arg) {
    (void)arg;
    while (!g_quit) {
        // take submissions + cancellations
        LightLock_Lock(&g_lock);
        Req *pend = g_pend; g_pend = g_pend_tail = NULL;
        int cancels[32]; int nc = g_ncancel; memcpy(cancels, g_cancel, sizeof(int) * (size_t)nc); g_ncancel = 0;
        LightLock_Unlock(&g_lock);
        while (pend) { Req *n = pend->next; pend->next = NULL; start_req(pend); pend = n; }
        for (int i = 0; i < nc; i++)
            for (Req *r = g_active; r; r = r->next)
                if (r->id == cancels[i]) {
                    LOGF("net: cancel #%d", r->id);
                    LiMsg *m = new_msg(LIM_END, r); m->http = 0; m->curl_rc = -1; snprintf(m->err, sizeof m->err, "cancelled");
                    push_out(m);
                    unlink_req(r); free_req(r);
                    break;
                }

        int running = 0;
        curl_multi_perform(g_multi, &running);
        CURLMsg *cm; int left;
        while ((cm = curl_multi_info_read(g_multi, &left))) {
            if (cm->msg != CURLMSG_DONE) continue;
            Req *r = NULL; curl_easy_getinfo(cm->easy_handle, CURLINFO_PRIVATE, (char **)&r);
            long http = 0; curl_easy_getinfo(cm->easy_handle, CURLINFO_RESPONSE_CODE, &http);
            if (r) finish_req(r, (int)cm->data.result, http);
        }
        // idle watchdog for streams (the server sends a keep-alive newline every few seconds)
        u64 t = now_ms();
        for (Req *r = g_active; r; r = r->next)
            if (r->stream && t - r->last_data > STREAM_IDLE_MS) {
                LOGF("net: stream #%d idle, aborting", r->id);
                finish_req(r, CURLE_OPERATION_TIMEDOUT, 0);
                break;
            }
        if (!g_nactive) svcSleepThread(10 * 1000000LL);
        else { curl_multi_poll(g_multi, NULL, 0, 5, NULL); svcSleepThread(1000000LL); }
    }
    while (g_active) { Req *r = g_active; unlink_req(r); free_req(r); }
}

bool li_start(void) {
    LightLock_Init(&g_lock);
    g_soc = memalign(0x1000, 0x100000);
    Result rc = g_soc ? socInit(g_soc, 0x100000) : -1;
    if (R_FAILED(rc)) { LOGF("socInit failed %08lx", (unsigned long)rc); return false; }
    curl_global_init(CURL_GLOBAL_DEFAULT);
    g_multi = curl_multi_init();
    curl_multi_setopt(g_multi, CURLMOPT_MAXCONNECTS, 8L);
    APT_SetAppCpuTimeLimit(30);                       // lets the worker use the system core on an old 3DS
    g_quit = false;
    g_thread = threadCreate(worker, NULL, 128 * 1024, 0x30, 1, false);
    if (!g_thread) { LOGF("net thread failed"); return false; }
    LOGF("net: started");
    return true;
}

void li_stop(void) {
    if (!g_thread) return;
    g_quit = true;
    threadJoin(g_thread, U64_MAX); threadFree(g_thread); g_thread = NULL;
    curl_multi_cleanup(g_multi); curl_global_cleanup();
    socExit(); free(g_soc);
    while (g_out) { LiMsg *n = g_out->next; free(g_out->data); free(g_out); g_out = n; }
}

int li_request(int tag, const char *method, const char *path, const char *body, bool stream) {
    Req *r = calloc(1, sizeof *r);
    LightLock_Lock(&g_lock);
    r->id = g_next_id++;
    r->tag = tag; r->stream = stream;
    snprintf(r->method, sizeof r->method, "%s", method);
    r->path = strdup(path); r->body = body ? strdup(body) : NULL;
    if (g_pend_tail) g_pend_tail->next = r; else g_pend = r;
    g_pend_tail = r;
    int id = r->id;
    LightLock_Unlock(&g_lock);
    return id;
}

void li_cancel(int req) {
    LightLock_Lock(&g_lock);
    // not started yet? drop it silently
    for (Req **pp = &g_pend; *pp; pp = &(*pp)->next)
        if ((*pp)->id == req) { Req *r = *pp; *pp = r->next; if (g_pend_tail == r) g_pend_tail = NULL; free(r->path); free(r->body); free(r); LightLock_Unlock(&g_lock); return; }
    if (g_ncancel < 32) g_cancel[g_ncancel++] = req;
    LightLock_Unlock(&g_lock);
}

LiMsg *li_poll(void) {
    LightLock_Lock(&g_lock);
    LiMsg *m = g_out;
    if (m) { g_out = m->next; if (!g_out) g_out_tail = NULL; }
    LightLock_Unlock(&g_lock);
    return m;
}

void li_msg_free(LiMsg *m) { if (m) { free(m->data); free(m); } }
