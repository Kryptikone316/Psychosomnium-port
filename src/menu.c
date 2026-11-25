/* Launcher menu (Play Game / Settings / Exit Game) and the settings file. All settings live in gamedata/psychosomnium.cfg; the menu edits them
 * and applies them straight away, so "Play Game" starts with the settings as shown. The first row of Settings is the language: everything
 * below it (and the main menu, the pause and end screens, the dialogue) switches to it at once. */
#include "gm.h"
#include "lang.h"
#include "menu.h"
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------------------------------ settings state */
typedef struct {
    int lang;                /* index into LANG_CODES (0 = English) */
    int text;                /* 0 clear typeface, 1 the game's own font made legible (default), 2 original pink bubble (config file only) */
    int bunny_original;      /* 0 higher hops with hang time, 1 original */
    int flight_original;     /* 0 finer bee control, 1 original flaps */
    int jump_held;           /* 0 a jump needs a fresh press, 1 original: holding keeps jumping */
    int buffer;              /* frames */
    int ledge;               /* frames */
    int debounce;            /* ms */
    int drain;               /* 1 on */
    int family;              /* 1 = family-friendly language */
} Settings;

static const Settings DEFAULTS = { 0, 1, 0, 0, 0, 8, 0, 25, 1, 0 };
static Settings S = { 0, 1, 0, 0, 0, 8, 0, 25, 1, 0 };
static char g_path[1100], g_dir[1100];

static const int BUFFER_CHOICES[] = { 4, 6, 8, 10, 12 };
static const int LEDGE_CHOICES[] = { 0, 2, 4, 6 };
static const int DEBOUNCE_CHOICES[] = { 0, 10, 25, 40 };
#define NCH(a) ((int)(sizeof(a) / sizeof((a)[0])))

static int index_of(const int* a, int n, int v) {          /* the choice closest to v */
    int i, best = 0, bd = 1 << 30;
    for (i = 0; i < n; i++) { int d = abs(a[i] - v); if (d < bd) { bd = d; best = i; } }
    return best;
}

void settings_apply(void) {
    int got = lang_load(g_dir, S.lang);
    if (got != S.lang) S.lang = got;                    /* a language whose pack is missing falls back to English */
    gm_set_jump_mode(!S.jump_held);
    game_set_text_mode(S.text);
    game_set_flight_fine(!S.flight_original);
    game_set_bunny_boost(!S.bunny_original);
    gm_set_jump_buffer(S.buffer);
    gm_set_ledge(S.ledge);
    plat_input_debounce(S.debounce);
    plat_set_drain(S.drain);
    assets_set_family_language(S.family);
}

void settings_save(void) {
    FILE* f;
    if (!g_path[0]) return;
    f = fopen(g_path, "w");
    if (!f) return;
    fprintf(f,
        "# Psychosomnium settings. The Settings screen in the launcher edits this file; you can also edit it by hand (delete it to get the defaults back).\n"
        "# lang: en de es fr it pl pt tr ru ja zh ar (the languages of TreeFrogUI)\n"
        "lang=%s\n"
        "# jump: press = a jump needs a new press of the jump button (precise); held = original, holding the button keeps jumping\n"
        "jump=%s\n"
        "# text: original = the game's own font (the unreadable letters redrawn); clear = a plain larger typeface; bubble = the original pink bubble text\n"
        "text=%s\n"
        "# flight: fine = the bee climbs while Up is held, sinks while Down is held, a tap is a small nudge; original = one big flap per press\n"
        "flight=%s\n"
        "# drain: on = skip a picture now and then when the display has pictures queued up (cuts screen lag); off = never\n"
        "drain=%s\n"
        "# buffer: frames a jump press made just before landing is remembered (8 = about 117 ms before touchdown)\n"
        "buffer=%d\n"
        "# ledge: frames after walking off an edge in which a jump still works (0 = off, original behaviour)\n"
        "ledge=%d\n"
        "# bunnies: high = higher hops with a little hang time at the top (easier); original = the game's own hop\n"
        "bunnies=%s\n"
        "# debounce: ms a button still counts as held after the pad stops reporting it (0 = off)\n"
        "debounce=%d\n"
        "# language style: family = family-friendly (mild words instead of strong language: damn -> darn, hell -> heck); original = cuss-friendly (the game's own words)\n"
        "language=%s\n",
        LANG_CODES[S.lang], S.jump_held ? "held" : "press", S.text == 0 ? "clear" : S.text == 2 ? "bubble" : "original", S.flight_original ? "original" : "fine",
        S.drain ? "on" : "off", S.buffer, S.ledge, S.bunny_original ? "original" : "high", S.debounce, S.family ? "family" : "original");
    fclose(f);
}

void settings_load(const char* dir) {
    char line[128];
    FILE* f;
    snprintf(g_dir, sizeof g_dir, "%s", dir);
    snprintf(g_path, sizeof g_path, "%s/psychosomnium.cfg", dir);
    f = fopen(g_path, "r");
    if (f) {
        while (fgets(line, sizeof line, f)) {
            char k[32], v[32];
            if (line[0] == '#' || sscanf(line, " %31[a-z]=%31s", k, v) != 2) continue;
            if (!strcmp(k, "jump")) S.jump_held = !strcmp(v, "held");
            else if (!strcmp(k, "text")) S.text = !strcmp(v, "clear") ? 0 : !strcmp(v, "bubble") ? 2 : 1;
            else if (!strcmp(k, "flight")) S.flight_original = !strcmp(v, "original");
            else if (!strcmp(k, "bunnies")) S.bunny_original = !strcmp(v, "original");
            else if (!strcmp(k, "drain")) S.drain = strcmp(v, "off") != 0;
            else if (!strcmp(k, "buffer")) S.buffer = BUFFER_CHOICES[index_of(BUFFER_CHOICES, NCH(BUFFER_CHOICES), atoi(v))];
            else if (!strcmp(k, "ledge")) S.ledge = LEDGE_CHOICES[index_of(LEDGE_CHOICES, NCH(LEDGE_CHOICES), atoi(v))];
            else if (!strcmp(k, "debounce")) S.debounce = DEBOUNCE_CHOICES[index_of(DEBOUNCE_CHOICES, NCH(DEBOUNCE_CHOICES), atoi(v))];
            else if (!strcmp(k, "language")) S.family = !strcmp(v, "family");
            else if (!strcmp(k, "lang")) { int i; for (i = 0; i < N_LANGS; i++) if (!strcmp(v, LANG_CODES[i])) S.lang = i; }
        }
        fclose(f);
    } else settings_save();                              /* first start: write the defaults */
    settings_apply();
}

/* ------------------------------------------------------------------------------------------------------ menu */
#define COL_BG gm_rgb(22, 32, 42)
#define COL_PANEL gm_rgb(250, 246, 234)
#define COL_INK gm_rgb(38, 56, 70)
#define COL_PINK gm_rgb(252, 200, 200)
#define COL_DIM gm_rgb(150, 170, 185)
#define COL_EDGE C_SHELDON

enum { R_LANG, R_STYLE, R_TEXT, R_BUNNY, R_FLIGHT, R_JUMP, R_BUFFER, R_LEDGE, R_DEBOUNCE, R_DRAIN, R_RESET, R_BACK, N_ROWS };
static const int ROW_LABEL[N_ROWS] = { L_ROW_LANG, L_ROW_STYLE, L_ROW_TEXT, L_ROW_BUNNY, L_ROW_FLIGHT, L_ROW_JUMP, L_ROW_BUFFER, L_ROW_LEDGE, L_ROW_DEBOUNCE, L_ROW_DRAIN, L_ROW_RESET, L_ROW_BACK };
static const int ROW_DESC[N_ROWS] = { L_D_LANG, L_D_STYLE, L_D_TEXT, L_D_BUNNY, L_D_FLIGHT, L_D_JUMP, L_D_BUFFER, L_D_LEDGE, L_D_DEBOUNCE, L_D_DRAIN, L_D_RESET, L_D_BACK };

static void row_value(int r, char* out, size_t n) {
    switch (r) {
        case R_LANG: out[0] = 0; break;                                                  /* drawn from the names atlas */
        case R_STYLE: snprintf(out, n, "%s", ui(S.family ? L_V_FAMILY : L_V_CUSS)); break;
        case R_TEXT: snprintf(out, n, "%s", ui(S.text == 0 ? L_V_FONT_CLEAR : L_V_FONT_ORIG)); break;
        case R_BUNNY: snprintf(out, n, "%s", ui(S.bunny_original ? L_V_ORIGINAL : L_V_HIGHER)); break;
        case R_FLIGHT: snprintf(out, n, "%s", ui(S.flight_original ? L_V_ORIGINAL : L_V_FINE)); break;
        case R_JUMP: snprintf(out, n, "%s", ui(S.jump_held ? L_V_HOLD : L_V_PRESS)); break;
        case R_BUFFER: snprintf(out, n, ui(L_V_FRAMES), S.buffer); break;
        case R_LEDGE: if (S.ledge) snprintf(out, n, ui(L_V_FRAMES), S.ledge); else snprintf(out, n, "%s", ui(L_V_OFF)); break;
        case R_DEBOUNCE: if (S.debounce) snprintf(out, n, ui(L_V_MS), S.debounce); else snprintf(out, n, "%s", ui(L_V_OFF)); break;
        case R_DRAIN: snprintf(out, n, "%s", ui(S.drain ? L_V_ON : L_V_OFF)); break;
        default: out[0] = 0; break;
    }
}

static void cycle(int* v, const int* choices, int n, int dir) {
    int i = index_of(choices, n, *v);
    i = (i + dir + n) % n;
    *v = choices[i];
}

static void row_change(int r, int dir) {
    switch (r) {
        case R_LANG: S.lang = (S.lang + dir + N_LANGS) % N_LANGS; break;
        case R_STYLE: S.family = !S.family; break;
        case R_TEXT: S.text = S.text == 1 ? 0 : 1; break;
        case R_BUNNY: S.bunny_original = !S.bunny_original; break;
        case R_FLIGHT: S.flight_original = !S.flight_original; break;
        case R_JUMP: S.jump_held = !S.jump_held; break;
        case R_BUFFER: cycle(&S.buffer, BUFFER_CHOICES, NCH(BUFFER_CHOICES), dir); break;
        case R_LEDGE: cycle(&S.ledge, LEDGE_CHOICES, NCH(LEDGE_CHOICES), dir); break;
        case R_DEBOUNCE: cycle(&S.debounce, DEBOUNCE_CHOICES, NCH(DEBOUNCE_CHOICES), dir); break;
        case R_DRAIN: S.drain = !S.drain; break;
        case R_RESET: { int keep = S.lang; S = DEFAULTS; S.lang = keep; break; }        /* the language stays: you could not read the menu otherwise */
        default: break;
    }
    settings_apply();
    settings_save();
}

static void title(const char* sub) {
    r_ctext2x((SCR_W - r_ctext_width2x("PSYCHOSOMNIUM")) / 2, 18, "PSYCHOSOMNIUM", COL_PINK);
    if (sub) r_ctext((SCR_W - r_ctext_width(sub)) / 2, 100, sub, 99, COL_DIM);
}

static void footer(const char* s) { r_ctext((SCR_W - r_ctext_width(s)) / 2, 448, s, 99, COL_DIM); }

/* the description under the list, centred, at most three lines of w pixels (spaces break lines; Japanese/Chinese anywhere) */
static void desc_lines(const char* s, int y, int w) {
    uint32_t cp[400], vis[400];
    int n = u8_decode(s, cp, 400), start = 0, row = 0;
    while (start < n && row < 3) {
        int e = start, last = -1;
        while (e < n) {
            if (cp[e] == ' ') last = e;
            if (r_cp_width(cp + start, e - start + 1) > w && e > start) { if (!lang_cjk() && last > start) e = last; break; }
            e++;
        }
        r_visual(cp + start, e - start, vis);
        r_cp_draw((SCR_W - r_cp_width(vis, e - start)) / 2, y + row * r_ctext_line(), vis, e - start, COL_DIM);
        start = e + (e < n && cp[e] == ' ' ? 1 : 0);
        row++;
    }
}

static void draw_main(int sel) {
    static const int items[3] = { L_PLAY, L_SETTINGS, L_EXIT };
    int i;
    r_target(0);
    r_clear(COL_BG);
    title(ui(L_SUBTITLE));
    for (i = 0; i < 3; i++) {
        int w = 320, h = 46, x = (SCR_W - w) / 2, y = 170 + i * 66;
        if (i == sel) r_box(x, y, w, h, 3, COL_EDGE, COL_PANEL);
        else r_box(x, y, w, h, 2, gm_rgb(54, 76, 94), gm_rgb(32, 46, 60));
        r_ctext((SCR_W - r_ctext_width(ui(items[i]))) / 2, y + 11, ui(items[i]), 99, i == sel ? COL_INK : COL_DIM);
    }
    footer(ui(L_FOOT_MAIN));
}

static void draw_settings(int sel, int top) {
    enum { VIS = 6, ROWH = 34 };
    int i;
    char val[64];
    r_target(0);
    r_clear(COL_BG);
    title(ui(L_SETTINGS));
    for (i = 0; i < VIS && top + i < N_ROWS; i++) {
        int r = top + i, y = 134 + i * ROWH, x = 50, w = SCR_W - 100;
        if (r == sel) r_box(x, y, w, 32, 3, COL_EDGE, COL_PANEL);
        r_ctext(x + 16, y + 4, ui(ROW_LABEL[r]), 99, r == sel ? COL_INK : COL_DIM);
        if (r == R_LANG) {                                                          /* the language in its own script */
            int nw = r_name_width(S.lang), arrows = r == sel, xr = x + w - 16, nx = xr - nw - (arrows ? 26 : 0);
            r_name_draw(S.lang, nx, y + 4, r == sel ? COL_INK : COL_PINK);
            if (arrows) { r_ctext(nx - 22, y + 4, "<", 99, COL_INK); r_ctext(xr - 12, y + 4, ">", 99, COL_INK); }
            continue;
        }
        row_value(r, val, sizeof val);
        if (val[0]) {
            char buf[96];
            snprintf(buf, sizeof buf, r == sel ? "< %s >" : "%s", val);
            r_ctext(x + w - 16 - r_ctext_width(buf), y + 4, buf, 99, r == sel ? COL_INK : COL_PINK);
        }
    }
    if (top > 0) r_ctext(SCR_W / 2 - 6, 118, "^", 99, COL_DIM);
    if (top + VIS < N_ROWS) r_ctext(SCR_W / 2 - 6, 134 + VIS * ROWH - 4, "v", 99, COL_DIM);
    desc_lines(ui(sel == R_LANG && S.lang != 0 && !lang_has_script() ? L_D_LANG_NOSCRIPT : ROW_DESC[sel]), 362, SCR_W - 120);
    footer(ui(L_FOOT_SET));
}

int menu_run(void) {
    int sel = 0, screen = 0, ssel = 0, stop = 0;
    unsigned prev = plat_buttons() | B_A | B_START | B_B;        /* a button still held from the launcher or the pause does nothing */
    for (;;) {
        unsigned b = plat_buttons(), pressed = b & ~prev;
        prev = b;
        if ((b & B_SELECT) && (b & B_START)) return 0;
        if (screen == 0) {
            if (pressed & B_UP) { sel = (sel + 2) % 3; sound_play(SND_STEP); }
            if (pressed & B_DOWN) { sel = (sel + 1) % 3; sound_play(SND_STEP); }
            if (pressed & (B_A | B_START)) {
                sound_play(SND_SELECT);
                if (sel == 0) return 1;
                if (sel == 1) { screen = 1; ssel = 0; stop = 0; }
                else return 0;
            }
            if (pressed & B_B) { sel = 2; }
            draw_main(sel);
        } else {
            if (pressed & B_UP) { ssel = (ssel + N_ROWS - 1) % N_ROWS; sound_play(SND_STEP); }
            if (pressed & B_DOWN) { ssel = (ssel + 1) % N_ROWS; sound_play(SND_STEP); }
            if (pressed & B_LEFT) { row_change(ssel, -1); sound_play(SND_STEP); }
            if (pressed & B_RIGHT) { row_change(ssel, 1); sound_play(SND_STEP); }
            if (pressed & (B_A | B_START)) {
                if (ssel == R_BACK) { screen = 0; sound_play(SND_SELECT); }
                else { row_change(ssel, 1); sound_play(SND_STEP); }
            }
            if (pressed & B_B) { screen = 0; sound_play(SND_SELECT); }
            if (ssel < stop) stop = ssel;
            if (ssel >= stop + 6) stop = ssel - 5;
            draw_settings(ssel, stop);
        }
        plat_present();
    }
}
