/* merakiplus_snd.c — Meraki+ sound helper: plays ONE sound file on the display's speaker and exits.
 *
 *   merakiplus_snd <file.wav|file.mp3> <volume 0..100>     play (blocks until the sound has drained)
 *   merakiplus_snd --check                                  load the libraries + resolve the symbols, play nothing
 *   merakiplus_snd --selftest                               (host build with -DSND_SELFTEST: mock MI_AO, no hardware)
 *
 * Exit: 0 played (or volume 0 = nothing to do), 2 usage, 3 file unreadable / not a supported WAV, 4 a library or symbol is
 * missing, 5 an MI_AO call failed, 6 the mp3 player failed.
 *
 * WHY A SEPARATE PROCESS: merakiplus is a static musl binary; the display's audio stack is glibc shared objects
 * (/lib/libmi_sys.so + /lib/libmi_ao.so, the SigmaStar MI layer over /dev/mi_ao). This helper is linked against the
 * display's glibc (2.28, armhf) and dlopen()s exactly those libraries, so no vendor code is shipped or linked at build time.
 * merakiplus starts it once per sound (fork + exec, one at a time) and never signals it: the helper ends itself after
 * 8 s (alarm(), default SIGALRM action) — see docs/audio.md.
 *
 * THE PATH IS THE STOCK ONE (reversed from the stock UI demo_v9 and its player libhvplayer.so; docs/audio.md):
 *  - WAV (our own PCM, audio/<name>.wav): what libhvplayer does after decoding (mm_audio_init 0x46B0 / mm_audio_play 0x45A8):
 *      dlopen libmi_common.so (RTLD_GLOBAL: libmi_ao.so's CamOs* symbols), libmi_sys.so, libmi_ao.so
 *      MI_SYS_Init()                                  (the stock UI calls it once at start; libhvplayer itself does not)
 *      MI_AO_SetPubAttr(0, {48000 Hz, 16 bit, mode 0, mono 0 / stereo 1, FrmNum 6, PtNumPerFrm 2048, CodecChn 0, Chn 1|2})
 *      MI_AO_Enable(0); MI_AO_EnableChn(0, 0); MI_AO_SetVolume(0, dB); MI_AO_SetMute(0, vol == 0)
 *      MI_AO_SendFrame(0, 0, &frame{bitwidth, soundmode, apVirAddr[0] = pcm, u32Len}, 128 ms) per chunk,
 *          retried while it returns 0xA005200D (the channel buffer is full)
 *      MI_AO_DisableChn(0, 0); MI_AO_Disable(0)       (mm_audio_deinit 0x4674)
 *    The device's AO always runs at 48 kHz (libhvplayer resamples everything to 0xBB80); our files are 16 kHz mono and are
 *    upsampled here (linear, x3) so the PCM leaves exactly like the stock player's.
 *  - MP3 (the stock UI's own /customer/audio/<name>.mp3, only if the user picks STOCK SOUNDS; never shipped): the stock call
 *    sequence of mmplayer_play_audio_once 0x136CE8 / mm_player_thread 0x135C78: mm_player_set_opts("audio_device","",0),
 *    ("audio_only","",1), ("video_only","",0), ("play_mode","",0); mm_player_open(file, 0, 0, 0, 0);
 *    mm_player_set_volumn(vol); poll mm_player_get_status() until it is != 0; mm_player_close(). Needs
 *    LD_LIBRARY_PATH=/customer/libs (libhvplayer.so + its ffmpeg), which merakiplus sets.
 *  - VOLUME: merakiplus passes the config "volume" already capped at 70 (stock deal_play 0x1372C4). The dB value is
 *    libhvplayer's mm_player_set_volumn 0x563C: vol <= 0 -> mute (-60 dB), else min(vol * 90 / 100 - 60, 15).
 *
 * Build (device): zig cc -target arm-linux-gnueabihf.2.28 -mcpu=cortex_a7 -O2 -s -fno-PIE -no-pie -o merakiplus_snd src/merakiplus_snd.c -ldl
 * Build (test):   zig cc -DSND_SELFTEST -O2 -o merakiplus_snd_selftest src/merakiplus_snd.c && ./merakiplus_snd_selftest
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#ifndef SND_SELFTEST
#include <dlfcn.h>
#endif

#define AO_DEV 0
#define AO_CHN 0
#define AO_RATE 48000
#define AO_FRMNUM 6
#define AO_PTNUM 2048
#define AO_CHUNK (AO_PTNUM * 2)          /* bytes per SendFrame for mono 16 bit (stock sends up to 25000) */
#define AO_ERR_BUF_FULL 0xA005200Du      /* MI_AO_SendFrame: channel buffer full -> retry (mm_audio_play 0x4634) */
#define SEND_WAIT_MS 128                 /* mm_audio_play passes 128 */
#define MAX_PCM_BYTES (4 * 1024 * 1024)

/* MI_AUDIO_Attr_t as libhvplayer fills it on the stack (sp+8 .. sp+0x34): 11 words */
typedef struct { uint32_t rate, bitwidth, workmode, soundmode, frmnum, ptnum, codecchn, chn, i2s[3]; } AoAttr;
/* MI_AUDIO_Frame_t: bitwidth +0, soundmode +4, apVirAddr[16] +8, u64TimeStamp +0x48, u32Seq +0x50, u32Len +0x54, ... */
typedef struct { uint32_t bitwidth, soundmode; void *vir[16]; uint64_t ts; uint32_t seq, len, pool[2]; void *src[16]; uint32_t srclen; } AoFrame;
_Static_assert(__builtin_offsetof(AoFrame, len) == (sizeof(void *) == 4 ? 0x54 : 0x94), "u32Len must be at +0x54 on 32-bit ARM");

typedef int (*f_v)(void);
typedef int (*f_i)(int);
typedef int (*f_ii)(int, int);
typedef int (*f_ip)(int, void *);
typedef int (*f_iipi)(int, int, void *, int);
static struct {
    f_v sys_init;
    f_ip set_attr;
    f_i enable, disable;
    f_ii enable_chn, disable_chn, set_volume, set_mute;
    f_iipi send;
} mi;

/* libhvplayer mm_player_set_volumn 0x563C: 0 -> mute (and -60 dB); else (vol * 90) / 100 - 60, at most 15 dB */
static int vol_db(int vol, int *mute) {
    if (vol <= 0) { *mute = 1; return -60; }
    *mute = 0;
    int db = vol * 90 / 100 - 60;
    return db < 15 ? db : 15;
}

/* ---- WAV (RIFF PCM 16 bit, 1 or 2 channels) ---- */
typedef struct { int rate, ch; const uint8_t *pcm; uint32_t len; } Wav;
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static int wav_parse(const uint8_t *d, size_t n, Wav *w) {
    if (n < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) return 0;
    size_t o = 12; int fmt = 0;
    memset(w, 0, sizeof *w);
    while (o + 8 <= n) {
        uint32_t cl = le32(d + o + 4); const uint8_t *c = d + o + 8;
        if (cl > n - o - 8) return 0;
        if (!memcmp(d + o, "fmt ", 4) && cl >= 16) {
            if (le16(c) != 1 || le16(c + 14) != 16) return 0;      /* PCM, 16 bit only */
            w->ch = le16(c + 2); w->rate = (int)le32(c + 4); fmt = 1;
            if (w->ch < 1 || w->ch > 2 || w->rate < 8000 || w->rate > 48000) return 0;
        } else if (!memcmp(d + o, "data", 4)) {
            if (!fmt) return 0;
            w->pcm = c; w->len = cl & ~(uint32_t)(2 * w->ch - 1); return w->len > 0;
        }
        o += 8 + cl + (cl & 1);
    }
    return 0;
}
/* up to 48 kHz by an integer factor (16 k -> x3, 24 k -> x2), linear; returns malloc'd PCM, *on = bytes */
static int16_t *upsample(const Wav *w, uint32_t *on) {
    int f = AO_RATE % w->rate == 0 ? AO_RATE / w->rate : 1;
    uint32_t frames = w->len / (2u * w->ch), outf = frames * (uint32_t)f;
    if ((uint64_t)outf * 2 * w->ch > MAX_PCM_BYTES) return NULL;
    int16_t *o = malloc((size_t)outf * 2 * w->ch + 2);
    if (!o) return NULL;
    for (uint32_t i = 0; i < frames; i++)
        for (int c = 0; c < w->ch; c++) {
            int a = (int16_t)le16(w->pcm + 2 * (i * w->ch + c));
            int b = i + 1 < frames ? (int16_t)le16(w->pcm + 2 * ((i + 1) * w->ch + c)) : a;
            for (int k = 0; k < f; k++) o[(i * f + k) * w->ch + c] = (int16_t)(a + (b - a) * k / f);
        }
    *on = outf * 2u * w->ch;
    return o;
}
static void sleep_ms(int ms) { struct timespec t = {ms / 1000, (long)(ms % 1000) * 1000000L}; nanosleep(&t, NULL); }

/* the stock AO sequence; pcm at `rate` (48000 after upsampling) */
static int ao_play(const int16_t *pcm, uint32_t len, int rate, int ch, int vol) {
    int mute, db = vol_db(vol, &mute), rc = 0;
    AoAttr a; memset(&a, 0, sizeof a);
    a.rate = (uint32_t)rate; a.bitwidth = 0; a.workmode = 0; a.soundmode = ch == 2 ? 1 : 0;
    a.frmnum = AO_FRMNUM; a.ptnum = AO_PTNUM; a.codecchn = 0; a.chn = (uint32_t)ch;
    if (mi.sys_init() != 0) fprintf(stderr, "merakiplus_snd: MI_SYS_Init != 0 (continuing: already initialised?)\n");
    if (mi.set_attr(AO_DEV, &a) || mi.enable(AO_DEV) || mi.enable_chn(AO_DEV, AO_CHN)) { rc = 5; goto out; }
    mi.set_volume(AO_DEV, db);
    mi.set_mute(AO_DEV, mute);
    for (uint32_t off = 0; off < len && rc == 0;) {
        uint32_t n = len - off > (uint32_t)AO_CHUNK * ch ? (uint32_t)AO_CHUNK * ch : len - off;
        AoFrame f; memset(&f, 0, sizeof f);
        f.bitwidth = 0; f.soundmode = a.soundmode; f.vir[0] = (void *)((const uint8_t *)pcm + off); f.len = n;
        int tries = 0, r;
        while ((uint32_t)(r = mi.send(AO_DEV, AO_CHN, &f, SEND_WAIT_MS)) == AO_ERR_BUF_FULL && tries++ < 400) sleep_ms(5);
        if (r != 0) { fprintf(stderr, "merakiplus_snd: MI_AO_SendFrame = 0x%08x\n", (unsigned)r); rc = 5; break; }
        off += n;
    }
    /* drain: what is queued in the AO buffers (at most FrmNum frames) plays out before the channel is disabled */
    if (rc == 0) sleep_ms(AO_FRMNUM * AO_PTNUM * 1000 / rate + 60);
out:
    mi.disable_chn(AO_DEV, AO_CHN);
    mi.disable(AO_DEV);
    return rc;
}

#ifndef SND_SELFTEST
static void *sym(void *h, const char *n, int *ok) { void *p = h ? dlsym(h, n) : NULL; if (!p) { fprintf(stderr, "merakiplus_snd: missing %s\n", n); *ok = 0; } return p; }
/* The display's libmi_ao.so lists only libc.so.6 as DT_NEEDED but leaves its OS layer (CamOsMemAlloc, CamOsRwsem*,
 * CamOsGetTimeOfDay, CamOsPrintf, ...) undefined: in the stock UI those come from libmi_common.so, which demo_v9 links
 * (libhvplayer.so NEEDs it too). So libmi_common.so is loaded first, RTLD_GLOBAL, then libmi_sys.so and libmi_ao.so. */
static int mi_load(void) {
    void *hc = dlopen("libmi_common.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hc) { fprintf(stderr, "merakiplus_snd: dlopen: %s\n", dlerror()); return 0; }
    void *hs = dlopen("libmi_sys.so", RTLD_NOW | RTLD_GLOBAL), *ha = hs ? dlopen("libmi_ao.so", RTLD_NOW | RTLD_GLOBAL) : NULL;
    if (!hs || !ha) { fprintf(stderr, "merakiplus_snd: dlopen: %s\n", dlerror()); return 0; }
    int ok = 1;
    mi.sys_init = (f_v)sym(hs, "MI_SYS_Init", &ok);
    mi.set_attr = (f_ip)sym(ha, "MI_AO_SetPubAttr", &ok);
    mi.enable = (f_i)sym(ha, "MI_AO_Enable", &ok);
    mi.disable = (f_i)sym(ha, "MI_AO_Disable", &ok);
    mi.enable_chn = (f_ii)sym(ha, "MI_AO_EnableChn", &ok);
    mi.disable_chn = (f_ii)sym(ha, "MI_AO_DisableChn", &ok);
    mi.set_volume = (f_ii)sym(ha, "MI_AO_SetVolume", &ok);
    mi.set_mute = (f_ii)sym(ha, "MI_AO_SetMute", &ok);
    mi.send = (f_iipi)sym(ha, "MI_AO_SendFrame", &ok);
    return ok;
}
/* the stock UI's mp3 path through libhvplayer (mm_player), exactly its call sequence */
static int mp3_play(const char *file, int vol) {
    void *hs = dlopen("libmi_sys.so", RTLD_NOW | RTLD_GLOBAL), *hp = dlopen("libhvplayer.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hs || !hp) { fprintf(stderr, "merakiplus_snd: dlopen: %s\n", dlerror()); return 4; }
    int ok = 1;
    f_v sys_init = (f_v)sym(hs, "MI_SYS_Init", &ok);
    int (*set_opts)(const char *, const char *, int) = (int (*)(const char *, const char *, int))sym(hp, "mm_player_set_opts", &ok);
    int (*open_)(const char *, uint16_t, uint16_t, uint16_t, uint16_t) = (int (*)(const char *, uint16_t, uint16_t, uint16_t, uint16_t))sym(hp, "mm_player_open", &ok);
    f_i set_vol = (f_i)sym(hp, "mm_player_set_volumn", &ok);
    f_v status = (f_v)sym(hp, "mm_player_get_status", &ok);
    f_v close_ = (f_v)sym(hp, "mm_player_close", &ok);
    if (!ok) return 4;
    sys_init();
    set_opts("audio_device", "", 0); set_opts("audio_only", "", 1); set_opts("video_only", "", 0); set_opts("play_mode", "", 0);
    if (open_(file, 0, 0, 0, 0) < 0) { fprintf(stderr, "merakiplus_snd: mm_player_open failed\n"); return 6; }
    set_vol(vol);
    for (int t = 0; t < 7000 / 20; t++) { if (status() != 0) break; sleep_ms(20); }   /* stock: get_status != 0 -> done */
    close_();
    return 0;
}
static uint8_t *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    uint8_t *b = malloc(MAX_PCM_BYTES + 64); if (!b) { fclose(f); return NULL; }
    *n = fread(b, 1, MAX_PCM_BYTES + 64, f); fclose(f); return b;
}
int main(int argc, char **argv) {
    alarm(8);                                                     /* a hang in a vendor library must not outlive the sound */
    if (argc == 2 && !strcmp(argv[1], "--check")) { int ok = mi_load(); printf("merakiplus_snd: MI_SYS + MI_AO %s\n", ok ? "ok" : "MISSING"); return ok ? 0 : 4; }
    if (argc != 3) { fprintf(stderr, "usage: merakiplus_snd <file.wav|file.mp3> <volume 0..100> | --check\n"); return 2; }
    int vol = atoi(argv[2]); if (vol < 0) vol = 0; if (vol > 100) vol = 100;
    if (vol == 0) return 0;                                       /* mute: nothing to do (merakiplus does not call it at 0 either) */
    size_t L = strlen(argv[1]);
    if (L > 4 && !strcmp(argv[1] + L - 4, ".mp3")) return mp3_play(argv[1], vol);
    size_t n = 0; uint8_t *d = slurp(argv[1], &n); Wav w;
    if (!d || !wav_parse(d, n, &w)) { fprintf(stderr, "merakiplus_snd: %s: not a 16-bit PCM WAV\n", argv[1]); return 3; }
    uint32_t on = 0; int16_t *pcm = upsample(&w, &on);
    if (!pcm) return 3;
    if (!mi_load()) return 4;
    return ao_play(pcm, on, AO_RATE % w.rate == 0 ? AO_RATE : w.rate, w.ch, vol);
}
#else
/* ================= host self-test: a mock MI_AO records the calls ================= */
static char calls[4096]; static int n_send, n_full, last_len_max, vol_db_seen, mute_seen; static AoAttr attr_seen; static uint64_t bytes_sent;
static void rec(const char *s) { strncat(calls, s, sizeof calls - strlen(calls) - 1); strncat(calls, " ", sizeof calls - strlen(calls) - 1); }
static int m_sys(void) { rec("SYS_Init"); return 0; }
static int m_attr(int d, void *a) { (void)d; memcpy(&attr_seen, a, sizeof attr_seen); rec("SetPubAttr"); return 0; }
static int m_en(int d) { (void)d; rec("Enable"); return 0; }
static int m_dis(int d) { (void)d; rec("Disable"); return 0; }
static int m_enc(int d, int c) { (void)d; (void)c; rec("EnableChn"); return 0; }
static int m_disc(int d, int c) { (void)d; (void)c; rec("DisableChn"); return 0; }
static int m_vol(int d, int v) { (void)d; vol_db_seen = v; rec("SetVolume"); return 0; }
static int m_mute(int d, int m) { (void)d; mute_seen = m; rec("SetMute"); return 0; }
static int m_send(int d, int c, void *f, int ms) {
    (void)d; (void)c; (void)ms; AoFrame *fr = f;
    if (n_full < 2 && n_send == 2) { n_full++; return (int)AO_ERR_BUF_FULL; }   /* the buffer is full twice -> retried */
    if (n_send == 0) rec("SendFrame...");
    n_send++; bytes_sent += fr->len; if ((int)fr->len > last_len_max) last_len_max = (int)fr->len; return 0;
}
static int st_n, st_f;
static void chk(const char *name, int ok) { printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name); st_n++; if (!ok) st_f++; }
static uint8_t *rd(const char *p, size_t *n) { FILE *f = fopen(p, "rb"); if (!f) return NULL; uint8_t *b = malloc(1 << 22); *n = fread(b, 1, 1 << 22, f); fclose(f); return b; }
int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "audio";
    mi.sys_init = m_sys; mi.set_attr = m_attr; mi.enable = m_en; mi.disable = m_dis; mi.enable_chn = m_enc; mi.disable_chn = m_disc;
    mi.set_volume = m_vol; mi.set_mute = m_mute; mi.send = m_send;
    int m; int d0 = vol_db(0, &m), m0 = m, d40 = vol_db(40, &m), d70 = vol_db(70, &m), d100 = vol_db(100, &m), d1 = vol_db(1, &m);
    chk("volume -> dB = libhvplayer mm_player_set_volumn: 0 mute/-60, 1 -60, 40 -24, 70 +3, 100 capped +15", d0 == -60 && m0 && d1 == -60 && d40 == -24 && d70 == 3 && d100 == 15);
    chk("AO attribute layout = libhvplayer's stack frame (11 words; Frame.u32Len at +0x54 on ARM)", sizeof(AoAttr) == 44);
    static const char *const F[3] = {"start.wav", "ready_extract.wav", "ready_steam.wav"};
    uint32_t len3[3] = {0};
    for (int i = 0; i < 3; i++) {
        char p[512]; snprintf(p, sizeof p, "%s/%s", dir, F[i]); size_t n = 0; uint8_t *d = rd(p, &n); Wav w;
        int ok = d && wav_parse(d, n, &w) && w.rate == 16000 && w.ch == 1;
        chk(F[i], ok);
        if (!ok) continue;
        uint32_t on = 0; int16_t *pcm = upsample(&w, &on); len3[i] = on;
        chk("  upsampled 16 kHz -> 48 kHz: exactly 3x the samples, the first sample unchanged", pcm && on == w.len * 3 && pcm[0] == (int16_t)le16(w.pcm));
        calls[0] = 0; n_send = n_full = last_len_max = 0; bytes_sent = 0;
        int rc = ao_play(pcm, on, 48000, 1, 40);
        if (i == 0) {
            chk("  call order = stock: SYS_Init SetPubAttr Enable EnableChn SetVolume SetMute SendFrame... DisableChn Disable",
                !strcmp(calls, "SYS_Init SetPubAttr Enable EnableChn SetVolume SetMute SendFrame... DisableChn Disable "));
            chk("  attributes = libhvplayer mm_audio_init: 48000 Hz, 16 bit (0), mode 0, mono (0), FrmNum 6, PtNumPerFrm 2048, CodecChn 0, Chn 1",
                attr_seen.rate == 48000 && attr_seen.bitwidth == 0 && attr_seen.workmode == 0 && attr_seen.soundmode == 0 && attr_seen.frmnum == 6 &&
                attr_seen.ptnum == 2048 && attr_seen.codecchn == 0 && attr_seen.chn == 1);
            chk("  SendFrame: every byte once, chunks <= 4096 bytes (one AO frame), 0xA005200D (buffer full) retried",
                rc == 0 && bytes_sent == on && last_len_max <= 4096 && n_full == 2);
            chk("  volume 40 -> SetVolume(-24 dB), SetMute(0)", vol_db_seen == -24 && mute_seen == 0);
        }
        free(pcm); free(d);
    }
    chk("the three files differ in length (three distinct sounds)", len3[0] != len3[1] && len3[1] != len3[2] && len3[0] != len3[2]);
    { uint8_t bad[44] = "RIFF\0\0\0\0WAVEfmt "; Wav w; chk("a truncated / non-PCM file is rejected", !wav_parse(bad, sizeof bad, &w) && !wav_parse((const uint8_t *)"ID3", 3, &w)); }
    printf("merakiplus_snd selftest: %s (%d checks, %d failed)\n", st_f ? "FAILED" : "ALL OK", st_n, st_f);
    return st_f ? 1 : 0;
}
#endif
