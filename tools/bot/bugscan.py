"""Look through a game run's debug log for signs of bugs: enemies that do not move for long while alive, are sunk in
walls, cannot be reached, float above or sink below their floor, or live far too long; and anything odd in the console.
Made for --bot runs (the dev autopilot playing the waves, src/main.c BotSteer), 2026-11-19: it found enemies stuck at
ledges, against walls, under 0.9 m risers and at a trap's tank (tests/enemy-paths.sh).
    python3 tools/bot/bugscan.py <run dir holding log.jsonl (the run's chernobyl2-debug.log) and, if kept, console.txt>"""
import json, math, os, sys, collections
d = sys.argv[1]; ev = []
for line in open(os.path.join(d, "log.jsonl")):
    try: ev.append(json.loads(line))
    except Exception: pass
t0 = next((e["t"] for e in ev if e.get("ev") == "frame"), 0)
def ts(t): m, s = divmod(max(0.0, t - t0), 60); return f"{int(m)}:{s:04.1f}"
found = []
RANGED = {"rifleman", "gunner", "mutant", "priest"}                 # they stand to shoot (kEnemy: AK_HITSCAN, AK_BOLT, AK_RAISE)
holes = {e["i"]: e for e in ev if e.get("ev") == "hole"}            # the breaches: the waves climb out of them, a few seconds each
def by_hole(p): return any(abs(p[0] - h["at"][0]) < h["half"][0] + 1.5 and abs(p[2] - h["at"][2]) < h["half"][1] + 1.5 for h in holes.values())
deaths = [e["t"] for e in ev if e.get("ev") == "death"]             # after a death the world stands still until play again (or the end)
def frozen(a, b): return any(d <= b and a <= d + 8.0 for d in deaths)
# per enemy slot: track lives (a slot is reused; a new life starts after a gap or a big jump)
track = collections.defaultdict(list)
for e in ev:
    if e.get("ev") == "epos": track[e["i"]].append(e)
for i, xs in track.items():
    run = []
    for e in xs + [None]:
        if e is not None and run and (e["t"] - run[-1]["t"] > 3.0 or math.dist(e["pos"], run[-1]["pos"]) > 6.0): e2 = e; seg = run; run = [e2]
        elif e is not None: run.append(e); continue
        else: seg = run
        if len(seg) >= 2:
            # still for 12 s or more while steering somewhere
            j = 0
            for k in range(len(seg)):
                while seg[k]["t"] - seg[j]["t"] > 12.0: j += 1
                win = seg[j:k + 1]
                if seg[k]["t"] - seg[j]["t"] >= 11.5 and max(math.dist(a["pos"], win[0]["pos"]) for a in win) < 0.6 and any(abs(a["steer"][0]) + abs(a["steer"][1]) > 0.5 for a in win) and win[-1]["clip"] != 0:
                    found.append((seg[j]["t"], f"enemy {i} ({seg[k]['type']}) stood still for 12 s while steering {win[-1]['steer']} at {win[-1]['pos']} (stuck {win[-1]['stuck']} s, d {win[-1]['d']} m)")); break
            emb = [a for a in seg if a.get("embed", 0) > 0.5]
            if emb: found.append((emb[0]["t"], f"enemy {i} ({emb[0]['type']}) sunk in a wall {len(emb)} times (embed up to {max(a['embed'] for a in emb):.1f}) at {emb[0]['pos']}"))
            # off the route grid and not moving (a melee one: those that shoot stand still on purpose, often at a breach's
            # lip, which is off the grid -- that alone is no bug)
            unr = [a for a in seg if a.get("reach") == 0 and a["type"] not in RANGED]
            if len(unr) >= 6 and max(math.dist(a["pos"], unr[0]["pos"]) for a in unr) < 1.0 and not by_hole(unr[0]["pos"]):   # (not climbing out of a breach)
                found.append((unr[0]["t"], f"enemy {i} ({unr[0]['type']}) off the nav grid and still, {len(unr)} looks, at {unr[0]['pos']}"))
            # a melee one standing 5 s or more, away from the player, not climbing out of a breach, the world not frozen
            # by a death: a jam shorter than the 12 s above (the stand by (14, 6.6) held some 5-13 s before it held one for good)
            j = 0
            for k in range(len(seg)):
                while seg[k]["t"] - seg[j]["t"] > 5.0: j += 1
                win = seg[j:k + 1]
                if (seg[k]["t"] - seg[j]["t"] >= 4.8 and seg[k]["type"] not in RANGED and min(a["d"] for a in win) > 4.0
                        and max(math.dist(a["pos"], win[0]["pos"]) for a in win) < 0.4 and not by_hole(win[0]["pos"])
                        and not frozen(win[0]["t"], win[-1]["t"])):
                    found.append((seg[j]["t"], f"enemy {i} ({seg[k]['type']}) stood 5 s at {win[0]['pos']}, {min(a['d'] for a in win):.0f} m from the player, steering {win[-1]['steer']}")); break
            flo = [a for a in seg if a["drawY"] - a["pos"][1] > 0.5 or a["drawY"] - a["pos"][1] < -0.2]
            if len(flo) >= 3: found.append((flo[0]["t"], f"enemy {i} ({flo[0]['type']}) drawn {flo[0]['drawY'] - flo[0]['pos'][1]:+.2f} m off its floor {len(flo)} times, at {flo[0]['pos']}"))
            if seg[-1]["t"] - seg[0]["t"] > 120: found.append((seg[0]["t"], f"enemy {i} ({seg[0]['type']}) alive {seg[-1]['t'] - seg[0]['t']:.0f} s (from {seg[0]['pos']} to {seg[-1]['pos']})"))
        run = [e] if e is not None and seg is not run else run
for e in ev:
    vals = [v for v in e.values() if isinstance(v, float)] + [x for v in e.values() if isinstance(v, list) for x in v if isinstance(x, float)]
    if any(math.isnan(v) or math.isinf(v) for v in vals): found.append((e["t"], "a NaN or infinity in " + json.dumps(e)[:140]))
for k in ("holefall", "surrounded"):
    for e in ev:
        if e.get("ev") == k: found.append((e["t"], f"{k}: " + json.dumps({a: b for a, b in e.items() if a != 't'})[:120]))
con = open(os.path.join(d, "console.txt"), errors="ignore").read().splitlines() if os.path.exists(os.path.join(d, "console.txt")) else []
odd = [l for l in con if l.strip() and not any(x in l for x in ("No more than 2 texture coordinates", "Regular windows do not have icons", "GLD_TEXTURE_INDEX_2D", "record: ", "exit 0"))]
for l in odd[:12]: found.append((t0, "console: " + l[:150]))
print(f"{len(found)} things to look at")
for t, s in sorted(found)[:40]: print(f"  {ts(t)}  {s}")
