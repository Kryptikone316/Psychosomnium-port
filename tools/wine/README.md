# Reference runs of the original under Wine

The original psycho.exe runs under Wine 9 on the host (Xvfb, no real display needed), which gives a frame-accurate reference
to compare the port against. All of this uses a throw-away prefix and a private virtual screen (:77).

1. Copy psycho.exe to `work/wine/orig/` and build a silent stand-in for the Windows music DLL (the real bgm.dll fails to define its
   functions under Wine): `flatpak-spawn --host i686-w64-mingw32-gcc -shared -o bgm.dll bgm_stub.c -Wl,--kill-at`, put it next to the exe.
2. `flatpak-spawn --host tools/wine/rec.sh <name> <script>`: starts Xvfb + the game, records 640x480 at 60 fps (lossless) while xdotool plays the
   script (`sleep S`, `down KEY`, `up KEY`, `tap KEY MS`). Extract frames with `ffmpeg -i x.mkv f%04d.png`.
3. Replay the same inputs in the port (PSY_JOY, PSY_SHOTS, PSY_SHOT_EVERY) and diff frame by frame after quantising to RGB565. Align on an
   observable event (first x change / first y change), not on wall-clock time. Mask animated bits you do not drive (butterfly).

Caveats: Wine stalls for a few frames at room loads and when sounds start (frames repeat in the recording), so compare short windows that
contain no room change or new sound; compare logic by pose/position, not by absolute frame number after such a stall.

## What the reference found (2026-11-24)
- Transparency is the pixel data's alpha byte, not a colour key.
- The displayed sprite frame is the image_index rounded half-to-even (not floored).
- Sprite animation (image_index += image_speed) advances before the draw of the same frame.
- Walk/fall physics, collision, snapping (draw_snap) and the walking, butterfly and bubble animations are pixel-identical over 120+ frames.


## Scenario testing (2026-11-24, second round)
The game is linear, so instead of playing to a later room the exe itself is patched: `patch_exe.py psycho.exe out.exe ROOM --spawn OBJ,X,Y --save ROOM,TYPE --code "GML"`
moves the room to position 1 of the room order (right after the splash), appends instances (ids 200000+), and sets global.saveroom/global.type
(+ optional extra creation code) in that room. The port mirrors it with PSY_TESTROOM, PSY_SPAWNS, PSY_SAVE, PSY_SETVARS.
`scn.py record NAME ROOM "spawns" "save" script.txt` (Wine + ffmpeg, window looked up by exe name) and `seqcmp.py`/`scn.py fit` compare the SEQUENCE of distinct
pictures (run-length encoded frame hashes, PSY_HASHFILE in the port; Wine stalls only lengthen runs) and report how many original pictures the port does not
produce. Key-hold lengths matter by +-1 frame: measure them from the original (e.g. a 600 ms xdotool tap = 37 frames), do not let a coordinate search guess them.

Results: room 12 big guy walk + bunny dialogue (A) 0 of 115 unexplained; punch (B) 34 of 34 before the splash room; jumps with the bunnies (C) 1 of 162;
room 7 falling spikes (D) identical up to the fatal hit (death particles are random); Shirley (E) 1 of 166; gunman at the castle trigger (G) 0 of 96;
Tom/NPC (H) 0 of 117; full ending incl. castle, fade (G2) all but the random death particles and ~10 single frames of castle phase;
bee hover/takeoff/flight (F) identical until it leaves the room; bee flaps in place (F2) 3 single wing frames of 140.
Bugs this round found: animation advances at the START of the frame; the castle's parent object is `player` (so instance_exists(player) stays true and the
room does not restart); a room-placed instance of a persistent object did not get its Create event.


## Coverage of the 23 rooms (third round, 2026-11-24)
Static first-picture + sequence checks (patched exe starting in the room, tools/wine/scn.py): rooms 0 2 3 4 5 8 9 10 11 14 15 16 17 19 21 identical;
pump drops in 8/11/16 match; credits room 20: all 255 hue colours (0 of 257 pictures unexplained). Earlier rounds: 1 (butterfly, pump, walking), 6 (Shirley), 7 (falling spikes),
12 (bunnies, big guy), 13 (bee), 18 (gunman + castle ending), 22 (title).
Dynamic: Tom walking/falling (room 0), gunman walking/jumping (room 17) 0 of 67, checkpoint marker + spikes + restart + pump respawn (room 11),
Mitch calling Tom and the first two replies (room 10) 0 of 170, Tom wrapping at the left edge of room 3 (1 of 40).
Only deaths are unmatched (particles are random in both engines); everything before and after a death matches.
Fix found in this round: make_color_hsv = degrees h*360/255, sextant method with t=v*(1-s*(1-f)), channels TRUNCATED (not rounded).
Recording caveats: `flatpak-spawn --host` does not pass environment variables (REC_T is ignored); `pkill -f` matches its own shell.
