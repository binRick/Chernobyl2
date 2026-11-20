#!/bin/bash
# Enemies get to the player across the hall, as the route grid sends them (found by the --bot autopilot, 2026-11-19: the
# last of a wave running on the spot for minutes, and the wave never ending):
#   down  a cook stood on the platform in the middle of the hall (its top 1.35 m up), the player on the floor south of it.
#         Off the edge he landed at the foot of its side, his body in it, and could not step away (every step still
#         touched it) until the embed check moved him to a breach. He must walk it: off the edge, away, to the player.
#   up    the player on the 0.9 m deck east of the reactor's steps, a cook on the floor below its side. The route grid
#         sent him straight up the 0.9 m riser, which no enemy can step (0.7): he pushed at its side for good. He must
#         come round by the steps (the 0.45 m deck beside it) and reach the player.
#   trap  a cook north of the doorway by breach 2's coolant-spill trap, the player south of it. The route ran through the
#         trap's tank; pushed off it, he was slid round it to the side his slot number chose (odd one way, even the other),
#         here into the wall beside the doorway, and the next step put him back: on the spot for good. He must go round.
#   stand a fiend in the corridor north of the hall's east side, the player south-east of it. In the corridor stands a
#         small stand (a post, a shelf at 1 m, a board on it: at x 13.1-13.8, z 5.8-6.4), too small for the route grid's
#         0.8 m cells, which sent him straight at it. Every step on was refused; the sweep round it took one step aside,
#         and the next went back: on the spot for good (a fiend 38 s, a butcher 45 s in a 10-minute --bot run, 2026-11-19).
#         He must go round it (by it at most 3 s) and reach the player.
# Dev hooks: CHERNOBYL_AT stands the player (no waves); CHERNOBYL_ENEMY_AT stands the enemy. God mode: he may reach him.
cd "$(dirname "$0")/.." || exit 1
make -s all >/dev/null || { echo "FAIL build"; exit 1; }
L=chernobyl2-debug.log; fail=0
case_run(){   # name, player x z, enemy x z, its start height above the floor, [its type: 0 the cook], [how near it gets to the eyes]
    rm -f $L
    CHERNOBYL_BACKGROUND=1 CHERNOBYL_AT="$2" CHERNOBYL_ENEMY_AT="$3 ${5:-0}" ./build/chernobyl2 --no-intro --mute --debug --god --frames 2400 >/dev/null 2>&1 </dev/null
    grep -q '"ev":"holefall"' $L && { echo "FAIL $1: the player fell into a breach (the test stands him in one)"; fail=1; return; }
    python3 - $L "$1" "$4" "${6:-2.8}" <<'PY' || fail=1
import json, math, sys
log, name, up, reach = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4]); ev = []
for line in open(log):
    try: ev.append(json.loads(line))
    except Exception: pass
bad = 0
def ok(t): print("ok   " + name + ": " + t)
def no(t):
    global bad; print("FAIL " + name + ": " + t); bad = 1
at = next((e for e in ev if e.get("ev") == "enemyat"), None)
if not at: no("no enemy was stood there (no \"enemyat\" line)"); sys.exit(1)
(ok if abs(at["at"][1] - 0.5 - up) < 0.1 else no)(f"a {at['type']} stands {at['at'][1] - 0.5:.2f} m above the hall's floor ({up:.2f})")
me = [e for e in ev if e.get("ev") == "epos" and e.get("i") == at["i"]]
rel = [e for e in ev if e.get("ev") == "relocate" and e.get("i") == at["i"]]
fast = max([sum((a - b) ** 2 for a, b in zip(me[k]["pos"], me[k - 1]["pos"])) ** 0.5 / max(1e-3, me[k]["t"] - me[k - 1]["t"]) for k in range(1, len(me))] or [0.0])
(ok if not rel and fast < 9.0 else no)(f"he is never moved: walked all the way ({len(rel)} moved to a breach" + (f", from {rel[0]['from']}" if rel else "") + f"; fastest between two looks {fast:.1f} m/s, under 9 -- a cook runs 4.6)")
if name == "stand":                                    # the stand's middle is at (13.5, 6.1); its north-east shoulder, where he stuck, (14.0, 6.5)
    by = [e for e in me if math.dist((e["pos"][0], e["pos"][2]), (14.0, 6.5)) < 0.8]
    past = next((e for e in me if e["pos"][2] < 5.4), None)
    (ok if past and len(by) <= 3 else no)(f"he goes round the stand: by it {len(by)} looks (one a second; at most 3), " + (f"past it {past['t'] - me[0]['t']:.1f} s in" if past else f"never past it -- at {me[-1]['pos']}, steering {me[-1]['steer']}"))
land = [e for e in ev if e.get("ev") == "eland" and e.get("i") == at["i"]]
if land: (ok if all(e.get("clear") for e in land) else no)(f"each time he steps off a ledge he lands clear of its side ({sum(1 for e in land if e.get('clear'))} of {len(land)}; drops of {', '.join(format(e['drop'], '.2f') for e in land)} m)")
near = min([e["d"] for e in me] or [99.0]); last = me[-1] if me else {}
(ok if near < reach else no)(f"he reaches the player (nearest: {near:.1f} m from his eyes; under {reach})" + ("" if near < reach else f" -- he is at {last.get('pos')}, steering {last.get('steer')}, stuck {last.get('stuck')} s"))
sys.exit(bad)
PY
}
case_run down "-1.5 -5.5" "-1.0 -1.0" 1.35
case_run up "10.5 0.5" "10.5 -3.0" 0.0
case_run trap "-50 -6" "-47.7 9.0" 0.0                  # (the player clear of breach 4, x -51.3..-48.7, z -2.7..-0.1)
case_run stand "20.2 -3.6" "14.0 7.6" 0.0 10 3.4         # a fiend (type 10): his tentacles reach 2.8 m, so he stops further off
[ $fail = 0 ] && echo PASS || { echo FAIL; exit 1; }
