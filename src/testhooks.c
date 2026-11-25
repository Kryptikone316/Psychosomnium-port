/* Developer test hooks, compiled only into the desktop test build (build_host.sh defines PSY_TEST; the SF3500 build does not contain any of this).
 * They are driven by PSY_* environment variables and are what the scripted checks, the comparisons with the original under Wine and the
 * screenshots of tools/wine and DEV-NOTES.md use. */
#ifdef PSY_TEST
#include "gm.h"
#include "lang.h"
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "testhooks.h"

/* test hook PSY_SETVARS="obj:var=val,obj:var=val" (var: on, active, persistent): mirrors `with OBJ {var=val}` in a room creation code */
static void apply_setvars(void) {
    const char* s = getenv("PSY_SETVARS");
    int o, k, n;
    char var[32];
    double v;
    while (s && *s && sscanf(s, "%d:%31[a-z]=%lf%n", &o, var, &v, &n) == 3) {
        for (k = 0; k < nL; k++) {
            Inst* i = L[k];
            if (!i->alive || i->obj != o) continue;
            if (!strcmp(var, "on")) i->on = v; else if (!strcmp(var, "active")) i->active = v; else if (!strcmp(var, "persistent")) i->persistent = (int)v;
        }
        s += n; if (*s == ',') s++;
    }
}

static void mask_diff(void) {                    /* test hook PSY_MASKDIFF: sprites whose frames have different opaque pixels (union mask vs first-frame mask) */
    int i, f, x, y;
    for (i = 0; i < N_SPRITES; i++) {
        const Sprite* sp = &A.spr[i];
        int diff = 0, npx0 = 0, npxu = 0;
        if (!sp->valid || sp->nframes < 2) continue;
        for (y = 0; y < sp->h; y++) for (x = 0; x < sp->w; x++) {
            int in0 = (sp->frame[0][(size_t)y * sp->w + x] >> 24) != 0, inu = in0;
            for (f = 1; f < sp->nframes; f++) if (sp->frame[f][(size_t)y * sp->w + x] >> 24) inu = 1;
            npx0 += in0; npxu += inu; if (in0 != inu) diff++;
        }
        if (diff) fprintf(stderr, "sprite %d (%dx%d, %d frames): first-frame mask %d px, union %d px, %d px differ\n", i, sp->w, sp->h, sp->nframes, npx0, npxu, diff);
    }
}

static void dump_fonts(const char* dir) {        /* test hook PSY_DUMPFONT=<dir>: the game fonts' glyph atlases as PGM files + a metrics list */
    int f, c;
    for (f = 0; f < N_FONTS; f++) {
        char path[512];
        FILE* o;
        const Font* ft = &A.fnt[f];
        if (!ft->valid) continue;
        snprintf(path, sizeof path, "%s/font%d.pgm", dir, f);
        o = fopen(path, "wb");
        if (o) { fprintf(o, "P5 %d %d 255\n", ft->aw, ft->ah); fwrite(ft->alpha, 1, (size_t)ft->aw * ft->ah, o); fclose(o); }
        fprintf(stderr, "font %d: atlas %dx%d lineh %d\n", f, ft->aw, ft->ah, ft->lineh);
        for (c = 32; c < 128; c++) { const Glyph* g = &ft->g[c]; fprintf(stderr, "  %c(%d): x%d y%d w%d h%d shift%d off%d\n", c, c, g->x, g->y, g->w, g->h, g->shift, g->off); }
    }
}

static void print_masks(void) {                 /* test hook PSY_MASKS: sprite size, origin, and the box of opaque pixels (alpha > 0 / > 127) */
    static const int ids[] = { S_SPRITE4, S_SPRITE22, S_SPIKES, S_SPIKES2, S_P1, S_P2, S_BUNNY, -1 };
    int k, x, y;
    for (k = 0; ids[k] >= 0; k++) {
        const Sprite* sp = &A.spr[ids[k]];
        int l = 999, r = -1, t = 999, b = -1, l2 = 999, r2 = -1, t2 = 999, b2 = -1;
        if (!sp->valid) continue;
        for (y = 0; y < sp->h; y++) for (x = 0; x < sp->w; x++) {
            uint32_t px = sp->frame[0][(size_t)y * sp->w + x];
            unsigned a = px >> 24;
            if (a > 0) { if (x < l) l = x; if (x > r) r = x; if (y < t) t = y; if (y > b) b = y; }
            if (a > 127) { if (x < l2) l2 = x; if (x > r2) r2 = x; if (y < t2) t2 = y; if (y > b2) b2 = y; }
        }
        { int f; for (f = 0; f < sp->nframes; f++) {
            int fl = 999, fr = -1, ft = 999, fb = -1;
            for (y = 0; y < sp->h; y++) for (x = 0; x < sp->w; x++) if ((sp->frame[f][(size_t)y * sp->w + x] >> 24) > 0) { if (x < fl) fl = x; if (x > fr) fr = x; if (y < ft) ft = y; if (y > fb) fb = y; }
            fprintf(stderr, "  sprite %d frame %d: opaque x %d..%d y %d..%d\n", ids[k], f, fl, fr, ft, fb);
        } }
        { int mx0 = 999, mx1 = -1, my0 = 999, my1 = -1; if (sp->mask) for (y = 0; y < sp->h; y++) for (x = 0; x < sp->w; x++) if (sp->mask[(size_t)y * sp->w + x]) { if (x < mx0) mx0 = x; if (x > mx1) mx1 = x; if (y < my0) my0 = y; if (y > my1) my1 = y; }
          fprintf(stderr, "  sprite %d engine mask (union) x %d..%d y %d..%d, stored bbox l%d r%d t%d b%d\n", ids[k], mx0, mx1, my0, my1, sp->bl, sp->br, sp->bt, sp->bb); }
        fprintf(stderr, "sprite %d: %dx%d origin %d,%d frames %d  mask(alpha>0) x %d..%d y %d..%d   visible(alpha>127) x %d..%d y %d..%d  mask sprite %d\n", ids[k], sp->w, sp->h, sp->ox, sp->oy, sp->nframes, l, r, t, b, l2, r2, t2, b2, sp->mask ? 1 : 0);
    }
}

void th_room_entered(void) {                 /* test hook PSY_LISTOBJ: which objects the room just entered holds */
    int k, cnt[N_OBJECTS] = {0};
    if (getenv("PSY_DUMPROOM")) {                 /* every instance: obj x y width height (screen px) */
        for (k = 0; k < nL; k++) {
            Inst* i = L[k];
            const Sprite* sp = (i->sprite >= 0 && i->sprite < N_SPRITES) ? &A.spr[i->sprite] : NULL;
            if (i->alive) fprintf(stderr, "I %d %d %.0f %.0f %.0f %.0f\n", G.room, i->obj, i->x - (sp ? sp->ox * i->xscale : 0), i->y - (sp ? sp->oy * i->yscale : 0), sp ? sp->w * i->xscale : 0, sp ? sp->h * i->yscale : 0);
        }
    }
    if (!getenv("PSY_LISTOBJ")) return;
    for (k = 0; k < nL; k++) if (L[k]->alive && L[k]->obj >= 0 && L[k]->obj < N_OBJECTS) cnt[L[k]->obj]++;
    for (k = 0; k < N_OBJECTS; k++) if (cnt[k]) fprintf(stderr, "room %d: object %d x%d\n", G.room, k, cnt[k]);
}

/* test hook PSY_BEEBOT="delay,target[,mode]": a closed-loop pilot for the bee (right held; Up/Down chosen from the bee's height seen `delay` frames ago);
 * mode 0 = taps (original flight), 1 = holds (fine flight). Used to compare how controllable the corridor is with each flight model. */
unsigned th_pilot(unsigned b) {
    static double hy[64], hv[64];
    static int n;
    const char* e = getenv("PSY_BEEBOT");
    int delay = 0, mode = 1;
    double target = 258;
    Inst* p = instance_first(O_P4);
    static int cool;
    if (!e || !p) return b;
    sscanf(e, "%d,%lf,%d", &delay, &target, &mode);
    hy[n & 63] = p->y; hv[n & 63] = p->vspeed; n++;
    {
        double y = hy[(n - 1 - delay) & 63], v = hv[(n - 1 - delay) & 63], pred = y + 8 * v;
        b |= B_RIGHT;
        if (mode == 1) {
            if (pred > target + 2) b |= B_UP; else if (pred < target - 6) b |= B_DOWN;
        } else {
            if (cool > 0) cool--;
            else if (y >= target + 4 && v > -1.0) { b |= B_UP; cool = 4; }
            else if (cool == 0) b &= ~B_UP;
        }
    }
    return b;
}


static int trace_every;

/* test setup, mirrored by tools/wine/patch_exe.py for the original:
 * PSY_TESTROOM=<n> moves room n to right after the splash screen; PSY_SPAWNS="obj,x,y;obj,x,y" appends instances to it
 * (ids 200000+); PSY_SAVE="room,type" sets global.saveroom / global.type. */
void th_setup(void) {
    const char* tr = getenv("PSY_TESTROOM");
    const char* sp = getenv("PSY_SPAWNS");
    const char* sv = getenv("PSY_SAVE");
    if (tr) {
        int room = atoi(tr), k, n = 0, ord[N_ROOMS];
        ord[n++] = A.order[0];
        ord[n++] = room;
        for (k = 1; k < N_ROOMS; k++) if (A.order[k] != room && A.order[k] != A.order[0]) ord[n++] = A.order[k];
        memcpy(A.order, ord, sizeof ord);
        if (sp) {
            Room* r = &A.room[room];
            const char* p = sp;
            int cnt = 0, o, x, y, used;
            r->inst = (RInst*)realloc(r->inst, sizeof(RInst) * (size_t)(r->n + 16));
            while (sscanf(p, "%d,%d,%d%n", &o, &x, &y, &used) == 3) {
                RInst* ri = &r->inst[r->n++];
                ri->id = 200000 + cnt++; ri->obj = o; ri->x = x; ri->y = y; ri->xs = ri->ys = 1.0;
                p += used; if (*p == ';') p++; else break;
            }
        }
    }
    if (sv) { int r, t; if (sscanf(sv, "%d,%d", &r, &t) == 2) game_set_save(r, t); }
    if (getenv("PSY_SETVARS")) room_code_hook = apply_setvars;
}

void th_after_settings(void) {
    if (getenv("PSY_DUMPTEXT")) { int i; for (i = 0; i < A.ntext; i++) fprintf(stderr, "T %d %d %d %d: %s\n", A.text[i].obj, A.text[i].ev, A.text[i].sub, A.text[i].k, game_text(A.text[i].obj, A.text[i].ev, A.text[i].sub, A.text[i].k)); }
    if (getenv("PSY_DUMPFONT")) dump_fonts(getenv("PSY_DUMPFONT"));
    if (getenv("PSY_MASKS")) print_masks();
    if (getenv("PSY_MASKDIFF")) mask_diff();
    { const char* fp = getenv("PSY_FLT"); double a, c, t; if (fp && sscanf(fp, "%lf,%lf,%lf", &a, &c, &t) == 3) game_set_flight_params(a, c, t); }
    if (getenv("PSY_FONTSHEET")) {                         /* the repaired game font, alphabet and digits, at the dialogue size */
        static const char* rows[] = { "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "0123456789 !?.,:;'\"-*()", "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG", "PACK MY BOX WITH FIVE DOZEN LIQUOR JUGS. 1234567890", NULL };
        int i;
        r_target(0); r_clear(gm_rgb(250, 246, 234));
        for (i = 0; rows[i]; i++) r_text(FONT_FIXED, 20, 20 + i * 40, rows[i], 2, 2, gm_rgb(38, 56, 70), FA_LEFT, FA_TOP);
        plat_present(); plat_present(); plat_present();
        plat_quit(0);
    }
    { const char* tr = getenv("PSY_TRACE"); trace_every = tr ? atoi(tr) : 0; }
}

int th_menu_wanted(void) { return !plat_headless() || getenv("PSY_MENU"); }     /* scripted headless runs skip the launcher unless asked */

void th_frame(void) {
    if (getenv("PSY_TRACEB") && trace_every && plat_frame() % (unsigned)trace_every == 0) {
        Inst* bb = instance_first(O_BUNNY);
        if (bb) fprintf(stderr, "b%u y=%.2f vs=%.2f idx=%.1f grav=%.1f\n", plat_frame(), bb->y, bb->vspeed, bb->image_index, bb->gravity);
    }
    if (trace_every && plat_frame() % (unsigned)trace_every == 0) {
        Inst* p = instance_first(O_PLAYER);
        fprintf(stderr, "f%u room %d inst %d player %s x=%.2f y=%.2f vs=%.2f hs=%.3g msg=%d spr=%d bunnies=%d fr=%.2f sp=%.3g idx=%.2f\n", plat_frame(), G.room, nL,
                p ? "yes" : "no", p ? p->x : 0, p ? p->y : 0, p ? p->vspeed : 0, p ? p->hspeed : 0, instance_exists(O_MESSAGER), p ? p->sprite : -1, instance_number(O_BUNNY), p ? p->friction : 0, p ? p->speed : 0, p ? p->image_index : 0);
    }
}
#endif /* PSY_TEST */
