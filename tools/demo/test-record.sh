#!/bin/bash
# Test of --record: the game films itself (1920x1080, 60 fps, picture and sound in step).
cd "$(dirname "$0")/../.." || exit 1
make -s all >/dev/null || { echo "FAIL build"; exit 1; }
mkdir -p build/demo; rm -f build/demo/t.mp4 build/demo/t.wav build/demo/m.mp4 build/demo/n.mp4
fail=0; ok(){ echo "ok   $1"; }; bad(){ echo "FAIL $1"; fail=1; }
num(){ awk -v a="$1" -v lo="$2" -v hi="$3" 'BEGIN{exit !(a>=lo && a<=hi)}'; }

CHERNOBYL_BACKGROUND=1 ./build/chernobyl2 --no-intro --god --autofire --frames 300 --record build/demo/t.mp4 >build/demo/t.log 2>&1
f=build/demo/t.mp4
[ -s $f ] && ok "file written" || { bad "no $f"; exit 1; }
wh=$(ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0 $f)
[ "$wh" = "1920,1080" ] && ok "1920x1080" || bad "size $wh"
fr=$(ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate -of csv=p=0 $f)
[ "$fr" = "60/1" ] && ok "60 fps" || bad "rate $fr"
vd=$(ffprobe -v error -select_streams v:0 -show_entries stream=duration -of csv=p=0 $f)
ad=$(ffprobe -v error -select_streams a:0 -show_entries stream=duration -of csv=p=0 $f)
num "$vd" 3 8 && ok "video ${vd}s" || bad "video duration '$vd'"
num "$ad" 3 8 && ok "audio ${ad}s" || bad "audio duration '$ad'"
d=$(awk -v a="$vd" -v b="$ad" 'BEGIN{d=a-b; if(d<0)d=-d; print d}')
num "$d" 0 0.1 && ok "A/V differ ${d}s" || bad "A/V differ ${d}s"
y=$(ffmpeg -v error -i $f -vf "select=gte(t\,2),signalstats,metadata=print:key=lavfi.signalstats.YAVG:file=-" -frames:v 1 -f null - 2>/dev/null | awk -F= '/YAVG/{print $2; exit}')
grep -q "record: 1920x1080 " build/demo/t.log && ok "rendered at 1920x1080" || bad "rendered at $(grep -o 'record: [0-9x]*' build/demo/t.log)"
num "${y:-0}" 10 255 && ok "picture not black (Y ${y})" || bad "picture black (Y '${y}')"
mv=$(ffmpeg -v info -i $f -vn -af volumedetect -f null - 2>&1 | awk '/max_volume/{print $5}')
num "${mv:--99}" -40 1 && ok "sound present (${mv} dB)" || bad "sound silent ('${mv}')"

CHERNOBYL_BACKGROUND=1 ./build/chernobyl2 --no-intro --mute --frames 120 --record build/demo/m.mp4 >build/demo/m.log 2>&1
[ $? -eq 0 ] && [ -s build/demo/m.mp4 ] && ok "--mute: video, no crash" || bad "--mute run"

CHERNOBYL_BACKGROUND=1 PATH=/usr/bin:/bin ./build/chernobyl2 --no-intro --frames 120 --record build/demo/n.mp4 >build/demo/n.log 2>&1
[ $? -eq 0 ] && ok "no ffmpeg: game exits cleanly" || bad "no ffmpeg run exit $?"
grep -q "record:" build/demo/n.log && ok "no ffmpeg: one error logged" || bad "no ffmpeg: nothing logged"

[ $fail -eq 0 ] && echo PASS || { echo FAIL; exit 1; }
