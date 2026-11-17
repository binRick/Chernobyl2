#!/usr/bin/env python3
"""Every side of a raised deck that open air can see must be drawn. grid2map.py hides ("caulks") a deck side only
when it is buried; it once tested one point at the side's middle, so a side half against a neighbouring deck was
hidden whole and the player looked into the deck (owner's report 2026-11-16). This samples each hidden side of every
raised deck (metal-plate top above the floor) along its length and height, just outside it: every point must be
inside some other brush.   check_risers.py maps/chernobyl.map   (exit 1 on any exposed side)"""
import re, sys
txt = open(sys.argv[1] if len(sys.argv) > 1 else "maps/chernobyl.map").read()
num = r"(-?[\d.]+)"; FACE = re.compile(r"\( %s %s %s \) \( %s %s %s \) \( %s %s %s \) (\S+)" % ((num,) * 9))
boxes = []                                               # axis-aligned brushes only: (lo, hi, {side: texture})
for b in re.findall(r"\{\n(\(.*?)\n\}", txt, re.S):
    faces = []
    for line in b.strip().split("\n"):
        m = FACE.match(line.strip())
        if not m: faces = None; break
        p = [float(v) for v in m.groups()[:9]]; p0, p1, p2 = p[0:3], p[3:6], p[6:9]
        u = [p1[i] - p0[i] for i in range(3)]; v = [p2[i] - p0[i] for i in range(3)]
        n = [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]
        ax = [i for i in range(3) if abs(n[i]) > 1e-9]
        if len(ax) != 1: faces = None; break
        faces.append((ax[0], p0[ax[0]], m.group(10)))
    if not faces or len(faces) != 6: continue
    lo, hi, tx = [0]*3, [0]*3, {}
    try:
        for a in range(3):
            vals = sorted(set(f[1] for f in faces if f[0] == a)); lo[a], hi[a] = vals
            for f in faces:
                if f[0] == a: tx[("-" if f[1] == lo[a] else "+") + "xyz"[a]] = f[2]
    except ValueError: continue
    boxes.append((lo, hi, tx))
def buried(p, skip):
    return any(all(lo[i] - 0.01 <= p[i] <= hi[i] + 0.01 for i in range(3)) for j, (lo, hi, _t) in enumerate(boxes) if j != skip)
bad = 0
for j, (lo, hi, tx) in enumerate(boxes):
    if not tx.get("+z", "").endswith("floor_plate") or hi[2] <= 0: continue          # raised decks only
    for side in ("-x", "+x", "-y", "+y"):
        if not tx[side].endswith("caulk"): continue
        a = "xyz".index(side[1]); b = 1 if a == 0 else 0
        out = lo[a] - 0.5 if side[0] == "-" else hi[a] + 0.5
        open_ = [(fb, fz) for fb in (0.02, 0.25, 0.5, 0.75, 0.98) for fz in (0.1, 0.5, 0.9)
                 if not buried([out if i == a else lo[b] + (hi[b] - lo[b]) * fb if i == b else lo[2] + (hi[2] - lo[2]) * fz for i in range(3)], j)]
        if open_:
            bad += 1; print(f"FAIL deck x {lo[0]:g}..{hi[0]:g} y {lo[1]:g}..{hi[1]:g} top {hi[2]:g}: {side} side hidden but open ({len(open_)}/15 points)")
print(("PASS" if not bad else "FAIL") + f": {sum(1 for lo, hi, t in boxes if t.get('+z','').endswith('floor_plate') and hi[2] > 0)} raised deck pieces, {bad} exposed hidden sides")
sys.exit(1 if bad else 0)
