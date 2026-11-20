#!/bin/bash
# --bot as a demo of the game (the owner, 2026-11-20: "do more weapon switches and arm the traps. make it so we can see the
# traps execute. i want an impressive demo"). Three minutes of the waves from the start, god mode (so it is still there
# to be judged at the end), and in them it must:
#   traps  go to a trap's switch and arm it (E), have an enemy walk in and set it off, a trap kill someone -- and be
#          looking at the trap as it goes (within 25 degrees of its middle, a moment after it springs);
#   guns   draw at least four of its guns (minigun, flamethrower, grenade launcher, Remington), change at least eight
#          times, and kill with at least three of them;
#   mines  lay a claymore, and a claymore kill someone.
# The debug log names who killed each ("edeath" "by": a gun's slot, 100 a trap, 101 a claymore), and the bot logs
# each trap it arms ("botarm") and where it looked as one went off ("botwatch").
cd "$(dirname "$0")/.." || exit 1
make -s all >/dev/null || { echo "FAIL build"; exit 1; }
L=chernobyl2-debug.log; rm -f $L
CHERNOBYL_BACKGROUND=1 perl -e 'alarm 900; exec @ARGV' ./build/chernobyl2 --no-intro --mute --debug --god --bot --frames 10800 >/dev/null 2>&1 </dev/null
python3 - $L <<'PY'
import json, sys, collections
ev = []
for line in open(sys.argv[1]):
    try: ev.append(json.loads(line))
    except Exception: pass
fail = 0
def ok(t): print("ok   " + t)
def bad(t):
    global fail; print("FAIL " + t); fail = 1
t0 = next((e["t"] for e in ev if e.get("ev") == "frame"), 0); t1 = max([e["t"] for e in ev] or [0])
print(f"     (a run of {t1 - t0:.0f} s, {sum(1 for e in ev if e.get('ev') == 'edeath')} killed)")
# traps
armed = [e for e in ev if e.get("ev") == "botarm"]
(ok if armed else bad)(f"it armed traps at their switches: {len(armed)} ({', '.join(e.get('name', '?') for e in armed[:6])})")
mine_k = {e["k"] for e in armed}
sprung = [e for e in ev if e.get("ev") == "trap" and e.get("what") == "sprung" and e.get("k") in mine_k]
(ok if sprung else bad)(f"an enemy walked into one it armed and set it off: {len(sprung)}")
kills = collections.Counter(e.get("by", -1) for e in ev if e.get("ev") == "edeath")
(ok if kills[100] >= 1 else bad)(f"a trap killed: {kills[100]}")
watch = [e for e in ev if e.get("ev") == "botwatch"]
good = [e for e in watch if e["off"] < 25.0]
(ok if good else bad)(f"it looked at a trap as it went off: {len(good)} of {len(watch)} within 25 degrees (" + ", ".join(f"{e['name']} {e['off']:.0f}" for e in watch[:6]) + ")")
# guns
drawn = [e["slot"] for e in ev if e.get("ev") == "weapon" and "label" in e and e["t"] > t0 + 1]
names = {0: "grenade launcher", 1: "Remington", 2: "minigun", 5: "flamethrower"}
used = {s for s in drawn if s in names} | {2}
(ok if len(used) >= 4 else bad)(f"it drew {len(used)} of the 4 guns ({', '.join(names[s] for s in sorted(used))})")
(ok if len(drawn) >= 8 else bad)(f"it changed guns {len(drawn)} times (at least 8)")
gk = {s: kills[s] for s in names if kills[s] > 0}
(ok if len(gk) >= 3 else bad)(f"it killed with {len(gk)} guns (at least 3): " + ", ".join(f"{names[s]} {n}" for s, n in gk.items()))
# claymores
laid = [e for e in ev if e.get("ev") == "mine" and e.get("lay") == 1]
(ok if laid else bad)(f"it laid claymores: {len(laid)}")
(ok if kills[101] >= 1 else bad)(f"a claymore killed: {kills[101]}")
print("PASS" if not fail else "FAIL"); sys.exit(fail)
PY
