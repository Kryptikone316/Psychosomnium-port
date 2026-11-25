# Psychosomnium: extraction notes (2026-11-24)

Source: `Installer/psycho.exe` (2008-01-05, 2.6 MB, same file as psycho1.zip).
It is a **Game Maker 7.0** exe (Delphi runner, D3D8). Game logic is GML interpreted by the runner;
there is no native game code to convert. Target: C/C++ + SDL2 for MIPS (see port plan below).

## Reproduce

    ~/venvs/pydev/bin/python -I tools/extract.py Installer/psycho.exe extracted

`tools/gm7.py` = stream reader (swap-table decrypt). `tools/extract.py` = full extractor.
Reference for the format: LateralGM `GmFileReader.java` (GM 6.1/7.0 "701" flavour); the exe variant differs, see below.

## Exe layout (GM 7.0, verified on this file)

Data header = last `1234321, 700` in the file (0x1E3660 here); everything before is the Delphi runner.

1. Plain ints: `1234321, 700, <0>, 702(settings ver)` then 24 settings ints
   (fullscreen=1, noborder=1, scaling=100, res idx 2 = 640x480, Esc ends game, ...).
2. Loading image: int length + zlib BMP (`loading.bmp`).
3. A few more ints (error flags), then one constant: `c_sheldon = make_color_rgb(60,87,105)`.
4. Embedded `D3DX8.dll`: name, int length, zlib. (Windows-only, ignore.)
5. Resource block: int length + zlib to EOF. Inside: `s1, s2, skip s1 ints, seed, skip s2 ints`,
   then everything after is **swap-table scrambled** (table from `seed`, byte = table[b] - pos, pos = offset
   from the start of this block; first game-id byte is plain). Then game id, 16-byte GUID, one junk int,
   `700, 0` (no extension packages).
6. Resources in order: sounds(400) sprites(400) backgrounds(400) paths(420) scripts(400) fonts(540)
   timelines(500) objects(400) rooms(420), last instance/tile ids, included files, game info.

Differences from the `.gmk` layout LateralGM reads (the exe drops editor-only data):
- Sprite subimage = `540, exists, w, h, len, zlib(raw BGRA)` (alpha byte unused; transparent colour key
  = **bottom-left pixel**).
- Fonts carry baked glyph data after the header: 256 x (x, y, w, h, shift, offset) ints, then atlas
  `w, h, len, zlib(8-bit alpha)`. We can reuse the exact atlases (no dependence on Small Fonts / MS Gothic).
- Room: no snap/isometric/editor tail; instance and tile records have no `locked` flag
  (verified: last-id ints 101410 / 10000000 land right).
- Included files and game info text are zlib-compressed. No packages section.

## What is in it

- 11 sounds (16-bit mono 44.1 kHz WAV: jump, step, die, textsnd, change, expell, spikefall, select, punch, magicsnd, gunshot)
- 32 sprites (113 frames), 0 backgrounds, 0 paths, 0 timelines
- 28 objects (109 actions, about 19 KB of GML in code actions) + 40 scripts (about 9 KB; 35 of them
  are the `bgm_*` BASS wrapper stubs, real ones: `draw_snap`, `do_nothing`, `restart`)
- 23 rooms (all 640x480, speed 60), 3 fonts (font0 Small Fonts 5pt bold, font1/font2 MS Gothic)
- Music = 3 tiny **XM tracker modules** (1 to 19 KB each: lost_behind_a_chimney, little_nintendofairy, nintendo_heroes_)
  played through bass.dll/bgm.dll. On MIPS use libxmp or SDL_mixer with an XM-capable backend; the DLLs are unneeded.
- `c_sheldon` is the shadow/fog colour (RGB 60,87,105); the game draws a drop shadow with `d3d_set_fog`.

## Port plan (proposed, not started)

Game is small (platformer, 23 single-screen rooms, `place_free`/`move_contact_solid`/`instance_create`/`with`,
`draw_sprite_ext`, `draw_text_transformed`, `keyboard_check_direct`). Two options:
A) hand-written C engine with GML semantics for the handful of builtins used, objects ported by hand;
B) generate C from the extracted GML. At ~29 KB of code, A is likely simpler and easier to tune on the device.
Open questions before starting: exact collision model (precise masks vs bbox per sprite), room/instance
creation order, draw order/depth, step event order, `alarm`, `persistent`, `global.*` usage, and how
`draw_snap` works.
## Licence / packaging (checked 2026-11-24)

caiman.us lists it as "Freeware, 2008, by Cactus" (platform/puzzle, Z + arrows). No licence text anywhere:
the page only says freeware, the author's old site (cactus-soft.co.nr) is gone (Wayback copy has no terms),
and the 64digits download page errors out. "Freeware" is a distribution category, not a redistribution grant,
and the exe also embeds third-party tracker music (paths `E:\Games\wintermusic\Misc\...`) of unknown origin.

Decision: **user supplies psycho.exe** (same model as SkiFree). Ship only our engine + a converter that reads
the player's exe; do not publish `extracted/` (keep it local, not in any repo/release). Prefer an in-engine
converter in C (first run parses the exe, caches sprites/sounds/rooms next to the port) since the device has no
Python; `tools/extract.py` stays the reference. Revisit if the author (Cactus) grants permission to bundle.
