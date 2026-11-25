/* Psychosomnium's objects: every event is a hand port of the original game's GML, statement for statement
 * (including quirks such as unbraced `if` bodies and `exit` inside `with`). Dialogue and other strings are
 * NOT in this file: they come from the player's psycho.exe via game_text(). */
#include "gm.h"
#include "lang.h"
#include "plat.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* event numbers as used by game_text() keys (the original object file's main event index) */
enum { E_CREATE = 0, E_DESTROY = 1, E_ALARM = 2, E_STEP = 3, E_DRAW = 8 };
static int cp_count(const char* s) { int n = 0; for (; *s; s++) if (((unsigned char)*s & 0xc0) != 0x80) n++; return n; }     /* UTF-8 characters */
#define TX(obj, ev, k) game_text((obj), (ev), 0, (k))
#define PF(x, y) place_free(s, (x), (y))        /* GML place_free(): self is implicit */

/* "global." variables */
static int g_saveroom = 1, g_type = O_P1, g_spoken;
static int g_title = 0, g_song = 1, g_endsong = 2;       /* music handles = XM module indices */

/* draw state */
static int ds_font;
static uint32_t ds_color = 0;
static int ds_halign, ds_valign;
static int fog_on;
static uint32_t fog_col;

void game_set_save(int room, int type) { g_saveroom = room; g_type = type; }

void game_reset(void) { g_saveroom = 1; g_type = O_P1; g_spoken = 0; }

/* ----------------------------------------------------------------------------------------- helpers */
static int with_all(int obj, Inst** out) {
    int k, n = 0;
    for (k = 0; k < nL && n < 256; k++) if (L[k]->alive && obj_is(L[k]->obj, obj)) out[n++] = L[k];
    return n;
}

static void set_msg(Inst* m, int idx, const char* text) {
    if (idx < 0 || idx > 7) return;
    free(m->message[idx]);
    m->message[idx] = strdup(text);
}
static Inst* new_msg(double x, double y) { return inst_create(x, y, O_MESSAGER); }

static void draw_sprite_ext(int spr, double idx, double x, double y, double xs, double ys, uint32_t blend, double alpha) {
    if (fog_on) r_sprite_fog(spr, idx, x, y, xs, ys, fog_col, alpha);
    else r_sprite(spr, idx, x, y, xs, ys, blend, alpha);
}
static void fog(int on, uint32_t col) { fog_on = on; fog_col = col; }

static void draw_snap(Inst* s) {
    double rx = gm_round(s->x * 0.5) * 2, ry = gm_round(s->y * 0.5) * 2;
    fog(1, C_SHELDON);
    draw_sprite_ext(s->sprite, s->image_index, rx - 2, ry, s->xscale, s->yscale, C_SHELDON, s->alpha);
    draw_sprite_ext(s->sprite, s->image_index, rx, ry + 2, s->xscale, s->yscale, C_SHELDON, s->alpha);
    draw_sprite_ext(s->sprite, s->image_index, rx, ry - 2, s->xscale, s->yscale, C_SHELDON, s->alpha);
    draw_sprite_ext(s->sprite, s->image_index, rx + 2, ry, s->xscale, s->yscale, C_SHELDON, s->alpha);
    fog(0, 0);
    draw_sprite_ext(s->sprite, s->image_index, rx, ry, s->xscale, s->yscale, s->blend, s->alpha);
}
static void draw_snap2(Inst* s) {
    double rx = gm_round(s->x * 0.5) * 2, ry = gm_round(s->y * 0.5) * 2;
    fog(1, C_SHELDON);
    draw_sprite_ext(s->sprite, s->image_index, rx - 2, ry - 2, s->xscale, s->yscale, C_SHELDON, s->alpha);
    draw_sprite_ext(s->sprite, s->image_index, rx + 2, ry + 2, s->xscale, s->yscale, C_SHELDON, s->alpha);
    draw_sprite_ext(s->sprite, s->image_index, rx + 2, ry - 2, s->xscale, s->yscale, C_SHELDON, s->alpha);
    draw_sprite_ext(s->sprite, s->image_index, rx - 2, ry + 2, s->xscale, s->yscale, C_SHELDON, s->alpha);
    fog(0, 0);
    draw_sprite_ext(s->sprite, s->image_index, rx, ry, s->xscale, s->yscale, s->blend, s->alpha);
}

/* the player-type objects share most of their code; helpers for the repeated fragments */
static void die_here(Inst* s) { inst_destroy(s); inst_create(s->x, s->y, O_DEATH); sound_play(SND_DIE); }

/* ------------------------------------------------------------------------------------------- create */
/* Easier bunny room (default; bunnies=original in the config restores the game's own hop): higher hops (about 93 px instead of 61) with a short hang
 * around the top (about 0.25 s), so there is time to run underneath all three even with the pad's input delay. */
static int g_flight_fine = 1;
static double g_fl_acc = 0.15, g_fl_cap = 1.5, g_fl_tap = 1.0;      /* thrust per frame while Up/Down is held, speed limit, impulse of a tap */
void game_set_flight_params(double acc, double cap, double tap) { g_fl_acc = acc; g_fl_cap = cap; g_fl_tap = tap; }
void game_set_flight_fine(int on) { g_flight_fine = on; }
static int g_bunny_boost = 1;
void game_set_bunny_boost(int on) { g_bunny_boost = on; }

void ev_create(Inst* s) {
    switch (s->obj) {
        case O_OBJECT0: {
            int left, right;
            s->xscale = s->yscale = 2; s->image_speed = 0;
            left = PF(s->x - 17, s->y); right = PF(s->x + 17, s->y);
            if (left == 0 && right == 0) s->image_index = 0;
            if (left == 1 && right == 0) s->image_index = 1;
            if (left == 0 && right == 1) s->image_index = 2;
            if (left == 1 && right == 1) s->image_index = 3;
            break;
        }
        case O_P1: s->xscale = s->yscale = 2; s->image_speed = 0; s->active = 0; break;
        case O_SPIKES:
            s->yscale = 2; s->xscale = 2; s->image_speed = 0;
            if (PF(s->x + 4, s->y)) s->xscale = -2;
            break;
        case O_P2: s->xscale = -2; s->yscale = 2; s->image_speed = 0; s->active = 0; s->on = 1; break;
        case O_DEATH: s->image_speed = 1; s->on = 1; s->xscale = 2; s->yscale = 2; s->myx = s->x; s->myy = s->y; break;
        case O_SPIKES2:
            s->yscale = 2; s->xscale = 2; s->image_speed = 0;
            if (PF(s->x, s->y + 4)) { s->yscale = -2; s->y += 2; }
            break;
        case O_BUTTERFLY: s->image_speed = 0.1; s->xscale = 2; s->yscale = 2; s->on = 1; break;
        case O_MESSAGER:
            s->letter = 0; s->start = 0; s->y = 420; s->xscale = 2; s->yscale = 2; s->x = 16; s->image_speed = 0;
            s->messages = 0; s->current = 0; s->font = 0;
            break;
        case O_SHIRLEY: s->image_speed = 0.1; s->xscale = 2; s->yscale = 2; s->on = 1; break;
        case O_SPIKE: s->xscale = 2; s->yscale = 2; s->x += 2; break;
        case O_PUMP:
            s->xscale = 2; s->yscale = 2;
            if (!instance_exists(O_PLAYER)) s->alarm[0] = 60;
            if (G.room == 1) { g_saveroom = 1; g_type = O_P1; }
            s->image_speed = 0;
            break;
        case O_SAVER:
            s->xscale = 2; s->yscale = 2; s->image_speed = 0;
            if (G.room == g_saveroom) s->image_index = 1;
            break;
        case O_SHIFT: case O_SHIFT2: s->xscale = 2; s->yscale = 2; break;
        case O_P3: s->xscale = -2; s->yscale = 2; s->image_speed = 0; s->active = 0; s->on = 1; break;
        case O_BUNNY: s->image_speed = 0; s->xscale = 2; s->yscale = 2; g_spoken = 0; break;
        case O_P4: s->mydir = 0; s->xscale = -2; s->yscale = 2; s->active = 0; s->image_speed = 0; s->on = 1; break;
        case O_CASTLE: s->visible = 0; s->xscale = 2; s->yscale = 2; s->on = 1; s->reload = 20; s->played = 0; break;
        case O_P5: s->xscale = -2; s->yscale = 2; s->image_speed = 0; s->active = 0; s->on = 1; break;
        case O_OBJECT27: s->h = 0; break;
        case O_OBJECT28:
            g_title = 0; g_song = 1;
            music_load(g_title); music_load(g_song);
            music_play(g_title);
            s->image_speed = 0.05; s->xscale = 2; s->yscale = 2;
            break;
        default: break;
    }
}

/* ------------------------------------------------------------------------------------------ destroy */
void ev_destroy(Inst* s) {
    switch (s->obj) {
        case O_P1: {
            Inst* l[256]; int n = with_all(O_P2, l), i;
            for (i = 0; i < n; i++) {
                Inst* m = new_msg(l[i]->x, l[i]->y);
                l[i]->my = m;
                set_msg(m, 0, TX(O_P1, E_DESTROY, 0));
            }
            break;
        }
        case O_P2: case O_P5: {
            Inst* l[256]; int n = with_all(O_P3, l), i;
            for (i = 0; i < n; i++) {
                Inst* m = new_msg(l[i]->x, l[i]->y);
                l[i]->my = m;
                set_msg(m, 0, TX(s->obj, E_DESTROY, 0)); set_msg(m, 1, TX(s->obj, E_DESTROY, 1)); m->messages = 1;
            }
            break;
        }
        case O_DEATH:
            if (G.room == 13 && instance_number(s->obj) <= 1) { inst_create(416, 208, O_P4); room_goto(g_saveroom); return; }
            if (!instance_exists(O_PLAYER) && instance_number(s->obj) <= 1) room_goto(g_saveroom);
            if (instance_exists(O_P5)) { Inst* p5 = instance_first(O_P5); if (gm_eq(p5->active, 0) && !instance_exists(O_MESSAGER)) room_goto(g_saveroom); }
            if (G.room == 13 && instance_number(s->obj) <= 1 && instance_exists(O_P2)) room_goto(g_saveroom);
            break;
        default: break;
    }
}

/* -------------------------------------------------------------------------------------------- alarms */
void ev_alarm(Inst* s, int n) {
    (void)n;
    switch (s->obj) {
        case O_P1: case O_P2: case O_P3: case O_P4: s->active = 1; break;
        case O_PUMP: {
            Inst* m = inst_create(s->x, s->y, g_type);
            s->my = m;
            set_vspeed(m, 8); m->alarm[0] = 15; m->active = 0; m->persistent = 1;
            s->image_speed = 0.25;
            sound_play(SND_EXPELL);
            break;
        }
        case O_OBJECT28:
            music_play(g_song);
            /* bgm_Stop(title): the title track is replaced by the song above */
            room_goto_next();
            break;
        default: break;
    }
}

/* -------------------------------------------------------------------------------------------- step */
static void walk_bounds_p1(Inst* s) {
    if (gm_gt(s->x, G.w)) { s->x -= G.w; if (G.room == room_last()) { /* do_nothing */ } else room_goto_next(); }
    if (gm_lt(s->x, 0)) { s->x += G.w; if (G.room == 1) { /* do_nothing */ } else room_goto_previous(); }
    if (gm_gt(s->y, G.h)) { inst_destroy(s); sound_play(SND_DIE); inst_create(s->x, G.h - 2, O_DEATH); }
}
static void walk_bounds_p2(Inst* s) {       /* P2, P3, P5 */
    if (gm_gt(s->x, G.w)) { s->x -= G.w; if (G.room == room_last()) { } else room_goto_next(); }
    if (gm_lt(s->x, 0)) {
        s->x += G.w;
        if (G.room == room_first() || G.room == 3) { if (G.room == 3) sound_play(SND_CHANGE); }
        else room_goto_previous();
    }
    if (gm_gt(s->y, G.h)) { inst_destroy(s); sound_play(SND_DIE); inst_create(s->x, G.h - 2, O_DEATH); }
}

static void walk_input(Inst* s) {                 /* the left/right blocks shared by P1, P2, P5 */
    if (key_held(VK_LEFT) && gm_eq(s->active, 1)) {
        if (PF(s->x - 4, s->y)) s->x -= 4;
        s->image_index += 0.2;
        s->xscale = -2;
    }
    if (key_held(VK_RIGHT) && gm_eq(s->active, 1)) {
        if (PF(s->x + 4, s->y)) s->x += 4;
        s->image_index += 0.2;
        s->xscale = +2;
    }
}

void ev_step(Inst* s) {
    switch (s->obj) {
        case O_P1:
            if (!key_held(VK_LEFT) && !key_held(VK_RIGHT)) s->image_index = 0;
            if (PF(s->x, s->y + 1)) { s->image_index = 1; s->gravity = 0.4; } else s->gravity = 0;
            if (gm_gt(s->vspeed, 8)) set_vspeed(s, 8);
            if (instance_exists(O_MESSAGER)) { s->active = 0; s->image_index = 0; } else s->active = 1;
            if (gm_gt(s->alarm[0], 0)) s->active = 0;
            walk_input(s);
            walk_bounds_p1(s);
            break;

        case O_P2: {
            Inst* p1;
            if (!key_held(VK_LEFT) && !key_held(VK_RIGHT)) s->image_index = 0;
            if (PF(s->x, s->y + 1)) { s->image_index = 1; s->gravity = 0.4; } else s->gravity = 0;
            if (gm_gt(s->vspeed, 8)) set_vspeed(s, 8);
            if (!instance_exists(O_P1) && !instance_exists(O_DEATH)) { s->active = 1; s->persistent = 1; }
            if (instance_exists(O_MESSAGER)) { s->active = 0; s->image_index = 0; }
            if (gm_gt(s->alarm[0], 0)) s->active = 0;
            p1 = instance_first(O_P1);
            if (gm_eq(s->active, 0) && gm_eq(s->on, 1) && p1) {
                if (gm_lt(point_distance(s->x, s->y, p1->x, p1->y), 320)) {
                    Inst* m;
                    s->on = 0; m = new_msg(s->x, s->y); s->my = m;
                    set_msg(m, 0, TX(O_P2, E_STEP, 0)); set_msg(m, 1, TX(O_P2, E_STEP, 1)); set_msg(m, 2, TX(O_P2, E_STEP, 2));
                    m->messages = 2;
                }
            }
            walk_input(s);
            walk_bounds_p2(s);
            break;
        }

        case O_DEATH: {
            Inst* c;
            if (gm_gt(s->image_index, 22)) inst_destroy(s);
            if (gm_eq(s->on, 0)) { s->image_speed = 0.2; s->gravdir = point_direction(s->x, s->y, s->myx, s->myy); return; }
            c = inst_create(s->x - 10 + gm_random(20), s->y - 10 + gm_random(20), O_DEATH);
            s->my = c;
            c->gravity = 0.2;
            set_direction(c, gm_random(360));
            set_speed(c, 2 + gm_random(3));
            c->image_index = s->image_index + gm_random(23 - s->image_index);
            c->image_speed = 0;
            c->myx = s->x; c->myy = s->y;
            c->on = 0;
            break;
        }

        case O_BUTTERFLY: {
            Inst* p1;
            if (gm_eq(s->on, 0)) return;
            p1 = instance_first(O_P1);
            if (p1 && gm_lt(point_distance(s->x, s->y, p1->x, p1->y), 50)) {
                Inst* m;
                s->on = 0; m = new_msg(s->x, s->y); s->my = m;
                set_msg(m, 0, TX(O_BUTTERFLY, E_STEP, 0));
            }
            break;
        }

        case O_MESSAGER:
            if (gm_eq(s->start, 0)) { if (gm_lt(s->image_index, 11)) s->image_index += 0.5; else s->start = 1; }
            if (gm_eq(s->start, 2)) { if (gm_gt(s->image_index, 0)) s->image_index -= 0.5; else inst_destroy(s); }
            break;

        case O_SHIRLEY: {
            Inst* pl;
            if (gm_eq(s->on, 0)) return;
            pl = instance_first(O_PLAYER);
            if (pl) {
                if (gm_gt(pl->x, s->x)) s->xscale = -2; else s->xscale = 2;
                if (gm_lt(point_distance(s->x, s->y, pl->x, pl->y), 170)) {
                    Inst* m; int b;
                    s->on = 0; m = new_msg(s->x, s->y); s->my = m;
                    b = instance_exists(O_P2) ? 0 : 4;
                    set_msg(m, 0, TX(O_SHIRLEY, E_STEP, b + 0)); set_msg(m, 1, TX(O_SHIRLEY, E_STEP, b + 1));
                    set_msg(m, 2, TX(O_SHIRLEY, E_STEP, b + 2)); set_msg(m, 3, TX(O_SHIRLEY, E_STEP, b + 3));
                    m->messages = 3;
                }
            }
            break;
        }

        case O_SPIKE: {
            Inst* pl = instance_first(O_PLAYER);
            if (!pl) return;
            if (gm_gt(pl->x, s->x - 4) && gm_lt(pl->x, s->x + 4) && gm_gt(pl->y, s->y)) {
                if (gm_eq(s->vspeed, 0)) {
                    Inst* d;
                    sound_play(SND_SPIKEFALL);
                    d = inst_create(s->x + 4, s->y, O_DEATH); s->my = d;
                    d->on = 0; d->image_index = 19;
                }
                set_vspeed(s, 4);
            }
            break;
        }

        case O_P3: {
            Inst *p2, *p4;
            if (!key_held(VK_LEFT) && !key_held(VK_RIGHT)) { if (s->sprite == S_P3) { s->image_index = 0; s->image_speed = 0; } }
            if (PF(s->x, s->y + 1)) { s->image_index = 1; s->gravity = 0.4; set_hspeed(s, 0); s->image_speed = 0; } else s->gravity = 0;
            if (gm_gt(s->vspeed, 8)) set_vspeed(s, 8);
            if (!instance_exists(O_P2) && !instance_exists(O_DEATH) && !instance_exists(O_P4)) { s->active = 1; s->persistent = 1; }
            p4 = instance_first(O_P4);
            if (p4) { if (gm_eq(p4->on, 0)) { s->active = 0; s->persistent = 0; } }
            if (instance_exists(O_MESSAGER)) { s->image_speed = 0; s->image_index = 0; s->active = 0; }
            if (gm_gt(s->alarm[0], 0)) s->active = 0;
            p2 = instance_first(O_P2);
            if (gm_eq(s->active, 0)) { if (p2) { if (gm_gt(p2->x, s->x)) s->xscale = 2; else s->xscale = -2; } }
            if (gm_eq(s->active, 0) && gm_eq(s->on, 1) && p2) {
                if (gm_lt(point_distance(s->x, s->y, p2->x, p2->y), 200)) {
                    Inst* m; int k;
                    s->on = 0; m = new_msg(s->x, s->y); s->my = m;
                    for (k = 0; k < 6; k++) set_msg(m, k, TX(O_P3, E_STEP, k));
                    m->messages = 5;
                }
            }
            if (gm_eq(floor(s->hspeed), 0)) { set_hspeed(s, 0); s->sprite = S_P3; }
            if (key_held(VK_LEFT) && gm_eq(s->active, 1) && s->sprite == S_P3) {
                if (PF(s->x - 4, s->y)) s->x -= 4;
                s->image_speed = 0.2;
                s->xscale = -2;
            }
            if (key_held(VK_RIGHT) && gm_eq(s->active, 1) && s->sprite == S_P3) {
                if (PF(s->x + 4, s->y)) s->x += 4;
                s->image_speed = 0.2;
                s->xscale = 2;
            }
            if (gm_gt(floor(fabs(s->hspeed)), 0)) { if (gm_lt(s->image_index, 1)) s->image_index += 0.1; s->image_speed = 0; s->friction = 0.5; }
            if (gm_gt(fabs(s->vspeed), 0)) { s->friction = 0; set_hspeed(s, 0); }
            if (!PF(s->x + s->hspeed, s->y)) set_hspeed(s, 0);
            walk_bounds_p2(s);
            break;
        }

        case O_BUNNY: {
            Inst* pl;
            if (PF(s->x, s->y + 1)) {
                s->image_index = 1; s->gravity = 0.4;
                if (g_bunny_boost && gm_lt(fabs(s->vspeed), 1.5)) s->gravity = 0.2;      /* hang time around the top of the hop */
                if (gm_gt(s->vspeed, 0)) s->image_index = 2;
            } else { s->gravity = 0; s->image_index = 0; }
            if (gm_gt(s->vspeed, 8)) set_vspeed(s, 8);
            pl = instance_first(O_PLAYER);
            if (pl && gm_eq(g_spoken, 0)) {
                if (gm_lt(point_distance(s->x, s->y, pl->x, pl->y), 150)) {
                    Inst* m; int p2 = instance_exists(O_P2);
                    g_spoken = 1; m = new_msg(s->x, s->y); s->my = m;
                    set_msg(m, 0, TX(O_BUNNY, E_STEP, 0));
                    set_msg(m, 1, TX(O_BUNNY, E_STEP, p2 ? 1 : 2));
                    set_msg(m, 2, TX(O_BUNNY, E_STEP, 3));
                    set_msg(m, 3, TX(O_BUNNY, E_STEP, p2 ? 4 : 5));
                    set_msg(m, 4, TX(O_BUNNY, E_STEP, p2 ? 6 : 7));
                    set_msg(m, 5, TX(O_BUNNY, E_STEP, p2 ? 8 : 9));
                    m->messages = 5;
                }
            }
            pl = instance_first(O_PLAYER);
            if (pl) { if (gm_gt(pl->x, s->x)) s->xscale = -2; else s->xscale = 2; }
            break;
        }

        case O_P4: {
            Inst* p3;
            if (PF(s->x, s->y + 1) && PF(s->x, s->y - 1)) { s->gravity = 0.1; s->y += lengthdir_x(0.1, s->mydir); }
            else { if (!PF(s->x, s->y + 1)) s->gravity = 0; }
            s->mydir += 4;
            if (g_flight_fine && gm_eq(s->active, 1)) {      /* finer control: hold Up to climb, hold Down to sink (both gentle), a tap is half a flap */
                if (key_held(VK_UP) && gm_gt(s->vspeed, -g_fl_cap)) set_vspeed(s, fmax(-g_fl_cap, s->vspeed - g_fl_acc));
                if (key_held(VK_DOWN) && gm_lt(s->vspeed, g_fl_cap)) set_vspeed(s, fmin(g_fl_cap, s->vspeed + g_fl_acc));
            }
            if (!PF(s->x, s->y + s->vspeed)) set_vspeed(s, 0);
            if (gm_gt(s->vspeed, 4)) set_vspeed(s, 4);
            if (gm_lt(s->vspeed, -4)) set_vspeed(s, -4);
            p3 = instance_first(O_P3);
            if (p3) {
                if (gm_lt(point_distance(s->x, s->y, p3->x, p3->y), 200) && gm_eq(s->on, 1)) {
                    Inst* m; int k;
                    p3->solid = 1; s->on = 0; m = new_msg(s->x, s->y); s->my = m;
                    for (k = 0; k < 6; k++) set_msg(m, k, TX(O_P4, E_STEP, k));
                    m->messages = 5;
                }
            }
            if (gm_eq(s->active, 1) && key_held(VK_LEFT)) {
                if (PF(s->x - 4, s->y)) s->x -= 4;
                s->image_speed = 1;
                s->xscale = -2;
            }
            if (gm_eq(s->active, 1) && key_held(VK_RIGHT)) {
                if (PF(s->x + 4, s->y)) s->x += 4;
                s->image_speed = 1;
                s->xscale = 2;
            }
            if (gm_eq(s->on, 0)) { s->active = 1; s->persistent = 1; }
            if (instance_exists(O_MESSAGER)) s->active = 0;
            if (gm_gt(s->alarm[0], 0)) s->active = 0;
            if (gm_gt(s->x, G.w)) { s->x -= G.w; if (G.room == room_last()) { } else room_goto_next(); }
            if (gm_lt(s->x, 0)) { s->x += G.w; if (G.room == room_first()) { } else room_goto_previous(); }
            if (gm_gt(s->y, G.h) || gm_lt(s->y, 0)) {
                inst_destroy(s); sound_play(SND_DIE);
                if (gm_lt(s->y, 0)) s->y = 2;
                if (gm_gt(s->y, G.h)) s->y = G.h - 2;
                inst_create(s->x, s->y, O_DEATH);
            }
            if (gm_lt(s->image_speed, 0.05)) s->image_speed = 0.05;
            break;
        }

        case O_CASTLE: {
            Inst *p5, *m;
            if (gm_eq(s->visible, 1) && gm_gt(G.bg, 0)) {
                if (gm_gt(s->reload, 0)) s->reload -= 1;
                else {
                    int v;
                    s->reload = 15;
                    v = gm_color_val(G.bg);
                    if (v - 50 > 0) v -= 50; else v = 0;
                    G.bg = gm_hsv(gm_color_hue(G.bg), gm_color_sat(G.bg), v);
                }
            }
            p5 = instance_first(O_P5);
            if (p5 && gm_eq(s->on, 1)) {
                if (gm_ge(p5->x, 142)) {
                    m = new_msg(s->x, s->y); s->my = m;
                    set_msg(m, 0, TX(O_CASTLE, E_STEP, 0)); m->messages = 0; s->on = 0;
                }
                return;
            }
            if (instance_exists(O_MESSAGER)) return;
            else {
                Inst* l[256]; int n = with_all(O_P5, l), i;
                for (i = 0; i < n; i++) { sound_play(SND_DIE); inst_create(l[i]->x, l[i]->y, O_DEATH); inst_destroy(l[i]); }
            }
            if (instance_exists(O_DEATH)) return;
            if (s->visible) {
                if (gm_gt(s->reload, 0)) s->reload -= 1;
                else {
                    if (gm_gt(s->blend, 0)) {
                        int k, v;
                        s->reload = 15;
                        v = gm_color_val(s->blend);
                        if (v - 51 > 1) v -= 51; else v = 0;
                        s->v = v;
                        for (k = 0; k < nL; k++)
                            if (L[k]->alive) L[k]->blend = gm_hsv(gm_color_hue(G.bg), gm_color_sat(G.bg), s->v);
                    } else {
                        g_endsong = 2;
                        music_play(g_endsong);
                        room_goto_next();
                    }
                }
                return;
            }
            if (gm_eq(s->played, 0)) { s->played = 1; sound_play(SND_MAGIC); }
            m = new_msg(s->x, s->y); s->my = m;
            set_msg(m, 0, TX(O_CASTLE, E_STEP, 2)); m->messages = 0; m->font = 1;
            s->visible = 1;
            s->image_speed = 0.2;
            {
                Inst* l[1024]; int k, n = 0;
                for (k = 0; k < nL && n < 1024; k++) if (L[k]->alive && obj_is(L[k]->obj, O_BLOCK)) l[n++] = L[k];
                for (k = 0; k < n; k++) inst_destroy(l[k]);
            }
            break;
        }

        case O_P5: {
            Inst* p4;
            if (!key_held(VK_LEFT) && !key_held(VK_RIGHT)) s->image_index = 0;
            if (PF(s->x, s->y + 1)) { s->image_index = 1; s->gravity = 0.4; } else s->gravity = 0;
            if (gm_gt(s->vspeed, 8)) set_vspeed(s, 8);
            if (!instance_exists(O_P4) && !instance_exists(O_DEATH)) { s->active = 1; s->persistent = 1; }
            if (instance_exists(O_MESSAGER)) { s->active = 0; s->image_index = 0; }
            p4 = instance_first(O_P4);
            if (gm_eq(s->active, 0) && gm_eq(s->on, 1) && p4) {
                if (gm_lt(point_distance(s->x, s->y, p4->x, p4->y), 160) && gm_gt(p4->y, 384) && gm_lt(p4->y, 416)) {
                    Inst* m;
                    Inst* l[256]; int n = with_all(O_P4, l), i;
                    for (i = 0; i < n; i++) { music_stop_if(g_song); sound_play(SND_GUNSHOT); inst_create(l[i]->x, l[i]->y, O_DEATH); inst_destroy(l[i]); }
                    s->on = 0; m = new_msg(s->x, s->y); s->my = m;
                    set_msg(m, 0, TX(O_P5, E_STEP, 0)); set_msg(m, 1, TX(O_P5, E_STEP, 1)); m->messages = 1;
                }
            }
            walk_input(s);
            walk_bounds_p2(s);
            break;
        }
        default: break;
    }
}

/* ---------------------------------------------------------------------------------------- collision */
void ev_collision(Inst* s, Inst* other, int target) {
    switch (s->obj) {
        case O_P1: case O_P2: case O_P5: case O_P4:
            if (target == O_DANGEROUS) { if (getenv("PSY_COLLDBG")) fprintf(stderr, "hit: obj %d at %.0f,%.0f (y %.1f) by obj %d at %.0f,%.0f scale %.0f,%.0f\n", s->obj, s->x, s->y, s->y, other->obj, other->x, other->y, other->xscale, other->yscale); die_here(s); }
            else if (target == O_BLOCK && s->obj != O_P4) { move_contact_solid(s, s->direction, s->speed); set_vspeed(s, 0); s->image_index = 0; }
            break;
        case O_P3:
            if (target == O_DANGEROUS) {
                if (gm_gt(fabs(s->hspeed), 0.1)) {
                    if (other->obj == O_BUNNY) {
                        inst_create(other->x, other->y, O_DEATH); sound_play(SND_DIE); inst_destroy(other);
                        return;                       /* `exit` inside with(other) ends the whole event */
                    }
                }
                die_here(s);
            } else if (target == O_BLOCK) { move_contact_solid(s, s->direction, -1); set_vspeed(s, 0); s->image_index = 0; }
            break;
        case O_SPIKE: if (target == O_BLOCK) set_speed(s, 0); break;
        case O_SAVER:
            g_saveroom = G.room; g_type = other->obj;
            if (gm_eq(s->image_index, 0)) { sound_play(SND_SELECT); s->image_index = 1; }
            break;
        case O_BUNNY: if (target == O_BLOCK) { move_contact_solid(s, s->direction, s->speed * 2); set_vspeed(s, 0); } break;
        default: break;
    }
}

/* -------------------------------------------------------------------------------------------- keys */
void ev_key(Inst* s, int kind, int vk) {
    switch (s->obj) {
        case O_P1: case O_P2: case O_P5:
            if (kind == 0 && vk == VK_UP) { if (gm_eq(s->active, 0) || !gm_jump_allowed()) return; if (gm_can_jump(s)) { sound_play(SND_JUMP); s->coyote = 99; set_vspeed(s, -7); gm_jump_done(); } }
            else if (kind == 1 && (vk == VK_RIGHT || vk == VK_LEFT)) { if (gm_eq(s->active, 0)) return; if (!PF(s->x, s->y + 1)) { s->image_index = 0; sound_play(SND_STEP); } }
            else if (kind == 2 && vk == VK_UP) { if (gm_lt(s->vspeed, 0)) set_vspeed(s, s->vspeed * 0.25); }
            break;
        case O_P3:
            if (kind == 0 && vk == VK_UP) {
                if (gm_eq(s->active, 0) || s->sprite == S_P3PUNCH || !gm_jump_allowed()) return;
                if (gm_can_jump(s)) { sound_play(SND_JUMP); s->coyote = 99; set_vspeed(s, -9); gm_jump_done(); }
            } else if (kind == 1 && vk == VK_Z) {
                if (gm_eq(s->active, 0) || gm_gt(s->speed, 0)) return;
                if (!PF(s->x, s->y + 1)) {
                    s->image_speed = 0; s->sprite = S_P3PUNCH; set_hspeed(s, s->xscale * 4); s->image_index = 0; sound_play(SND_PUNCH);
                }
            } else if (kind == 1 && vk == VK_RIGHT) {
                if (gm_eq(s->active, 0) || gm_gt(fabs(s->hspeed), 0)) return;
                if (!PF(s->x, s->y + 1)) { s->image_index = 0; sound_play(SND_STEP); s->image_index = gm_round(s->image_index); }
            } else if (kind == 1 && vk == VK_LEFT) {
                if (gm_eq(s->active, 0)) return;
                if (gm_gt(fabs(s->hspeed), 0)) return;
                if (!PF(s->x, s->y + 1)) { s->image_index = 0; sound_play(SND_STEP); s->image_index = gm_round(s->image_index); }
            } else if (kind == 2 && vk == VK_UP) { if (gm_lt(s->vspeed, 0)) set_vspeed(s, s->vspeed * 0.25); }
            break;
        case O_BUNNY:
            if (kind == 0 && vk == VK_UP) {
                Inst* pl = instance_first(O_PLAYER);
                if (pl) { if (!PF(s->x, s->y + 1) && gm_eq(pl->active, 1)) set_vspeed(s, g_bunny_boost ? -8.5 : -7); }
            }
            break;
        case O_P4:
            if (kind == 1 && vk == VK_UP) { if (gm_eq(s->active, 0)) return; set_vspeed(s, s->vspeed - (g_flight_fine ? g_fl_tap : 2)); s->image_speed = 1; }
            break;
        case O_MESSAGER:
            if (kind == 1 && vk == VK_Z) {
                const char* m;
                if (gm_lt(s->start, 1)) return;
                m = s->message[(int)s->current];
                if (!m) m = "";
                if (gm_ge(s->letter, (double)cp_count(m))) { if (gm_lt(s->current, s->messages)) { s->current += 1; s->letter = 0; } else s->start = 2; }
                else s->letter = (double)cp_count(m);
            }
            break;
        case O_OBJECT28:
            if (kind == 1 && vk == VK_ENTER) {
                if (gm_lt(s->alarm[0], 1)) { music_fade(1500); sound_play(SND_DIE); s->image_speed = 0.25; s->alarm[0] = 120; }
            }
            break;
        default: break;
    }
}

/* ------------------------------------------------------------------------------------------- other */
void ev_other(Inst* s, int sub) {
    switch (sub) {
        case 0:
            if (s->obj == O_DEATH) inst_destroy(s);
            break;
        case 4:                                       /* room start */
            if (s->obj == O_P3) {
                if (instance_number(O_P3) > 1) {
                    Inst* l[256]; int n = with_all(O_P3, l), i;
                    for (i = 0; i < n; i++) if (gm_eq(l[i]->active, 0)) inst_destroy(l[i]);
                }
                if (G.room == 13) { if (instance_number(O_P4) == 0) inst_create(416, 208, O_P4); }
            } else if (s->obj == O_P4) {
                if (gm_eq(s->active, 1)) {
                    Inst* l[256]; int n = with_all(O_PLAYER, l), i;
                    for (i = 0; i < n; i++) { if (l[i]->obj == O_P4) { /* do_nothing */ } else l[i]->active = 0; }
                }
                if (instance_number(s->obj) > 1) {
                    Inst* l[256]; int n = with_all(s->obj, l), i;
                    for (i = 0; i < n; i++) if (gm_eq(l[i]->active, 0)) inst_destroy(l[i]);
                }
                if (G.room == 13) A.room[13].persistent = 1;      /* room_set_persistent(room13,1) */
            }
            break;
        case 7:                                       /* animation end */
            switch (s->obj) {
                case O_P1: case O_P2: case O_P3: case O_P5: sound_play(SND_STEP); break;
                case O_DEATH: inst_destroy(s); break;
                case O_PUMP: s->image_speed = 0; break;
                case O_P4: s->image_speed = s->image_speed * 0.8; break;
                default: break;
            }
            break;
        default: break;
    }
}

/* -------------------------------------------------------------------------------------------- draw */
/* ---- readable dialogue: same typing, sounds and timing as the original bubble, but drawn as a taller box with dark
 * mixed-case text on a light ground and proper word wrapping. */
static int g_text_mode = 1;      /* 0 clear typeface, 1 the game's own font (repaired), 2 the original pink bubble text */
void game_set_text_mode(int m) { g_text_mode = m; }

#define MSG_MAX_LINES 6
#define MSG_MAX_CP 512
static int is_close_punct(uint32_t c) {                 /* may not start a line in Japanese/Chinese */
    switch (c) { case 0x3001: case 0x3002: case 0xff0c: case 0xff0e: case 0xff01: case 0xff1f: case 0xff1a: case 0xff1b: case 0x300d: case 0x300f: case 0xff09: case 0x2026: case '!': case '?': case ',': case '.': return 1; default: return 0; }
}
/* word wrap on characters (spaces break lines; Japanese/Chinese may break anywhere) */
static int wrap_cps(const uint32_t* cp, int len, int maxw, int* st, int* ln) {
    int n = 0, i = 0, cjk = lang_cjk();
    while (i <= len && n < MSG_MAX_LINES) {
        int start = i, last_sp = -1, e = i;
        while (e < len) {
            if (cp[e] == ' ') last_sp = e;
            if (r_cp_width(cp + start, e - start + 1) > maxw && e > start) {
                if (cjk) { while (e > start + 1 && is_close_punct(cp[e])) e--; }
                else if (last_sp > start) e = last_sp;
                break;
            }
            e++;
        }
        st[n] = start; ln[n] = e - start; n++;
        if (e < len && cp[e] == ' ') e++;                      /* the space at the wrap is not drawn */
        if (e >= len) break;
        i = e;
    }
    return n;
}

static void draw_clear_msg(Inst* s) {
    const char* orig = s->message[(int)s->current];
    uint32_t cp[MSG_MAX_CP], vis[MSG_MAX_CP];
    int st[MSG_MAX_LINES], ln[MSG_MAX_LINES], nl, len, boxh, boxy, k, n;
    const int bx = 8, bw = 624, bottom = 470, pad = 12, lh = r_ctext_line();
    double t = s->image_index / 11.0;
    if (!orig) orig = "";
    len = u8_decode(orig, cp, MSG_MAX_CP);
    for (k = 0; k < len; k++) if (cp[k] == '#') cp[k] = ' ';       /* the 8x8 bubble's line break: just a space here */
    nl = wrap_cps(cp, len, bw - 2 * pad - 8, st, ln);
    boxh = 2 * pad + nl * lh;
    if (t > 1) t = 1;
    if (t < 0) t = 0;
    if (!gm_eq(s->start, 1)) boxh = (int)(boxh * (0.25 + 0.75 * t));
    boxy = bottom - boxh;
    r_box(bx, boxy, bw, boxh, 3, C_SHELDON, gm_rgb(250, 246, 234));
    if (!gm_eq(s->start, 1)) return;
    if (gm_lt(s->letter, (double)len)) {                    /* the typing step, as in the original */
        s->letter += 0.25;
        if (gm_eq(s->letter - trunc(s->letter), 0)) {
            int idx = (int)gm_round(s->letter);
            uint32_t c = (idx >= 1 && idx <= len) ? cp[idx - 1] : 0;
            if (c == ' ') { /* do_nothing */ } else sound_play(SND_TEXT);
        }
    }
    n = (int)gm_round(s->letter);
    if (n < 0) n = 0;
    if (n > len) n = len;
    for (k = 0; k < nl; k++) {
        int shown = n - st[k], x = bx + pad + 4;
        if (shown <= 0) break;
        if (shown > ln[k]) shown = ln[k];
        r_visual(cp + st[k], shown, vis);
        if (r_is_rtl()) x = bx + bw - pad - 4 - r_cp_width(vis, shown);         /* right-to-left text grows from the right edge */
        r_cp_draw(x, boxy + pad + k * lh, vis, shown, gm_rgb(38, 56, 70));
    }
}


/* ---- the game's own larger font (M, N, W redrawn, see render.c) in the same light box: dark letters, 2x size, upper case like the original */
static void draw_font_msg(Inst* s) {
    const char* orig = s->message[(int)s->current];
    char msg[MSG_MAX_CP], line[MSG_MAX_CP];
    int st[MSG_MAX_LINES], ln[MSG_MAX_LINES], nl = 0, len, boxh, boxy, k, n, i;
    const double xs = 2, ys = 2;
    const int bx = 8, bw = 624, bottom = 470, pad = 12, lh = 13 * (int)ys, maxw = bw - 2 * pad - 8;
    double t = s->image_index / 11.0;
    if (!orig) orig = "";
    snprintf(msg, sizeof msg, "%s", orig);
    for (k = 0; msg[k]; k++) { if (msg[k] == '#') msg[k] = ' '; else if (msg[k] >= 'a' && msg[k] <= 'z') msg[k] = (char)(msg[k] - 32); }      /* string_upper, as the game does */
    len = (int)strlen(msg);
    for (i = 0; i <= len && nl < MSG_MAX_LINES;) {
        int start = i, last_sp = -1, e = i;
        while (e < len) {
            if (msg[e] == ' ') last_sp = e;
            memcpy(line, msg + start, (size_t)(e - start + 1)); line[e - start + 1] = 0;
            if (r_font_width(FONT_FIXED, line, xs) > maxw && e > start) { if (last_sp > start) e = last_sp; break; }
            e++;
        }
        st[nl] = start; ln[nl] = e - start; nl++;
        if (e < len && msg[e] == ' ') e++;
        if (e >= len) break;
        i = e;
    }
    boxh = 2 * pad + nl * lh;
    if (t > 1) t = 1;
    if (t < 0) t = 0;
    if (!gm_eq(s->start, 1)) boxh = (int)(boxh * (0.25 + 0.75 * t));
    boxy = bottom - boxh;
    r_box(bx, boxy, bw, boxh, 3, C_SHELDON, gm_rgb(250, 246, 234));
    if (!gm_eq(s->start, 1)) return;
    if (gm_lt(s->letter, (double)len)) {
        s->letter += 0.25;
        if (gm_eq(s->letter - trunc(s->letter), 0)) {
            int idx = (int)gm_round(s->letter);
            char c = (idx >= 1 && idx <= len) ? msg[idx - 1] : 0;
            if (c == ' ') { /* do_nothing */ } else sound_play(SND_TEXT);
        }
    }
    n = (int)gm_round(s->letter);
    if (n < 0) n = 0;
    if (n > len) n = len;
    for (k = 0; k < nl; k++) {
        int shown = n - st[k];
        if (shown <= 0) break;
        if (shown > ln[k]) shown = ln[k];
        memcpy(line, msg + st[k], (size_t)shown); line[shown] = 0;
        r_text(FONT_FIXED, bx + pad + 4, boxy + pad + k * lh - 2, line, xs, ys, gm_rgb(38, 56, 70), FA_LEFT, FA_TOP);
    }
}

void ev_draw(Inst* s) {
    switch (s->obj) {
        case O_OBJECT0:
            fog(1, C_SHELDON);
            draw_sprite_ext(s->sprite, s->image_index, s->x, s->y + 2, s->xscale, s->yscale, C_WHITE, s->alpha);
            fog(0, 0);
            draw_sprite_ext(s->sprite, s->image_index, s->x, s->y, s->xscale, s->yscale, s->blend, s->alpha);
            break;
        case O_CASTLE: draw_snap2(s); break;
        case O_OBJECT27: {
            const char* text = TX(O_OBJECT27, E_DRAW, 0);
            if (s->h < 255) s->h += 1; else s->h = 1;
            ds_font = 0; ds_color = gm_hsv(s->h, 255, 255); ds_halign = FA_CENTER; ds_valign = FA_MIDDLE;
            r_text(ds_font, 320, 220, text, 2, 2, ds_color, ds_halign, ds_valign);
            break;
        }
        case O_OBJECT28:
            draw_sprite_ext(s->sprite, s->image_index, 0, 0, s->xscale, s->yscale, s->blend, s->alpha);
            break;
        case O_MESSAGER: {
            if (lang_index() != 0 || g_text_mode == 0) { draw_clear_msg(s); break; }     /* other languages need the clear typeface (the game's own font is ASCII only) */
            if (g_text_mode == 1) { draw_font_msg(s); break; }
            draw_snap(s);
            if (gm_eq(s->start, 1)) {
                const char* msg = s->message[(int)s->current];
                char buf[512];
                int n, len, k;
                if (!msg) msg = "";
                len = (int)strlen(msg);
                if (gm_lt(s->letter, (double)len)) {
                    s->letter += 0.25;
                    if (gm_eq(s->letter - trunc(s->letter), 0)) {
                        int idx = (int)gm_round(s->letter);
                        char c = (idx >= 1 && idx <= len) ? msg[idx - 1] : 0;
                        if (c == ' ') { /* do_nothing */ } else sound_play(SND_TEXT);
                    }
                }
                ds_font = gm_eq(s->font, 0) ? 0 : 1;
                ds_halign = FA_LEFT; ds_valign = FA_TOP;
                n = (int)gm_round(s->letter);
                if (n < 0) n = 0;
                if (n > len) n = len;
                if (n > (int)sizeof buf - 1) n = (int)sizeof buf - 1;
                for (k = 0; k < n; k++) { char c = msg[k]; buf[k] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }   /* string_upper */
                buf[n] = 0;
                r_text(ds_font, s->x + s->font * 4, s->y + s->font * 4 - 2, buf, 2, 2, C_SHELDON, ds_halign, ds_valign);
                r_text(ds_font, s->x + s->font * 4, s->y + s->font * 4 + 2, buf, 2, 2, C_SHELDON, ds_halign, ds_valign);
                r_text(ds_font, s->x + s->font * 4 - 2, s->y + s->font * 4, buf, 2, 2, C_SHELDON, ds_halign, ds_valign);
                r_text(ds_font, s->x + s->font * 4 + 2, s->y + s->font * 4, buf, 2, 2, C_SHELDON, ds_halign, ds_valign);
                r_text(ds_font, s->x + s->font * 4, s->y + s->font * 4, buf, 2, 2, gm_rgb(252, 200, 200), ds_halign, ds_valign);
            }
            break;
        }
        default: draw_snap(s); break;     /* every other drawing object just calls draw_snap() */
    }
}
