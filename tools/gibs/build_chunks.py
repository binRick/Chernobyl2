#!/usr/bin/env python3
"""What a blast leaves of a man: eight chunks of meat and bone, and the three small pictures they wear.

    uv run --no-project --with numpy==2.5.5 --with pillow==13.0.0 python tools/gibs/build_chunks.py [--out DIR]

The game threw thirteen flat-coloured balls (src/gibs.h, once DrawSphereEx). These are what it throws now: three lumps of
muscle, two torn flaps, a long bone snapped at one end, a joint end, a piece of rib. Each is a closed mesh that lies on
y = 0 as it would lie on a floor (what would hang below is pressed flat), its furthest point from the y axis at 1, so the
game sets the size and stands it on the floor by its own lowest point.

Nothing here is modelled by hand. A lump is a sphere pushed in and out by three octaves of lattice noise, stretched,
creased, and torn ragged on one side. A bone is a lathed shaft -- knuckles at a joint end, a jagged rim and a dark
hollow where it snapped -- with meat on it by plain overlap (every part is closed by itself; nothing is cut into
anything). The colour is in the vertices (muscle, fat, clot; bone, stained toward the meat; darker in the hollows), so
one material serves all eight and the pictures only add grain, bumps and wetness:

    meat_c.png    a near-white grain the vertex colour is multiplied by (fibres, pores)
    meat_n.png    the same grain as bumps
    meat_orm.png  occlusion in the grain's hollows, low roughness and high shine (it is wet), and alpha 0: the game's lit
                  shader lets light bleed red through a surface whose mask is 0 (main.c: sss = 1 - alpha)

The model names no picture (tests/model-images.sh): the game binds the three by the material's name, once. The output
is the same bytes on every run (tests/gibs.sh; numpy and Pillow are pinned there so that it stays so)."""
import json, os, struct, sys
import numpy as np
from PIL import Image

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
NAMES = ("lump1", "lump2", "lump3", "flap1", "flap2", "bone1", "bone2", "rib1")
MUSCLE, DEEP, FAT, CLOT = (206, 50, 44), (146, 24, 26), (238, 198, 158), (88, 12, 14)        # (bright: the lit shader's halls are dim, and a pool of blood beside it is brighter still)
BONE, STAIN, MARROW = (244, 234, 212), (186, 56, 42), (88, 16, 16)


# ------------------------------------------------------------------ noise
class Noise:
    """Lattice value noise, the same on every machine (numpy's legacy generator), in -1 .. 1; wraps every 32 cells."""
    def __init__(self, seed): self.g = np.random.RandomState(seed).rand(32, 32, 32) * 2.0 - 1.0

    def __call__(self, p):
        p = np.asarray(p, np.float64); i = np.floor(p).astype(np.int64); f = p - i; f = f * f * (3.0 - 2.0 * f); out = 0.0
        for dx in (0, 1):
            for dy in (0, 1):
                for dz in (0, 1):
                    w = (f[:, 0] if dx else 1 - f[:, 0]) * (f[:, 1] if dy else 1 - f[:, 1]) * (f[:, 2] if dz else 1 - f[:, 2])
                    out = out + w * self.g[(i[:, 0] + dx) % 32, (i[:, 1] + dy) % 32, (i[:, 2] + dz) % 32]
        return out

    def fbm(self, p, octaves=3):
        p = np.asarray(p, np.float64); a = 1.0; s = 0.0; t = 0.0
        for k in range(octaves): s = s + a * self(p * (2.0 ** k) + 7.3 * k); t += a; a *= 0.5
        return s / t


def smooth(a, b, x): t = np.clip((x - a) / (b - a), 0.0, 1.0); return t * t * (3.0 - 2.0 * t)


def unit(v): return v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-12)


def rot(axis, deg):
    a = np.asarray(axis, np.float64); a = a / np.linalg.norm(a); c, s = np.cos(np.radians(deg)), np.sin(np.radians(deg)); x, y, z = a
    return np.array([[c + x * x * (1 - c), x * y * (1 - c) - z * s, x * z * (1 - c) + y * s], [y * x * (1 - c) + z * s, c + y * y * (1 - c), y * z * (1 - c) - x * s],
                     [z * x * (1 - c) - y * s, z * y * (1 - c) + x * s, c + z * z * (1 - c)]])


# ------------------------------------------------------------------ closed surfaces
def icosphere(n):
    t = (1.0 + 5.0 ** 0.5) / 2.0
    V = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t), (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    F = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
         (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    V = [tuple(np.asarray(v, np.float64) / np.linalg.norm(v)) for v in V]
    for _ in range(n):
        mid = {}; G = []
        def m(a, b):
            k = (min(a, b), max(a, b))
            if k not in mid: p = (np.asarray(V[a]) + np.asarray(V[b])) / 2.0; V.append(tuple(p / np.linalg.norm(p))); mid[k] = len(V) - 1
            return mid[k]
        for a, b, c in F: ab, bc, ca = m(a, b), m(b, c), m(c, a); G += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        F = G
    return np.array(V, np.float64), np.array(F, np.int64)


def lathe(axis_pts, radii, around, seed, oval=1.0, wob=0.06, cap0=0.0, cap1=0.0, jag1=0.0):
    """A tube about a polyline: ring k of `around` vertices at axis_pts[k], radius radii[k] (wobbled by noise, `oval` wide
    across the bend); each end closed by a fan to a point `cap` radii inside it (a hollow) or outside (a dome, cap < 0).
    jag1: the last rings' vertices are pushed along the axis by this much of the radius at random (a snapped end)."""
    A = np.asarray(axis_pts, np.float64); n = len(A); nz = Noise(seed); rs = np.random.RandomState(seed); V = []; F = []
    T = np.gradient(A, axis=0); T = unit(T); up = np.array([0.0, 1.0, 0.0])
    for k in range(n):
        s = np.cross(T[k], up); s = s / max(np.linalg.norm(s), 1e-9); u = np.cross(s, T[k])
        for a in range(around):
            th = 2 * np.pi * a / around; d = np.cos(th) * s * oval + np.sin(th) * u
            r = radii[k] * (1.0 + wob * nz.fbm(np.array([A[k] * 2.3 + d * 1.7 + 11.0]), 2)[0])
            p = A[k] + d * r
            if jag1 and k >= n - 2: p = p + T[k] * radii[k] * jag1 * (rs.rand() - (0.15 if k == n - 1 else 0.6))
            V.append(p)
    for k in range(n - 1):
        for a in range(around):
            b = (a + 1) % around; i0, i1, i2, i3 = k * around + a, k * around + b, (k + 1) * around + a, (k + 1) * around + b
            F += [(i0, i2, i1), (i1, i2, i3)]
    V.append(A[0] + T[0] * radii[0] * cap0); c0 = len(V) - 1; V.append(A[-1] - T[-1] * radii[-1] * cap1); c1 = len(V) - 1
    for a in range(around):
        b = (a + 1) % around; F.append((c0, a, b)); F.append((c1, (n - 1) * around + b, (n - 1) * around + a))
    return np.array(V, np.float64), np.array(F, np.int64)


def outward(V, F):
    """Faces wound so that the surface's volume is positive (front faces out)."""
    a, b, c = V[F[:, 0]], V[F[:, 1]], V[F[:, 2]]
    return F if np.einsum("ij,ij->i", a, np.cross(b, c)).sum() > 0 else F[:, ::-1].copy()


def normals(V, F):
    n = np.cross(V[F[:, 1]] - V[F[:, 0]], V[F[:, 2]] - V[F[:, 0]]); N = np.zeros_like(V)
    for k in range(3): np.add.at(N, F[:, k], n)
    return unit(N)


def hollows(V, F, N):
    """0 .. 1 at each vertex: how far it lies below its neighbours' middle, against the mesh's own grain (a hollow is dark)."""
    S = np.zeros_like(V); C = np.zeros(len(V))
    for a, b in ((0, 1), (1, 2), (2, 0)):
        np.add.at(S, F[:, a], V[F[:, b]]); np.add.at(C, F[:, a], 1.0); np.add.at(S, F[:, b], V[F[:, a]]); np.add.at(C, F[:, b], 1.0)
    d = np.einsum("ij,ij->i", S / np.maximum(C, 1.0)[:, None] - V, N); e = np.linalg.norm(V[F[:, 0]] - V[F[:, 1]], axis=1).mean()
    return np.clip(d / (0.35 * e), 0.0, 1.0)


# ------------------------------------------------------------------ the parts
def meat(seed, stretch, level=3, rough=1.0, tear=0.5, crease=0.16, lie=True):
    """A lump of muscle: positions, faces, colours. lie: turned so that it lies on its broadest side, its length along x
    (a part placed on a bone is not turned: `stretch` is then its length, height and width as given)."""
    D, F = icosphere(level); nz = Noise(seed); rs = np.random.RandomState(seed); o = rs.rand(5, 3) * 20.0
    r = 1.0 + rough * (0.36 * nz.fbm(D * 1.2 + o[0]) + 0.15 * nz.fbm(D * 2.7 + o[1]) + 0.035 * nz.fbm(D * 6.4 + o[2], 2))
    r = r - crease * (1.0 - np.abs(nz(D * 2.6 + o[3]))) ** 2                       # creases between the bundles of muscle
    t = unit(rs.randn(3)); w = smooth(0.15, 0.95, D @ t)                           # one side torn away, ragged
    r = r * (1.0 - tear * w * (0.62 + 0.38 * nz.fbm(D * 5.0 + o[1], 2)))
    V = D * r[:, None]
    if lie:                                          # (shaped first, then laid down: the tear and the creases fall where they fall)
        V = V @ rot(rs.randn(3), rs.rand() * 360.0).T * np.asarray(stretch, np.float64)
        ev, evec = np.linalg.eigh(np.cov((V - V.mean(0)).T)); R = np.stack([evec[:, 2], evec[:, 0], np.cross(evec[:, 2], evec[:, 0])])     # longest -> x, shortest -> y
        V = (V - V.mean(0)) @ R.T
    else: V = V * np.asarray(stretch, np.float64)
    fat = smooth(0.22, 0.55, nz.fbm(V * 2.3 + o[2], 2)) * smooth(-0.2, 0.35, nz(V * 4.5 + o[0]))          # fat and sinew: pale patches and streaks
    deep = smooth(-0.25, 0.45, nz.fbm(V * 1.4 + o[3], 2)); clot = smooth(0.35, 0.7, nz.fbm(V * 1.9 + o[4], 2))
    C = np.outer(1 - deep, MUSCLE) + np.outer(deep, DEEP); C = C * (1 - 0.85 * fat)[:, None] + np.outer(0.85 * fat, FAT)
    C = C * (1 - 0.7 * clot)[:, None] + np.outer(0.7 * clot, CLOT)
    C = C * (1 - 0.45 * w)[:, None] + np.outer(0.45 * w, CLOT)                       # the torn face is the darkest
    return V, F, C


def flap(seed, stretch):
    """A torn flap: a lump pressed thin, its outline ragged, one edge curled."""
    V, F, C = meat(seed, (1.0, 1.0, 1.0), rough=0.7, tear=0.25, crease=0.05, lie=False); nz = Noise(seed + 5)
    ang = np.arctan2(V[:, 2], V[:, 0]); edge = 1.0 + 0.38 * nz.fbm(np.stack([np.cos(ang) * 2.2, np.sin(ang) * 2.2, np.zeros(len(V))], 1) + 3.0, 3)
    V = V * np.stack([edge, np.ones(len(V)), edge], 1) * np.asarray(stretch, np.float64)
    V[:, 1] *= 0.5 + 0.5 * smooth(1.1, 0.2, np.hypot(V[:, 0], V[:, 2]) / np.hypot(V[:, 0], V[:, 2]).max())     # thin at the rim
    V[:, 1] += 0.55 * smooth(0.25, 1.0, V[:, 0] / np.abs(V[:, 0]).max()) ** 2 * np.abs(V[:, 0]).max() * 0.5    # one edge curls up
    rim = smooth(0.55, 1.0, np.hypot(V[:, 0], V[:, 2]) / np.hypot(V[:, 0], V[:, 2]).max()) * (0.5 + 0.5 * nz.fbm(V * 3.0 + 9.0, 2))
    C = C * (1 - 0.6 * rim)[:, None] + np.outer(0.6 * rim, FAT)                    # skin and fat show pale at the torn rim
    return V, F, C


def bone(seed, pts, radii, around=12, oval=1.0, snapped=True, knob=True):
    """A bone: a lathed shaft; its last end snapped (a jagged rim about a dark hollow) or a blunt dome."""
    V, F = lathe(pts, radii, around, seed, oval=oval, wob=0.08, cap0=-0.45 if knob else -0.2, cap1=0.9 if snapped else -0.3, jag1=1.1 if snapped else 0.0)
    nz = Noise(seed + 9); C = np.tile(np.asarray(BONE, np.float64), (len(V), 1)) * (0.93 + 0.07 * nz.fbm(V * 4.0, 2))[:, None]
    if snapped: C[-1] = MARROW                                                        # the hollow's far point: marrow
    return V, F, C


def stain(V, C, at, reach, seed):
    """Blood on a bone, strongest toward `at` (where the meat is, or the break)."""
    nz = Noise(seed); d = np.linalg.norm(V - np.asarray(at, np.float64), axis=1); w = smooth(reach, reach * 0.25, d) * (0.55 + 0.45 * nz.fbm(V * 3.0 + 5.0, 2))
    return C * (1 - w)[:, None] + np.outer(w, STAIN)


def place(V, R=None, scale=1.0, at=(0, 0, 0)):
    V = V * np.asarray(scale, np.float64)
    if R is not None: V = V @ R.T
    return V + np.asarray(at, np.float64)


def chunk(name):
    """[(V, F, C)] of the closed parts of one chunk, and how deep it is pressed into the floor (in its own height)."""
    if name == "lump1": return [meat(11, (1.0, 0.72, 0.85), tear=0.55)], 0.20
    if name == "lump2": return [meat(23, (1.35, 0.6, 0.75), rough=1.15, tear=0.4)], 0.22
    if name == "lump3": return [meat(37, (0.95, 0.7, 1.0), rough=0.9, tear=0.62, crease=0.22)], 0.18
    if name == "flap1": return [flap(41, (1.25, 0.2, 0.8))], 0.30
    if name == "flap2": return [flap(53, (1.0, 0.16, 1.1))], 0.30
    if name == "bone1":                              # a long bone, a knuckle at one end, snapped at the other, meat about the knuckle
        x = np.linspace(-1.0, 1.0, 15); pts = np.stack([x, 0.05 * np.sin((x + 1) * 1.4), np.zeros_like(x)], 1)
        rad = 0.125 + 0.17 * np.exp(-((x + 0.92) / 0.2) ** 2) + 0.03 * np.exp(-((x - 0.8) / 0.3) ** 2); rad[0] *= 0.82
        Vb, Fb, Cb = bone(61, pts, rad, oval=1.12)
        Vm, Fm, Cm = meat(67, (0.50, 0.30, 0.40), level=3, rough=1.25, tear=0.6, lie=False); Vm = place(Vm, at=(-0.50, 0.02, 0.06))
        Vs, Fs, Cs = meat(71, (0.42, 0.10, 0.17), level=2, rough=1.1, tear=0.35, lie=False); Vs = place(Vs, rot((0, 1, 0), 14), at=(0.02, 0.06, 0.03))     # a shred along the shaft
        Cb = stain(Vb, Cb, (-0.5, 0, 0), 0.9, 62); Cb = stain(Vb, Cb, (1.0, 0, 0), 0.4, 63)
        return [(Vb, Fb, Cb), (Vm, Fm, Cm), (Vs, Fs, Cs)], 0.15
    if name == "bone2":                              # a joint end: two knuckles on a stub of shaft, half buried in meat
        x = np.linspace(-0.55, 0.6, 10); pts = np.stack([x, np.zeros_like(x), np.zeros_like(x)], 1)
        rad = 0.16 + 0.17 * np.exp(-((x + 0.45) / 0.22) ** 2); rad[0] *= 0.8
        Vb, Fb, Cb = bone(79, pts, rad, oval=1.18)
        Vm, Fm, Cm = meat(83, (0.62, 0.36, 0.52), level=3, rough=1.3, tear=0.6, lie=False); Vm = place(Vm, rot((0, 1, 0), 20), at=(0.22, 0.0, 0.16))
        Vs, Fs, Cs = meat(85, (0.36, 0.2, 0.3), level=2, rough=1.2, tear=0.4, lie=False); Vs = place(Vs, at=(0.05, 0.02, -0.26))
        Cb = stain(Vb, Cb, (0.2, 0, 0.1), 0.8, 80)
        return [(Vb, Fb, Cb), (Vm, Fm, Cm), (Vs, Fs, Cs)], 0.14
    if name == "rib1":                               # a piece of rib: a flat curved bone, snapped, the meat between the ribs still on it
        th = np.radians(np.linspace(-58, 58, 15)); pts = np.stack([0.95 * np.sin(th), np.zeros_like(th), 0.95 * (np.cos(th) - 0.78)], 1)
        rad = np.full(len(th), 0.085); rad[0] = 0.07
        Vb, Fb, Cb = bone(89, pts, rad, around=10, oval=0.62, knob=False); Vb[:, 1] *= 0.75
        th2 = np.radians(np.linspace(-44, 34, 4)); parts = [(Vb, Fb, stain(Vb, Cb, (0.2, 0, 0.3), 0.9, 90))]
        for k, t in enumerate(th2):                  # the strip of muscle along its inner curve, in four lumps that overlap
            Vm, Fm, Cm = meat(97 + 4 * k, (0.30, 0.085, 0.17), level=2, rough=1.0, tear=0.3, lie=False)
            parts.append((place(Vm, rot((0, 1, 0), np.degrees(t)), at=(0.80 * np.sin(t), 0.01, 0.80 * np.cos(t) - 0.95 * 0.78)), Fm, Cm))
        return parts, 0.12
    raise KeyError(name)


def build(name):
    """One chunk, ready to draw: positions, normals, texture coordinates, colours (0..255), triangles."""
    parts, sink = chunk(name); allV = np.vstack([p[0] for p in parts])
    lo, hi = allV[:, 1].min(), allV[:, 1].max(); floor = lo + sink * (hi - lo)
    mid = (allV[:, [0, 2]].max(0) + allV[:, [0, 2]].min(0)) / 2.0
    Vs = []; Fs = []; Ns = []; Cs = []; base = 0
    for V, F, C in parts:
        V = V - np.array([mid[0], floor, mid[1]]); y = V[:, 1]
        sag = 1.0 + 0.07 * np.exp(-np.maximum(y, 0.0) / (0.16 * (hi - lo)))           # what rests on the floor spreads a little
        V = np.stack([V[:, 0] * sag, np.maximum(y, 0.0), V[:, 2] * sag], 1)
        F = outward(V, F); N = normals(V, F); h = hollows(V, F, N)
        C = np.asarray(C, np.float64) * (1.0 - 0.26 * h)[:, None]                     # darker in the hollows
        under = smooth(0.10 * (hi - lo), 0.0, V[:, 1]); C = C * (1.0 - 0.22 * under)[:, None]       # and where it meets the floor (its own blood)
        Vs.append(V); Fs.append(F + base); Ns.append(N); Cs.append(C); base += len(V)
    V, F, N, C = np.vstack(Vs), np.vstack(Fs), np.vstack(Ns), np.vstack(Cs)
    V = V / np.hypot(V[:, 0], V[:, 2]).max()                                         # a unit across; the lowest point stays on y = 0
    # texture coordinates by the face's dominant axis (the grain has no seams to hide: a vertex is repeated where two
    # faces look along different axes)
    fn = np.cross(V[F[:, 1]] - V[F[:, 0]], V[F[:, 2]] - V[F[:, 0]]); ax = np.abs(fn).argmax(1); key = {}; P2 = []; N2 = []; C2 = []; U2 = []; F2 = []
    for t, a in zip(F, ax):
        tri = []
        for v in t:
            k = (int(v), int(a))
            if k not in key:
                key[k] = len(P2); p = V[v]; P2.append(p); N2.append(N[v]); C2.append(C[v]); U2.append((p[1], p[2]) if a == 0 else (p[0], p[2]) if a == 1 else (p[0], p[1]))
            tri.append(key[k])
        F2.append(tri)
    col = np.concatenate([np.clip(np.round(np.array(C2)), 0, 255), np.full((len(C2), 1), 255.0)], 1).astype(np.uint8)
    return np.array(P2, np.float32), np.array(N2, np.float32), (np.array(U2) * 0.8).astype(np.float32), col, np.array(F2, np.uint16)


# ------------------------------------------------------------------ the pictures (256 px, tiling)
def tile_noise(seed, n, cells_x, cells_y):
    """Value noise on an n x n picture that repeats at its edges, in -1 .. 1."""
    g = np.random.RandomState(seed).rand(cells_y, cells_x) * 2.0 - 1.0
    y, x = np.mgrid[0:n, 0:n].astype(np.float64); x = x * cells_x / n; y = y * cells_y / n
    xi, yi = np.floor(x).astype(int), np.floor(y).astype(int); fx, fy = x - xi, y - yi; fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy)
    a = g[yi % cells_y, xi % cells_x]; b = g[yi % cells_y, (xi + 1) % cells_x]; c = g[(yi + 1) % cells_y, xi % cells_x]; d = g[(yi + 1) % cells_y, (xi + 1) % cells_x]
    return a * (1 - fx) * (1 - fy) + b * fx * (1 - fy) + c * (1 - fx) * fy + d * fx * fy


def pictures(n=256):
    fibre = 0.55 * tile_noise(3, n, 6, 48) + 0.30 * tile_noise(5, n, 12, 96) + 0.15 * tile_noise(7, n, 24, 24)       # muscle runs one way
    lumps = 0.6 * tile_noise(13, n, 8, 8) + 0.4 * tile_noise(17, n, 16, 16)
    h = 0.6 * fibre + 0.4 * lumps; h = (h - h.min()) / (h.max() - h.min())                                           # the grain's height, 0 .. 1
    g = 0.80 + 0.20 * h
    colour = np.stack([g, g * (0.90 + 0.06 * h), g * (0.88 + 0.07 * h)], 2)
    s = 5.0; dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * s; dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * s
    nrm = np.stack([-dx, -dy, np.ones_like(h)], 2); nrm = nrm / np.linalg.norm(nrm, axis=2, keepdims=True)
    wet = 0.5 + 0.5 * tile_noise(19, n, 10, 10)
    orm = np.stack([0.70 + 0.30 * h, 0.20 + 0.16 * wet, 0.84 + 0.12 * (1 - wet), np.zeros_like(h)], 2)
    as8 = lambda a: np.clip(np.round(a * 255.0), 0, 255).astype(np.uint8)
    return {"meat_c.png": Image.fromarray(as8(colour), "RGB"), "meat_n.png": Image.fromarray(as8(nrm * 0.5 + 0.5), "RGB"), "meat_orm.png": Image.fromarray(as8(orm), "RGBA")}


# ------------------------------------------------------------------ out
def write(out_dir):
    os.makedirs(out_dir, exist_ok=True); blob = bytearray(); views = []; accs = []; meshes = []
    def put(a, ctype, typ, target, minmax=False, norm=False):
        while len(blob) % 4: blob.append(0)
        raw = np.ascontiguousarray(a).tobytes(); views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(raw), "target": target}); blob.extend(raw)
        acc = {"bufferView": len(views) - 1, "componentType": ctype, "count": len(a), "type": typ}
        if norm: acc["normalized"] = True
        if minmax: acc["min"] = [float(x) for x in a.min(0)]; acc["max"] = [float(x) for x in a.max(0)]
        accs.append(acc); return len(accs) - 1
    for name in NAMES:
        P, N, U, C, F = build(name)
        prim = {"attributes": {"POSITION": put(P, 5126, "VEC3", 34962, True), "NORMAL": put(N, 5126, "VEC3", 34962), "TEXCOORD_0": put(U, 5126, "VEC2", 34962),
                               "COLOR_0": put(C, 5121, "VEC4", 34962, norm=True)}, "indices": put(F.reshape(-1, 1), 5123, "SCALAR", 34963), "material": 0}
        meshes.append({"name": name, "primitives": [prim]})
        print("%-6s %5d vertices %5d triangles, %.2f high" % (name, len(P), len(F), float(P[:, 1].max())))
    while len(blob) % 4: blob.append(0)
    js = {"asset": {"version": "2.0", "generator": "tools/gibs/build_chunks.py", "extras": {"title": "flesh chunks", "author": "built by tools/gibs/build_chunks.py for Chernobyl 2"}},
          "scene": 0, "scenes": [{"nodes": list(range(len(NAMES)))}], "nodes": [{"name": n, "mesh": k} for k, n in enumerate(NAMES)], "meshes": meshes,
          "materials": [{"name": "meat", "pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 1.0}}],
          "accessors": accs, "bufferViews": views, "buffers": [{"uri": "chunks.bin", "byteLength": len(blob)}]}
    open(os.path.join(out_dir, "chunks.bin"), "wb").write(bytes(blob))
    open(os.path.join(out_dir, "chunks.gltf"), "w").write(json.dumps(js, separators=(",", ":"), sort_keys=True) + "\n")
    for f, im in pictures().items(): im.save(os.path.join(out_dir, f), format="PNG", compress_level=9)
    print("%s: chunks.gltf, chunks.bin (%.0f KB), three pictures" % (out_dir, len(blob) / 1024.0))


if __name__ == "__main__":
    a = sys.argv[1:]; write(a[a.index("--out") + 1] if "--out" in a else os.path.join(REPO, "assets", "gibs"))
