/* Psychosomnium for the SF3500: bring-your-own-data port. Put psycho.exe (or psycho1.zip) into gamedata/;
 * the first start unpacks it into gamedata/cache/. */
#include "gm.h"
#include "lang.h"
#include "menu.h"
#include "plat.h"
#include "testhooks.h"
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
#ifdef PSY_TEST
    th_setup();
#endif
    settings_load(dir);
#ifdef PSY_TEST
    th_after_settings();
#endif
#ifdef PSY_TEST
    if (th_menu_wanted())
#endif
    {                                                      /* the launcher: Play Game / Settings / Exit Game */
        if (!menu_run()) plat_quit(0);
    }
    gm_start();
    gm_draw_now();
    plat_present();

    for (;;) {
        static int paused, start_prev;
        int st;
        b = plat_buttons();
#ifdef PSY_TEST
        b = th_pilot(b);
#endif
        if (plat_quit_combo(b)) plat_quit(0);
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
            if (G.pending) {
                gm_load_pending();
#ifdef PSY_TEST
                th_room_entered();
#endif
                gm_draw_now(); draw = 1;
            }
            if (G.room == END_ROOM && draw) { const char* h = ui(L_END_EXIT); r_target(0); r_ctext((SCR_W - r_ctext_width(h)) / 2, 430, h, 99, gm_rgb(150, 230, 150)); }
            plat_perf_work(plat_now_ms() - w0, gm_prof);
            gm_prof[1] = gm_prof[2] = gm_prof[3] = 0;
            if (!draw) { plat_skip_frame(); continue; }
        }
#ifdef PSY_TEST
        th_frame();
#endif
        plat_present();
    }
}
