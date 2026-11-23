#include "audio.h"

#define NCH 6
static const char *FILES[SND_COUNT] = {"move_self", "move_opp", "capture", "castle", "check", "promote", "game_start",
                                       "win", "lose", "draw", "low_time", "premove", "illegal", "notify", "click"};
typedef struct { s16 *data; u32 n; u32 rate; } Sample;
static Sample g_snd[SND_COUNT];
static ndspWaveBuf g_wb[NCH];
static bool g_ok;
static int g_next;

static bool load_wav(const char *path, Sample *out) {
    size_t len; u8 *d = (u8 *)read_file(path, &len);
    if (!d || len < 44 || memcmp(d, "RIFF", 4)) { free(d); return false; }
    u32 rate = 22050; size_t off = 12; const u8 *pcm = NULL; u32 pcm_len = 0;
    while (off + 8 <= len) {
        u32 sz = *(u32 *)(d + off + 4);
        if (!memcmp(d + off, "fmt ", 4)) rate = *(u32 *)(d + off + 12);
        else if (!memcmp(d + off, "data", 4)) { pcm = d + off + 8; pcm_len = sz; break; }
        off += 8 + sz + (sz & 1);
    }
    if (!pcm) { free(d); return false; }
    if (off + 8 + pcm_len > len) pcm_len = (u32)(len - off - 8);
    s16 *lin = linearAlloc(pcm_len);
    if (!lin) { free(d); return false; }
    memcpy(lin, pcm, pcm_len);
    DSP_FlushDataCache(lin, pcm_len);
    out->data = lin; out->n = pcm_len / 2; out->rate = rate;
    free(d);
    return true;
}

bool audio_init(void) {
    Result rc = ndspInit();
    if (R_FAILED(rc)) { LOGF("ndspInit failed %08lx (no DSP firmware?) - sound disabled", (unsigned long)rc); return false; }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    for (int i = 0; i < SND_COUNT; i++) {
        char p[64]; snprintf(p, sizeof p, "romfs:/sfx/%s.wav", FILES[i]);
        if (!load_wav(p, &g_snd[i])) LOGF("sound %s missing", p);
    }
    for (int c = 0; c < NCH; c++) {
        ndspChnReset(c);
        ndspChnSetInterp(c, NDSP_INTERP_LINEAR);
        ndspChnSetFormat(c, NDSP_FORMAT_MONO_PCM16);
        g_wb[c].status = NDSP_WBUF_DONE;
    }
    g_ok = true;
    LOGF("audio ready");
    return true;
}

void audio_exit(void) {
    if (!g_ok) return;
    ndspExit();
    for (int i = 0; i < SND_COUNT; i++) if (g_snd[i].data) linearFree(g_snd[i].data);
    g_ok = false;
}

void audio_play(SndId id) {
    if (!g_ok || !g_set.sound || g_set.volume <= 0 || id >= SND_COUNT || !g_snd[id].data) return;
    int ch = -1;
    for (int k = 0; k < NCH; k++) {                    // prefer an idle channel, otherwise steal the next one
        int c = (g_next + k) % NCH;
        if (g_wb[c].status == NDSP_WBUF_DONE) { ch = c; break; }
    }
    if (ch < 0) ch = g_next;
    g_next = (ch + 1) % NCH;
    float vol = (float)g_set.volume / 100.0f; vol = vol * vol * 1.2f;
    if (vol > 1.0f) vol = 1.0f;
    float mix[12] = {0};
    mix[0] = mix[1] = vol;
    ndspChnWaveBufClear(ch);
    ndspChnSetRate(ch, (float)g_snd[id].rate);
    ndspChnSetMix(ch, mix);
    memset(&g_wb[ch], 0, sizeof g_wb[ch]);
    g_wb[ch].data_vaddr = g_snd[id].data;
    g_wb[ch].nsamples = g_snd[id].n;
    ndspChnWaveBufAdd(ch, &g_wb[ch]);
}

void audio_play_for_move(const Pos *before, Move m, const Pos *after, bool mine) {
    int w;
    bool check = pos_in_check(after, after->turn);
    OutKind o = pos_outcome(after, &w);
    if (o == OUT_CHECKMATE || check) { audio_play(SND_CHECK); return; }
    if (m.promo) { audio_play(SND_PROMOTE); return; }
    if (m.flags & MF_CASTLE) { audio_play(SND_CASTLE); return; }
    if (m.flags & MF_CAPTURE) { audio_play(SND_CAPTURE); return; }
    (void)before;
    audio_play(mine ? SND_MOVE_SELF : SND_MOVE_OPP);
}
