/* First-run converter: reads the player's psycho.exe (Game Maker 7.0 runner + game data) and builds the
 * engine's asset set, cached as gamedata/cache/psycho.pak. Format notes: Notes/EXTRACTION.md and
 * tools/extract.py (the Python reference this is a port of). Only the data the engine needs is kept: sprites,
 * the four fonts' glyph atlases, sounds, the three XM modules, room layouts and the room order. The GML
 * itself is not read: the game logic lives in game.c. */
#include "assets.h"
#include "lang.h"
#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "../third_party/stb_image.h"

Assets A;

#define EXE_SIZE 2658887L
#define EXE_CRC 0x50536bf9u
#define PAK_MAGIC 0x43595350u   /* "PSYC" */
#define PAK_VERSION 6

/* ---------------------------------------------------------------------------------------- helpers */
static uint32_t crc32_of(const uint8_t* p, long n) {
    static uint32_t t[256];
    uint32_t c = 0xffffffffu;
    long i;
    if (!t[1]) for (i = 0; i < 256; i++) { uint32_t v = (uint32_t)i; int k; for (k = 0; k < 8; k++) v = (v & 1) ? 0xedb88320u ^ (v >> 1) : v >> 1; t[i] = v; }
    for (i = 0; i < n; i++) c = t[(c ^ p[i]) & 255] ^ (c >> 8);
    return ~c;
}

static uint8_t* inflate_buf(const uint8_t* src, long len, int header, int* outlen) {
    char* o = header ? stbi_zlib_decode_malloc_guesssize_headerflag((const char*)src, (int)len, (int)len * 4 + 64, outlen, 1)
                     : stbi_zlib_decode_malloc_guesssize_headerflag((const char*)src, (int)len, (int)len * 4 + 64, outlen, 0);
    return (uint8_t*)o;
}

static uint8_t* read_file(const char* path, long* len) {
    FILE* f = fopen(path, "rb");
    uint8_t* b;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = (uint8_t*)malloc((size_t)n + 1);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    fclose(f);
    *len = n;
    return b;
}

static int write_file(const char* path, const uint8_t* b, long n) {
    FILE* f = fopen(path, "wb");
    int ok;
    if (!f) return -1;
    ok = fwrite(b, 1, (size_t)n, f) == (size_t)n;
    ok = (fclose(f) == 0) && ok;
    return ok ? 0 : -1;
}

/* ------------------------------------------------------------------- the Game Maker 7 byte stream */
typedef struct {
    const uint8_t* d;
    long n, p, pos;
    uint8_t table[256];
    int scrambled, bad;
} Stream;

static void make_table(uint8_t* dec, int seed) {
    int a = 6 + (seed % 250), b = seed / 250, i, enc[256];
    for (i = 0; i < 256; i++) enc[i] = i;
    for (i = 1; i < 10001; i++) {
        int j = 1 + ((i * a + b) % 254), t = enc[j];
        enc[j] = enc[j + 1]; enc[j + 1] = t;
    }
    for (i = 0; i < 256; i++) dec[i] = 0;
    for (i = 1; i < 256; i++) dec[enc[i]] = (uint8_t)i;
}

static void sraw(Stream* s, uint8_t* out, long n) {
    long i;
    if (n < 0 || s->p + n > s->n) { s->bad = 1; if (out) memset(out, 0, (size_t)(n > 0 ? n : 0)); return; }
    for (i = 0; i < n; i++) {
        uint8_t b = s->d[s->p + i];
        if (s->scrambled) b = (uint8_t)(s->table[b] - s->pos - i);
        if (out) out[i] = b;
    }
    s->p += n;
    s->pos += n;
}

static int si4(Stream* s) { uint8_t b[4]; sraw(s, b, 4); return (int)((uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24); }
static double sdbl(Stream* s) { uint8_t b[8]; double v; sraw(s, b, 8); memcpy(&v, b, 8); return v; }
static void sskip(Stream* s, long n) { sraw(s, NULL, n); }
/* reads a length-prefixed block into a fresh buffer (caller frees) */
static uint8_t* sblock(Stream* s, int* len) {
    int n = si4(s);
    uint8_t* b;
    if (n < 0 || s->p + n > s->n) { s->bad = 1; *len = 0; return NULL; }
    b = (uint8_t*)malloc((size_t)n + 1);
    sraw(s, b, n);
    *len = n;
    return b;
}
static void sstr(Stream* s, char* out, int cap) {
    int n = si4(s);
    if (n < 0 || s->p + n > s->n) { s->bad = 1; if (cap) out[0] = 0; return; }
    if (n < cap) { sraw(s, (uint8_t*)out, n); out[n] = 0; }
    else { sskip(s, n); if (cap) out[0] = 0; }
}
static uint8_t* szblock(Stream* s, int* outlen) {   /* int length + zlib data */
    int n = 0, o = 0;
    uint8_t* z = sblock(s, &n);
    uint8_t* r;
    if (!z) return NULL;
    r = inflate_buf(z, n, 1, &o);
    free(z);
    if (!r) s->bad = 1;
    *outlen = o;
    return r;
}

#define FAIL(...) do { snprintf(err, errlen, __VA_ARGS__); return -1; } while (0)
#define EXPECT(s, v, what) do { int _v = si4(s); if (_v != (v)) FAIL("psycho.exe: unexpected %s version %d (wanted %d)", what, _v, (int)(v)); } while (0)

static void add_text(Assets* out, int obj, int ev, int sub, int k, const char* s, int n) {
    Text* t;
    out->text = (Text*)realloc(out->text, sizeof(Text) * (size_t)(out->ntext + 1));
    t = &out->text[out->ntext++];
    t->obj = obj; t->ev = ev; t->sub = sub; t->k = k;
    t->s = (char*)malloc((size_t)n + 1);
    memcpy(t->s, s, (size_t)n); t->s[n] = 0;
}

/* every "..." literal in a code action, numbered per event */
static void scan_literals(Assets* out, int obj, int ev, int sub, int* k, const char* code, int len) {
    int i = 0;
    while (i < len) {
        if (code[i] == '/' && i + 1 < len && code[i + 1] == '/') { while (i < len && code[i] != '\n') i++; continue; }
        if (code[i] == '"') {
            int j = i + 1;
            while (j < len && code[j] != '"') j++;
            if (j >= len) break;
            add_text(out, obj, ev, sub, (*k)++, code + i + 1, j - i - 1);
            i = j + 1;
        } else i++;
    }
}

static void scan_actions(Stream* s, Assets* out, int obj, int ev, int sub) {
    int n, k, i, lit = 0;
    if (si4(s) != 400) { s->bad = 1; return; }
    n = si4(s);
    for (i = 0; i < n && !s->bad; i++) {
        int lib, id, a;
        sskip(s, 4);
        lib = si4(s); id = si4(s);
        sskip(s, 4 * 5);                 /* kind, allow relative, question, can apply, exec type */
        sskip(s, si4(s));                /* function name */
        sskip(s, si4(s));                /* code */
        si4(s);                          /* arguments used */
        k = si4(s); sskip(s, 4L * k);    /* argument kinds */
        sskip(s, 4 * 2);                 /* applies to, relative */
        k = si4(s);
        for (a = 0; a < k && !s->bad; a++) {
            int bl = 0;
            uint8_t* b = sblock(s, &bl);
            if (b && a == 0 && lib == 1 && id == 603) scan_literals(out, obj, ev, sub, &lit, (const char*)b, bl);
            free(b);
        }
        sskip(s, 4);                     /* not */
    }
}

/* "image_xscale=16" / "image_yscale=480/16" style room-instance creation code */
static int parse_creation(const char* code, double* xs, double* ys) {
    const char* p = code;
    while (*p) {
        double* dst = NULL;
        char* e;
        double v;
        while (*p == '\r' || *p == '\n' || *p == ' ') p++;
        if (!*p) break;
        if (!strncmp(p, "image_xscale=", 13)) { dst = xs; p += 13; }
        else if (!strncmp(p, "image_yscale=", 13)) { dst = ys; p += 13; }
        else return -1;
        v = strtod(p, &e);
        if (e == p) return -1;
        p = e;
        if (*p == '/') { double d = strtod(p + 1, &e); if (e == p + 1 || d == 0) return -1; v /= d; p = e; }
        *dst = v;
    }
    return 0;
}

int unpack_exe(const uint8_t* exe, long len, Assets* out, char* err, int errlen) {
    static const uint8_t magic[8] = { 0x91, 0xd5, 0x12, 0x00, 0xbc, 0x02, 0x00, 0x00 };
    long mo = -1, i;
    Stream h, s;
    int n, k, j, dll_n = 0, B_len = 0;
    uint8_t* B = NULL;
    uint8_t* tmp;
    char name[128];
    int seed, s1, s2;
    int ret = -1;

    memset(out, 0, sizeof *out);
    for (i = len - 8; i >= 0; i--) if (!memcmp(exe + i, magic, 8)) { mo = i; break; }
    if (mo < 0) FAIL("psycho.exe: Game Maker data header not found");

    /* plain part: settings, loading picture, one constant, the embedded D3DX8.dll */
    memset(&h, 0, sizeof h); h.d = exe; h.n = len; h.p = mo;
    sskip(&h, 4 * 4);                    /* magic, 700, <0>, 702 */
    sskip(&h, 4 * 24);                   /* settings flags */
    sskip(&h, si4(&h));                  /* loading picture (zlib) */
    sskip(&h, 4 * 7);
    n = si4(&h);
    while (n-- > 0 && !h.bad) { sskip(&h, si4(&h)); sskip(&h, si4(&h)); }
    sskip(&h, si4(&h));                  /* "D3DX8.dll" */
    sskip(&h, si4(&h));                  /* its data */
    if (h.bad) FAIL("psycho.exe: damaged header");
    {
        const uint8_t* zp = exe + h.p + 4;
        long zl = (long)((uint32_t)exe[h.p] | (uint32_t)exe[h.p + 1] << 8 | (uint32_t)exe[h.p + 2] << 16 | (uint32_t)exe[h.p + 3] << 24);
        if (zl <= 0 || h.p + 4 + zl > len) FAIL("psycho.exe: damaged resource block");
        B = inflate_buf(zp, zl, 1, &B_len);
    }
    (void)dll_n;
    if (!B) FAIL("psycho.exe: could not decompress the game data");

    memset(&s, 0, sizeof s); s.d = B; s.n = B_len;
    s1 = si4(&s); s2 = si4(&s);
    if (s1 < 0 || s2 < 0 || (long)(s1 + s2) * 4 + 64 > B_len) { free(B); FAIL("psycho.exe: damaged game data"); }
    sskip(&s, 4L * s1);
    seed = si4(&s);
    sskip(&s, 4L * s2);
    sskip(&s, 1);
    make_table(s.table, seed); s.scrambled = 1;
    sskip(&s, 3 + 16 + 4);               /* rest of the game id, GUID, one junk int */
    {
        int a = si4(&s), b = si4(&s);
        if (a != 700 || b != 0) { free(B); FAIL("psycho.exe: unexpected extension block"); }
    }

    /* ---- sounds */
    if (si4(&s) != 400) { ret = -2; goto bad; }
    n = si4(&s);
    for (i = 0; i < n; i++) {
        int id = (int)i, dl = 0;
        uint8_t* wav = NULL;
        if (!si4(&s)) continue;
        sstr(&s, name, sizeof name);
        if (si4(&s) != 600) { ret = -2; goto bad; }
        si4(&s);
        sskip(&s, si4(&s)); sskip(&s, si4(&s));          /* type, file name */
        if (si4(&s)) wav = szblock(&s, &dl);
        sskip(&s, 4 + 8 + 8 + 4);
        if (id < N_SOUNDS && wav) { out->wav[id].data = wav; out->wav[id].len = dl; } else free(wav);
    }

    /* ---- sprites */
    if (si4(&s) != 400) { ret = -2; goto bad; }
    n = si4(&s);
    for (i = 0; i < n && !s.bad; i++) {
        Sprite* sp;
        int transp, nsub, w, h2, x, y;
        if (!si4(&s)) continue;
        if (i >= N_SPRITES) { ret = -3; goto bad; }
        sp = &out->spr[i];
        sstr(&s, name, sizeof name);
        if (si4(&s) != 542) { ret = -2; goto bad; }
        sp->w = w = si4(&s); sp->h = h2 = si4(&s);
        sp->bl = si4(&s); sp->br = si4(&s); sp->bb = si4(&s); sp->bt = si4(&s);
        transp = si4(&s);
        sskip(&s, 4 * 4);                /* smooth, preload, bbox mode, precise */
        sp->ox = si4(&s); sp->oy = si4(&s);
        nsub = si4(&s);
        if (nsub < 0 || nsub > MAX_FRAMES || w <= 0 || h2 <= 0 || w > 2048 || h2 > 2048) { if (nsub == 0) { sp->valid = 1; continue; } ret = -3; goto bad; }
        sp->valid = 1;
        sp->mask = (uint8_t*)calloc((size_t)w * h2, 1);
        for (j = 0; j < nsub; j++) {
            int fw, fh, dl = 0;
            uint8_t* px;
            uint32_t* fr;
            if (si4(&s) != 540) { ret = -2; goto bad; }
            if (!si4(&s)) continue;
            fw = si4(&s); fh = si4(&s);
            px = szblock(&s, &dl);
            if (!px || fw != w || fh != h2 || dl != w * h2 * 4) { free(px); ret = -3; goto bad; }
            fr = (uint32_t*)malloc((size_t)w * h2 * 4);
            /* transparency is the pixel data's own alpha byte (0 = transparent), not a colour key */
            for (y = 0; y < h2; y++)
                for (x = 0; x < w; x++) {
                    const uint8_t* q = px + ((size_t)y * w + x) * 4;       /* B G R A */
                    size_t o = (size_t)y * w + x;
                    uint32_t rgb = (uint32_t)q[2] << 16 | (uint32_t)q[1] << 8 | q[0];
                    if (q[3]) { fr[o] = 0xff000000u | rgb; if (j == 0) sp->mask[o] = 1; }   /* one shared mask, from the first subimage (checked against the original: the union of the spikes' two frames made ghost hazards) */
                    else fr[o] = rgb;
                }
            free(px);
            sp->frame[sp->nframes++] = fr;
        }
    }

    /* ---- backgrounds, paths: none in this game */
    if (si4(&s) != 400) { ret = -2; goto bad; }
    n = si4(&s); for (i = 0; i < n; i++) if (si4(&s)) { ret = -3; goto bad; }
    if (si4(&s) != 420) { ret = -2; goto bad; }
    n = si4(&s); for (i = 0; i < n; i++) if (si4(&s)) { ret = -3; goto bad; }

    /* ---- scripts (compiled/encoded GML, not used) */
    if (si4(&s) != 400) { ret = -2; goto bad; }
    n = si4(&s);
    for (i = 0; i < n && !s.bad; i++) {
        if (!si4(&s)) continue;
        sskip(&s, si4(&s)); si4(&s); sskip(&s, si4(&s));
    }

    /* ---- fonts */
    if (si4(&s) != 540) { ret = -2; goto bad; }
    n = si4(&s);
    for (i = 0; i < n && !s.bad; i++) {
        Font* f;
        int gi, dl = 0;
        uint8_t* al;
        if (!si4(&s)) continue;
        if (i >= N_FONTS) { ret = -3; goto bad; }
        f = &out->fnt[i];
        sstr(&s, name, sizeof name);
        if (si4(&s) != 540) { ret = -2; goto bad; }
        sskip(&s, si4(&s));              /* family */
        sskip(&s, 4 * 3 + 2 + 1 + 1 + 4);/* size, bold, italic, range min, charset, antialias, range max */
        for (gi = 0; gi < 256; gi++) {
            Glyph* g = &f->g[gi];
            g->x = si4(&s); g->y = si4(&s); g->w = si4(&s); g->h = si4(&s); g->shift = si4(&s); g->off = si4(&s);
            if (g->h > f->lineh) f->lineh = g->h;
        }
        f->aw = si4(&s); f->ah = si4(&s);
        al = szblock(&s, &dl);
        if (!al || f->aw <= 0 || f->ah <= 0 || dl != f->aw * f->ah) { free(al); ret = -3; goto bad; }
        f->alpha = al; f->valid = 1;
    }

    /* ---- timelines: none */
    if (si4(&s) != 500) { ret = -2; goto bad; }
    n = si4(&s); for (i = 0; i < n; i++) if (si4(&s)) { ret = -3; goto bad; }

    /* ---- objects: only stepped over (logic is built into the engine) */
    if (si4(&s) != 400) { ret = -2; goto bad; }
    n = si4(&s);
    for (i = 0; i < n && !s.bad; i++) {
        int nev;
        if (!si4(&s)) continue;
        sskip(&s, si4(&s));
        if (si4(&s) != 430) { ret = -2; goto bad; }
        sskip(&s, 4 * 7);
        nev = si4(&s) + 1;
        for (j = 0; j < nev && !s.bad; j++)
            for (;;) {
                int sub = si4(&s);
                if (sub == -1 || s.bad) break;
                scan_actions(&s, out, (int)i, j, sub);
            }
    }
    if (s.bad) { ret = -3; goto bad; }

    /* ---- rooms */
    if (si4(&s) != 420) { ret = -2; goto bad; }
    n = si4(&s);
    for (i = 0; i < n && !s.bad; i++) {
        Room* r;
        char code[256];
        int ni, nb, nv, nt, q;
        if (!si4(&s)) continue;
        if (i >= N_ROOMS) { ret = -3; goto bad; }
        r = &out->room[i];
        sskip(&s, si4(&s));              /* name */
        if (si4(&s) != 541) { ret = -2; goto bad; }
        sskip(&s, si4(&s));              /* caption */
        r->w = si4(&s); r->h = si4(&s); r->speed = si4(&s);
        r->persistent = si4(&s) != 0;
        r->bg = (uint32_t)si4(&s);
        si4(&s);                         /* draw background colour */
        sstr(&s, code, sizeof code);
        nb = si4(&s); sskip(&s, 4L * 10 * nb);
        si4(&s);
        nv = si4(&s); sskip(&s, 4L * 14 * nv);
        ni = si4(&s);
        if (ni < 0 || ni > 100000) { ret = -3; goto bad; }
        r->inst = (RInst*)calloc((size_t)ni + 1, sizeof(RInst));
        for (q = 0; q < ni && !s.bad; q++) {
            RInst* in = &r->inst[q];
            in->x = si4(&s); in->y = si4(&s); in->obj = si4(&s); in->id = si4(&s);
            sstr(&s, code, sizeof code);
            in->xs = in->ys = 1.0;
            if (code[0] && parse_creation(code, &in->xs, &in->ys)) { ret = -4; goto bad; }
        }
        r->n = ni;
        nt = si4(&s);
        if (nt) { ret = -4; goto bad; }
        r->valid = 1;
    }
    if (s.bad) { ret = -3; goto bad; }
    out->last_inst_id = si4(&s); si4(&s);

    /* ---- included files: the three XM modules (the DLLs are Windows-only and skipped) */
    {
        int ver = si4(&s);
        if (ver != 620 && ver != 600 && ver != 430) { ret = -2; goto bad; }
    }
    n = si4(&s);
    for (i = 0; i < n && !s.bad; i++) {
        char fname[128];
        int stored, size, dl = 0;
        uint8_t* data = NULL;
        if (si4(&s) != 620) { ret = -2; goto bad; }
        sstr(&s, fname, sizeof fname);
        sskip(&s, si4(&s));
        si4(&s);
        size = si4(&s);
        stored = si4(&s);
        if (stored) {
            int bl = 0;
            tmp = sblock(&s, &bl);
            if (tmp) { data = inflate_buf(tmp, bl, 1, &dl); free(tmp); }
            (void)size;
        }
        sskip(&s, 4); sskip(&s, si4(&s)); sskip(&s, 4 * 3);
        k = -1;
        if (!strcmp(fname, "nintendo_heroes_.xm")) k = 0;
        else if (!strcmp(fname, "lost_behind_a_chimney.xm")) k = 1;
        else if (!strcmp(fname, "little_nintendofairy.xm")) k = 2;
        if (k >= 0 && data) { out->xm[k].data = data; out->xm[k].len = dl; } else free(data);
    }
    /* game information, library creation code, room order */
    {
        int ver = si4(&s);
        if (ver != 430 && ver != 600 && ver != 620) { ret = -2; goto bad; }
    }
    sskip(&s, 4 * 2); sskip(&s, si4(&s)); sskip(&s, 4 * 8); sskip(&s, si4(&s));
    if (si4(&s) != 500) { ret = -2; goto bad; }
    n = si4(&s); for (i = 0; i < n && !s.bad; i++) sskip(&s, si4(&s));
    k = si4(&s);
    if (k != 500 && k != 540 && k != 700) { ret = -2; goto bad; }
    n = si4(&s);
    if (n != N_ROOMS) { ret = -3; goto bad; }
    for (i = 0; i < n; i++) { out->order[i] = si4(&s); if (out->order[i] < 0 || out->order[i] >= N_ROOMS) { ret = -3; goto bad; } }
    if (s.bad) { ret = -3; goto bad; }

    for (i = 0; i < N_SOUNDS; i++) if (!out->wav[i].data) { ret = -5; goto bad; }
    for (i = 0; i < N_XM; i++) if (!out->xm[i].data) { ret = -5; goto bad; }
    for (i = 0; i < N_FONTS; i++) if (!out->fnt[i].valid) { ret = -5; goto bad; }
    free(B);
    return 0;

bad:
    free(B);
    if (ret == -2) FAIL("psycho.exe: the game data has an unexpected layout (wrong version of the game?)");
    if (ret == -4) FAIL("psycho.exe: unsupported room data (wrong version of the game?)");
    if (ret == -5) FAIL("psycho.exe: some game data is missing (wrong version of the game?)");
    FAIL("psycho.exe: the game data could not be read (damaged file?)");
}

/* ------------------------------------------------------------------------------------- cache file */
typedef struct { uint8_t* b; long n, cap, p; int wr, bad; } Ser;

static void io_raw(Ser* s, void* p, long n) {
    if (n <= 0) return;
    if (s->wr) {
        if (s->n + n > s->cap) { s->cap = (s->n + n) * 2; s->b = (uint8_t*)realloc(s->b, (size_t)s->cap); }
        memcpy(s->b + s->n, p, (size_t)n); s->n += n;
    } else {
        if (s->p + n > s->n) { s->bad = 1; memset(p, 0, (size_t)n); return; }
        memcpy(p, s->b + s->p, (size_t)n); s->p += n;
    }
}
static void io_i(Ser* s, int* v) { io_raw(s, v, 4); }
static void io_d(Ser* s, double* v) { io_raw(s, v, 8); }
static void io_buf(Ser* s, void** p, long n) {      /* n bytes; allocated when reading */
    if (!s->wr) { if (n < 0 || n > (1L << 28) || s->p + n > s->n) { s->bad = 1; *p = NULL; return; } *p = malloc((size_t)n + 1); }
    io_raw(s, *p, n);
}

static void pak_io(Ser* s, Assets* a) {
    int i, j, have;
    for (i = 0; i < N_SPRITES; i++) {
        Sprite* sp = &a->spr[i];
        io_i(s, &sp->valid); io_i(s, &sp->w); io_i(s, &sp->h); io_i(s, &sp->nframes); io_i(s, &sp->ox); io_i(s, &sp->oy);
        io_i(s, &sp->bl); io_i(s, &sp->br); io_i(s, &sp->bb); io_i(s, &sp->bt);
        if (s->bad || sp->nframes < 0 || sp->nframes > MAX_FRAMES || sp->w < 0 || sp->h < 0 || sp->w > 2048 || sp->h > 2048) { s->bad = 1; return; }
        have = sp->mask != NULL; io_i(s, &have);
        if (have) io_buf(s, (void**)&sp->mask, (long)sp->w * sp->h);
        for (j = 0; j < sp->nframes; j++) io_buf(s, (void**)&sp->frame[j], (long)sp->w * sp->h * 4);
    }
    for (i = 0; i < N_FONTS; i++) {
        Font* f = &a->fnt[i];
        io_i(s, &f->valid); io_i(s, &f->aw); io_i(s, &f->ah); io_i(s, &f->lineh);
        io_raw(s, f->g, sizeof f->g);
        if (f->valid) { if (s->bad || f->aw <= 0 || f->ah <= 0) { s->bad = 1; return; } io_buf(s, (void**)&f->alpha, (long)f->aw * f->ah); }
    }
    for (i = 0; i < N_ROOMS; i++) {
        Room* r = &a->room[i];
        io_i(s, &r->valid); io_i(s, &r->w); io_i(s, &r->h); io_i(s, &r->speed); io_i(s, &r->persistent); io_i(s, (int*)&r->bg); io_i(s, &r->n);
        if (s->bad || r->n < 0 || r->n > 100000) { s->bad = 1; return; }
        if (!s->wr) r->inst = (RInst*)calloc((size_t)r->n + 1, sizeof(RInst));
        for (j = 0; j < r->n; j++) {
            RInst* in = &r->inst[j];
            io_i(s, &in->id); io_i(s, &in->obj); io_i(s, &in->x); io_i(s, &in->y); io_d(s, &in->xs); io_d(s, &in->ys);
        }
    }
    for (i = 0; i < N_SOUNDS; i++) { io_i(s, &a->wav[i].len); io_buf(s, (void**)&a->wav[i].data, a->wav[i].len); }
    for (i = 0; i < N_XM; i++) { io_i(s, &a->xm[i].len); io_buf(s, (void**)&a->xm[i].data, a->xm[i].len); }
    io_raw(s, a->order, sizeof a->order);
    io_i(s, &a->last_inst_id);
    io_i(s, &a->ntext);
    if (s->bad || a->ntext < 0 || a->ntext > 100000) { s->bad = 1; return; }
    if (!s->wr) a->text = (Text*)calloc((size_t)a->ntext + 1, sizeof(Text));
    for (i = 0; i < a->ntext; i++) {
        Text* t = &a->text[i];
        int len = s->wr ? (int)strlen(t->s) : 0;
        io_i(s, &t->obj); io_i(s, &t->ev); io_i(s, &t->sub); io_i(s, &t->k); io_i(s, &len);
        if (s->bad || len < 0 || len > 100000) { s->bad = 1; return; }
        io_buf(s, (void**)&t->s, len);
        if (!s->wr && t->s) t->s[len] = 0;
    }
}

static int pak_write(const char* path, const Assets* a, uint32_t crc) {
    Ser s;
    int hdr[4] = { (int)PAK_MAGIC, PAK_VERSION, (int)EXE_SIZE, (int)crc };
    int r;
    memset(&s, 0, sizeof s); s.wr = 1;
    io_raw(&s, hdr, sizeof hdr);
    pak_io(&s, (Assets*)a);
    r = write_file(path, s.b, s.n);
    free(s.b);
    return r;
}

static int pak_read(const char* path, Assets* a) {
    long n = 0;
    uint8_t* b = read_file(path, &n);
    Ser s;
    int hdr[4];
    if (!b) return -1;
    memset(&s, 0, sizeof s); s.b = b; s.n = n;
    memset(a, 0, sizeof *a);
    io_raw(&s, hdr, sizeof hdr);
    if (s.bad || hdr[0] != (int)PAK_MAGIC || hdr[1] != PAK_VERSION || hdr[2] != (int)EXE_SIZE || hdr[3] != (int)EXE_CRC) { free(b); return -1; }
    pak_io(&s, a);
    free(b);
    return s.bad ? -1 : 0;
}

/* ------------------------------------------------------------------------- finding the exe / zip */
static int ieq(const char* a, const char* b) { for (; *a && *b; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0; return *a == *b; }

static int find_in_dir(const char* dir, const char* want, char* out, size_t cap) {
    DIR* d = opendir(dir);
    struct dirent* e;
    int found = 0;
    if (!d) return 0;
    while ((e = readdir(d))) if (ieq(e->d_name, want)) { snprintf(out, cap, "%s/%s", dir, e->d_name); found = 1; break; }
    closedir(d);
    return found;
}

/* A zip entry by file name (any folder prefix is ignored, case does not matter): stored or deflated entries whose sizes are in the local header.
 * Used for psycho1.zip ("psycho.exe") and for the optional dialogue translation download ("<code>.script"). Returns a malloc'd copy or NULL. */
uint8_t* zip_find(const uint8_t* z, long n, const char* base, long* outlen) {
    long p = 0;
    while (p + 30 <= n && z[p] == 'P' && z[p + 1] == 'K' && z[p + 2] == 3 && z[p + 3] == 4) {
        int method = z[p + 8] | z[p + 9] << 8, nl = z[p + 26] | z[p + 27] << 8, xl = z[p + 28] | z[p + 29] << 8;
        long csz = (long)((uint32_t)z[p + 18] | (uint32_t)z[p + 19] << 8 | (uint32_t)z[p + 20] << 16 | (uint32_t)z[p + 21] << 24);
        long usz = (long)((uint32_t)z[p + 22] | (uint32_t)z[p + 23] << 8 | (uint32_t)z[p + 24] << 16 | (uint32_t)z[p + 25] << 24);
        char nm[160];
        const char* leaf;
        long data = p + 30 + nl + xl;
        memset(nm, 0, sizeof nm);
        memcpy(nm, z + p + 30, (size_t)(nl < 159 ? nl : 159));
        if (data + csz > n) return NULL;
        leaf = strrchr(nm, '/');
        leaf = leaf ? leaf + 1 : nm;
        if (ieq(leaf, base)) {
            if (method == 0) { uint8_t* r = (uint8_t*)malloc((size_t)usz + 1); memcpy(r, z + data, (size_t)usz); *outlen = usz; return r; }
            if (method == 8) { int ol = 0; uint8_t* r = inflate_buf(z + data, csz, 0, &ol); *outlen = ol; return r; }
            return NULL;
        }
        p = data + csz;
    }
    return NULL;
}
static uint8_t* exe_from_zip(const uint8_t* z, long n, long* outlen) { return zip_find(z, n, "psycho.exe", outlen); }

int assets_load(const char* dir, char* err, int errlen) {
    char pak[1100], cdir[1024], path[1100];
    long len = 0;
    uint8_t* exe = NULL;
    snprintf(cdir, sizeof cdir, "%s/cache", dir);
    snprintf(pak, sizeof pak, "%s/psycho.pak", cdir);
    if (pak_read(pak, &A) == 0) return 0;

    if (find_in_dir(dir, "psycho.exe", path, sizeof path)) exe = read_file(path, &len);
    if (!exe && find_in_dir(dir, "psycho1.zip", path, sizeof path)) {
        long zl = 0;
        uint8_t* z = read_file(path, &zl);
        if (z) { exe = exe_from_zip(z, zl, &len); free(z); }
    }
    if (!exe) FAIL("psycho.exe not found.\nCopy psycho.exe (or psycho1.zip) into the gamedata folder.");
    if (len != EXE_SIZE || crc32_of(exe, len) != EXE_CRC) {
        free(exe);
        FAIL("This is not the expected psycho.exe\n(the 2008 release, %ld bytes).", EXE_SIZE);
    }
    memset(&A, 0, sizeof A);
    if (unpack_exe(exe, len, &A, err, errlen)) { free(exe); return -1; }
    free(exe);
    mkdir(cdir, 0777);
    if (pak_write(pak, &A, EXE_CRC) != 0) fprintf(stderr, "psychosomnium: could not write %s (running from memory)\n", pak);
    return 0;
}

/* ---- family-friendly language: whole-word, case-preserving replacements applied to the dialogue as it is looked up (the player's own text stays untouched) */
static int g_family;
static char** g_family_cache;                     /* one filtered copy per text, made on first use */
void assets_set_family_language(int on) { g_family = on; }

static const struct { const char* bad; const char* good; } FAMILY[] = {
    { "goddamn", "gosh darn" }, { "goddamned", "gosh darned" }, { "damned", "darned" }, { "damn", "darn" }, { "dammit", "darn it" },
    { "hell", "heck" }, { "bloody", "darn" }, { "shitty", "crummy" }, { "shit", "shoot" }, { "bullshit", "baloney" },
    { "crap", "crud" }, { "crappy", "crummy" }, { "asshole", "jerk" }, { "ass", "butt" }, { "bastard", "rascal" }, { "bitch", "meanie" },
    { "fucking", "freaking" }, { "fuck", "fudge" }, { "piss", "tick" }, { "pissed", "ticked" }, { NULL, NULL }
};

static char* family_filter(const char* src) {
    size_t n = strlen(src), cap = n * 2 + 16, o = 0, i = 0;
    char* out = (char*)malloc(cap);
    if (!out) return NULL;
    while (i < n) {
        if (isalpha((unsigned char)src[i]) && (i == 0 || !isalpha((unsigned char)src[i - 1]))) {
            size_t e = i, k;
            int hit = -1;
            while (e < n && (isalpha((unsigned char)src[e]) || src[e] == '\'')) e++;
            for (k = 0; FAMILY[k].bad; k++) {
                size_t bl = strlen(FAMILY[k].bad);
                if (bl == e - i && strncasecmp(src + i, FAMILY[k].bad, bl) == 0) { hit = (int)k; break; }
            }
            if (hit >= 0) {
                const char* g = FAMILY[hit].good;
                size_t gl = strlen(g), j;
                int upper_first = isupper((unsigned char)src[i]), all_upper = 1;
                for (j = i; j < e; j++) if (!isupper((unsigned char)src[j])) all_upper = 0;
                if (o + gl + 1 >= cap) { cap = (o + gl) * 2 + 16; out = (char*)realloc(out, cap); if (!out) return NULL; }
                for (j = 0; j < gl; j++) out[o++] = all_upper && e - i > 1 ? (char)toupper((unsigned char)g[j]) : (j == 0 && upper_first ? (char)toupper((unsigned char)g[j]) : g[j]);
                i = e;
                continue;
            }
            if (o + (e - i) + 1 >= cap) { cap = (o + (e - i)) * 2 + 16; out = (char*)realloc(out, cap); if (!out) return NULL; }
            memcpy(out + o, src + i, e - i); o += e - i; i = e;
            continue;
        }
        if (o + 2 >= cap) { cap *= 2; out = (char*)realloc(out, cap); if (!out) return NULL; }
        out[o++] = src[i++];
    }
    out[o] = 0;
    return out;
}

const char* game_text(int obj, int ev, int sub, int k) {
    int i;
    { const char* tr = lang_script(obj, ev, sub, k, g_family); if (tr) return tr; }       /* a translation from the active language's script file */
    for (i = 0; i < A.ntext; i++) {
        const Text* t = &A.text[i];
        if (t->obj == obj && t->ev == ev && t->sub == sub && t->k == k) {
            if (!g_family) return t->s;
            if (!g_family_cache) g_family_cache = (char**)calloc((size_t)A.ntext, sizeof(char*));
            if (g_family_cache && !g_family_cache[i]) g_family_cache[i] = family_filter(t->s);
            return (g_family_cache && g_family_cache[i]) ? g_family_cache[i] : t->s;
        }
    }
    fprintf(stderr, "psychosomnium: missing game text %d/%d/%d/%d\n", obj, ev, sub, k);
    return "";
}
