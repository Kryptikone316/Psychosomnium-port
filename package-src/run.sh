#!/bin/sh
set -u

PORT_DIR=/mnt/sdcard/roms/ports/psychosomnium
cd "$PORT_DIR"
rm -f /mnt/sdcard/cubegm/port_error.txt
export LD_LIBRARY_PATH=/mnt/sdcard/cubegm/lib:/mnt/sdcard/cubegm/usr/lib:${LD_LIBRARY_PATH:-}

# The game's own files (psycho.exe, and the unpacked copy in gamedata/cache/) live in gamedata/.
export PSY_GAMEDATA="$PORT_DIR/gamedata"

# No real SDL video or audio device on this hardware: SDL's dummy drivers let SDL start,
# the frames go to the device driver directly and the sound through the software mixer.
export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy

# Output goes to log.txt in the port folder, with an exit code line.
# the previous run's log is kept as log.prev.txt, and the log of any run shorter than 10 seconds (a launch that dropped back to the
# front-end) as log.short.txt, so a failed start can still be looked at after the next run
[ -f log.txt ] && cp log.txt log.prev.txt
: > log.txt
rc=0
t0=$(date +%s)
"$PORT_DIR/psychosomnium.mipsel" < /dev/null >> log.txt 2>&1 || rc=$?
echo "EXIT CODE: $rc" >> log.txt
t1=$(date +%s)
if [ $((t1 - t0)) -lt 10 ]; then cp log.txt log.short.txt; fi
exit "$rc"
