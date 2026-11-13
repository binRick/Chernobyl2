#!/usr/bin/env python3
"""Bake a weapon's authored PART animation into <model>.glb.parts.

Some first-person rigs animate the gun's own pieces as plain glTF nodes, not bones -- the grenade
launcher's barrel breaking open, its release lever, the six grenades going into the drum. raylib
only plays skeletal animation and bakes every unskinned mesh into its rest position, so in game
those pieces never moved. This samples each mesh node's world matrix through the clip, 60 times a
second, relative to its rest matrix:  D(t) = W(t) * inverse(W_rest).  The game draws mesh k with
D_k(frame) in front of the model transform (src/main.c, PartsLoad / DrawPinnedGun).

For meshes named slug* a second "seated" track is written: the round held where it sits once
loaded, riding the drum. The game shows that for rounds already in the weapon while the authored
track plays the one being loaded.

    python3 tools/gen_parts.py assets/grenadelauncher.glb [seated-frame=400]

File: "C2PT", int32 version, tracks, frames; then per track: char name[32], int32 mesh (the n-th
mesh node in node order = raylib's mesh index), int32 seated, frames * 12 floats (rows 0-2 of D).
"""
import json, math, struct, sys


def mul(a, b): return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def trs(t, q, s):
    x, y, z, w = q
    r = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
         [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
         [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
    return [[r[i][0] * s[0], r[i][1] * s[1], r[i][2] * s[2], t[i]] for i in range(3)] + [[0, 0, 0, 1]]


def inv(m):                      # general 4x4 inverse (Gauss-Jordan)
    a = [row[:] + [1.0 if i == j else 0.0 for j in range(4)] for i, row in enumerate(m)]
    for c in range(4):
        p = max(range(c, 4), key=lambda r: abs(a[r][c])); a[c], a[p] = a[p], a[c]
        d = a[c][c]; a[c] = [x / d for x in a[c]]
        for r in range(4):
            if r != c:
                f = a[r][c]; a[r] = [x - f * y for x, y in zip(a[r], a[c])]
    return [row[4:] for row in a]


def main():
    path = sys.argv[1]; seat_f = int(sys.argv[2]) if len(sys.argv) > 2 else 400
    d = open(path, "rb").read(); jl = struct.unpack("<I", d[12:16])[0]; j = json.loads(d[20:20 + jl]); B = d[20 + jl + 8:]
    N = j["nodes"]; par = {c: i for i, n in enumerate(N) for c in n.get("children", [])}

    def acc(i):
        a = j["accessors"][i]; bv = j["bufferViews"][a["bufferView"]]; n = {"SCALAR": 1, "VEC3": 3, "VEC4": 4}[a["type"]]
        off = bv.get("byteOffset", 0) + a.get("byteOffset", 0); st = bv.get("byteStride", 4 * n)
        return [struct.unpack_from("<%df" % n, B, off + k * st) for k in range(a["count"])]
    anim = max(j["animations"], key=lambda a: len(a["channels"])); ch = {}; tmax = 0.0
    for c in anim["channels"]:
        s = anim["samplers"][c["sampler"]]; t = [x[0] for x in acc(s["input"])]; v = acc(s["output"])
        kind = s.get("interpolation", "LINEAR")
        if kind == "CUBICSPLINE": v = v[1::3]; kind = "LINEAR"            # the key values, without their tangents
        ch[(c["target"]["node"], c["target"]["path"])] = (t, v, kind); tmax = max(tmax, t[-1])

    def key(node, path, time, rest):
        if (node, path) not in ch: return rest
        t, v, kind = ch[(node, path)]
        if time <= t[0]: return v[0]
        if time >= t[-1]: return v[-1]
        lo, hi = 0, len(t) - 1
        while hi - lo > 1:
            m = (lo + hi) // 2
            if t[m] <= time: lo = m
            else: hi = m
        if kind == "STEP": return v[lo]
        f = (time - t[lo]) / (t[hi] - t[lo]); a, b = v[lo], v[hi]
        if path == "rotation":
            if sum(x * y for x, y in zip(a, b)) < 0: b = [-x for x in b]
            q = [x + (y - x) * f for x, y in zip(a, b)]; L = math.sqrt(sum(x * x for x in q)); return [x / L for x in q]
        return [x + (y - x) * f for x, y in zip(a, b)]

    def local(i, time):          # time None = the rest pose in the file
        n = N[i]
        if "matrix" in n: m = n["matrix"]; return [[m[c * 4 + r] for c in range(4)] for r in range(4)]
        T, R, S = n.get("translation", [0, 0, 0]), n.get("rotation", [0, 0, 0, 1]), n.get("scale", [1, 1, 1])
        if time is not None: T, R, S = key(i, "translation", time, T), key(i, "rotation", time, R), key(i, "scale", time, S)
        return trs(T, R, S)

    def world(i, time, override=None):
        m = override if override is not None else local(i, time)
        while i in par:
            i = par[i]; m = mul(local(i, time), m)
        return m
    frames = int(round(tmax * 60)) + 1
    mesh_nodes = [i for i, n in enumerate(N) if "mesh" in n]
    tracks = []
    for k, i in enumerate(mesh_nodes):
        if "skin" in N[i]: continue                                        # the arms: raylib animates those
        Wr = inv(world(i, None)); name = j["meshes"][N[i]["mesh"]]["name"]
        tracks.append((name, k, 0, [mul(world(i, f / 60.0), Wr) for f in range(frames)]))
        if name.lower().startswith("slug"):
            seat = local(i, seat_f / 60.0)                                 # its place in the drum once loaded
            tracks.append((name, k, 1, [mul(world(i, f / 60.0, seat), Wr) for f in range(frames)]))
    out = path + ".parts"
    with open(out, "wb") as f:
        f.write(b"C2PT" + struct.pack("<iii", 1, len(tracks), frames))
        for name, k, seated, ms in tracks:
            f.write(name.encode()[:31].ljust(32, b"\0") + struct.pack("<ii", k, seated))
            for m in ms: f.write(struct.pack("<12f", *m[0], *m[1], *m[2]))
    print(f"{out}: {len(tracks)} tracks x {frames} frames")
    for name, k, seated, ms in tracks:
        mv = max(abs(ms[f][r][c] - ms[0][r][c]) for f in range(frames) for r in range(3) for c in range(4))
        print(f"  mesh {k:2d} {name:24s} {'seated' if seated else '      '} moves {mv:8.3f}   D(0) off identity {max(abs(ms[0][r][c] - (1.0 if r == c else 0.0)) for r in range(3) for c in range(4)):.4f}")


if __name__ == "__main__":
    main()
