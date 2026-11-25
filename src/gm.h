/* A small Game Maker 7 runtime: just the behaviour Psychosomnium relies on (instances, motion, precise
 * collision, event order, rooms, persistence). The semantics follow the Game Maker runner as documented by
 * the OpenGMK project's behaviour notes; the code is our own. */
#ifndef GM_H
#define GM_H

#include "assets.h"
#include <stdint.h>

/* ---- object ids (the order in the original game's resource list) */
enum {
    O_OBJECT0 = 0, O_P1 = 1, O_SPIKES = 2, O_BLOCK = 3, O_P2 = 5, O_DANGEROUS = 6, O_DEATH = 7, O_SPIKES2 = 8,
    O_BUTTERFLY = 9, O_MESSAGER = 10, O_SHIRLEY = 11, O_PLAYER = 12, O_SPIKE = 13, O_PUMP = 14, O_SAVER = 15,
    O_SHIFT = 16, O_OBJCOLOR1 = 17, O_BGTHING = 18, O_BGCONTROL = 19, O_P3 = 20, O_BUNNY = 21, O_SHIFT2 = 22,
    O_OBJCOLOR2 = 23, O_P4 = 24, O_CASTLE = 25, O_P5 = 26, O_OBJECT27 = 27, O_OBJECT28 = 28, N_OBJECTS = 29
};
/* sprite ids */
enum {
    S_P1 = 0, S_P2 = 1, S_SPRITE2 = 2, S_SPRITE3 = 3, S_SPRITE4 = 4, S_SPIKES = 5, S_SPIKES2 = 6, S_DEATH = 7,
    S_BUTTERFLY = 8, S_BUBBLE = 9, S_SPRITE10 = 10, S_SPIKE = 11, S_TRANS = 12, S_SPRITE13 = 13, S_PUMP = 14,
    S_SAVER = 15, S_SPRITE16 = 16, S_SPRITE17 = 17, S_SPRITE18 = 18, S_P3 = 19, S_SPRITE20 = 20, S_BUNNY = 21,
    S_SPRITE22 = 22, S_SPRITE23 = 23, S_SPRITE24 = 24, S_P3PUNCH = 25, S_SPRITE26 = 26, S_SPRITE27 = 27,
    S_SPRITE28 = 28, S_CASTLE = 30, S_SPLASH = 32
};
/* sound ids */
enum { SND_JUMP, SND_STEP, SND_DIE, SND_TEXT, SND_CHANGE, SND_EXPELL, SND_SPIKEFALL, SND_SELECT, SND_PUNCH, SND_MAGIC, SND_GUNSHOT };

/* virtual keys (Windows codes, as the game's events name them) */
enum { VK_LEFT = 37, VK_UP = 38, VK_RIGHT = 39, VK_DOWN = 40, VK_Z = 90, VK_ENTER = 13, VK_F11 = 122 };

#define C_WHITE 0xffffffu
#define C_SHELDON (60u | 87u << 8 | 105u << 16)    /* make_color_rgb(60,87,105), GM stores colours as BGR */

typedef struct Inst Inst;
struct Inst {
    int id, obj, alive, persistent, solid, visible, depth;
    double x, y, xprev, yprev, hspeed, vspeed, speed, direction, gravity, gravdir, friction;
    int sprite, mask;
    double image_index, image_speed, xscale, yscale, alpha;
    uint32_t blend;
    double alarm[12];
    /* object variables (GML instance variables, all start as 0) */
    double active, on, myx, myy, mydir, letter, start, messages, current, font, h, reload, played, v;
    int coyote;                  /* frames since a player character last stood on something (ledge forgiveness) */
    Inst* my;                    /* my_id */
    char* message[8];
};

typedef struct {
    int pending;                 /* a room change is waiting for the end of the frame */
    int room;                    /* current room index */
    int target;
    int w, h;
    uint32_t bg;                 /* background_color (GM BGR) */
} RoomCtl;
extern RoomCtl G;

/* ---- the world */
#define MAX_INST 3000
extern Inst* L[MAX_INST];
extern int nL;

extern double gm_round(double v);               /* banker's rounding, like Delphi/GM */
extern int gm_eq(double a, double b);
extern int gm_lt(double a, double b);
extern int gm_le(double a, double b);
extern int gm_gt(double a, double b);
extern int gm_ge(double a, double b);
extern double gm_random(double n);
extern uint32_t gm_rgb(int r, int g, int b);
extern uint32_t gm_hsv(double h, double s, double v);
extern int gm_color_hue(uint32_t c), gm_color_sat(uint32_t c), gm_color_val(uint32_t c);

Inst* inst_create(double x, double y, int obj);
void inst_destroy(Inst* i);                      /* instance_destroy(): runs the Destroy event first */
int obj_is(int obj, int target);                 /* obj is target or one of its descendants */
int instance_exists(int obj);
int instance_number(int obj);
Inst* instance_first(int obj);                   /* NULL when there is none */
int place_free(Inst* s, double x, double y);
void move_contact_solid(Inst* s, double dir, double maxd);
int check_collision(Inst* a, Inst* b);

void set_hspeed(Inst* i, double v);
void set_vspeed(Inst* i, double v);
void set_speed(Inst* i, double v);
void set_direction(Inst* i, double v);
void set_hvspeed(Inst* i, double h, double v);

double point_distance(double x1, double y1, double x2, double y2);
double point_direction(double x1, double y1, double x2, double y2);
double lengthdir_x(double l, double d);

void room_goto(int r);
void room_goto_next(void);
void room_goto_previous(void);
int room_first(void), room_last(void);

/* ---- engine entry points used by main */
void gm_init(void);                              /* after assets are loaded */
void gm_start(void);                             /* enters the first room */
void gm_frame(unsigned keys_held, int draw);     /* one 60 Hz step (draw=0 skips drawing); sets G.pending when a room change is due */
extern double gm_prof[4];                        /* ms since last cleared: logic (not filled), static-layer rebuild, cache copy, sprites+text */
void gm_draw_now(void);                          /* draws the current room (the "frame 0" after a room change) */
extern void (*room_code_hook)(void);              /* test hook: runs where the room creation code runs */
void gm_load_pending(void);                      /* performs the room change */
int key_held(int vk);
void gm_set_jump_mode(int press_mode);           /* 1: a jump needs a fresh press of the jump button (default); 0: original, holding keeps jumping */
int gm_jump_allowed(void);                       /* press mode: is there a fresh press for a player character to jump on */
void gm_set_jump_buffer(int frames);             /* how many frames a jump press stays valid while the character is in the air */
void gm_set_ledge(int frames);                   /* ledge forgiveness: a jump still works this many frames after walking off an edge (0 = off) */
int gm_can_jump(Inst* s);                        /* standing on something, or just walked off (ledge forgiveness) */
void gm_jump_done(void);                         /* a character took off: use up the buffered press */
int sprite_frames(int spr);

/* ---- hooks implemented by game.c */
void ev_create(Inst* s);
void ev_destroy(Inst* s);
void ev_alarm(Inst* s, int n);
void ev_step(Inst* s);
void ev_collision(Inst* s, Inst* other, int target);
void ev_key(Inst* s, int kind, int vk);          /* kind: 0 held, 1 pressed, 2 released */
void ev_other(Inst* s, int sub);                 /* 0 outside room, 4 room start, 7 animation end */
void ev_draw(Inst* s);
int has_draw_event(int obj);
void game_reset(void);
void game_set_save(int room, int type);          /* test hook: global.saveroom / global.type */

/* ---- drawing (render.c) */
void r_begin(uint16_t* fb);
void r_clear(uint32_t bgr);
void r_sprite(int spr, double index, double x, double y, double xs, double ys, uint32_t blend, double alpha);
void r_sprite_fog(int spr, double index, double x, double y, double xs, double ys, uint32_t fog, double alpha);
#define FONT_FIXED 100                        /* the game's font 1 with its M, N and W redrawn (see render.c) */
double r_font_width(int font, const char* s, double xs);
enum { FA_LEFT = 0, FA_CENTER = 1, FA_TOP = 0, FA_MIDDLE = 1 };
void r_text(int font, double x, double y, const char* s, double xs, double ys, uint32_t col, int halign, int valign);
void r_target(int to_cache);                 /* 1 = draw into the static-layer cache, 0 = the screen */
void r_blit_cache(void);                         /* copy the cache onto the screen */
void r_message(const char* text);
int r_ctext_width(const char* s);                /* clear text (readable dialogue, pause screen) */
int r_ctext_line(void);
void r_ctext(int x, int y, const char* s, int len, uint32_t col);
void r_box(int x, int y, int w, int h, int border, uint32_t border_bgr, uint32_t fill_bgr);
void r_ctext2x(int x, int y, const char* s, uint32_t col);
int r_ctext_width2x(const char* s);
void r_dim(void);
void game_set_flight_fine(int on);               /* finer flight control for the bee (default): small taps, hold Up to climb, hold Down to sink */
void game_set_flight_params(double acc, double cap, double tap);
void game_set_bunny_boost(int on);               /* higher, floatier bunny hops (default) instead of the original's */
void game_set_text_mode(int mode);               /* dialogue text: 0 clear typeface, 1 the game's own font made legible (default), 2 the original pink bubble text */                /* plain-text notice screen (errors, instructions) */

/* ---- audio (audio.c) */
void snd_init(void);
void sound_play(int id);
void music_load(int xm_index);
void music_play(int xm_index);
void music_stop(void);
void music_stop_if(int xm);                      /* bgm_Stop(handle): only if that track is the one playing */
void music_fade(int ms);
void music_pause(int on);                        /* silence the song without losing its place */
void audio_tick(void);

#endif
