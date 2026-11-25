#!/bin/bash
# Desktop build for development and tests: SDL2 window, keyboard (arrows, Z=A, X=B, Enter=START).
set -e
cd "$(dirname "$0")"
OUT=${OUT:-build-host}
mkdir -p "$OUT"
gcc -std=gnu99 ${SAN:+-fsanitize=address,undefined -fno-omit-frame-pointer} -O2 -g -Wall -Wno-unused-function -Wno-unused-variable -Wno-misleading-indentation \
  -Isrc $(sdl2-config --cflags) \
  src/main.c src/lang.c src/menu.c src/unpack.c src/gm.c src/game.c src/render.c src/audio.c src/plat_sdl.c \
  -o "$OUT/psychosomnium" $(sdl2-config --libs) -lm -lpthread
ls -l "$OUT/psychosomnium"
