# Psychosomnium for the Datafrog SF3500

A port of **Psychosomnium** (Cactus, 2008, a Game Maker 7 freeware game) to the Datafrog SF3500 handheld (MIPS32r2, hard float). It is a small
engine that re-implements the Game Maker behaviours the game uses; **it contains nothing from the game**. You supply your own copy of the game,
and the port unpacks everything it needs from it on the first start.

## Using it
1. Get the game: `psycho.exe` (2,658,887 bytes) or `psycho1.zip` from its author's page (a free download).
2. Put it in the port's `gamedata` folder.
3. Start the port. The first start converts the game's data into `gamedata/cache/psycho.pak` (a few seconds); the dialogue, graphics, sounds and
   music are all read from your copy.

The launcher has Play Game / Settings / Exit Game. Settings has the language (the 12 languages of TreeFrogUI: the menus are translated in this
repository; the game's dialogue translations are a separate optional download, see the release notes), readability and control tweaks, and a
family-friendly language option. See `package-src/README.txt` for the controls and the settings file.

## Building
* Desktop (development, tests): `./build_host.sh` (needs SDL2).
* SF3500: `./build_sf3500.sh` (the Codescape `mips-mti-linux-gnu` toolchain with an SDL2 sysroot); `./package.sh` assembles the card folder.
* Language packs: `tools/lang/build_packs.py` (menu translations in `tools/lang/tr`, Python with Pillow; Noto Sans CJK is needed for ja/zh).
* `DEV-NOTES.md` explains how it works and how it was checked against the original running under Wine (`tools/wine`); `docs/EXTRACTION.md`
  documents the exe format findings.

## Not included
Any artwork, sound, music, text or level data of the game, and the game's dialogue in other languages (that is a derivative of the original script and
is offered separately, as an optional download, with credit to the original authors). Third-party code and fonts are listed in `package-src/NOTICE.md`.

## Credits
Psychosomnium: Cactus (game), Zabutom (music), Cow (splash screen). Music playback: jar_xm (WTFPL). zlib decoding: stb_image. Fonts: DejaVu, Noto Sans CJK.

## Licence
The port's own code and tools: MIT (see `LICENSE`). Third-party parts keep their own licences (`package-src/NOTICE.md`). The game is not part of this repository.
