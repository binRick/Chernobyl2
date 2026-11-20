#!/bin/bash
# --bot, the dev autopilot that plays the waves (BotSteer in src/main.c; filmed runs: --bot --record).
#   frozen  stood where it once froze for good in a run (2026-11-19) -- the corner of a wall block and the rubble against it,
#           and inside a console's footprint -- with no enemies, it walks off. It judged the way open by two thin rays,
#           which passed where the player's body (the sphere the movement tests) did not, so every step it chose was refused.
# Dev hooks: CHERNOBYL_AT stands the player there (no waves).
cd "$(dirname "$0")/.." || exit 1
make -s all >/dev/null || { echo "FAIL build"; exit 1; }
L=chernobyl2-debug.log; fail=0
spot(){   # name, where
    rm -f $L
    CHERNOBYL_BACKGROUND=1 CHERNOBYL_AT="$2" ./build/chernobyl2 --no-intro --mute --debug --god --bot --frames 900 >/dev/null 2>&1 </dev/null
    python3 - $L "$1" "$2" <<'PY' || fail=1
import json, sys
log, name, at = sys.argv[1:4]; ev = []
for line in open(log):
    try: ev.append(json.loads(line))
    except Exception: pass
fr = [e["pos"] for e in ev if e.get("ev") == "frame"]
x0, z0 = (float(v) for v in at.split())
moved = max([((p[0] - x0) ** 2 + (p[2] - z0) ** 2) ** 0.5 for p in fr] or [0.0])
good = moved > 2.0
print(("ok   " if good else "FAIL ") + f"frozen: stood {name} ({at}), the autopilot walks off ({moved:.2f} m at most from there; more than 2)")
sys.exit(0 if good else 1)
PY
}
spot "in the corner of a wall block and its rubble" "20.3 -16.3"
spot "inside a console's footprint" "14.1 5.9"
[ $fail = 0 ] && echo PASS || { echo FAIL; exit 1; }
