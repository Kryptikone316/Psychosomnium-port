# Psychosomnium (SF3500) - developer notes

Port of Psychosomnium (Cactus, 2008, freeware, no stated redistribution terms) as a **bring-your-own-data** engine:
the player drops `psycho.exe` (or `psycho1.zip`) into `gamedata/`; the first start unpacks it to `gamedata/cache/psycho.pak`.
Nothing from the game ships. Dialogue is NOT in the source: `unpack.c` stores every string literal of each object's code
(`game_text(obj, event, sub, k)`), `game.c` only refers to them by index.

## Layout
- `src/unpack.c`   exe -> assets.  Port of `tools/extract.py` (format notes in `docs/EXTRACTION.md`). Validates size 2658887 and CRC32 0x50536bf9.
- `src/gm.c/.h`    mini Game Maker 7 runtime: instances, speed/direction/gravity/friction, precise collision (bbox + union pixel masks,
                   scaled), event order, room change + persistence (persistent instances, persistent room 13), static-layer cache.
- `src/game.c`     the 28 objects' events, hand-ported statement by statement from the original GML (see below).
- `src/render.c`   software blitter (scale/flip/blend/fog silhouettes), glyph-atlas text. `src/audio.c` sounds (48 kHz stereo) + XM music (jar_xm).
- `src/plat*.{c,h}`, `plat/`  shared SF3500 glue (hwdisp, joykey, sf3500_mixer/hwaudio) + desktop SDL2 backend. 60 fps pacing is fractional (16.667 ms)
  and `plat_behind()` drops *drawing* (never logic) when the loop is late.

## GML semantics that matter (checked against OpenGMK's behaviour; code is ours)
- Frame: alarms -> keyboard(held) -> key press -> key release -> step -> motion -> outside-room -> collisions -> draw -> advance image_index (+ animation-end).
- Events are processed object by object (ascending object id), instance by instance; instances created during iteration are visited.
- Collision with a solid: both instances go back to xprevious/yprevious, event runs, speeds are re-applied, moved back if still colliding.
- Real comparisons use epsilon 1e-13 (`gm_eq/lt/gt`); `floor()` is exact; `round()` is banker's; `speed/direction` setters use the 0.0001 fudge.
- Events are skipped while a room change is pending (and the rest of that frame is skipped incl. drawing); destroy events do not run on room change.
- room_goto_next/previous/first/last follow the **room order list** (22,1,2,0,3,...,20), not room indices.
- GML quirk kept: `if cond a=1 b=2` - only the first statement is conditional.
- bgcontrol/bgthing (floating background dots) are never placed in a room, so they are not implemented.

## Test hooks (env, all builds)
`PSY_HEADLESS=1` no window/driver; `PSY_SHOTS=dir` + `PSY_SHOT_FRAMES=2,200` or `PSY_SHOT_EVERY=n` BMP frames (RGB565, convert to PNG for viewing);
`PSY_JOY="frame:mask,..."` scripted pad (bits: LEFT 1, RIGHT 2, UP 4, DOWN 8, A 16, B 32, L1 64, R1 128, X 256, Y 512, START 1024, SELECT 2048);
`PSY_FUZZ=<seed>` random pad (presses START at frame 10); `PSY_MAXFRAMES=n`; `PSY_TESTROOM=<room>`, `PSY_SPAWNS="obj,x,y;..."`, `PSY_SAVE="room,type"`; `PSY_TRACE=<n>` state line every n frames;
`PSY_GAMEDATA=<dir>`. `./build_host.sh` (SAN=1 for ASan/UBSan), `./build_sf3500.sh`, `./package.sh`. QEMU: `qemu-mipsel-static -L <sysroot> ...` with `SDL_VIDEODRIVER=dummy`.
`tools/xmdump.c` renders the three XM modules to WAV (level/NaN check).

## Verified (2026-11-24)
Host and QEMU-MIPS: identical unpacked cache (byte-identical pak) and identical frames (after RGB565 quantisation; the BMP writers of the two SDLs differ by 1 level).
All 23 rooms render; scripted run: title -> room 1 -> pump spawns the player -> butterfly dialogue -> room 2 -> fall -> death -> restart of room 1 -> respawn.
ASan/UBSan clean over 12000 random frames. zip input, missing exe and wrong exe paths tested.

**Against the original, running under Wine (tools/wine/):** pixel-identical frames for the title, room 1 at rest, walking start/stop, falling off the
ledge and landing (122+ consecutive frames), and three jumps (600/150/300 ms key holds -> 0 differing pixels over 50 frames each, incl. the
variable-height cut on release), the butterfly and dialogue-bubble animations, and the dialogue typing cadence (3,5,3,5 frames per letter).
Bugs found this way and fixed: transparency is the pixel data's alpha byte (not a colour key); the shown sprite frame is image_index rounded half-to-even;
sprite animation advances at the START of the frame (before alarms); the castle is a child of `player`; a room-placed persistent-type instance runs Create.
Third round: all 23 rooms checked (static layouts + the logic rooms), credits colours exact (hue formula), see tools/wine/README.md.
Second round (patched exes starting in other rooms, sequence comparison, see tools/wine/README.md): bunnies/big guy/Shirley/gunman/Tom/castle ending/spikes/bee all match. Not exactly comparable: anything right after a room load or when a sound starts (Wine stalls for a few frames).

## Not verified / open
- Device test (display pacing at 60 fps, audio levels, XM music on the real mixer, pad feel). FPU quirk of the device (see sf3500-fpu-flakiness) untested here.
- Not compared against the original: room 13's persistence on re-entry, and a continuous playthrough of the real route (each room and each character was checked separately, see tools/wine/README.md).
- Port code licence not decided yet. Original download page: caiman.us (freeware, no terms); author unreachable.

## Device feedback round (2026-11-24, after the first device test)
- **Jump feel / double jumps.** The original jumps on every step Up is down and the character stands on something, so a hold that runs a bit long, or contact
  chatter, rebounds at once. New default `jump=press` (psychosomnium.cfg): the held-Up event only fires within 6 frames of a fresh press (`up_arm` in gm.c,
  cleared by `gm_jump_done()` when a character takes off); the release cut (`vspeed*0.25`) is unchanged. `jump=held` restores the original.
- **Input latency.** Device build runs a pad thread (plat_sdl.c) polling the shared pad word at ~1 kHz: taps shorter than a frame are latched, a button counts
  as held for `debounce` ms (default 20) after it was last seen, and gaps under 60 ms between two presses are logged to log.txt ("pad: button N released for only X ms")
  so the next device test shows what the hardware really does. Display/driver latency itself is not changed.
- **Pause.** START outside the first (title) room toggles pause: screen dimmed with a PAUSED box, `music_pause()` outputs silence without advancing the module. SELECT+START still quits.
- **Readable text.** `text=clear` (default): taller light box with dark DejaVu Sans Condensed Bold 20 px (baked by tools/bake_font.py into src/clearfont.h), mixed case,
  word-wrapped ('#' treated as space; 43 messages -> max 3 lines), same typing speed/sounds. `text=original` keeps the pink bubble text.

## Device log findings and bunny rule (2026-11-24, second device test)
- The pad word on the device only changes in 20 ms steps (gaps in the "pad:" log lines are 20/40/60 ms +-1), so input has 0-20 ms of quantisation on top of the 60 Hz frame sampling.
  Single-sample dropouts (a 19-21 ms release in the middle of a long hold) do occur; hence `debounce=25` (swallows one missed sample, not two).
- Press mode applies to the PLAYER characters only (`gm_jump_allowed()`); bunnies keep the original rule (hop whenever grounded while Up is down), because the bunny room is
  timed around it: tap Up -> bunnies take full hops, the player's release-cut hop is short, so the player can run under them.
- **Bunny room ease (user request, 2026-11-24):** `bunnies=high` (default): hop speed -8.5 (rise ~97 px vs ~65) and gravity 0.2 while |vspeed|<1.5 (hang near the top); airtime 50 frames vs 35.
  `bunnies=original` restores the game's own -7 / 0.4. Deliberate deviation from the original, for the device's pad delay.
- **Ledge forgiveness (2026-11-24):** `ledge=0`: `Inst.coyote` counts frames since a player character (P1/P2/P3/P5) last stood on something; `gm_can_jump()` accepts a take-off up to 4 frames after
  leaving an edge while vspeed < 2; a take-off sets coyote to 99 (no double jump). Tested: with the player walking off a step, presses 0-3 frames after leaving jump, 4+ do not; ledge=0 = original.
- **Timing log + jump buffer 8 (2026-11-24):** plat_sdl.c writes a `perf:` line to log.txt every 600 frames and at exit (frame avg/max, late >20 ms, skipped pictures, present time, game work, slept = headroom);
  PSY_PERF=1 forces it in headless runs. `buffer=` (default 8, was 6): measured with a held press before touchdown, buffer 6 accepted presses up to 4 frames early, 8 up to 6, 10 up to 8 (the press frame counts as one).
- **Finer bee flight (2026-11-24, user: the spiked corridor, room 17 in play order = internal 16, is likely impossible):** the corridor leaves only ~28 px of free height (origin y 246..270, spikes above and below, 320 px long);
  the original flap (-2 per fresh press, gravity 0.1) swings ~20 px, so every flap has to be timed to a few frames, and a pad dropout (extra press) kills. `flight=fine` (default): a press is -1, holding Up adds
  -0.15/frame (limit -1.5), holding Down adds +0.15/frame (limit +1.5; D-pad Down is new in flight). Tested with PSY_BEEBOT="delay,target,mode" (closed-loop pilot with `delay` frames of reaction lag; PSY_FLT="acc,cap,tap"
  tunes it): original flight passes with a zero-lag pilot but fails at 3+ frames of lag in every case tried; fine (0.15,1.5,1) passes 5/5 targets at 3 frames and 4/5 at 6 frames (acc 0.25/cap 2: 5/5 and 2/5).
- **Collision masks (2026-11-24, user: the bee's/spikes' hit-boxes seem too large in the corridor):** a sprite's collision mask is now the opaque pixels of its FIRST subimage only (PAK_VERSION 6). Before, it was the union of all
  subimages: the spike sprites have two frames (frame 0 = a strip at the bottom of the tile, frame 1 = a strip at the top, never used in play: image_index stays 0 and the Create event flips them with yscale -2),
  so every spike tile carried a ghost hazard strip 24 px away from the visible spikes. In the corridor this shrank the free height from ~110 px to ~62 px (bee survivable y 246..270 = 28 px of slack instead of
  218..296). Checked against the original under Wine (room 16 patched, bee spawned at y=240: it survives until it falls onto the floor spikes; the union build died on frame 3). Hooks: PSY_MASKS, PSY_MASKDIFF, PSY_COLLDBG, PSY_COLLDBG2.
  The earlier all-rooms comparisons were made with the union mask; spikes met from the side/top with a falling player were not part of them.
- **Performance (from the device log, 2026-11-24):** perf lines showed game work 11-13 ms + present 4-5 ms of a 16.7 ms frame (zero headroom, 10-30% of frames late, 40-200 pictures skipped per 10 s). Cause on the logic side: every
  frame copied the whole 614 KB static-layer cache into the screen. Now render.c restores only the rectangles drawn on last frame (`mark_dirty`, 96 rects, full copy after a cache rebuild/dim/clear/notice; PSY_FULLCOPY=1
  forces the old path; verified identical frames over 4 x 4000 fuzz frames with PSY_SEED fixed). Host cost per frame fell from ~0.8 ms to ~0.07 ms. The perf line now splits the work into rebuild / cache copy / sprites+text / logic.
- **Display queue lag (2026-11-24, device log after the dirty-rect build):** game work fell to ~2 ms but present rose to 10-14 ms (and "goto reset ge, but ge busy, retry" appears in the log): the pictures now always
  reach the driver and its queue stays full, each queued picture being a frame of lag (before, 10-30% of pictures were dropped because the frame ran late, which happened to drain it). `drain=on`: when a present takes > 4 ms
  longer than the quickest of the last 120, the next picture is skipped (logic keeps its 60 Hz and its sleep), at most one per 4 frames. perf lines now also give the sleep overshoot ("woke late") and the drain count.
  Not yet verified on the device.
- **End screen (2026-11-24):** the credits object (obj 27) only draws the cycling-colour text; the original has no key events there (its window closes with Esc/the title bar). On the device START now exits from room 20 (END_ROOM), with a "Press START to exit" line at the bottom.
- **Launcher + settings (2026-11-24):** src/menu.c: on the device the program opens a menu (Play Game / Settings / Exit Game; headless tests skip it unless PSY_MENU=1). Settings = every tweak (dialogue text, bunny hops, bee flight, jump press/hold,
  jump buffer, ledge grace, button smoothing, screen lag fix, language) with Left/Right, saved to gamedata/psychosomnium.cfg at once (menu.c owns the cfg now; main.c no longer parses it) and applied live.
  Language: `assets_set_family_language()` filters game_text() lookups (whole words, case preserving: damn->darn, hell->heck, plus a table of stronger words the game does not use) - the cached text stays original.
  The game only contains Damn (x3) and hell (x2).

## Languages, the game's own font, the Language row (2026-11-24)
- **Languages** = the 12 of TreeFrogUI (`TreeFrogUI_v1.9.0_f/release/frogui/lang/builtin`: en de es fr it ja pl pt_BR ru tr zh_CN ar): English is built in (menus) / read from the exe (dialogue); the other 11 come from `lang/<code>.pak`
  (menu strings + baked 20 px glyphs: DejaVu Sans Condensed Bold for Latin/Cyrillic/Arabic, Noto Sans CJK Bold for Japanese/Chinese) and `lang/<code>.script` (dialogue; entry key = obj/ev/sub/k of the original text, key|0x80 = the family-friendly variant).
  Built by `tools/lang/build_packs.py` from `tools/lang/tr/<code>.py` (UI + SCRIPT + ALT dicts; English UI list in ui_en.py, `gen_ids.py` writes src/lang_ids.h, `gen_names.py` bakes the language names into src/clearnames.h). Arabic is shaped at build time (presentation forms,
  lam-alef) and reordered at run time (`r_visual`: reversed, Latin/digit runs kept); Japanese/Chinese wrap anywhere. The translations are mine (machine quality, unreviewed). The `.script` files are translations of Cactus's text: keep them out of public packages (`PUBLIC=1 ./package.sh`).
- **Settings row 1 = Language**: everything below (and the main menu, pause, end screens, dialogue) switches at once; the language name is drawn in its own script from the built-in names atlas. `language=` (cuss/family) is now "Language style". Reset to defaults keeps the language.
- **Dialogue font**: default `text=original` draws the dialogue in the game's own font 1 (the larger 9-pixel font; font 0 is a 5-pixel font in which Y looks like T and digits are blobs) at 2x, upper case, dark ink in the light box; its M, N and W come out as solid blocks, so they are redrawn
  (render.c FIX table, `FONT_FIXED`). `clear` = DejaVu. Other languages always use the pack's clear font (the game font is ASCII only). Hooks: PSY_FONTSHEET, PSY_DUMPFONT, PSY_RAWFRAME/PSY_RAWFILE.
- Host vs QEMU-MIPS menu frames are raw-identical (checked on the settings screen in Japanese); note that the menu rewrites psychosomnium.cfg when a setting changes, so back-to-back test runs must reset it.
- **Language folder bug (v1.0.0, found on the device 2026-11-25):** the packs are looked up in `<port>/lang` (next to gamedata) but the code only searched `<port>/gamedata/lang` (my tests used that layout), so every pack was "not found" and the Language row snapped back to English. Fixed: both folders are searched (`<gamedata>/../lang` first), for the .pak, the loose .script and the optional zip. Test layout: PSY_GAMEDATA=<port>/gamedata with <port>/lang beside it.
- **Test hooks are PSY_TEST-only (2026-11-25):** every PSY_* environment hook (PSY_HEADLESS/JOY/SHOTS/FUZZ/HASHFILE/TESTROOM/SPAWNS/BEEBOT/... ) now lives in `src/testhooks.c` and `#ifdef PSY_TEST` blocks; `build_host.sh` defines PSY_TEST, `build_sf3500.sh` does not,
  so the card build contains none of them (`strings psychosomnium.mipsel | grep PSY_` shows only PSY_GAMEDATA, the game folder that run.sh sets). The perf lines in log.txt stay (they are diagnostics, not hooks).
- **Quick drop back to the front-end (user report, 2026-11-25):** zhijack.log showed a launch whose child exited rc=0 about 1 s after starting. Candidates: a glitchy/stale pad word at launch reading as SELECT+START, or the display init failing while the previous process still holds
  the driver. Fixes: `plat_quit_combo()` (SELECT+START must be held 500 ms AND the pad must have been seen without them since the start), hwdisp_init retried up to 5 times (400 ms apart), every exit logs its reason and uptime, crash signals are logged, the first pad words after start are logged,
  and run.sh keeps log.prev.txt (previous run) and log.short.txt (any run shorter than 10 s) so a failed start survives the next run. If it happens again: read log.short.txt.
