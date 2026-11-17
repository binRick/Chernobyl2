#!/bin/bash
# Proves the narration in the cut plays whole and on time: re-cuts with the game muted (GAME_VOL=0) to a scratch file,
# finds each stretch of voice, and checks it against where edit.sh placed each line and how long that line is.
cd "$(dirname "$0")/../.." || exit 1
W=build/demo/edit-check OUT=build/demo/vo-check.mp4 GAME_VOL=0 tools/demo/edit.sh >/dev/null || { echo "FAIL edit"; exit 1; }
ffmpeg -nostdin -v error -y -i build/demo/vo-check.mp4 -vn -ac 1 -ar 8000 -f s16le build/demo/vo-check.raw || exit 1
python3 - build/demo/vo-check.raw build/demo/edit-check/vo.txt <<'PY'
import array, subprocess, sys
a = array.array('h'); a.frombytes(open(sys.argv[1], 'rb').read()); w = 80          # 10 ms windows
e = [sum(abs(x) for x in a[i:i+w]) / w for i in range(0, len(a) - w, w)]
runs, st = [], None
for i, x in enumerate(e + [0]):
    if x > 200 and st is None: st = i
    if x <= 200 and st is not None:
        if runs and st - runs[-1][1] < 70: runs[-1] = (runs[-1][0], i)          # pauses inside a line join it
        else: runs.append((st, i))
        st = None
runs = [(s / 100, t / 100) for s, t in runs]
fail = 0
for line in open(sys.argv[2]):
    clip, at = line.split(); at = float(at)
    d = float(subprocess.check_output(['ffprobe', '-v', 'error', '-show_entries', 'format=duration', '-of', 'csv=p=0', clip]))
    r = [x for x in runs if x[0] < at + d and x[1] > at]
    if not r: print(f"FAIL {clip}: no voice near {at:.2f}s"); fail = 1; continue
    s, t = r[0][0], r[-1][1]
    ok = abs(s - at) < 0.25 and t > at + d - 0.45 and t < at + d + 0.3          # starts on time, runs to (about) its own end
    print(f"{'ok  ' if ok else 'FAIL'} {clip.split('/')[-1]:14} placed {at:6.2f}-{at+d:6.2f}  heard {s:6.2f}-{t:6.2f}")
    fail |= not ok
print("PASS" if not fail else "FAIL"); sys.exit(fail)
PY
