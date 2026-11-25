#!/bin/bash
# usage: rec.sh <name> <script-file> [exe name in orig/, default psycho.exe]   (NOSOUND=1 disables dsound: no sound stalls)   (script lines: "sleep S" | "down KEY" | "up KEY" | "tap KEY MS")
W=$HOME/handhelds/sf3000-work/psychosomnium/work/wine
NAME=$1; SCRIPT=$2; EXE=${3:-psycho.exe}
export WINEPREFIX=$W/prefix WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml${NOSOUND:+,dsound}=d" WINEARCH=win32
Xvfb :77 -screen 0 800x600x24 >/dev/null 2>&1 &
XP=$!
sleep 2
export DISPLAY=:77
cd $W/orig
wine $EXE >$W/wine_$NAME.log 2>&1 &
sleep 12
BASE=${EXE%.exe}
WID=$(xdotool search --class "$BASE" 2>/dev/null | head -1)
[ -z "$WID" ] && WID=$(xdotool search --onlyvisible --name "" 2>/dev/null | tail -1)
xdotool windowfocus $WID 2>/dev/null
if [ -n "$NOSOUND" ]; then   # dismiss the 'Failed to create Direct Music Performance' dialog
  xdotool mousemove 400 326 click 1 2>/dev/null; sleep 1; xdotool key Return 2>/dev/null; sleep 4
fi
ffmpeg -loglevel error -y -f x11grab -framerate 60 -video_size 640x480 -i :77+80,60 -c:v ffv1 -t ${REC_T:-40} $W/shots/$NAME.mkv &
FP=$!
sleep 1
START=$(date +%s.%N)
while read -r cmd a b; do
  case "$cmd" in
    sleep) sleep $a;;
    down) xdotool keydown $a;;
    up) xdotool keyup $a;;
    tap) xdotool keydown $a; sleep $(echo "$b/1000" | bc -l); xdotool keyup $a;;
  esac
done < $SCRIPT
echo "script finished at $(echo "$(date +%s.%N)-$START" | bc -l)" >> $W/wine_$NAME.log
sleep 1
kill -INT $FP 2>/dev/null; sleep 2
kill $FP 2>/dev/null
wineserver -k 2>/dev/null; kill $XP 2>/dev/null
echo "wid=$WID"
