/* Languages: UI strings and glyphs from lang/<code>.pak, the game's dialogue from lang/<code>.script (optional). English is built in (UI) and
 * comes from the player's psycho.exe (dialogue). */
#ifndef LANG_H
#define LANG_H

#include <stdint.h>
#include "lang_ids.h"

typedef struct { uint32_t cp; uint16_t x, y; uint8_t w, h; int8_t bx, ty; uint8_t adv, pad; } LGlyph;
typedef struct { int n; const LGlyph* g; const unsigned char* atlas; int aw, ah; int ascent, line; } LFont;

const char* ui(int id);                             /* UI string in the active language */
int lang_load(const char* dir, int idx);            /* 0 = English; returns the language now in effect (falls back to 0 when its pack is missing) */
int lang_index(void);
int lang_rtl(void);
int lang_cjk(void);
int lang_has_script(void);                          /* the active language's dialogue translation is installed */
const char* lang_script(int obj, int ev, int sub, int k, int family);   /* the active language's dialogue line, or NULL */

/* render.c */
void r_set_font(const LFont* f, int rtl);
int r_is_rtl(void);
int u8_decode(const char* s, uint32_t* out, int max);
int r_cp_width(const uint32_t* cp, int n);
void r_visual(const uint32_t* in, int n, uint32_t* out);
void r_cp_draw(int x, int y, const uint32_t* cp, int n, uint32_t col);
int r_name_width(int idx);
void r_name_draw(int idx, int x, int y, uint32_t col);

#endif
