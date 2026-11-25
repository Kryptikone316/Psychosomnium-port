/* Game Maker 7 runtime core: see gm.h. Order of work per frame (same as the original runner):
 * alarms -> keyboard (held) -> key press -> key release -> step -> motion (friction, gravity, speeds)
 * -> outside-room -> collisions -> advance sprite animation (+ animation-end) -> remove deleted -> draw. */
#include "gm.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

RoomCtl G;
Inst* L[MAX_INST];
int nL;

#define POOL 4096
static Inst pool[POOL];
static unsigned char used[POOL];
static int pool_hint;
static Inst dummy_inst;
static int last_id;

/* sprite collision masks: per sprite, the bounding box of the union mask (for the pixel test) */
static struct { int l, r, t, b; } mbox[N_SPRITES];

/* ------------------------------------------------------------------------------------ GML numerics */
#define EPS 1e-13
double gm_round(double v) {
    double f = floor(v), d = v - f;
    if (d < 0.5) return f;
    if (d > 0.5) return f + 1.0;
    return f + (double)((long long)f & 1);
}
int gm_eq(double a, double b) { return fabs(a - b) < EPS; }
int gm_lt(double a, double b) { return (a - b) <= -EPS; }
int gm_le(double a, double b) { return (a - b) < EPS; }
int gm_gt(double a, double b) { return (a - b) >= EPS; }
int gm_ge(double a, double b) { return (a - b) > -EPS; }

static uint32_t rng_state = 0x2545f491u;
double gm_random(double n) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng_state = x;
    return n * ((double)(x >> 8) / 16777216.0);
}
uint32_t gm_rgb(int r, int g, int b) { return (uint32_t)(r & 255) | (uint32_t)(g & 255) << 8 | (uint32_t)(b & 255) << 16; }

/* make_color_hsv as the original computes it (verified against all 255 hues of the credits text): degrees = h*360/255, sextant method with
 * t = v*(1-s*(1-f)), channels truncated (not rounded). */
uint32_t gm_hsv(double h, double s, double v) {
    double hd = h * 360.0 / 255.0, hp = hd / 60.0, f, p, q, t, r, g, b;
    int i;
    s /= 255.0; v /= 255.0;
    i = (int)floor(hp); f = hp - i;
    p = v * (1 - s); q = v * (1 - s * f); t = v * (1 - s * (1 - f));
    switch (((i % 6) + 6) % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    return gm_rgb((int)(r * 255.0), (int)(g * 255.0), (int)(b * 255.0));
}

static void rgb2hsv(uint32_t c, int* H, int* S, int* V) {
    double r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255;
    double mn = fmin(r, fmin(g, b)), mx = fmax(r, fmax(g, b)), diff = mx - mn, h;
    *V = (int)gm_round(mx);
    if (mx == mn) { *S = 0; *H = 0; return; }
    *S = (int)gm_round(diff / mx * 255.0);
    if (mx == g) h = 60.0 * ((b - r) / diff) + 120.0;
    else if (mx == b) h = 60.0 * ((r - g) / diff) + 240.0;
    else h = 60.0 * ((g - b) / diff) + 360.0;
    h = fmod(h, 360.0);
    *H = (int)gm_round(h / 360.0 * 255.0);
}
int gm_color_hue(uint32_t c) { int h, s, v; rgb2hsv(c, &h, &s, &v); return h; }
int gm_color_sat(uint32_t c) { int h, s, v; rgb2hsv(c, &h, &s, &v); return s; }
int gm_color_val(uint32_t c) { int h, s, v; rgb2hsv(c, &h, &s, &v); return v; }

double point_distance(double x1, double y1, double x2, double y2) { double dx = x2 - x1, dy = y2 - y1; return sqrt(dx * dx + dy * dy); }
double point_direction(double x1, double y1, double x2, double y2) {
    double d = atan2(y1 - y2, x2 - x1) * (180.0 / M_PI);
    d = fmod(d, 360.0); if (d < 0) d += 360.0;
    return d;
}
double lengthdir_x(double l, double d) { return cos(d * (M_PI / 180.0)) * l; }

/* ------------------------------------------------------------------------- speed / direction pairs */
static void update_speed_direction(Inst* i) {
    double d = atan2(-i->vspeed, i->hspeed) * (180.0 / M_PI);
    d = fmod(d, 360.0); if (d < 0) d += 360.0;
    i->direction = fabs(d - gm_round(d)) < 0.0001 ? gm_round(d) : d;
    i->speed = sqrt(i->hspeed * i->hspeed + i->vspeed * i->vspeed);
}
static void update_hvspeed(Inst* i) {
    double h = cos(i->direction * (M_PI / 180.0)) * i->speed;
    double v = -sin(i->direction * (M_PI / 180.0)) * i->speed;
    double rh = gm_round(h), rv = gm_round(v);
    i->hspeed = fabs(rh - h) < 0.0001 ? rh : h;      /* the original's 0.0001 fudge factor */
    i->vspeed = fabs(rv - v) < 0.0001 ? rv : v;
}
void set_hspeed(Inst* i, double v) { if (i->hspeed != v) { i->hspeed = v; update_speed_direction(i); } }
void set_vspeed(Inst* i, double v) { if (i->vspeed != v) { i->vspeed = v; update_speed_direction(i); } }
void set_hvspeed(Inst* i, double h, double v) { if (i->hspeed != h || i->vspeed != v) { i->hspeed = h; i->vspeed = v; update_speed_direction(i); } }
void set_speed(Inst* i, double v) { i->speed = v; update_hvspeed(i); }
void set_direction(Inst* i, double v) {
    v = fmod(v, 360.0); if (v < 0) v += 360.0;
    i->direction = v; update_hvspeed(i);
}

/* ------------------------------------------------------------------------------------ the object table */
static int parent_of(int o) {
    switch (o) {
        case O_OBJECT0: return O_BLOCK;
        case O_P1: case O_P2: case O_P3: case O_P4: case O_P5: case O_CASTLE: return O_PLAYER;
        case O_SPIKES: case O_SPIKES2: case O_SPIKE: case O_BUNNY: return O_DANGEROUS;
        default: return -1;
    }
}
int obj_is(int obj, int target) {
    while (obj >= 0) { if (obj == target) return 1; obj = parent_of(obj); }
    return 0;
}

typedef struct { int sprite, mask, depth, solid, persistent; } ObjDef;
static void obj_def(int o, ObjDef* d) {
    d->sprite = -1; d->mask = -1; d->depth = 0; d->solid = 0; d->persistent = 0;
    switch (o) {
        case O_OBJECT0: d->sprite = S_SPRITE2; d->mask = S_SPRITE3; d->depth = 100; d->solid = 1; break;
        case O_P1: d->sprite = S_P1; d->mask = S_SPRITE4; d->persistent = 1; break;
        case O_SPIKES: d->sprite = S_SPIKES; break;
        case O_P2: d->sprite = S_P2; d->mask = S_SPRITE4; break;
        case O_DEATH: d->sprite = S_DEATH; break;
        case O_SPIKES2: d->sprite = S_SPIKES2; break;
        case O_BUTTERFLY: d->sprite = S_BUTTERFLY; break;
        case O_MESSAGER: d->sprite = S_BUBBLE; break;
        case O_SHIRLEY: d->sprite = S_SPRITE10; break;
        case O_SPIKE: d->sprite = S_SPIKE; break;
        case O_PUMP: d->sprite = S_PUMP; d->depth = -1; break;
        case O_SAVER: d->sprite = S_SAVER; break;
        case O_SHIFT: d->sprite = S_SPRITE16; d->depth = 100; break;
        case O_OBJCOLOR1: d->sprite = S_SPRITE17; d->depth = 100; break;
        case O_BGTHING: d->sprite = S_SPRITE18; break;
        case O_BGCONTROL: d->depth = 100; d->persistent = 1; break;
        case O_P3: d->sprite = S_P3; d->mask = S_SPRITE20; break;
        case O_BUNNY: d->sprite = S_BUNNY; d->mask = S_SPRITE4; break;
        case O_SHIFT2: d->sprite = S_SPRITE23; d->depth = 100; break;
        case O_OBJCOLOR2: d->sprite = S_SPRITE24; d->depth = 100; break;
        case O_P4: d->sprite = S_SPRITE22; d->mask = S_SPRITE4; break;
        case O_CASTLE: d->sprite = S_CASTLE; break;
        case O_P5: d->sprite = S_SPRITE27; d->mask = S_SPRITE4; break;
        case O_OBJECT28: d->sprite = S_SPLASH; break;
        default: break;
    }
}

int has_draw_event(int o) {
    switch (o) {
        case O_OBJECT0: case O_P1: case O_SPIKES: case O_P2: case O_DEATH: case O_SPIKES2: case O_BUTTERFLY: case O_MESSAGER:
        case O_SHIRLEY: case O_SPIKE: case O_PUMP: case O_SAVER: case O_P3: case O_BUNNY: case O_P4: case O_CASTLE: case O_P5:
        case O_OBJECT27: case O_OBJECT28: return 1;
        default: return 0;
    }
}
static int is_static_obj(int o) {
    switch (o) { case O_OBJECT0: case O_SPIKES: case O_SPIKES2: case O_SHIFT: case O_OBJCOLOR1: case O_SHIFT2: case O_OBJCOLOR2: return 1; default: return 0; }
}

int sprite_frames(int spr) { return (spr >= 0 && spr < N_SPRITES) ? A.spr[spr].nframes : 0; }

/* ----------------------------------------------------------------------------------- instance store */
static Inst* alloc_inst(void) {
    int k;
    for (k = 0; k < POOL; k++) {
        int i = (pool_hint + k) % POOL;
        if (!used[i]) { used[i] = 1; pool_hint = i + 1; memset(&pool[i], 0, sizeof(Inst)); return &pool[i]; }
    }
    return NULL;
}
static void free_inst(Inst* i) {
    int k;
    if (i < pool || i >= pool + POOL) return;
    for (k = 0; k < 8; k++) free(i->message[k]);
    used[i - pool] = 0;
}

static void init_inst(Inst* i, int id, int obj, double x, double y) {
    ObjDef d;
    int k;
    obj_def(obj, &d);
    i->coyote = 99;
    i->id = id; i->obj = obj; i->alive = 1; i->x = i->xprev = x; i->y = i->yprev = y;
    i->visible = 1; i->depth = d.depth; i->solid = d.solid; i->persistent = d.persistent;
    i->sprite = d.sprite; i->mask = d.mask;
    i->gravdir = 270.0; i->image_speed = 1.0; i->xscale = i->yscale = 1.0; i->alpha = 1.0; i->blend = 0xffffff;
    for (k = 0; k < 12; k++) i->alarm[k] = -1.0;
}

static int scene_blocked(void) { return G.pending; }   /* events are not run while a room change is pending */

Inst* inst_create(double x, double y, int obj) {
    Inst* i = alloc_inst();
    if (!i || nL >= MAX_INST) { memset(&dummy_inst, 0, sizeof dummy_inst); return &dummy_inst; }
    init_inst(i, ++last_id, obj, x, y);
    L[nL++] = i;
    if (!scene_blocked()) ev_create(i);
    return i;
}

void inst_destroy(Inst* i) {
    if (!i->alive) return;
    if (!scene_blocked()) ev_destroy(i);
    i->alive = 0;
}

int instance_exists(int obj) {
    int k;
    for (k = 0; k < nL; k++) if (L[k]->alive && obj_is(L[k]->obj, obj)) return 1;
    return 0;
}
int instance_number(int obj) {
    int k, n = 0;
    for (k = 0; k < nL; k++) if (L[k]->alive && obj_is(L[k]->obj, obj)) n++;
    return n;
}
Inst* instance_first(int obj) {
    int k;
    for (k = 0; k < nL; k++) if (L[k]->alive && obj_is(L[k]->obj, obj)) return L[k];
    return NULL;
}

/* ----------------------------------------------------------------------------------------- collision */
typedef struct { int l, r, t, b, ok; } BBox;

static const Sprite* mask_sprite(const Inst* i) {
    int s = i->mask >= 0 ? i->mask : i->sprite;
    return (s >= 0 && s < N_SPRITES && A.spr[s].valid && A.spr[s].nframes > 0) ? &A.spr[s] : NULL;
}

static BBox get_bbox(const Inst* i) {
    BBox b;
    const Sprite* sp = mask_sprite(i);
    double x0, y0, x1, y1, xs = i->xscale, ys = i->yscale;
    if (!sp) { b.l = b.r = b.t = b.b = -100000; b.ok = 0; return b; }
    x0 = (i->x - sp->ox * xs) + sp->bl * xs;
    y0 = (i->y - sp->oy * ys) + sp->bt * ys;
    x1 = x0 + (sp->br + 1 - sp->bl) * xs - 1.0;
    y1 = y0 + (sp->bb + 1 - sp->bt) * ys - 1.0;
    if (xs <= 0) { double t = x0; x0 = x1; x1 = t; }
    if (ys <= 0) { double t = y0; y0 = y1; y1 = t; }
    b.l = (int)gm_round(fmin(x0, x1)); b.r = (int)gm_round(fmax(x0, x1));
    b.t = (int)gm_round(fmin(y0, y1)); b.b = (int)gm_round(fmax(y0, y1));
    b.ok = 1;
    return b;
}

static int mask_pixel(const Sprite* sp, int spr, int px, int py, int ix, int iy, double xs, double ys) {
    int x = sp->ox + (int)floor((double)(px - ix) / xs);
    int y = sp->oy + (int)floor((double)(py - iy) / ys);
    if (x < mbox[spr].l || y < mbox[spr].t || x > mbox[spr].r || y > mbox[spr].b) return 0;
    return sp->mask[(size_t)y * sp->w + x] != 0;
}

int check_collision(Inst* a, Inst* b) {
    BBox ba, bb;
    const Sprite *sa, *sb;
    int sia, sib, x1, y1, x2, y2, t, bt, l, r, px, py;
    if (a == b) return 0;
    ba = get_bbox(a); bb = get_bbox(b);
    if (ba.r < bb.l || bb.r < ba.l || ba.b < bb.t || bb.b < ba.t) return 0;
    sa = mask_sprite(a); sb = mask_sprite(b);
    if (!sa || !sb) return 0;
    sia = (a->mask >= 0 ? a->mask : a->sprite); sib = (b->mask >= 0 ? b->mask : b->sprite);
    x1 = (int)gm_round(a->x); y1 = (int)gm_round(a->y);
    x2 = (int)gm_round(b->x); y2 = (int)gm_round(b->y);
    t = ba.t > bb.t ? ba.t : bb.t;
    bt = ba.b < bb.b ? ba.b : bb.b;
    l = ba.l > bb.l ? ba.l : bb.l;
    r = ba.r < bb.r ? ba.r : bb.r;
    for (py = t; py <= bt; py++)
        for (px = l; px <= r; px++)
            if (mask_pixel(sa, sia, px, py, x1, y1, a->xscale, a->yscale) && mask_pixel(sb, sib, px, py, x2, y2, b->xscale, b->yscale)) {
#ifdef PSY_TEST
                if (getenv("PSY_COLLDBG2")) fprintf(stderr, "coll a=obj%d mask%d bbox x%d..%d y%d..%d  b=obj%d mask%d bbox x%d..%d y%d..%d at px,py=%d,%d\n", a->obj, sia, ba.l, ba.r, ba.t, ba.b, b->obj, sib, bb.l, bb.r, bb.t, bb.b, px, py);
#endif
                return 1;
            }
    return 0;
}

static Inst* collide_solid(Inst* s) {
    int k;
    for (k = 0; k < nL; k++) {
        Inst* t = L[k];
        if (t->alive && t->solid && t != s && check_collision(s, t)) return t;
    }
    return NULL;
}

int place_free(Inst* s, double x, double y) {
    double ox = s->x, oy = s->y;
    int free_;
    s->x = x; s->y = y;
    free_ = collide_solid(s) == NULL;
    s->x = ox; s->y = oy;
    return free_;
}

void move_contact_solid(Inst* s, double dir, double maxd) {
    int n = maxd > 0 ? (int)gm_round(maxd) : 1000, k;
    double sx = cos(dir * (M_PI / 180.0)), sy = -sin(dir * (M_PI / 180.0));
    if (collide_solid(s)) return;
    for (k = 0; k < n; k++) {
        double ox = s->x, oy = s->y;
        s->x += sx; s->y += sy;
        if (collide_solid(s)) { s->x = ox; s->y = oy; break; }
    }
}

/* ---------------------------------------------------------------------------------------------- rooms */
void room_goto(int r) { G.pending = 1; G.target = r; }
static int order_index(int r) { int i; for (i = 0; i < N_ROOMS; i++) if (A.order[i] == r) return i; return -1; }
void room_goto_next(void) { int i = order_index(G.room); if (i >= 0 && i + 1 < N_ROOMS) room_goto(A.order[i + 1]); }
void room_goto_previous(void) { int i = order_index(G.room); if (i > 0) room_goto(A.order[i - 1]); }
int room_first(void) { return A.order[0]; }
int room_last(void) { return A.order[N_ROOMS - 1]; }

typedef struct { int valid, n; Inst* list[MAX_INST]; uint32_t bg; } Stored;
static Stored* stored[N_ROOMS];

static unsigned held_mask, prev_mask;
int key_held(int vk) {
    switch (vk) {
        case VK_LEFT: return (held_mask & 1) != 0;
        case VK_UP: return (held_mask & 2) != 0;
        case VK_RIGHT: return (held_mask & 4) != 0;
        case VK_Z: return (held_mask & 8) != 0;
        case VK_ENTER: return (held_mask & 16) != 0;
        case VK_DOWN: return (held_mask & 32) != 0;
        default: return 0;
    }
}

static void compact(void) {
    int i, o = 0;
    for (i = 0; i < nL; i++) {
        if (L[i]->alive) L[o++] = L[i];
        else free_inst(L[i]);
    }
    nL = o;
}

static void gm_draw(void);

void (*room_code_hook)(void);

void gm_load_pending(void) {
    int r = G.target, i;
    static Inst* persist[MAX_INST];
    int np = 0, is_stored = 0;
    Room* rm;
    if (r < 0 || r >= N_ROOMS || !A.room[r].valid) { G.pending = 0; return; }
    rm = &A.room[r];
    G.pending = 0;                                   /* (room end events: none in this game) */

    /* take the persistent instances out; keep the old room's rest if that room is persistent */
    compact();
    {
        int o = 0;
        for (i = 0; i < nL; i++) {
            if (L[i]->persistent) persist[np++] = L[i];
            else L[o++] = L[i];
        }
        nL = o;
    }
    if (G.room >= 0 && A.room[G.room].persistent) {
        Stored* st = stored[G.room];
        if (!st) st = stored[G.room] = (Stored*)calloc(1, sizeof(Stored));
        st->valid = 1; st->n = nL; st->bg = G.bg;
        memcpy(st->list, L, sizeof(Inst*) * (size_t)nL);
        nL = 0;
    } else {
        for (i = 0; i < nL; i++) free_inst(L[i]);
        nL = 0;
    }

    G.room = r; G.w = rm->w; G.h = rm->h; G.bg = rm->bg;
    {
        Stored* st = stored[r];
        int first_new = 0, n_created = 0;
        if (rm->persistent && st && st->valid) {
            is_stored = 1;
            nL = st->n; memcpy(L, st->list, sizeof(Inst*) * (size_t)nL);
            G.bg = st->bg;
            st->valid = 0;
        } else {
            for (i = 0; i < rm->n; i++) {
                RInst* ri = &rm->inst[i];
                Inst* n = alloc_inst();
                if (!n || nL >= MAX_INST) break;
                init_inst(n, ri->id, ri->obj, ri->x, ri->y);
                n->xscale = ri->xs; n->yscale = ri->ys;
                L[nL++] = n;
            }
            first_new = 0;
            n_created = nL;
        }
        /* persistent instances come back after the room's own; a room instance with the same id is dropped */
        for (i = 0; i < np; i++) {
            int k;
            for (k = 0; k < nL; k++) if (L[k]->alive && L[k]->id == persist[i]->id) { L[k]->alive = 0; }
            if (nL < MAX_INST) L[nL++] = persist[i];
        }
        if (!is_stored) {
            /* only the room's own instances run Create; the ones carried over from the previous room do not */
            for (i = first_new; i < n_created; i++) if (L[i]->alive) ev_create(L[i]);
        }
    }
    if (room_code_hook) room_code_hook();             /* room creation code */
    prev_mask = held_mask;                            /* no key press edges from before the change */
    /* room start for every instance, in drawing order */
    {
        static Inst* ord[MAX_INST];
        int n = 0, k, j;
        for (k = 0; k < nL; k++) if (L[k]->alive) ord[n++] = L[k];
        for (k = 1; k < n; k++) { Inst* v = ord[k]; for (j = k - 1; j >= 0 && ord[j]->depth < v->depth; j--) ord[j + 1] = ord[j]; ord[j + 1] = v; }
        for (k = 0; k < n; k++) if (ord[k]->alive && !G.pending) ev_other(ord[k], 4);
    }
}

/* ---------------------------------------------------------------------------------------- frame loop */
static const int H_ALARM0[] = { O_P1, O_P2, O_PUMP, O_P3, O_P4, O_OBJECT28, -1 };
static const int H_STEP[] = { O_P1, O_P2, O_DEATH, O_BUTTERFLY, O_MESSAGER, O_SHIRLEY, O_SPIKE, O_P3, O_BUNNY, O_P4, O_CASTLE, O_P5, -1 };
static const int H_COLL[] = { O_P1, O_P2, O_SPIKE, O_SAVER, O_P3, O_BUNNY, O_P4, O_P5, -1 };
static const int H_KEY38_HELD[] = { O_P1, O_P2, O_P3, O_BUNNY, O_P5, -1 };
static const int H_KEY39[] = { O_P1, O_P2, O_P3, O_P5, -1 };
static const int H_KEY37[] = { O_P1, O_P2, O_P3, O_P5, -1 };
static const int H_KEY90[] = { O_MESSAGER, O_P3, -1 };
static const int H_KEY122[] = { O_P4, -1 };
static const int H_KEY38_PRESS[] = { O_P4, -1 };
static const int H_KEY13[] = { O_OBJECT28, -1 };
static const int H_KEYREL38[] = { O_P1, O_P2, O_P3, O_P5, -1 };

/* collision targets per object, in the order the original lists them */
static int coll_targets(int obj, int* t) {
    switch (obj) {
        case O_P1: case O_P2: case O_P3: case O_P5: t[0] = O_DANGEROUS; t[1] = O_BLOCK; return 2;
        case O_SPIKE: case O_BUNNY: t[0] = O_BLOCK; return 1;
        case O_SAVER: t[0] = O_PLAYER; return 1;
        case O_P4: t[0] = O_DANGEROUS; return 1;
        default: return 0;
    }
}

static void dispatch_key(int kind, int vk, const int* holders) {
    int h, k;
    for (h = 0; holders[h] >= 0; h++)
        for (k = 0; k < nL; k++) {
            Inst* s = L[k];
            if (s->alive && s->obj == holders[h] && !G.pending) ev_key(s, kind, vk);
        }
}

/* The original jumps every step the Up key is down and the character stands on something, so a button held a little too long (or
 * one that chatters) rebounds off the floor at once. Press mode: the PLAYER characters need a fresh press (it stays valid for a few
 * frames, so one pressed just before landing still counts). The bunnies keep the original rule (they hop whenever they are on the
 * ground and Up is down), which the bunny room's timing depends on. */
static int jump_buffer = 8;
static int jump_press_mode = 1, up_arm, up_used;
void gm_set_jump_buffer(int frames) { jump_buffer = frames < 1 ? 1 : (frames > 20 ? 20 : frames); }
void gm_set_jump_mode(int m) { jump_press_mode = m; }
void gm_jump_done(void) { up_used = 1; }
int gm_jump_allowed(void) { return !jump_press_mode || up_arm > 0; }

/* Ledge forgiveness: the pad and the display add some delay, so a jump pressed the moment the character leaves an edge arrives a few frames
 * late. For `ledge_frames` frames after walking off (and only while not yet moving up fast) the character may still take off. */
static int ledge_frames = 0;
void gm_set_ledge(int frames) { ledge_frames = frames < 0 ? 0 : (frames > 12 ? 12 : frames); }
int gm_can_jump(Inst* s) {
    if (!place_free(s, s->x, s->y + 1)) return 1;
    return ledge_frames > 0 && s->coyote <= ledge_frames && s->vspeed < 2.0;
}
static void update_grounded(void) {
    int k;
    for (k = 0; k < nL; k++) {
        Inst* s = L[k];
        if (!s->alive || !(s->obj == O_P1 || s->obj == O_P2 || s->obj == O_P3 || s->obj == O_P5)) continue;
        if (!place_free(s, s->x, s->y + 1)) s->coyote = 0; else if (s->coyote < 99) s->coyote++;
    }
}

static void run_keys(unsigned pressed, unsigned released) {
    update_grounded();
    if (pressed & 2) up_arm = jump_buffer;
    if (!(held_mask & 2)) up_arm = 0;
    up_used = 0;
    if (held_mask & 2) dispatch_key(0, VK_UP, H_KEY38_HELD);
    if (up_used) up_arm = 0;                          /* a character took off: the press is used up (after everyone had their turn) */
    else if (up_arm > 0) up_arm--;
    if (G.pending) return;
    /* key press events, in the order the keys were first registered by the game's objects: 39, 37, 90, 122, 38, 13 */
    if (pressed & 4) dispatch_key(1, VK_RIGHT, H_KEY39);
    if (pressed & 1) dispatch_key(1, VK_LEFT, H_KEY37);
    if (pressed & 8) dispatch_key(1, VK_Z, H_KEY90);
    (void)H_KEY122;
    if (pressed & 2) dispatch_key(1, VK_UP, H_KEY38_PRESS);
    if (pressed & 16) dispatch_key(1, VK_ENTER, H_KEY13);
    if (G.pending) return;
    if (released & 2) dispatch_key(2, VK_UP, H_KEYREL38);
}

static void process_speeds(void) {
    int k;
    for (k = 0; k < nL; k++) {
        Inst* s = L[k];
        if (!s->alive) continue;
        if (s->friction != 0) {
            double sp = s->speed;
            if (sp >= 0) { if (s->friction > sp) set_speed(s, 0); else if (sp != 0) set_speed(s, sp - s->friction); }
            else { if (s->friction > -sp) set_speed(s, 0); else if (sp != 0) set_speed(s, sp + s->friction); }
        }
        if (s->gravity != 0) {
            double gd = s->gravdir * (M_PI / 180.0);
            set_hvspeed(s, s->hspeed + cos(gd) * s->gravity, s->vspeed - sin(gd) * s->gravity);
        }
    }
}
static void apply_speeds(Inst* s) {
    if (s->hspeed != 0 || s->vspeed != 0) { s->x += s->hspeed; s->y += s->vspeed; }
}

static void run_collisions(void) {
    int h, k;
    for (h = 0; H_COLL[h] >= 0; h++) {
        int obj = H_COLL[h], tg[4], nt = coll_targets(obj, tg), ti;
        for (k = 0; k < nL; k++) {
            Inst* a = L[k];
            int j, stop = 0;
            if (!a->alive || a->obj != obj) continue;
            for (ti = 0; ti < nt && !stop; ti++)
                for (j = 0; j < nL; j++) {
                    Inst* b = L[j];
                    if (!b->alive || !obj_is(b->obj, tg[ti]) || !check_collision(a, b)) continue;
                    if (a->solid || b->solid) { a->x = a->xprev; a->y = a->yprev; b->x = b->xprev; b->y = b->yprev; }
                    if (!G.pending) ev_collision(a, b, tg[ti]);
                    if (a->solid || b->solid) {
                        apply_speeds(a); apply_speeds(b);
                        if (a->alive && b->alive && check_collision(a, b)) { a->x = a->xprev; a->y = a->yprev; b->x = b->xprev; b->y = b->yprev; }
                    }
                    if (!a->alive) { stop = 1; break; }
                }
        }
    }
}

static int outside_room(Inst* s) {
    BBox b = get_bbox(s);
    if (!b.ok) return s->x < 0 || s->y < 0 || s->x > G.w || s->y > G.h;
    return b.r < 0 || b.b < 0 || b.l > G.w || b.t > G.h;
}

static uint32_t cache_sig;
static int cache_has;

double gm_prof[4];                                 /* ms spent since the last read: logic, static-layer rebuild, cache copy, sprites+text */
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000.0 + t.tv_nsec / 1e6; }

static void gm_draw(void) {
    static Inst* dyn[MAX_INST];
    static Inst* sta[MAX_INST];
    int nd = 0, ns = 0, k, j;
    double t_a, t_b;
    uint32_t sig = 2166136261u ^ G.bg;
    for (k = 0; k < nL; k++) {
        Inst* s = L[k];
        if (!s->alive || !s->visible) continue;
        if (is_static_obj(s->obj)) {
            sta[ns++] = s;
            sig = (sig ^ (uint32_t)s->id) * 16777619u;
            sig = (sig ^ (uint32_t)(int)s->x) * 16777619u;
            sig = (sig ^ (uint32_t)(int)s->y) * 16777619u;
            sig = (sig ^ (uint32_t)(int)(s->image_index * 4)) * 16777619u;
            sig = (sig ^ s->blend) * 16777619u;
            sig = (sig ^ (uint32_t)(int)(s->yscale * 8) ^ (uint32_t)(int)(s->xscale * 8) << 8) * 16777619u;
        } else dyn[nd++] = s;
    }
    sig = (sig ^ (uint32_t)ns) * 16777619u;
    for (k = 1; k < ns; k++) { Inst* v = sta[k]; for (j = k - 1; j >= 0 && sta[j]->depth < v->depth; j--) sta[j + 1] = sta[j]; sta[j + 1] = v; }
    for (k = 1; k < nd; k++) { Inst* v = dyn[k]; for (j = k - 1; j >= 0 && dyn[j]->depth < v->depth; j--) dyn[j + 1] = dyn[j]; dyn[j + 1] = v; }
    t_a = now_ms();
    if (!cache_has || sig != cache_sig) {
#ifdef PSY_TEST
        if (getenv("PSY_CACHEDBG")) fprintf(stderr, "static layer rebuilt (%d static, %d dynamic)\n", ns, nd);
#endif
        r_target(1);
        r_clear(G.bg);
        for (k = 0; k < ns; k++) { if (has_draw_event(sta[k]->obj)) ev_draw(sta[k]); else r_sprite(sta[k]->sprite, sta[k]->image_index, sta[k]->x, sta[k]->y, sta[k]->xscale, sta[k]->yscale, sta[k]->blend, sta[k]->alpha); }
        r_target(0);
        cache_sig = sig; cache_has = 1;
    }
    t_b = now_ms(); gm_prof[1] += t_b - t_a;
    r_blit_cache();
    t_a = now_ms(); gm_prof[2] += t_a - t_b;
    for (k = 0; k < nd; k++) {
        Inst* s = dyn[k];
        if (!s->alive) continue;
        if (has_draw_event(s->obj)) ev_draw(s);
        else r_sprite(s->sprite, s->image_index, s->x, s->y, s->xscale, s->yscale, s->blend, s->alpha);
    }
    gm_prof[3] += now_ms() - t_a;
}

void gm_draw_now(void) { gm_draw(); }

void gm_frame(unsigned keys, int draw) {
    int k, h;
    unsigned pressed, released;
    held_mask = keys;
    pressed = keys & ~prev_mask; released = ~keys & prev_mask;
    prev_mask = keys;

    for (k = 0; k < nL; k++) if (L[k]->alive) { L[k]->xprev = L[k]->x; L[k]->yprev = L[k]->y; }

    /* advance sprite animation at the START of the frame, before alarms (checked against the original running under Wine:
     * the pump's alarm sets image_speed and its animation starts one frame after the player is created) */
    for (k = 0; k < nL; k++) {
        Inst* s = L[k];
        int n;
        if (!s->alive) continue;
        n = sprite_frames(s->sprite);
        if (n <= 0) continue;
        s->image_index += s->image_speed;
        if (s->image_index >= n) {
            s->image_index -= n;
            if (!G.pending) ev_other(s, 7);
        }
    }
    if (G.pending) return;

    /* alarms */
    for (h = 0; H_ALARM0[h] >= 0; h++)
        for (k = 0; k < nL; k++) {
            Inst* s = L[k];
            if (!s->alive || s->obj != H_ALARM0[h] || G.pending) continue;
            if (s->alarm[0] >= 0) { s->alarm[0] -= 1; if (s->alarm[0] == 0) ev_alarm(s, 0); }
        }
    if (G.pending) return;
    run_keys(pressed, released);
    if (G.pending) return;
    for (h = 0; H_STEP[h] >= 0; h++)
        for (k = 0; k < nL; k++) {
            Inst* s = L[k];
            if (s->alive && s->obj == H_STEP[h] && !G.pending) ev_step(s);
        }
    if (G.pending) return;

    process_speeds();
    for (k = 0; k < nL; k++) if (L[k]->alive) apply_speeds(L[k]);

    for (k = 0; k < nL; k++) {                       /* outside room (death particles) */
        Inst* s = L[k];
        if (s->alive && s->obj == O_DEATH && outside_room(s) && !G.pending) ev_other(s, 0);
    }
    if (G.pending) return;
    run_collisions();
    if (G.pending) return;

    compact();
    if (draw) gm_draw();
}

/* ------------------------------------------------------------------------------------------- start */
void gm_init(void) {
    int i, x, y;
    rng_state ^= (uint32_t)time(NULL) * 2654435761u;
#ifdef PSY_TEST
    if (getenv("PSY_SEED")) rng_state = (uint32_t)atoi(getenv("PSY_SEED"));      /* test hook: reproducible random numbers (death particles) */
#endif
    if (!rng_state) rng_state = 1;
    last_id = A.last_inst_id;
    for (i = 0; i < N_SPRITES; i++) {
        const Sprite* sp = &A.spr[i];
        int l = 1 << 30, r = -1, t = 1 << 30, b = -1;
        if (!sp->valid || !sp->mask) { mbox[i].l = mbox[i].t = 1; mbox[i].r = mbox[i].b = 0; continue; }
        for (y = 0; y < sp->h; y++)
            for (x = 0; x < sp->w; x++)
                if (sp->mask[(size_t)y * sp->w + x]) { if (x < l) l = x; if (x > r) r = x; if (y < t) t = y; if (y > b) b = y; }
        mbox[i].l = l; mbox[i].r = r; mbox[i].t = t; mbox[i].b = b;
    }
    game_reset();
    G.room = -1;
}

void gm_start(void) {
    room_goto(A.order[0]);
    gm_load_pending();
}
