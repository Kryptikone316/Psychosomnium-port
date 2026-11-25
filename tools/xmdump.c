/* Test helper: renders each XM module of an unpacked psycho.pak to a WAV and reports peak level / NaNs. */
#include "../src/assets.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define JAR_XM_IMPLEMENTATION
#include "../third_party/jar_xm.h"
int main(int argc, char** argv) {
    char err[256];
    int i, secs = argc > 2 ? atoi(argv[2]) : 30;
    if (assets_load(argc > 1 ? argv[1] : "gamedata", err, sizeof err)) { fprintf(stderr, "%s\n", err); return 1; }
    for (i = 0; i < N_XM; i++) {
        jar_xm_context_t* c = NULL;
        long n = 48000L * secs, k, bad = 0;
        float* buf = (float*)malloc((size_t)n * 8);
        float peak = 0;
        char fn[64];
        FILE* f;
        if (jar_xm_create_context_safe(&c, (const char*)A.xm[i].data, (size_t)A.xm[i].len, 48000)) { printf("xm%d: load failed\n", i); continue; }
        jar_xm_generate_samples(c, buf, (size_t)n);
        for (k = 0; k < n * 2; k++) { if (!isfinite(buf[k])) bad++; else if (fabsf(buf[k]) > peak) peak = fabsf(buf[k]); }
        printf("xm%d: %d bytes, peak %.3f, bad samples %ld, loops %d\n", i, A.xm[i].len, peak, bad, jar_xm_get_loop_count(c));
        snprintf(fn, sizeof fn, "xm%d.wav", i);
        f = fopen(fn, "wb");
        {
            unsigned dl = (unsigned)(n * 4), rl = dl + 36, sr = 48000, br = 192000;
            unsigned short fmt = 1, ch = 2, ba = 4, bps = 16;
            unsigned fl = 16;
            fwrite("RIFF", 1, 4, f); fwrite(&rl, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fl, 4, 1, f);
            fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bps, 2, 1, f);
            fwrite("data", 1, 4, f); fwrite(&dl, 4, 1, f);
            for (k = 0; k < n * 2; k++) { float v = buf[k] * 32767.0f; short s = (short)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v)); fwrite(&s, 2, 1, f); }
        }
        fclose(f);
        jar_xm_free_context(c);
        free(buf);
    }
    return 0;
}
