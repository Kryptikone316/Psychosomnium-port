/* Platform layer on SDL2.
 *  - SF3500 build (-DSF3500): frames go to the device display driver (hwdisp), the pad is read from
 *    the shared joy_key memory (joykey), sound goes to the software mixer -> audio driver.
 *    SDL itself is only used for timing and screenshots there: this device has no usable SDL video
 *    or audio device (run it with SDL_VIDEODRIVER=dummy).
 *  - Desktop build: an SDL2 window, the keyboard, SDL audio.
 * Test hooks (both builds): PSY_HEADLESS=1 (no window/driver), PSY_SHOTS=<dir> + PSY_SHOT_EVERY=<frames>,
 * PSY_JOY="frame:mask,frame:mask" scripted pad (mask in hex or decimal), PSY_MAXFRAMES=<n>, PSY_SHOT_FRAMES="f1,f2,..." (save only those frames). */
#include "plat.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

#ifdef SF3500
#include "hwdisp.hpp"
#include "joykey.hpp"
#include "sf3500_mixer.hpp"
#endif

#define NSND 16

static uint16_t* g_fb;
static int g_fps = 50, g_headless, g_shot_every, g_maxframes;
static unsigned g_frame, g_last_ticks;
static double g_next;                         /* when the next frame is due (ms), kept as a fraction: 60 fps = 16.667 ms */
static const char* g_shots;
static char g_script[2048];
static unsigned g_shot_list[1024];
static int g_nshot_list;
static SDL_Surface* g_surf;

#ifdef SF3500
/* Pad reader thread: polls the shared pad word at ~1 kHz instead of once per frame, so a tap shorter than a frame is not lost, a button
 * that flickers for a few milliseconds while held does not register as release+press (which made one hold into two jumps), and brief
 * glitches are logged (log.txt) to see what the hardware really does. */
#include <time.h>
static volatile unsigned g_in_cur;              /* the last polled state */
static volatile unsigned g_in_tap;              /* buttons seen pressed since the game last looked */
static double g_in_last[16];                    /* ms timestamp of the last time each button was seen down (poll thread only) */
static volatile int g_in_debounce = 25;         /* ms a button counts as still down after it was last seen down */
static volatile int g_in_run;
static SDL_Thread* g_in_thread;
static double mono_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000.0 + t.tv_nsec / 1e6; }
static int input_thread(void* ud) {
    unsigned prev = 0;
    double down_at[16] = {0}, up_at[16] = {0};
    int logged = 0, i;
    (void)ud;
    while (g_in_run) {
        unsigned raw = joykey_read();
        double now = mono_ms();
        for (i = 0; i < 14; i++) {
            unsigned m = 1u << i;
            if (raw & m) {
                if (!(prev & m)) {
                    double gap = now - up_at[i];
                    if (up_at[i] > 0 && gap < 60 && logged < 60) { fprintf(stderr, "pad: button %d released for only %.1f ms (held for %.1f ms before)\n", i, gap, up_at[i] - down_at[i]); logged++; }
                    down_at[i] = now;
                    g_in_tap |= m;
                }
                g_in_last[i] = now;
            } else if (prev & m) up_at[i] = now;
        }
        prev = raw;
        g_in_cur = raw;
        SDL_Delay(1);
    }
    return 0;
}
static unsigned input_state(void) {
    double now = mono_ms();
    unsigned out = g_in_cur, tap = g_in_tap;
    int i;
    g_in_tap = 0;
    out |= tap;
    for (i = 0; i < 14; i++) if (g_in_last[i] > 0 && now - g_in_last[i] < (double)g_in_debounce) out |= 1u << i;
    return out;
}
static Sf3500Voice* g_voice[NSND];
static int g_mixer_ok;
static Sf3500Voice* g_stream;
static PlatFill g_stream_fill;
static void* g_stream_ud;
static int stream_pull(void* ud, short* dst, int frames) { (void)ud; return g_stream_fill(g_stream_ud, dst, frames); }
#else
static SDL_Window* g_win;
static SDL_Renderer* g_ren;
static SDL_Texture* g_tex;
static SDL_AudioDeviceID g_adev;
typedef struct { const short* pcm; int frames; volatile int pos; volatile float gain; volatile int on; } HVoice;
static HVoice g_hv[NSND];
static unsigned g_keys_held;
static struct { PlatFill fill; void* ud; volatile int on; volatile float gain; } g_hstream;
#endif

uint16_t* plat_fb(void) { return g_fb; }
int plat_headless(void) { return g_headless; }
unsigned plat_frame(void) { return g_frame; }
void plat_set_fps(int fps) { g_fps = fps < 5 ? 5 : (fps > 120 ? 120 : fps); }
unsigned plat_ticks(void) { return SDL_GetTicks(); }

#ifndef SF3500
static void audio_cb(void* ud, Uint8* stream, int len) {
    short* out = (short*)stream;
    int n = len / 4, i, v;
    (void)ud;
    memset(stream, 0, (size_t)len);
    for (v = 0; v < NSND; v++) {
        HVoice* h = &g_hv[v];
        if (!h->on || !h->pcm) continue;
        for (i = 0; i < n && h->pos < h->frames; i++, h->pos++) {
            int l = out[i * 2] + (int)(h->pcm[h->pos * 2] * h->gain), r = out[i * 2 + 1] + (int)(h->pcm[h->pos * 2 + 1] * h->gain);
            out[i * 2] = (short)(l > 32767 ? 32767 : (l < -32768 ? -32768 : l));
            out[i * 2 + 1] = (short)(r > 32767 ? 32767 : (r < -32768 ? -32768 : r));
        }
        if (h->pos >= h->frames) h->on = 0;
    }
    if (g_hstream.on && g_hstream.fill) {            /* the music (48 kHz stereo here; the device mixer resamples other rates) */
        short tmp[2048 * 2];
        int done = 0;
        while (done < n) {
            int want = n - done > 2048 ? 2048 : n - done, got = g_hstream.fill(g_hstream.ud, tmp, want);
            for (i = 0; i < got * 2; i++) {
                int m = out[done * 2 + i] + (int)(tmp[i] * g_hstream.gain);
                out[done * 2 + i] = (short)(m > 32767 ? 32767 : (m < -32768 ? -32768 : m));
            }
            done += got;
            if (got < want) { g_hstream.on = 0; break; }
        }
    }
}
#endif

int plat_init(void) {
    const char* s;
    g_headless = getenv("PSY_HEADLESS") && atoi(getenv("PSY_HEADLESS"));
    g_shots = getenv("PSY_SHOTS");
    g_shot_every = getenv("PSY_SHOT_EVERY") ? atoi(getenv("PSY_SHOT_EVERY")) : 0;
    g_maxframes = getenv("PSY_MAXFRAMES") ? atoi(getenv("PSY_MAXFRAMES")) : 0;
    if ((s = getenv("PSY_SHOT_FRAMES"))) {      /* "120,400,900": only those frames are saved */
        while (*s && g_nshot_list < 1024) { char* e; g_shot_list[g_nshot_list++] = (unsigned)strtoul(s, &e, 10); if (e == s) break; s = *e ? e + 1 : e; }
    }
    if ((s = getenv("PSY_JOY"))) snprintf(g_script, sizeof g_script, "%s", s);
    g_fb = (uint16_t*)calloc((size_t)SCR_W * SCR_H, sizeof(uint16_t));
    if (!g_fb) return -1;
    g_surf = SDL_CreateRGBSurfaceWithFormatFrom(g_fb, SCR_W, SCR_H, 16, SCR_W * 2, SDL_PIXELFORMAT_RGB565);
#ifdef SF3500
    if (SDL_Init(SDL_INIT_TIMER) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return -1; }
    setpriority(PRIO_PROCESS, 0, -10);      /* the card's background scripts wake up often; frames stay even */
    if (!g_headless) {
        if (hwdisp_init() != 0) { fprintf(stderr, "psychosomnium: hwdisp_init failed\n"); return -1; }
        joykey_init();
        g_in_run = 1;
        g_in_thread = SDL_CreateThread(input_thread, "pad", NULL);
        if (!g_in_thread) { g_in_run = 0; fprintf(stderr, "psychosomnium: pad thread failed, reading the pad once per frame\n"); }
        g_mixer_ok = sf3500_mixer_init();
        if (!g_mixer_ok) fprintf(stderr, "psychosomnium: mixer/hwaudio unavailable, sound off\n");
    }
#else
    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS | (g_headless ? 0 : (SDL_INIT_VIDEO | SDL_INIT_AUDIO))) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return -1; }
    if (!g_headless) {
        SDL_AudioSpec want, have;
        g_win = SDL_CreateWindow("Psychosomnium", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCR_W * 3 / 2, SCR_H * 3 / 2, SDL_WINDOW_RESIZABLE);
        g_ren = SDL_CreateRenderer(g_win, -1, 0);
        SDL_RenderSetLogicalSize(g_ren, SCR_W, SCR_H);
        g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, SCR_W, SCR_H);
        memset(&want, 0, sizeof want);
        want.freq = 48000; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024; want.callback = audio_cb;
        g_adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (g_adev) SDL_PauseAudioDevice(g_adev, 0);
    }
#endif
    g_last_ticks = SDL_GetTicks();
    g_next = 0;
    return 0;
}

static void save_shot(void) {
    char path[512];
    snprintf(path, sizeof path, "%s/frame%06u.bmp", g_shots, g_frame);
    SDL_SaveBMP(g_surf, path);
}

/* Frame timing report (log.txt, every 600 frames and at exit): how long a frame takes from end to end, how long the present call blocks, how much of
 * each frame the game's own work uses, how much was slept (headroom), how many frames ran long (> 20 ms) or had their picture skipped. */
static double pf_last, pf_sum, pf_max, pf_present_sum, pf_present_max, pf_work_sum, pf_sleep_sum;
static unsigned pf_n, pf_late, pf_skipped, pf_lines, pf_drained;
static double pf_over_sum, pf_over_max;
static int g_drop_next, g_drain_skip, g_since_drop = 100, g_drain_on = 1;       /* queue-drain request: skip the next picture */
static double pr_hist[120];
static unsigned pr_n;
static double mono_ms2(void) { return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency(); }
static double pf_rebuild_sum, pf_copy_sum, pf_spr_sum;
void plat_perf_work(double ms, const double* d) { pf_work_sum += ms; pf_rebuild_sum += d[1]; pf_copy_sum += d[2]; pf_spr_sum += d[3]; }
double plat_now_ms(void) { return mono_ms2(); }
static void perf_report(int final) {
    if (!pf_n || (g_headless && !getenv("PSY_PERF")) || pf_lines >= 200) return;
    fprintf(stderr, "perf%s: %u frames, frame avg %.2f max %.1f ms, late(>20ms) %u, skipped pictures %u, present avg %.2f max %.1f ms, game work avg %.2f ms (picture: rebuild %.2f, cache copy %.2f, sprites+text %.2f; rest = logic %.2f), slept avg %.2f ms (woke late avg %.2f max %.1f ms), queue-drain skips %u\n",
            final ? " (exit)" : "", pf_n, pf_sum / pf_n, pf_max, pf_late, pf_skipped, pf_present_sum / pf_n, pf_present_max, pf_work_sum / pf_n, pf_rebuild_sum / pf_n, pf_copy_sum / pf_n, pf_spr_sum / pf_n, (pf_work_sum - pf_rebuild_sum - pf_copy_sum - pf_spr_sum) / pf_n, pf_sleep_sum / pf_n, pf_over_sum / pf_n, pf_over_max, pf_drained);
    pf_lines++;
    pf_n = pf_late = pf_skipped = 0;
    pf_sum = pf_max = pf_present_sum = pf_present_max = pf_work_sum = pf_sleep_sum = 0;
    pf_rebuild_sum = pf_copy_sum = pf_spr_sum = 0;
    pf_over_sum = pf_over_max = 0; pf_drained = 0;
}
static void perf_frame(void) {
    double now = mono_ms2();
    if (pf_last > 0) {
        double d = now - pf_last;
        pf_sum += d; if (d > pf_max) pf_max = d; if (d > 20.0) pf_late++;
        pf_n++;
        if (pf_n >= 600) perf_report(0);
    }
    pf_last = now;
}

static void pace(int sleep_ok) {
    unsigned now;
    if (g_headless) return;
    now = SDL_GetTicks();
    if (g_next == 0) g_next = now;
    g_next += 1000.0 / g_fps;
    if ((double)now > g_next + 250.0) g_next = now;      /* far behind (suspend, long stall): start over */
    else if (sleep_ok && (double)now < g_next) {
        double want = g_next - (double)now, t0 = mono_ms2(), over;
        pf_sleep_sum += want; SDL_Delay((Uint32)want);
        over = mono_ms2() - t0 - (double)(Uint32)want; if (over < 0) over = 0;
        pf_over_sum += over; if (over > pf_over_max) pf_over_max = over;
    }
}

/* test hook PSY_HASHFILE=<path>: one line per presented frame, "<frame> <hash>", hash = sum(rgb565[i] * w[i]) mod 2^64 with w[i] = i*2654435761+12345 (32-bit wrap);
 * tools/wine/seqcmp.py computes the same for the original's recorded frames. */
static void write_hash(void) {
    static FILE* f;
    uint64_t h = 0;
    int i;
    if (!f) { const char* p = getenv("PSY_HASHFILE"); if (!p) return; f = fopen(p, "w"); if (!f) return; }
    for (i = 0; i < SCR_W * SCR_H; i++) h += (uint64_t)g_fb[i] * (uint64_t)(uint32_t)((uint32_t)i * 2654435761u + 12345u);
    fprintf(f, "%u %llu\n", g_frame, (unsigned long long)h);
    fflush(f);
}

void plat_present(void) {
    double t0 = mono_ms2(), dt;
    { const char* rp = getenv("PSY_RAWFRAME"); if (rp && g_frame + 1 == (unsigned)atoi(rp) && getenv("PSY_RAWFILE")) { FILE* rf = fopen(getenv("PSY_RAWFILE"), "wb"); if (rf) { fwrite(g_fb, 1, (size_t)SCR_W * SCR_H * 2, rf); fclose(rf); } } }       /* test hook: dump one frame as raw RGB565 */
    if (getenv("PSY_HASHFILE")) write_hash();
#ifdef SF3500
    if (!g_headless) hwdisp_present(g_fb, SCR_W, SCR_H, SCR_W * 2);
#else
    if (!g_headless) {
        SDL_UpdateTexture(g_tex, NULL, g_fb, SCR_W * 2);
        SDL_RenderClear(g_ren);
        SDL_RenderCopy(g_ren, g_tex, NULL, NULL);
        SDL_RenderPresent(g_ren);
    }
#endif
    dt = mono_ms2() - t0;
    if (!g_headless) {
        /* Display queue depth: a present that takes much longer than the quickest recent one had to wait for a free slot, i.e. pictures are queued
         * ahead of this one and every queued picture is a frame of lag. Skip the next picture now and then to let the queue drain (the game itself
         * keeps its 60 Hz). A present that is slow even on an empty queue (vsync-bound) is the new minimum, so nothing is skipped for it. */
        double mn = dt;
        int i, cnt = pr_n < 120 ? (int)pr_n : 120;
        pr_hist[pr_n++ % 120] = dt;
        for (i = 0; i < cnt; i++) if (pr_hist[i] < mn) mn = pr_hist[i];
        if (g_drain_on && g_since_drop >= 4 && cnt >= 30 && dt > mn + 4.0) { g_drop_next = 1; g_since_drop = 0; pf_drained++; }
        else g_since_drop++;
    }
    pf_present_sum += dt; if (dt > pf_present_max) pf_present_max = dt;
    g_frame++;
    if (g_shots && g_shot_every > 0 && g_frame % (unsigned)g_shot_every == 0) save_shot();
    if (g_shots && g_nshot_list) { int k; for (k = 0; k < g_nshot_list; k++) if (g_shot_list[k] == g_frame) save_shot(); }
    if (g_maxframes && g_frame >= (unsigned)g_maxframes) plat_quit(0);
    pace(1);
    perf_frame();
}

int plat_behind(void) {
    if (g_drop_next) { g_drop_next = 0; g_drain_skip = 1; return 1; }
    return !g_headless && g_next > 0 && (double)SDL_GetTicks() > g_next + 1000.0 / g_fps; }

void plat_skip_frame(void) {
    pf_skipped++;
    g_frame++;
    if (g_maxframes && g_frame >= (unsigned)g_maxframes) plat_quit(0);
    pace(g_drain_skip);                              /* a queue-drain skip keeps the normal frame time; only catching up runs without sleeping */
    g_drain_skip = 0;
    perf_frame();
}

/* "frame:mask,frame:mask": from that frame on the pad reads as mask until the next entry. */
static unsigned scripted(void) {
    static unsigned cur;
    static const char* p;
    static int init;
    if (!init) { init = 1; p = g_script; }
    while (p && *p) {
        char* end;
        unsigned long fr = strtoul(p, &end, 10);
        if (*end != ':' || fr > g_frame) break;
        cur = (unsigned)strtoul(end + 1, &end, 0);
        p = (*end == ',') ? end + 1 : end;
    }
    return cur;
}

/* test hook PSY_FUZZ=<seed>: a random but human-ish pad (d-pad taps, A, B; never START/SELECT) */
static unsigned fuzz(void) {
    static unsigned cur, rs, next;
    if (!rs) rs = (unsigned)atoi(getenv("PSY_FUZZ")) * 2654435761u + 1;
    if (g_frame >= 10 && g_frame < 13) return B_START;      /* leave the title screen */
    if (g_frame >= next) {
        static const unsigned k[8] = { 0, 0, B_LEFT, B_RIGHT, B_UP, B_DOWN, B_A, B_B };
        rs = rs * 1664525u + 1013904223u;
        cur = (cur == 0) ? k[(rs >> 20) & 7] : 0;           /* press, release, press, ... */
        if ((rs >> 12) % 9 == 0) cur = B_A;
        next = g_frame + 2 + ((rs >> 8) % 5);
    }
    return cur;
}

unsigned plat_buttons(void) {
    if (g_script[0]) return scripted();
    if (g_headless && getenv("PSY_FUZZ")) return fuzz();
#ifdef SF3500
    return g_headless ? 0 : (g_in_run ? input_state() : joykey_read());     /* joykey's order is the B_* order */
#else
    {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) plat_quit(0);
            if ((e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) && !e.key.repeat) {
                unsigned bit = 0;
                switch (e.key.keysym.sym) {
                    case SDLK_LEFT: bit = B_LEFT; break;
                    case SDLK_RIGHT: bit = B_RIGHT; break;
                    case SDLK_UP: bit = B_UP; break;
                    case SDLK_DOWN: bit = B_DOWN; break;
                    case SDLK_z: bit = B_A; break;
                    case SDLK_x: bit = B_B; break;
                    case SDLK_c: bit = B_X; break;
                    case SDLK_v: bit = B_Y; break;
                    case SDLK_a: bit = B_L1; break;
                    case SDLK_s: bit = B_R1; break;
                    case SDLK_q: bit = B_L2; break;
                    case SDLK_w: bit = B_R2; break;
                    case SDLK_RETURN: bit = B_START; break;
                    case SDLK_RSHIFT: case SDLK_BACKSPACE: bit = B_SELECT; break;
                    case SDLK_ESCAPE: if (e.type == SDL_KEYDOWN) plat_quit(0); break;
                    default: break;
                }
                if (e.type == SDL_KEYDOWN) g_keys_held |= bit; else g_keys_held &= ~bit;
            }
        }
        return g_keys_held;
    }
#endif
}

void plat_set_drain(int on) { g_drain_on = on; }

void plat_input_debounce(int ms) {
#ifdef SF3500
    g_in_debounce = ms < 0 ? 0 : (ms > 100 ? 100 : ms);
#else
    (void)ms;
#endif
}

void plat_snd_set(int id, const short* pcm, int frames) {
    if (id < 0 || id >= NSND) return;
#ifdef SF3500
    if (!g_mixer_ok) return;
    if (g_voice[id]) sf3500_voice_destroy(g_voice[id]);
    g_voice[id] = sf3500_voice_create_static(pcm, frames);
    if (g_voice[id]) sf3500_voice_set_relative(g_voice[id], true);
#else
    g_hv[id].pcm = pcm; g_hv[id].frames = frames; g_hv[id].on = 0;
#endif
}

void plat_snd_play(int id, float gain) {
    if (id < 0 || id >= NSND) return;
#ifdef SF3500
    if (!g_mixer_ok || !g_voice[id]) return;
    sf3500_voice_set_gain(g_voice[id], gain);
    sf3500_voice_play(g_voice[id]);
#else
    g_hv[id].gain = gain; g_hv[id].pos = 0; g_hv[id].on = 1;
#endif
}

int plat_stream_ok(void) {
#ifdef SF3500
    return g_mixer_ok;
#else
    return g_adev != 0;
#endif
}

int plat_stream_start(PlatFill fill, void* ud, int rate, float gain) {
    plat_stream_stop();
#ifdef SF3500
    if (!g_mixer_ok) return 0;
    g_stream_fill = fill; g_stream_ud = ud;
    g_stream = sf3500_voice_create_stream(stream_pull, NULL, rate, 2);
    if (!g_stream) return 0;
    sf3500_voice_set_relative(g_stream, true);
    sf3500_voice_set_looping(g_stream, false);
    sf3500_voice_set_gain(g_stream, gain);
    sf3500_voice_play(g_stream);
    return 1;
#else
    (void)rate;
    if (!g_adev) return 0;
    SDL_LockAudioDevice(g_adev);
    g_hstream.fill = fill; g_hstream.ud = ud; g_hstream.gain = gain; g_hstream.on = 1;
    SDL_UnlockAudioDevice(g_adev);
    return 1;
#endif
}

void plat_stream_gain(float gain) {
#ifdef SF3500
    if (g_stream) sf3500_voice_set_gain(g_stream, gain);
#else
    g_hstream.gain = gain;
#endif
}

int plat_stream_playing(void) {
#ifdef SF3500
    return g_stream && sf3500_voice_playing(g_stream);
#else
    return g_hstream.on;
#endif
}

void plat_stream_stop(void) {
#ifdef SF3500
    if (g_stream) { sf3500_voice_destroy(g_stream); g_stream = NULL; }
#else
    if (g_adev) SDL_LockAudioDevice(g_adev);
    g_hstream.on = 0; g_hstream.fill = NULL;
    if (g_adev) SDL_UnlockAudioDevice(g_adev);
#endif
}

void plat_quit(int code) {
    perf_report(1);
#ifdef SF3500
    g_in_run = 0;
    if (g_in_thread) SDL_WaitThread(g_in_thread, NULL);
    if (g_mixer_ok) sf3500_mixer_deinit();
    if (!g_headless) hwdisp_deinit();
#endif
    SDL_Quit();
    exit(code);
}
