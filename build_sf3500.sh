#!/bin/bash
# Cross build for the Datafrog SF3500 (MIPS32r2, little endian, hard float).
# Output: build-sf3500/psychosomnium.stripped.mipsel
# Toolchain: ~/handhelds/sf3000-work/buildroot (Codescape mips-mti-linux-gnu, glibc 2.20 sysroot with SDL2).
set -e
cd "$(dirname "$0")"
TC=${SF3500_TC:-$HOME/handhelds/sf3000-work/buildroot/output/host}
SYSROOT=$TC/mipsel-buildroot-linux-gnu/sysroot
CC=$TC/bin/mips-mti-linux-gnu-gcc
CXX=$TC/bin/mips-mti-linux-gnu-g++
ARCH="-mips32r2 -mtune=74kc -mhard-float -EL --sysroot=$SYSROOT"
# -O2 on purpose; no fast-math, no dspr2
CFLAGS="$ARCH -O2 -std=gnu99 -fno-strict-aliasing -Wall -Wno-unused-result -Wno-unused-function -Wno-misleading-indentation -DSF3500 -Isrc -Iplat -I$SYSROOT/usr/include/SDL2"
OUT=${OUT:-build-sf3500}
mkdir -p "$OUT"
pids=()
for f in main lang menu unpack gm game render audio plat_sdl; do
  $CC $CFLAGS -c src/$f.c -o "$OUT/$f.o" & pids+=($!)
done
for f in hwdisp joykey hwaudio sf3500_mixer; do
  $CXX $ARCH -O2 -w -fno-exceptions -fno-rtti -Iplat -c plat/$f.cpp -o "$OUT/plat_$f.o" & pids+=($!)
done
for p in "${pids[@]}"; do wait "$p"; done
$CXX $ARCH "$OUT"/*.o -o "$OUT/psychosomnium.mipsel" -lSDL2 -lpthread -ldl -lm
$TC/bin/mips-mti-linux-gnu-strip -o "$OUT/psychosomnium.stripped.mipsel" "$OUT/psychosomnium.mipsel"
ls -l "$OUT"/psychosomnium*.mipsel
