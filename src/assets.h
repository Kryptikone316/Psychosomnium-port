/* Game data as the engine uses it. Produced from the player's psycho.exe by unpack.c and cached in
 * gamedata/cache/psycho.pak; nothing from the original game ships with the port. */
#ifndef ASSETS_H
#define ASSETS_H

#include <stdint.h>

#define N_SPRITES 33
#define N_SOUNDS 11
#define N_FONTS 3
#define N_ROOMS 23
#define N_XM 3
#define MAX_FRAMES 24

typedef struct {
    int valid, w, h, nframes, ox, oy;
    int bl, br, bb, bt;             /* stored bounding box (left,right,bottom,top), inclusive */
    uint32_t* frame[MAX_FRAMES];    /* 0xAARRGGBB, A is 0 for the colour-key pixels and 255 otherwise */
    uint8_t* mask;                  /* w*h, opaque pixels of the first subimage (the sprite's one shared "precise" mask) */
} Sprite;

typedef struct { int x, y, w, h, shift, off; } Glyph;
typedef struct {
    int valid, aw, ah, lineh;
    uint8_t* alpha;                 /* aw*ah coverage atlas */
    Glyph g[256];
} Font;

typedef struct { int id, obj, x, y; double xs, ys; } RInst;
typedef struct { int valid, w, h, speed, persistent; uint32_t bg; int n; RInst* inst; } Room;
typedef struct { uint8_t* data; int len; } Blob;
typedef struct { int obj, ev, sub, k; char* s; } Text;   /* string literal #k of an object's event code (dialogue etc.) */

typedef struct {
    Sprite spr[N_SPRITES];
    Font fnt[N_FONTS];
    Room room[N_ROOMS];
    Blob wav[N_SOUNDS];
    Blob xm[N_XM];                  /* nintendo_heroes_, lost_behind_a_chimney, little_nintendofairy */
    int order[N_ROOMS];             /* room execution order (room_goto_next walks this) */
    int last_inst_id;
    Text* text; int ntext;
} Assets;

extern Assets A;
const char* game_text(int obj, int ev, int sub, int k);   /* text from the player's exe; "" if missing */
void assets_set_family_language(int on);                   /* swap strong language (damn, hell, ...) for mild words as text is looked up */

/* Makes sure the assets are loaded. dir = the port's gamedata folder (psycho.exe or psycho1.zip goes in
 * there; the converted cache is kept in dir/cache). Returns 0 on success, else err describes the problem. */
int assets_load(const char* dir, char* err, int errlen);

uint8_t* zip_find(const uint8_t* z, long n, const char* base, long* outlen);   /* one entry of a zip by file name (malloc'd), NULL if absent */

/* unpack.c internals, exposed for the tools/tests */
int unpack_exe(const uint8_t* exe, long len, Assets* out, char* err, int errlen);

#endif
