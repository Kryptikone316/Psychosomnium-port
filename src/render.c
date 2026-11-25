/* Software renderer for the 640x480 RGB565 screen: scaled/flipped sprites with blend colour, "fog"
 * (solid-colour silhouettes, used for the drop shadows) and glyph-atlas text. */
#include "gm.h"
#include "../third_party/font8x8.h"
#include "clearfont.h"
#include "clearnames.h"
#include "lang.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 480

static uint16_t* fb;
static uint16_t* cache;
static uint16_t* tgt;
static int ndirty, full_dirty = 1;

void r_begin(uint16_t* screen) {
    fb = screen;
    if (!cache) cache = (uint16_t*)malloc((size_t)W * H * 2);
    tgt = fb;
    full_dirty = 1;
}
/* The screen only differs from the static-layer cache where sprites/text/boxes were drawn last frame, so r_blit_cache() restores just those
 * rectangles instead of copying the whole 600 KB picture every frame (a full copy is made after the cache itself changed, after a
 * dim/clear/notice screen, and for the first frame). */
#define MAX_DIRTY 96
static struct { int x0, y0, x1, y1; } dirty[MAX_DIRTY];
static inline void mark_dirty(int x0, int y0, int x1, int y1) {
    if (tgt != fb || full_dirty) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W - 1) x1 = W - 1;
    if (y1 > H - 1) y1 = H - 1;
    if (x0 > x1 || y0 > y1) return;
    if (ndirty >= MAX_DIRTY) { full_dirty = 1; return; }
    dirty[ndirty].x0 = x0; dirty[ndirty].y0 = y0; dirty[ndirty].x1 = x1; dirty[ndirty].y1 = y1; ndirty++;
}
void r_target(int to_cache) { tgt = to_cache ? cache : fb; if (to_cache) full_dirty = 1; }   /* the cache is about to change: next copy is a full one */
void r_blit_cache(void) {
#ifdef PSY_TEST
    static int force = -1;
    if (force < 0) force = getenv("PSY_FULLCOPY") != NULL;     /* test hook: always copy the whole picture (to compare against the dirty-rectangle path) */
#else
    const int force = 0;
#endif
    if (full_dirty || force) memcpy(fb, cache, (size_t)W * H * 2);
    else {
        int i, y;
        for (i = 0; i < ndirty; i++)
            for (y = dirty[i].y0; y <= dirty[i].y1; y++)
                memcpy(fb + (size_t)y * W + dirty[i].x0, cache + (size_t)y * W + dirty[i].x0, (size_t)(dirty[i].x1 - dirty[i].x0 + 1) * 2);
    }
    full_dirty = 0; ndirty = 0;
}

static inline uint16_t p565(int r, int g, int b) { return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }
static inline uint16_t bgr565(uint32_t c) { return p565((int)(c & 255), (int)((c >> 8) & 255), (int)((c >> 16) & 255)); }

void r_clear(uint32_t bgr) {
    uint16_t c = bgr565(bgr);
    int i;
    if (tgt == fb) full_dirty = 1;
    for (i = 0; i < W * H; i++) tgt[i] = c;
}

static inline void put_mix(uint16_t* d, int r, int g, int b, int a /* 0..256 */) {
    uint16_t o = *d;
    int orr = (o >> 11) << 3, og = ((o >> 5) & 63) << 2, ob = (o & 31) << 3;
    orr |= orr >> 5; og |= og >> 6; ob |= ob >> 5;
    r = orr + (((r - orr) * a) >> 8);
    g = og + (((g - og) * a) >> 8);
    b = ob + (((b - ob) * a) >> 8);
    *d = p565(r, g, b);
}

static void blit(const Sprite* sp, int frame, double x, double y, double xs, double ys, uint32_t blend, double alpha, int fog, uint32_t fogc) {
    double x0, x1, y0, y1, lx, hx, ly, hy;
    int dx0, dx1, dy0, dy1, dx, dy, a256, white, br, bg, bb;
    static int ucol[W + 64], vrow[H + 64];
    uint16_t fogp;
    const uint32_t* src;
    if (!sp || !sp->valid || sp->nframes <= 0 || xs == 0 || ys == 0) return;
    frame %= sp->nframes; if (frame < 0) frame += sp->nframes;
    src = sp->frame[frame];
    x0 = x - sp->ox * xs; x1 = x + (sp->w - sp->ox) * xs;
    y0 = y - sp->oy * ys; y1 = y + (sp->h - sp->oy) * ys;
    lx = x0 < x1 ? x0 : x1; hx = x0 < x1 ? x1 : x0;
    ly = y0 < y1 ? y0 : y1; hy = y0 < y1 ? y1 : y0;
    dx0 = (int)ceil(lx - 0.5); dx1 = (int)ceil(hx - 0.5) - 1;
    dy0 = (int)ceil(ly - 0.5); dy1 = (int)ceil(hy - 0.5) - 1;
    if (dx0 < 0) dx0 = 0;
    if (dy0 < 0) dy0 = 0;
    if (dx1 > W - 1) dx1 = W - 1;
    if (dy1 > H - 1) dy1 = H - 1;
    if (dx0 > dx1 || dy0 > dy1) return;
    mark_dirty(dx0, dy0, dx1, dy1);
    for (dx = dx0; dx <= dx1; dx++) {
        double u = (dx + 0.5 - lx) / (hx - lx) * sp->w;
        int iu;
        if (xs < 0) u = sp->w - u;
        iu = (int)floor(u); if (iu < 0) iu = 0; if (iu > sp->w - 1) iu = sp->w - 1;
        ucol[dx - dx0] = iu;
    }
    for (dy = dy0; dy <= dy1; dy++) {
        double v = (dy + 0.5 - ly) / (hy - ly) * sp->h;
        int iv;
        if (ys < 0) v = sp->h - v;
        iv = (int)floor(v); if (iv < 0) iv = 0; if (iv > sp->h - 1) iv = sp->h - 1;
        vrow[dy - dy0] = iv;
    }
    a256 = (int)(alpha * 256.0 + 0.5); if (a256 > 256) a256 = 256;
    if (a256 <= 0) return;
    white = (blend & 0xffffff) == 0xffffff;
    br = (int)(blend & 255) + 1; bg = (int)((blend >> 8) & 255) + 1; bb = (int)((blend >> 16) & 255) + 1;
    fogp = bgr565(fogc);
    for (dy = dy0; dy <= dy1; dy++) {
        const uint32_t* row = src + (size_t)vrow[dy - dy0] * sp->w;
        uint16_t* d = tgt + (size_t)dy * W + dx0;
        for (dx = dx0; dx <= dx1; dx++, d++) {
            uint32_t px = row[ucol[dx - dx0]];
            int r, g, b;
            if (!(px >> 24)) continue;
            if (fog) {
                if (a256 >= 256) { *d = fogp; continue; }
                r = (int)(fogc & 255); g = (int)((fogc >> 8) & 255); b = (int)((fogc >> 16) & 255);
            } else {
                r = (int)((px >> 16) & 255); g = (int)((px >> 8) & 255); b = (int)(px & 255);
                if (!white) { r = (r * br) >> 8; g = (g * bg) >> 8; b = (b * bb) >> 8; }
                if (a256 >= 256) { *d = p565(r, g, b); continue; }
            }
            put_mix(d, r, g, b, a256);
        }
    }
}

void r_sprite(int spr, double index, double x, double y, double xs, double ys, uint32_t blend, double alpha) {
    if (spr < 0 || spr >= N_SPRITES) return;
    blit(&A.spr[spr], (int)gm_round(index), x, y, xs, ys, blend, alpha, 0, 0);       /* the original rounds (half to even) */
}
void r_sprite_fog(int spr, double index, double x, double y, double xs, double ys, uint32_t fog, double alpha) {
    if (spr < 0 || spr >= N_SPRITES) return;
    blit(&A.spr[spr], (int)gm_round(index), x, y, xs, ys, 0xffffff, alpha, 1, fog);
}

/* ------------------------------------------------------------------------------------------- text */
static void glyph(const Font* f, const Glyph* g, double x, double y, double xs, double ys, uint32_t col) {
    int dx0 = (int)ceil(x - 0.5), dx1 = (int)ceil(x + g->w * xs - 0.5) - 1;
    int dy0 = (int)ceil(y - 0.5), dy1 = (int)ceil(y + g->h * ys - 0.5) - 1;
    int dx, dy, cr = (int)(col & 255), cg = (int)((col >> 8) & 255), cb = (int)((col >> 16) & 255);
    if (dx0 < 0) dx0 = 0;
    if (dy0 < 0) dy0 = 0;
    if (dx1 > W - 1) dx1 = W - 1;
    if (dy1 > H - 1) dy1 = H - 1;
    mark_dirty(dx0, dy0, dx1, dy1);
    for (dy = dy0; dy <= dy1; dy++) {
        int sy = g->y + (int)floor((dy + 0.5 - y) / ys);
        uint16_t* d;
        if (sy < g->y || sy >= g->y + g->h) continue;
        d = tgt + (size_t)dy * W + dx0;
        for (dx = dx0; dx <= dx1; dx++, d++) {
            int sx = g->x + (int)floor((dx + 0.5 - x) / xs), a;
            if (sx < g->x || sx >= g->x + g->w) continue;
            a = f->alpha[(size_t)sy * f->aw + sx];
            if (!a) continue;
            if (a >= 255) *d = p565(cr, cg, cb); else put_mix(d, cr, cg, cb, a + (a >> 7));
        }
    }
}


/* ---- the game's own larger font (font 1, 9 pixels high with 2-pixel strokes), with the three letters that come out as solid blocks in it
 * (M, N, W) redrawn in the same style. Used for the dialogue in "original font" mode (the game's smaller font 0 is a 5-pixel font in which Y
 * looks like T and 0/8/9 are blobs: unreadable). */
typedef struct { char c; int w, shift; const char* rows[9]; } FixGlyph;
static const FixGlyph FIX[] = {
    { 'M', 7, 9, { "##...##", "###.###", "#######", "##.#.##", "##.#.##", "##...##", "##...##", "##...##", "##...##" } },
    { 'N', 7, 9, { "##...##", "###..##", "###..##", "##.#.##", "##.#.##", "##..###", "##..###", "##...##", "##...##" } },
    { 'W', 9, 10, { "##.....##", "##.....##", "##..#..##", "##..#..##", "##.###.##", "##.###.##", ".###.###.", ".##...##.", ".##...##." } },
};
#define N_FIX ((int)(sizeof FIX / sizeof FIX[0]))
#define FIX_BASE 1
static Font fixfont;
static int fix_ready;
const Font* fix_font(void) {
    int i, r, x;
    int strip = (N_FIX + 11) / 12;
    if (fix_ready) return &fixfont;
    if (!A.fnt[FIX_BASE].valid) return NULL;
    fixfont = A.fnt[FIX_BASE];
    fixfont.ah = A.fnt[FIX_BASE].ah + 13 * strip;
    fixfont.alpha = (uint8_t*)calloc((size_t)fixfont.aw * fixfont.ah, 1);
    memcpy(fixfont.alpha, A.fnt[FIX_BASE].alpha, (size_t)A.fnt[FIX_BASE].aw * A.fnt[FIX_BASE].ah);
    for (i = 0; i < N_FIX; i++) {
        int gx = (i % 12) * 10, gy = A.fnt[FIX_BASE].ah + (i / 12) * 13;
        Glyph* g = &fixfont.g[(unsigned char)FIX[i].c];
        for (r = 0; r < 9; r++)
            for (x = 0; FIX[i].rows[r][x]; x++)
                if (FIX[i].rows[r][x] == '#') fixfont.alpha[(size_t)(gy + 2 + r) * fixfont.aw + gx + x] = 255;
        g->x = gx; g->y = gy; g->w = FIX[i].w; g->h = 13; g->shift = FIX[i].shift; g->off = 0;
    }
    fix_ready = 1;
    return &fixfont;
}

void r_text(int fi, double x, double y, const char* s, double xs, double ys, uint32_t col, int halign, int valign) {
    const Font* f;
    int nlines = 1, line = 0;
    const char* p;
    double lh, total, cy;
    if (fi == FONT_FIXED) { f = fix_font(); if (!f) return; }
    else { if (fi < 0 || fi >= N_FONTS || !A.fnt[fi].valid) return; f = &A.fnt[fi]; }
    for (p = s; *p; p++) if (*p == '#') nlines++;
    lh = f->lineh * ys;
    total = lh * nlines;
    cy = y;
    if (valign == FA_MIDDLE) cy = y - total / 2.0;
    p = s;
    while (line < nlines) {
        const char* e = p;
        double wdt = 0, cx;
        const char* q;
        while (*e && *e != '#') e++;
        for (q = p; q < e; q++) wdt += f->g[(unsigned char)*q].shift * xs;
        cx = x;
        if (halign == FA_CENTER) cx = x - wdt / 2.0;
        for (q = p; q < e; q++) {
            const Glyph* g = &f->g[(unsigned char)*q];
            if (g->w > 0 && g->h > 0) glyph(f, g, cx + g->off * xs, cy, xs, ys, col);
            cx += g->shift * xs;
        }
        cy += lh;
        line++;
        p = *e ? e + 1 : e;
    }
}

/* ---- clear text: anti-aliased font (UTF-8), boxes, a dimmer: the readable dialogue, the launcher, the pause screen.
 * The built-in glyphs are ASCII (DejaVu Sans Condensed Bold); a language pack brings its own glyphs (Latin accents, Cyrillic, Arabic forms, Japanese, Chinese). */
typedef struct { int x, y, w, h, bx, ty, adv; const unsigned char* atlas; int aw; } Gl;
static const LFont* af;                              /* active pack font, NULL = built-in */
static int g_rtl;
void r_set_font(const LFont* f, int rtl) { af = f; g_rtl = rtl; }
int r_is_rtl(void) { return g_rtl; }
int r_ctext_line(void) { return af ? af->line : CF_LINE; }
static int r_ascent(void) { return af ? af->ascent : CF_ASCENT; }

static int get_glyph(uint32_t cp, Gl* g) {
    if (af) {
        int lo = 0, hi = af->n - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            const LGlyph* q = &af->g[mid];
            if (q->cp == cp) { g->x = q->x; g->y = q->y; g->w = q->w; g->h = q->h; g->bx = q->bx; g->ty = q->ty; g->adv = q->adv; g->atlas = af->atlas; g->aw = af->aw; return 1; }
            if (q->cp < cp) lo = mid + 1; else hi = mid - 1;
        }
        return 0;
    }
    if (cp < 32 || cp >= 127) return 0;
    {
        const short* q = cf_glyph[cp - 32];
        g->x = q[0]; g->y = q[1]; g->w = q[2]; g->h = q[3]; g->bx = q[4]; g->ty = q[5]; g->adv = q[6]; g->atlas = cf_atlas; g->aw = CF_AW;
    }
    return 1;
}

int u8_decode(const char* s, uint32_t* out, int max) {
    int n = 0;
    const unsigned char* p = (const unsigned char*)s;
    while (*p && n < max) {
        uint32_t c = *p;
        int extra = 0;
        if (c < 0x80) { p++; }
        else if ((c & 0xe0) == 0xc0) { c &= 0x1f; extra = 1; }
        else if ((c & 0xf0) == 0xe0) { c &= 0x0f; extra = 2; }
        else if ((c & 0xf8) == 0xf0) { c &= 0x07; extra = 3; }
        else { p++; c = '?'; extra = 0; out[n++] = c; continue; }
        if (extra) { p++; while (extra-- && (*p & 0xc0) == 0x80) { c = (c << 6) | (*p & 0x3f); p++; } }
        out[n++] = c;
    }
    return n;
}

int r_cp_width(const uint32_t* cp, int n) {
    int i, w = 0;
    for (i = 0; i < n; i++) { Gl g; w += get_glyph(cp[i], &g) ? g.adv : 10; }
    return w;
}

/* right-to-left text: the logical string is turned into display order; runs of Latin letters/digits keep their own order */
static int is_ltr_char(uint32_t c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= 0xc0 && c < 0x250); }
void r_visual(const uint32_t* in, int n, uint32_t* out) {
    int i;
    if (!g_rtl) { memcpy(out, in, (size_t)n * sizeof *out); return; }
    for (i = 0; i < n; i++) out[i] = in[n - 1 - i];
    for (i = 0; i < n;) {
        int j = i;
        while (j < n && is_ltr_char(out[j])) j++;
        if (j - i > 1) { int a = i, b = j - 1; while (a < b) { uint32_t t = out[a]; out[a] = out[b]; out[b] = t; a++; b--; } }
        i = j > i ? j : i + 1;
    }
}

void r_cp_draw(int x, int y, const uint32_t* cp, int n, uint32_t col) {
    int cr = (int)(col & 255), cg = (int)((col >> 8) & 255), cb = (int)((col >> 16) & 255), i;
    for (i = 0; i < n; i++) {
        Gl g;
        int gx, gy;
        if (!get_glyph(cp[i], &g)) { x += 10; continue; }
        mark_dirty(x + g.bx, y + r_ascent() + g.ty, x + g.bx + g.w, y + r_ascent() + g.ty + g.h);
        for (gy = 0; gy < g.h; gy++) {
            int py = y + r_ascent() + g.ty + gy;
            if (py < 0 || py >= H) continue;
            for (gx = 0; gx < g.w; gx++) {
                int px = x + g.bx + gx, a = g.atlas[(g.y + gy) * g.aw + g.x + gx];
                if (px < 0 || px >= W || !a) continue;
                if (a >= 250) tgt[py * W + px] = p565(cr, cg, cb); else put_mix(&tgt[py * W + px], cr, cg, cb, a + (a >> 7));
            }
        }
        x += g.adv;
    }
}

int r_ctext_width(const char* s) {
    uint32_t cp[256];
    return r_cp_width(cp, u8_decode(s, cp, 256));
}
void r_ctext(int x, int y, const char* s, int len, uint32_t col) {       /* len = number of characters to draw (99 = all) */
    uint32_t cp[256], vis[256];
    int n = u8_decode(s, cp, 256);
    if (len < n) n = len;
    r_visual(cp, n, vis);
    r_cp_draw(x, y, vis, n, col);
}

/* built-in ASCII glyphs at double size (the menu title) */
void r_ctext2x(int x, int y, const char* s, uint32_t col) {
    int cr = (int)(col & 255), cg = (int)((col >> 8) & 255), cb = (int)((col >> 16) & 255);
    for (; *s; s++) {
        const short* g;
        int gx, gy, a2, ox, oy;
        if ((unsigned char)*s < 32 || (unsigned char)*s >= 127) continue;
        g = cf_glyph[*s - 32];
        mark_dirty(x + 2 * g[4], y + 2 * (CF_ASCENT + g[5]), x + 2 * (g[4] + g[2]) + 1, y + 2 * (CF_ASCENT + g[5] + g[3]) + 1);
        for (gy = 0; gy < g[3]; gy++)
            for (gx = 0; gx < g[2]; gx++) {
                a2 = cf_atlas[(g[1] + gy) * CF_AW + g[0] + gx];
                if (!a2) continue;
                for (oy = 0; oy < 2; oy++) for (ox = 0; ox < 2; ox++) {
                    int px = x + 2 * (g[4] + gx) + ox, py = y + 2 * (CF_ASCENT + g[5] + gy) + oy;
                    if (px < 0 || px >= W || py < 0 || py >= H) continue;
                    if (a2 >= 250) tgt[py * W + px] = p565(cr, cg, cb); else put_mix(&tgt[py * W + px], cr, cg, cb, a2 + (a2 >> 7));
                }
            }
        x += 2 * g[6];
    }
}
int r_ctext_width2x(const char* s) {
    int w = 0;
    for (; *s; s++) if ((unsigned char)*s >= 32 && (unsigned char)*s < 127) w += 2 * cf_glyph[*s - 32][6];
    return w;
}

/* a language's name in its own script, from the built-in names atlas (so the Language row can show every language whatever pack is active) */
static int nm_find(int cp, int set) {
    int i;
    for (i = 0; i < NM_N; i++) if (nm_g[i][0] == cp && nm_g[i][1] == set) return i;
    return -1;
}
int r_name_width(int idx) {
    int w = 0, i;
    for (i = 0; nm_text[idx][i]; i++) { int g = nm_find(nm_text[idx][i], nm_set[idx]); w += g >= 0 ? nm_g[g][8] : 10; }
    return w;
}
void r_name_draw(int idx, int x, int y, uint32_t col) {
    int cr = (int)(col & 255), cg = (int)((col >> 8) & 255), cb = (int)((col >> 16) & 255), i;
    for (i = 0; nm_text[idx][i]; i++) {
        int gi = nm_find(nm_text[idx][i], nm_set[idx]), gx, gy;
        const int* g;
        if (gi < 0) { x += 10; continue; }
        g = nm_g[gi];
        mark_dirty(x + g[6], y + CF_ASCENT + g[7], x + g[6] + g[4], y + CF_ASCENT + g[7] + g[5]);
        for (gy = 0; gy < g[5]; gy++) {
            int py = y + CF_ASCENT + g[7] + gy;
            if (py < 0 || py >= H) continue;
            for (gx = 0; gx < g[4]; gx++) {
                int px = x + g[6] + gx, a = nm_atlas[(g[3] + gy) * NM_AW + g[2] + gx];
                if (px < 0 || px >= W || !a) continue;
                if (a >= 250) tgt[py * W + px] = p565(cr, cg, cb); else put_mix(&tgt[py * W + px], cr, cg, cb, a + (a >> 7));
            }
        }
        x += g[8];
    }
}

void r_box(int x, int y, int w, int h, int border, uint32_t border_bgr, uint32_t fill_bgr) {
    uint16_t bp = bgr565(border_bgr), fp = bgr565(fill_bgr);
    int dx, dy;
    mark_dirty(x, y, x + w - 1, y + h - 1);
    for (dy = 0; dy < h; dy++) {
        int py = y + dy;
        if (py < 0 || py >= H) continue;
        for (dx = 0; dx < w; dx++) {
            int px = x + dx, ex = dx < 4 ? 4 - dx : (dx >= w - 4 ? dx - (w - 5) : 0), ey = dy < 4 ? 4 - dy : (dy >= h - 4 ? dy - (h - 5) : 0);
            if (px < 0 || px >= W) continue;
            if (ex && ey && ex + ey > 5) continue;                                   /* rounded corners */
            tgt[py * W + px] = (dx < border || dx >= w - border || dy < border || dy >= h - border || (ex && ey && ex + ey > 4)) ? bp : fp;
        }
    }
}
void r_dim(void) {                                  /* darken the whole screen to a quarter */
    int i;
    if (tgt == fb) full_dirty = 1;
    for (i = 0; i < W * H; i++) tgt[i] = (uint16_t)((tgt[i] >> 2) & 0x39e7);
}

/* Plain notice screen using the built-in public-domain 8x8 font (used before/without game data). */
void r_message(const char* text) {
    int x = 24, y = 24, i;
    full_dirty = 1;
    for (i = 0; i < W * H; i++) fb[i] = 0x1082;
    for (; *text; text++) {
        unsigned char c = (unsigned char)*text;
        int gx, gy;
        if (c == '\n' || x > W - 40) { x = 24; y += 20; if (c == '\n') continue; }
        if (c >= 128) c = '?';
        for (gy = 0; gy < 8; gy++)
            for (gx = 0; gx < 8; gx++)
                if (font8x8_basic[c][gy] & (1 << gx)) {
                    int ox, oy;
                    for (oy = 0; oy < 2; oy++) for (ox = 0; ox < 2; ox++) {
                        int px = x + gx * 2 + ox, py = y + gy * 2 + oy;
                        if (px < W && py < H) fb[py * W + px] = 0xffff;
                    }
                }
        x += 16;
    }
}

/* width in pixels of one line of text in a game font at horizontal scale xs */
double r_font_width(int fi, const char* s, double xs) {
    const Font* f = fi == FONT_FIXED ? fix_font() : (fi >= 0 && fi < N_FONTS && A.fnt[fi].valid ? &A.fnt[fi] : NULL);
    double w = 0;
    if (!f) return 0;
    for (; *s; s++) w += f->g[(unsigned char)*s].shift * xs;
    return w;
}
