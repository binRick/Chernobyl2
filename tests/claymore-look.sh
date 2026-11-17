#!/bin/bash
# How a laid claymore LOOKS, read off screenshots the game takes of itself (--shot) in a hidden window.
#   - Its lasers are clean lines (owner, 2026-11-17: "claymore lasers are kinda ragged, not clean lines"). Each was a round
#     rod 4.4 mm thick: two metres off that is under a pixel, so the line broke into dashes, and nothing softened the
#     staircase of what was drawn. They are ribbons now, soft across and never under a few pixels wide (src/mines.h MineBeam).
#   - It can be found again in the dark ("give the mine the minimum brightness too"). Lit by the lamps where it stands, in
#     a dark corner it was the floor's own black. It is never shown in less light than the one in the hand (MINE_MIN_LIT).
# Dev hooks, no synthetic keys: --mines (the sandbox), CHERNOBYL_MINE_AUTO=30 lays one and no more, --look aims the view,
# CHERNOBYL_AT stands him somewhere, CHERNOBYL_MINE_FLOOR=0 takes the floor away (to measure against).
cd "$(dirname "$0")/.." || exit 1
make -s all >/dev/null || { echo "FAIL build"; exit 1; }
python3 - <<'PY'
import json, os, subprocess, sys
from PIL import Image
fail = 0
def ok(t): print("ok   " + t)
def bad(t):
    global fail; print("FAIL " + t); fail = 1
def red(p): return p[0] - max(p[1], p[2])
def lum(p): return 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]

def shoot(look, env=None, lamp_off=False):
    """One mine laid at his feet and armed, then a screenshot. lamp_off: taken again (a few times) if its own red lamp was
    lit in that frame -- it blinks for a seventh of every second, and lights the mine and the floor round it."""
    for _ in range(6):
        for f in ("chernobyl2-shot.png", "chernobyl2-debug.log"):
            if os.path.exists(f): os.remove(f)
        e = dict(os.environ, CHERNOBYL_BACKGROUND="1", CHERNOBYL_CLEAN_HUD="1", CHERNOBYL_MINE_AUTO="30"); e.update(env or {})
        subprocess.run(["perl", "-e", "alarm 120; exec @ARGV", "./build/chernobyl2", "--mines", "--god", "--no-enemies", "--no-intro", "--mute", "--debug",
                        "--look", "-94.7", str(look), "--shot", "560", "--frames", "575"], env=e, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        ev = []
        for line in open("chernobyl2-debug.log"):
            try: ev.append(json.loads(line))
            except Exception: pass
        shot = [x for x in ev if x.get("ev") == "shot"]; armed = [x for x in ev if x.get("ev") == "mine" and "armed" in x]
        if not shot or not os.path.exists("chernobyl2-shot.png"): return None, "no screenshot taken"
        if not armed or armed[0]["t"] > shot[0]["t"]: return None, "the mine was not armed by the screenshot"
        if lamp_off and not (0.30 < shot[0]["t"] % 1.0 < 0.97): continue
        return Image.open("chernobyl2-shot.png").convert("RGB"), ""
    return None, "its lamp was lit in every screenshot tried"

# ---- the lasers: seen from above and behind, the fan's left beam crosses a third of the screen's height
im, why = shoot(-16)
if im is None: bad("lasers: " + why)
else:
    W, H = im.size; px = im.load()
    rows = [[red(px[x, y]) for x in range(int(W * 0.36), int(W * 0.47))] for y in range(int(H * 0.60), int(H * 0.93))]
    mx = [max(r) for r in rows]; med = sorted(mx)[len(mx) // 2]          # each row's brightest pixel; the line's median brightness
    if med < 30: bad(f"lasers: no beam where it should cross the screen (brightness {med})")
    else:
        weak = sum(1 for v in mx if v < 0.5 * med) / len(mx); width = sum(sum(1 for v in r if v > 0.25 * med) for r in rows) / len(rows)
        (ok if weak <= 0.02 else bad)(f"lasers unbroken: {weak:.0%} of the {len(mx)} rows a beam crosses are dim or empty (a clean line: none)")
        (ok if width >= 2.0 else bad)(f"lasers wide enough to be smooth: {width:.1f} px across (under 2 is a staircase of single pixels)")

# ---- found in the dark: a corner no lamp reaches (map x 30, z -24), looked down at
def body(im):                                                              # the mine's body under where its beams start, and the bare floor beside it
    W, H = im.size; px = im.load(); k = H / 720.0; ox, oy = W / 2 + 10 * k, 679 * k
    m = [px[x, y] for y in range(int(oy + 6 * k), min(H, int(oy + 30 * k))) for x in range(int(ox - 34 * k), int(ox + 35 * k))]
    m = [p for p in m if red(p) < 25]
    f = [px[x, y] for y in range(int(oy - 40 * k), int(oy - 10 * k)) for x in range(int(ox - 150 * k), int(ox - 90 * k))]
    return sum(map(lum, m)) / max(1, len(m)), sum(map(lum, f)) / max(1, len(f))
a, why = shoot(-38, {"CHERNOBYL_AT": "30 -24"}, lamp_off=True)
b, why2 = shoot(-38, {"CHERNOBYL_AT": "30 -24", "CHERNOBYL_MINE_FLOOR": "0"}, lamp_off=True)
if a is None or b is None: bad("dark corner: " + (why or why2))
else:
    (mine, floor), (bare, _) = body(a), body(b)
    (ok if floor < 14 else bad)(f"the corner is dark (its floor: {floor:.0f} of 255)")
    (ok if mine >= 1.35 * bare else bad)(f"the mine there is shown at {mine:.0f} of 255; by the lamps alone it would be {bare:.0f}")
    (ok if mine >= 2.1 * floor else bad)(f"it stands out from the floor: {mine / max(floor, 0.01):.1f} times as bright")
for f in ("chernobyl2-shot.png",):
    if os.path.exists(f): os.remove(f)
print("PASS" if not fail else "FAIL"); sys.exit(fail)
PY
