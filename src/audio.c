/* Sound effects (the game's 11 WAVs, resampled to 48 kHz stereo once) and the XM music through jar_xm
 * (WTFPL, derived from libxm) on the platform layer's single music stream. */
#include "gm.h"
#include "plat.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JAR_XM_IMPLEMENTATION
#include "../third_party/jar_xm.h"

#define RATE 48000
#define MUSIC_GAIN 0.7f
#define SFX_GAIN 1.0f

static short* pcm[N_SOUNDS];

static int le16(const uint8_t* p) { return p[0] | p[1] << 8; }
static long le32(const uint8_t* p) { return (long)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }

/* RIFF/WAVE (PCM 8/16 bit, mono/stereo) -> 48 kHz stereo S16 */
static short* wav_to_48k(const uint8_t* d, long n, int* frames) {
    long p = 12, dlen = 0, dpos = -1;
    int chans = 1, bits = 16, rate = 44100, fmt_ok = 0;
    long nin, i, nout;
    short* out;
    if (n < 44 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) return NULL;
    while (p + 8 <= n) {
        long cl = le32(d + p + 4);
        if (!memcmp(d + p, "fmt ", 4) && p + 8 + 16 <= n) {
            fmt_ok = le16(d + p + 8) == 1;
            chans = le16(d + p + 10); rate = (int)le32(d + p + 12); bits = le16(d + p + 22);
        } else if (!memcmp(d + p, "data", 4)) { dpos = p + 8; dlen = cl; if (dpos + dlen > n) dlen = n - dpos; break; }
        p += 8 + cl + (cl & 1);
    }
    if (!fmt_ok || dpos < 0 || (chans != 1 && chans != 2) || (bits != 16 && bits != 8) || rate <= 0) return NULL;
    nin = dlen / (chans * bits / 8);
    nout = nin * RATE / rate;
    if (nout < 1) return NULL;
    out = (short*)malloc((size_t)nout * 4);
    for (i = 0; i < nout; i++) {
        double pos = (double)i * rate / RATE;
        long i0 = (long)pos, i1 = i0 + 1;
        double f = pos - (double)i0;
        int c;
        if (i1 >= nin) i1 = nin - 1;
        for (c = 0; c < 2; c++) {
            int ch = chans == 1 ? 0 : c;
            double a, b;
            if (bits == 16) {
                a = (short)le16(d + dpos + (i0 * chans + ch) * 2);
                b = (short)le16(d + dpos + (i1 * chans + ch) * 2);
            } else {
                a = (d[dpos + i0 * chans + ch] - 128) * 256.0;
                b = (d[dpos + i1 * chans + ch] - 128) * 256.0;
            }
            out[i * 2 + c] = (short)lrint(a + (b - a) * f);
        }
    }
    *frames = (int)nout;
    return out;
}

void snd_init(void) {
    int i;
    for (i = 0; i < N_SOUNDS; i++) {
        int fr = 0;
        pcm[i] = wav_to_48k(A.wav[i].data, A.wav[i].len, &fr);
        if (pcm[i]) plat_snd_set(i, pcm[i], fr);
        else fprintf(stderr, "psychosomnium: sound %d could not be decoded\n", i);
    }
}

void sound_play(int id) {
    if (id >= 0 && id < N_SOUNDS && pcm[id]) plat_snd_play(id, SFX_GAIN);
}

/* ------------------------------------------------------------------------------------------ music */
static jar_xm_context_t* ctx;
static volatile int playing_idx = -1;
static volatile float fade_gain = 1.0f;
static volatile int fade_left, fade_total;

static volatile int music_paused;
void music_pause(int on) { music_paused = on; }

static int music_fill(void* ud, short* dst, int frames) {
    static float buf[1024 * 2];
    int done = 0;
    (void)ud;
    if (music_paused) { memset(dst, 0, (size_t)frames * 4); return frames; }      /* paused: silence, the song does not advance */
    while (done < frames) {
        int n = frames - done > 1024 ? 1024 : frames - done, i;
        float g = fade_gain * MUSIC_GAIN;
        jar_xm_generate_samples(ctx, buf, (size_t)n);
        for (i = 0; i < n * 2; i++) {
            float v = buf[i] * g * 32767.0f;
            if (v > 32767.0f) v = 32767.0f;
            if (v < -32768.0f) v = -32768.0f;
            dst[done * 2 + i] = (short)v;
        }
        if (fade_left > 0) {
            fade_left -= n; if (fade_left < 0) fade_left = 0;
            fade_gain = fade_total ? (float)fade_left / (float)fade_total : 0.0f;
        }
        done += n;
    }
    return frames;
}

void music_load(int xm) { (void)xm; /* contexts are built on demand when a track starts */ }

void music_stop(void) {
    plat_stream_stop();
    if (ctx) { jar_xm_free_context(ctx); ctx = NULL; }
    playing_idx = -1;
}

void music_stop_if(int xm) { if (playing_idx == xm) music_stop(); }

void music_play(int xm) {
    music_stop();
    if (xm < 0 || xm >= N_XM || !A.xm[xm].data || !plat_stream_ok()) return;
    if (jar_xm_create_context_safe(&ctx, (const char*)A.xm[xm].data, (size_t)A.xm[xm].len, RATE) != 0) { ctx = NULL; fprintf(stderr, "psychosomnium: music %d could not be loaded\n", xm); return; }
    fade_gain = 1.0f; fade_left = fade_total = 0;
    playing_idx = xm;
    if (!plat_stream_start(music_fill, NULL, RATE, 1.0f)) { jar_xm_free_context(ctx); ctx = NULL; playing_idx = -1; }
}

void music_fade(int ms) {
    fade_total = fade_left = (int)((long)ms * RATE / 1000);
    if (fade_total <= 0) fade_gain = 0.0f;
}

void audio_tick(void) {}
