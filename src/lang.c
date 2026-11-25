#include "assets.h"
#include "lang.h"
#include <dirent.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t obj, ev, sub, k; const char* text; } SLine;

static int g_idx, g_flags;
static unsigned char* g_pak;            /* the whole .pak file (strings and the atlas point into it) */
static char* g_ui[L_COUNT];
static LGlyph* g_glyphs;
static LFont g_font;
static unsigned char* g_script;
static SLine* g_lines;
static int g_nlines;

static uint32_t rd32(const unsigned char* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t rd16(const unsigned char* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }

static unsigned char* slurp(const char* path, long* len) {
    FILE* f = fopen(path, "rb");
    unsigned char* b;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = (unsigned char*)malloc((size_t)n + 1);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    b[n] = 0; fclose(f); *len = n;
    return b;
}

static void unload(void) {
    int i;
    r_set_font(NULL, 0);
    for (i = 0; i < L_COUNT; i++) { free(g_ui[i]); g_ui[i] = NULL; }
    { int k; for (k = 0; k < g_nlines; k++) free((void*)g_lines[k].text); }
    free(g_pak); g_pak = NULL; free(g_glyphs); g_glyphs = NULL; free(g_script); g_script = NULL; free(g_lines); g_lines = NULL;
    g_nlines = 0; g_flags = 0; g_idx = 0;
}

const char* ui(int id) { return (id >= 0 && id < L_COUNT && g_ui[id]) ? g_ui[id] : L_EN[id]; }
int lang_index(void) { return g_idx; }
int lang_rtl(void) { return g_flags & 1; }
int lang_has_script(void) { return g_lines != NULL; }
int lang_cjk(void) { return g_flags & 2; }

int lang_load(const char* dir, int idx) {
    char path[1200];
    long len;
    unsigned char *b, *p;
    uint32_t n, i;
    unload();
    if (idx <= 0 || idx >= N_LANGS) return 0;
    snprintf(path, sizeof path, "%s/lang/%s.pak", dir, LANG_CODES[idx]);
    b = slurp(path, &len);
    if (!b || len < 20 || memcmp(b, "PSYL", 4) != 0 || rd32(b + 4) != 1) { free(b); fprintf(stderr, "psychosomnium: language pack %s not found, using English\n", path); return 0; }
    g_pak = b;
    g_flags = (int)rd32(b + 8);
    g_font.ascent = (int)rd16(b + 12); g_font.line = (int)rd16(b + 14);
    p = b + 16;
    n = rd32(p); p += 4;
    for (i = 0; i < n && i < L_COUNT; i++) { uint32_t l = rd16(p); p += 2; g_ui[i] = (char*)malloc(l + 1); memcpy(g_ui[i], p, l); g_ui[i][l] = 0; p += l; }
    n = rd32(p); p += 4;
    g_glyphs = (LGlyph*)calloc(n, sizeof(LGlyph));
    for (i = 0; i < n; i++) {
        LGlyph* g = &g_glyphs[i];
        g->cp = rd32(p); g->x = (uint16_t)rd16(p + 4); g->y = (uint16_t)rd16(p + 6); g->w = p[8]; g->h = p[9]; g->bx = (int8_t)p[10]; g->ty = (int8_t)p[11]; g->adv = p[12];
        p += 14;
    }
    g_font.n = (int)n; g_font.g = g_glyphs; g_font.aw = (int)rd16(p); g_font.ah = (int)rd16(p + 2); g_font.atlas = p + 4;
    g_idx = idx;
    r_set_font(&g_font, g_flags & 1);
    /* the dialogue (optional: without it the game's English text is shown, in the pack's font) */
    snprintf(path, sizeof path, "%s/lang/%s.script", dir, LANG_CODES[idx]);
    g_script = slurp(path, &len);
    if (!g_script) {                                       /* or from the optional download: any .zip in the lang folder holding <code>.script */
        char zdir[1200], want[32];
        DIR* d;
        struct dirent* de;
        snprintf(zdir, sizeof zdir, "%s/lang", dir);
        snprintf(want, sizeof want, "%s.script", LANG_CODES[idx]);
        d = opendir(zdir);
        while (d && !g_script && (de = readdir(d))) {
            size_t nl = strlen(de->d_name);
            if (nl > 4 && strcasecmp(de->d_name + nl - 4, ".zip") == 0) {
                unsigned char* z;
                long zl = 0, sl = 0;
                snprintf(path, sizeof path, "%s/%s", zdir, de->d_name);
                z = slurp(path, &zl);
                if (z) { g_script = zip_find(z, zl, want, &sl); if (g_script) { unsigned char* t = (unsigned char*)realloc(g_script, (size_t)sl + 1); if (t) { g_script = t; g_script[sl] = 0; } len = sl; } free(z); }
            }
        }
        if (d) closedir(d);
    }
    if (g_script && len >= 12 && memcmp(g_script, "PSYS", 4) == 0 && rd32(g_script + 4) == 1) {
        unsigned char* q = g_script + 12;
        g_nlines = (int)rd32(g_script + 8);
        g_lines = (SLine*)calloc((size_t)g_nlines, sizeof(SLine));
        for (i = 0; i < (uint32_t)g_nlines; i++) {
            uint32_t l = rd16(q + 4);
            g_lines[i].obj = q[0]; g_lines[i].ev = q[1]; g_lines[i].sub = q[2]; g_lines[i].k = q[3];
            { char* t = (char*)malloc(l + 1); memcpy(t, q + 6, l); t[l] = 0; g_lines[i].text = t; }
            q += 6 + l;
        }
    } else { free(g_script); g_script = NULL; }
    return idx;
}

const char* lang_script(int obj, int ev, int sub, int k, int family) {
    int i;
    if (!g_lines) return NULL;
    if (family) {
        for (i = 0; i < g_nlines; i++) if (g_lines[i].obj == obj && g_lines[i].ev == ev && g_lines[i].sub == sub && g_lines[i].k == (k | 0x80)) return g_lines[i].text;
    }
    for (i = 0; i < g_nlines; i++) if (g_lines[i].obj == obj && g_lines[i].ev == ev && g_lines[i].sub == sub && g_lines[i].k == k) return g_lines[i].text;
    return NULL;
}
