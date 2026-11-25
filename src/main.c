/* Psychosomnium for the SF3500: bring-your-own-data port. Put psycho.exe (or psycho1.zip) into gamedata/;
 * the first start unpacks it into gamedata/cache/. */
#include "gm.h"
#include "lang.h"
#include "menu.h"
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KB_LEFT 1
#define KB_UP 2
#define KB_RIGHT 4
#define KB_Z 8
#define KB_ENTER 16
#define KB_DOWN 32
#define END_ROOM 20                 /* "THE END" / credits, the last room of this game (psycho.exe is checked by size and CRC) */

static int trace_every;

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

static void draw_pause(void) {
    const char* a = ui(L_PAUSED);
    const char* b = ui(L_RESUME);
    int w = (r_ctext_width(b) > r_ctext_width(a) ? r_ctext_width(b) : r_ctext_width(a)) + 60, h = 3 * r_ctext_line() + 20, x = (SCR_W - w) / 2, y = (SCR_H - h) / 2;
    r_target(0);
    r_dim();
    r_box(x, y, w, h, 3, C_SHELDON, gm_rgb(250, 246, 234));
    r_ctext((SCR_W - r_ctext_width(a)) / 2, y + 10, a, 99, gm_rgb(38, 56, 70));
    r_ctext((SCR_W - r_ctext_width(b)) / 2, y + 10 + 2 * r_ctext_line(), b, 99, gm_rgb(38, 56, 70));
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

static void list_objects(void) {                 /* test hook PSY_LISTOBJ: which objects the room just entered holds */
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
static unsigned beebot(unsigned b) {
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

static unsigned map_keys(unsigned b) {
    unsigned k = 0;
    if (b & B_LEFT) k |= KB_LEFT;
    if (b & B_RIGHT) k |= KB_RIGHT;
    if (b & (B_UP | B_B | B_Y)) k |= KB_UP;            /* jump: D-pad up, B or Y */
    if (b & B_DOWN) k |= KB_DOWN;
    if (b & (B_A | B_X)) k |= KB_Z;                    /* action / punch / next line: A or X */
    if ((b & B_START) && !(b & B_SELECT) && G.room == room_first()) k |= KB_ENTER;     /* START starts the game from the title; elsewhere it pauses */
    return k;
}

static void wait_start(void) {
    for (;;) {
        unsigned b = plat_buttons();
        if (b & (B_START | B_A | B_B)) { while (plat_buttons() & (B_START | B_A | B_B)) plat_present(); break; }
        if (trace_every && plat_frame() % (unsigned)trace_every == 0) {
            Inst* p = instance_first(O_PLAYER);
            fprintf(stderr, "f%u room %d inst %d player %s x=%.2f y=%.2f vs=%.2f hs=%.3g msg=%d spr=%d bunnies=%d fr=%.2f sp=%.3g idx=%.2f\n", plat_frame(), G.room, nL,
                    p ? "yes" : "no", p ? p->x : 0, p ? p->y : 0, p ? p->vspeed : 0, p ? p->hspeed : 0, instance_exists(O_MESSAGER), p ? p->sprite : -1, instance_number(O_BUNNY), p ? p->friction : 0, p ? p->speed : 0, p ? p->image_index : 0);
        }
        plat_present();
    }
}

int main(void) {
    char err[512];
    const char* dir = getenv("PSY_GAMEDATA");
    unsigned b;
    if (!dir) dir = "gamedata";
    if (plat_init() != 0) return 1;
    r_begin(plat_fb());
    plat_set_fps(60);

    {   /* the unpack takes a few seconds on the device: say so, but only when there is no cache yet */
        char cp[1100];
        FILE* f;
        snprintf(cp, sizeof cp, "%s/cache/psycho.pak", dir);
        f = fopen(cp, "rb");
        if (f) fclose(f);
        else { r_message("PSYCHOSOMNIUM\n\nPreparing the game data...\n(first start only, a moment)"); plat_present(); }
    }
    err[0] = 0;
    if (assets_load(dir, err, sizeof err) != 0) {
        char msg[1024];
        snprintf(msg, sizeof msg, "PSYCHOSOMNIUM\n\n%s\n\nGame folder:\n%s\n\nPress START to exit.", err, dir);
        fprintf(stderr, "psychosomnium: %s\n", err);
        r_message(msg);
        if (plat_headless()) plat_quit(2);
        wait_start();
        plat_quit(2);
    }
    snd_init();
    gm_init();
    {   /* test setup, mirrored by tools/wine/patch_exe.py for the original:
         * PSY_TESTROOM=<n> moves room n to right after the splash screen; PSY_SPAWNS="obj,x,y;obj,x,y" appends instances to it
         * (ids 200000+); PSY_SAVE="room,type" sets global.saveroom / global.type. */
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
    }
    if (getenv("PSY_SETVARS")) room_code_hook = apply_setvars;
    settings_load(dir);
    if (getenv("PSY_DUMPTEXT")) { int i; for (i = 0; i < A.ntext; i++) fprintf(stderr, "T %d %d %d %d: %s\n", A.text[i].obj, A.text[i].ev, A.text[i].sub, A.text[i].k, game_text(A.text[i].obj, A.text[i].ev, A.text[i].sub, A.text[i].k)); }   /* test hook */
    if (getenv("PSY_DUMPFONT")) dump_fonts(getenv("PSY_DUMPFONT"));
    if (getenv("PSY_MASKS")) print_masks();
    if (getenv("PSY_MASKDIFF")) mask_diff();
    { const char* fp = getenv("PSY_FLT"); double a, c, t; if (fp && sscanf(fp, "%lf,%lf,%lf", &a, &c, &t) == 3) game_set_flight_params(a, c, t); }   /* test hook */
    if (getenv("PSY_FONTSHEET")) {                         /* test hook: the repaired game font, alphabet and digits, at the dialogue size */
        static const char* rows[] = { "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "0123456789 !?.,:;'\"-*()", "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG", "PACK MY BOX WITH FIVE DOZEN LIQUOR JUGS. 1234567890", NULL };
        int i;
        r_target(0); r_clear(gm_rgb(250, 246, 234));
        for (i = 0; rows[i]; i++) r_text(FONT_FIXED, 20, 20 + i * 40, rows[i], 2, 2, gm_rgb(38, 56, 70), FA_LEFT, FA_TOP);
        plat_present(); plat_present(); plat_present();
        plat_quit(0);
    }
    if (!plat_headless() || getenv("PSY_MENU")) {          /* the launcher: Play Game / Settings / Exit Game (tests skip it unless PSY_MENU is set) */
        if (!menu_run()) plat_quit(0);
    }
    gm_start();
    gm_draw_now();
    plat_present();

    {   /* PSY_TRACE=<n>: print the room and the first player every n frames (tests) */
        const char* tr = getenv("PSY_TRACE");
        trace_every = tr ? atoi(tr) : 0;
    }
    for (;;) {
        static int paused, start_prev;
        int st;
        b = beebot(plat_buttons());
        if ((b & B_SELECT) && (b & B_START)) plat_quit(0);
        st = (b & B_START) != 0;
        if (st && !start_prev && G.room == END_ROOM) plat_quit(0);        /* the end screen: START leaves the game (the original closes its window here) */
        if (st && !start_prev && G.room != room_first()) {
            paused = !paused;
            music_pause(paused);
            if (paused) draw_pause();
        }
        start_prev = st;
        if (paused) { plat_present(); continue; }
        {
            int draw = !plat_behind();                  /* behind schedule: keep the game's speed, drop this picture */
            double w0 = plat_now_ms();
            gm_frame(map_keys(b), draw);
            if (G.pending) { gm_load_pending(); list_objects(); gm_draw_now(); draw = 1; }
            if (G.room == END_ROOM && draw) { const char* h = ui(L_END_EXIT); r_target(0); r_ctext((SCR_W - r_ctext_width(h)) / 2, 430, h, 99, gm_rgb(150, 230, 150)); }
            plat_perf_work(plat_now_ms() - w0, gm_prof);
            gm_prof[1] = gm_prof[2] = gm_prof[3] = 0;
            if (!draw) { plat_skip_frame(); continue; }
        }
        if (getenv("PSY_TRACEB") && plat_frame() % (unsigned)trace_every == 0) {
            Inst* bb = instance_first(O_BUNNY);
            if (bb) fprintf(stderr, "b%u y=%.2f vs=%.2f idx=%.1f grav=%.1f\n", plat_frame(), bb->y, bb->vspeed, bb->image_index, bb->gravity);
        }
        if (trace_every && plat_frame() % (unsigned)trace_every == 0) {
            Inst* p = instance_first(O_PLAYER);
            fprintf(stderr, "f%u room %d inst %d player %s x=%.2f y=%.2f vs=%.2f hs=%.3g msg=%d spr=%d bunnies=%d fr=%.2f sp=%.3g idx=%.2f\n", plat_frame(), G.room, nL,
                    p ? "yes" : "no", p ? p->x : 0, p ? p->y : 0, p ? p->vspeed : 0, p ? p->hspeed : 0, instance_exists(O_MESSAGER), p ? p->sprite : -1, instance_number(O_BUNNY), p ? p->friction : 0, p ? p->speed : 0, p ? p->image_index : 0);
        }
        plat_present();
    }
}
