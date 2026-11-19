#!/bin/bash
# What a blast leaves of a man (owner, 2026-11-18: "i saw little flesh pieces on the floor. they are simple object. can you
# increase their quality to be real chunks"). They were thirteen flat-coloured balls, unlit. They are chunks of meat and
# bone now: eight shapes built by tools/gibs/build_chunks.py, drawn through the enemies' lit shader (src/gibs.h).
#   files   eight closed meshes, every part of each right side out, that lie flat and are not balls, coloured as meat and
#           bone, no picture named; three small tiling pictures, the wet one letting light bleed through; and the tool
#           builds the same bytes again.
#   look    loaded with the guns (and, loaded on the loader's thread, made drawable on the drawing thread); the eight laid
#           in a row are each in the picture, shaded where a ball was one flat colour, red, the bones showing bone and
#           the meat none, neither black nor glowing, and the room's lamps light them; without the files the row is the
#           balls, and they are in the picture too.
#   pools   a chunk lies ON the blood it lands in, not under it: as much is seen of each with its wet mark as without.
#   burst   a burst is thirteen of the eight shapes, large and small, its lamps looked up once; they tumble in the air,
#           come to rest lying on the floor as they are drawn, and stay 20 to 28 seconds.
#   end     what a burst leaves is seen on its blood; in its last second each shrinks away where it lies; then it is gone.
#   breach  a piece that comes down over a breach goes down it; the rest lie at its lip; the lamps stay the lip's.
#   deck    a piece that comes down inside a raised deck's footprint ends on top of it; one past a deck's edge, on the floor below.
#   balls   without the files a burst throws the balls it always did, and the game runs on.
# To show a check failing on a build with a fault put back: GIBS_BIN=<game> GIBS_PART="<parts>" GIBS_DIR=<chunk files>.
cd "$(dirname "$0")/.." || exit 1
make -s all >/dev/null || { echo "FAIL build"; exit 1; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
export GIBS_PART="${GIBS_PART:-files look pools burst end breach deck balls}" GIBS_DIR="${GIBS_DIR:-assets/gibs}"
NP="uv run --quiet --no-project --with numpy==2.5.5 --with pillow==13.0.0 python"      # (pinned: the tool's bytes must not drift with a release)
echo 0 > "$T/files"; echo 1 > "$T/game"             # (a part that does not run to its end has failed)
case " $GIBS_PART " in *" files "*) echo 1 > "$T/files"
if [ -f tools/gibs/build_chunks.py ]; then $NP tools/gibs/build_chunks.py --out "$T/built" >/dev/null 2>"$T/err" || { echo "FAIL tools/gibs/build_chunks.py did not run: $(tail -1 "$T/err")"; exit 1; }; fi
python3 - "$T" <<'PY'
import json, math, os, struct, sys
from PIL import Image
tmp = sys.argv[1]; fail = 0
def ok(t): print("ok   " + t)
def bad(t):
    global fail; print("FAIL " + t); fail = 1
D = os.environ["GIBS_DIR"]; NAMES = "lump1 lump2 lump3 flap1 flap2 bone1 bone2 rib1".split()
if not os.path.exists(f"{D}/chunks.gltf"):
    bad(f"no {D}/chunks.gltf: what a blast leaves is still the flat-coloured balls"); print("FAIL"); open(os.path.join(tmp, "files"), "w").write("1"); sys.exit(0)
j = json.load(open(f"{D}/chunks.gltf")); b = open(f"{D}/" + j["buffers"][0]["uri"], "rb").read()
SZ = {5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4}; NC = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}; FMT = {5121: "B", 5123: "H", 5125: "I", 5126: "f"}
def arr(i):
    a = j["accessors"][i]; bv = j["bufferViews"][a["bufferView"]]; o = bv.get("byteOffset", 0) + a.get("byteOffset", 0); n = NC[a["type"]]
    v = struct.unpack_from("<%d%s" % (a["count"] * n, FMT[a["componentType"]]), b, o); return [v[k:k + n] for k in range(0, len(v), n)]
def sub(a, c): return [x - y for x, y in zip(a, c)]
def cross(u, v): return [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
(ok if [m.get("name") for m in j["meshes"]] == NAMES and [j["nodes"][n].get("mesh") for n in j["scenes"][0]["nodes"]] == list(range(8)) else bad)(f"eight chunks, in order: {' '.join(m.get('name', '?') for m in j['meshes'])}")
refs = sum(1 for m in j.get("materials", []) for k in ("normalTexture", "occlusionTexture", "emissiveTexture") if k in m) + sum(1 for m in j.get("materials", []) for k in ("baseColorTexture", "metallicRoughnessTexture") if k in m.get("pbrMetallicRoughness", {}))
(ok if not j.get("images") and refs == 0 and [m.get("name") for m in j.get("materials", [])] == ["meat"] else bad)(f"the file names no picture (the game binds its three, by their file names, once): images {len(j.get('images', []))}, references {refs}")
for m in j["meshes"]:
    nm = m.get("name"); p = m["primitives"][0]; at = p["attributes"]
    if not all(k in at for k in ("POSITION", "NORMAL", "TEXCOORD_0", "COLOR_0")): bad(f"{nm}: lacks one of position, normal, texture coordinate, colour ({sorted(at)})"); continue
    P = arr(at["POSITION"]); C = arr(at["COLOR_0"]); I = [x[0] for x in arr(p["indices"])]; T = [I[k:k + 3] for k in range(0, len(I), 3)]
    weld = {}; W = [weld.setdefault(tuple(round(c, 4) for c in v), len(weld)) for v in P]; edges = {}
    par = list(range(len(weld)))                    # its parts: the pieces of surface that share no vertex (a rib and the meat left on it)
    def find(a):
        while par[a] != a: par[a] = par[par[a]]; a = par[a]
        return a
    for t in T:
        r = find(W[t[0]]); par[find(W[t[1]])] = r; par[find(W[t[2]])] = r
        for k in range(3):
            e = (min(W[t[k]], W[t[k - 1]]), max(W[t[k]], W[t[k - 1]])); edges[e] = edges.get(e, 0) + 1
    odd = sum(1 for c in edges.values() if c != 2)
    area = vol = low = pale = red = 0.0; vols = {}  # (colour by area: a bone's shaft is few vertices and much of what is seen)
    isp = [c[1] > 110 and c[2] > 90 for c in C]; isr = [c[0] > 90 and c[0] > 2 * c[1] for c in C]
    for t in T:
        a, c, d = P[t[0]], P[t[1]], P[t[2]]; n = cross(sub(c, a), sub(d, a)); ar = math.sqrt(sum(x * x for x in n)) / 2; area += ar
        pale += ar * sum(isp[v] for v in t) / 3; red += ar * sum(isr[v] for v in t) / 3
        v6 = (a[0] * (c[1] * d[2] - c[2] * d[1]) - a[1] * (c[0] * d[2] - c[2] * d[0]) + a[2] * (c[0] * d[1] - c[1] * d[0])) / 6
        vol += v6; r = find(W[t[0]]); vols[r] = vols.get(r, 0.0) + v6
        if max(a[1], c[1], d[1]) < 0.03: low += ar
    ball = (36 * math.pi * vol * vol) ** (1 / 3) / area; rad = max(math.hypot(v[0], v[2]) for v in P); ymin = min(v[1] for v in P)
    pale /= area; red /= area; inside_out = sum(1 for v in vols.values() if v <= 0)
    limit = 0.60 if nm.startswith("flap") else 0.86; boney = nm.startswith(("bone", "rib"))
    good = 150 <= len(T) <= 2000 and odd == 0 and abs(ymin) < 1e-4 and low / area >= 0.05 and 0.98 <= rad <= 1.0001 and vol > 0 and inside_out == 0 and ball < limit and red >= 0.25 and (pale >= 0.12 if boney else True)
    (ok if good else bad)(f"{nm}: {len(T)} triangles (150 to 2000), closed (edges not shared by two: {odd}), its {len(vols)} part{'s' if len(vols) != 1 else ''} right side out ({inside_out} inside out), lies flat (lowest point {ymin:.4f}; "
                          f"{low / area:.0%} of it on the floor, 5% or more), reaches 1 from its middle ({rad:.3f}), not a ball ({ball:.2f} of one; under {limit}), {red:.0%} red meat (25% or more)" + (f", {pale:.0%} pale bone (12% or more)" if boney else ""))
pics = {}
for n, mode in (("meat_c", "RGB"), ("meat_n", "RGB"), ("meat_orm", "RGBA")):
    f = f"{D}/{n}.png"
    if not os.path.exists(f): bad(f"no {f}"); continue
    pics[n] = Image.open(f); (ok if pics[n].size == (256, 256) and pics[n].mode == mode else bad)(f"{n}.png: {pics[n].size[0]} x {pics[n].size[1]} {pics[n].mode} (256 x 256 {mode})")
if "meat_orm" in pics:
    px = list(pics["meat_orm"].getdata()); g = sum(p[1] for p in px) / len(px); bl = sum(p[2] for p in px) / len(px); amax = max(p[3] for p in px)
    (ok if amax == 0 and g < 110 and bl > 180 else bad)(f"it is wet, and light bleeds through it: roughness {g:.0f} of 255 (under 110), shine {bl:.0f} (over 180), the bleed's mask {amax} everywhere (0)")
same = all(os.path.exists(os.path.join(tmp, "built", f)) and open(os.path.join(tmp, "built", f), "rb").read() == open(f"{D}/{f}", "rb").read() for f in ("chunks.gltf", "chunks.bin", "meat_c.png", "meat_n.png", "meat_orm.png"))
(ok if same else bad)("the shipped files are what the tool builds")
open(os.path.join(tmp, "files"), "w").write(str(fail))
PY
;; esac
# ---- the game. Every run is hidden, from a folder of its own (nothing it writes reaches the repo), and the pieces are put
# there by src/gibs.h's dev hooks: CHERNOBYL_GIB_ROW (the eight shapes in a row, each let fall 4 cm so that it lands as a
# thrown one does), CHERNOBYL_GIBS (a burst), CHERNOBYL_GIB_DRY / _KEY / _DARK (from that frame: no blood / each chunk flat
# green / the room's lamps left out), CHERNOBYL_GIB_LIFE (how long they stay). Where each piece is in the picture, and how
# it lies as drawn, is what the game's debug log says of it at rest ("gibrest").
python3 - "$T" <<'PY'
import json, os, shutil, subprocess, sys
from PIL import Image
tmp = sys.argv[1]; fail = 0
BIN = os.path.abspath(os.environ.get("GIBS_BIN") or "build/chernobyl2"); PART = os.environ["GIBS_PART"].split()
NAMES = "lump1 lump2 lump3 flap1 flap2 bone1 bone2 rib1".split(); LUMPS = (0, 1, 2); BONES = (5, 6, 7)
def ok(t): print("ok   " + t)
def bad(t):
    global fail; print("FAIL " + t); fail = 1
def stage(name, without=()):                        # a folder to run the game from: the repo's assets, less `without`
    d = os.path.join(tmp, name); os.makedirs(os.path.join(d, "assets")); os.makedirs(os.path.join(d, "build", "shots"))
    for f in os.listdir("assets"):
        if f not in without: os.symlink(os.path.abspath(os.path.join("assets", f)), os.path.join(d, "assets", f))
    os.symlink(os.path.abspath("maps"), os.path.join(d, "maps"))
    for f in os.listdir("."):
        if f.endswith(".txt") and (f.startswith("vm_tune") or f == "options.txt"): shutil.copy(f, d)
    return d
def run(args, cwd, env=None):
    log = os.path.join(cwd, "chernobyl2-debug.log"); shots = os.path.join(cwd, "build", "shots")
    for f in [log] + [os.path.join(shots, f) for f in os.listdir(shots)]:
        if os.path.exists(f): os.remove(f)
    r = subprocess.run(["perl", "-e", "alarm 300; exec @ARGV", BIN, "--god", "--no-enemies", "--no-intro", "--mute", "--debug"] + args,
                       env=dict(os.environ, CHERNOBYL_BACKGROUND="1", CHERNOBYL_CLEAN_HUD="1", **(env or {})), cwd=cwd, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ev = []
    for line in open(log) if os.path.exists(log) else []:
        try: ev.append(json.loads(line))
        except Exception: pass
    return ev, r.returncode
def one(ev, name): return next((e for e in ev if e.get("ev") == name), {})
def all_(ev, name): return [e for e in ev if e.get("ev") == name]
W, H = 1440, 932                                    # pictures are measured at this size
def pic(cwd, name):
    p = os.path.join(cwd, "build", "shots", name)
    if not os.path.exists(p): return None
    im = Image.open(p).convert("RGB"); return (im if im.size == (W, H) else im.resize((W, H), Image.BILINEAR)).tobytes()
def lum(D, i): return 0.2126 * D[i] + 0.7152 * D[i + 1] + 0.0722 * D[i + 2]
def differ(A, B, i, by=20): return max(abs(A[i] - B[i]), abs(A[i + 1] - B[i + 1]), abs(A[i + 2] - B[i + 2])) > by
def red(D, i): return D[i] > D[i + 1] * 1.5 and D[i] > D[i + 2] * 1.5
def green(D, i): return D[i + 1] >= 200 and D[i] <= 70 and D[i + 2] <= 70      # a chunk drawn flat green (CHERNOBYL_GIB_KEY); nothing else in the picture is (the crosshair is blue-green)
def pale(D, i): return D[i + 1] > 0.62 * D[i] and D[i] > 70
def patches(rest):                                  # the patch of the picture each resting piece of a row is in: the columns nearer it than its neighbours, 0.07 of the
    xs = [e["sx"] * W for e in rest]                # picture's height above and below it (clear of the gun in his hands, the crosshair's column aside)
    own = [min(range(len(xs)), key=lambda k: abs(xs[k] - x)) if xs and min(abs(c - x) for c in xs) < 0.04 * W else -1 for x in range(W)]
    return own, max(0, int((min(e["sy"] for e in rest) - 0.07) * H)), min(H, int((max(e["sy"] for e in rest) + 0.07) * H))
def mm(v): return f"{v * 1000:.1f}"
LIFT = (0.0145, 0.0165)                             # where the foot of a chunk is drawn: 15 mm above the floor, on the blood (its pools are films 7 to 14 mm up)
HALL = {"CHERNOBYL_AT": "-38 -27.7"}; DOWN = ["--look", "180", "-40"]     # the rows are laid under the hall's lamp at (-38, -30), 4.7 m above the floor
at = stage("with"); bare = stage("without", ("gibs",))

if "look" in PART:
    ev, rc = run(DOWN + ["--shot-every", "30", "--frames", "185"], at, dict(HALL, CHERNOBYL_GIB_ROW="40", CHERNOBYL_GIB_DRY="0", CHERNOBYL_GIB_DARK="130"))
    g = one(ev, "gibs"); told = next((k for k, e in enumerate(ev) if e.get("ev") == "gibs" and "meshes" in e), None); done = next((k for k, e in enumerate(ev) if e.get("ev") == "load" and e.get("done")), None)
    (ok if g.get("meshes") == 8 and g.get("tex") == 3 and g.get("lit") == 1 and told is not None and done is not None and told < done else bad)(
        f"the chunks are loaded with the guns, before the game starts ({'before' if told is not None and done is not None and told < done else 'NOT before'} the loading is done): {g.get('meshes')} shapes, {g.get('tex')} pictures, through the lit shader ({g.get('lit')})")
    if done is not None and ev[done].get("thread"):
        v = next((e["vaos"] for e in ev if e.get("ev") == "gibs" and "vaos" in e), None)
        (ok if v == 8 else bad)(f"loaded on the loader's thread, their vertex arrays are made again on the drawing thread ({v} of 8; this Mac's driver shares them between the two, so the picture cannot show it)")
    else: ok("(this run loaded on the drawing thread: no vertex arrays to make again)")
    rest = sorted(all_(ev, "gibrest"), key=lambda e: e["sx"]); A, B, C = pic(at, "f0030.png"), pic(at, "f0090.png"), pic(at, "f0180.png")
    if len(rest) != 8 or not (A and B and C): bad(f"the row of eight was not laid and pictured ({len(rest)} at rest)")
    else:
        own, y0, y1 = patches(rest); st = [dict(n=0, pale=0, lit=0.0, dark=0.0) for _ in rest]; reds = []
        for y in range(y0, y1):
            for x in range(W):
                k = own[x]; i = (y * W + x) * 3
                if k >= 0 and differ(A, B, i):       # what changed in the picture when the row was laid IS the chunks (there is no blood in this run)
                    s = st[k]; s["n"] += 1; s["pale"] += pale(B, i); s["lit"] += lum(B, i); s["dark"] += lum(C, i)
                    if red(B, i): reds.append(lum(B, i))
        n = sum(s["n"] for s in st); m = max(1, n); reds.sort(); q = lambda f: reds[int(f * (len(reds) - 1))] if reds else 1.0
        least = min(range(8), key=lambda k: st[k]["n"])
        (ok if st[least]["n"] >= 150 else bad)(f"laid in a row, each of the eight is in the picture (the least of them, {NAMES[rest[least]['kind']]}: {st[least]['n']} pixels; 150 or more)")
        look = dict(spread=(q(0.9) - q(0.1)) / max(q(0.5), 1.0), red=len(reds) / m, lit=sum(s["lit"] for s in st) / m, dark=sum(s["dark"] for s in st) / m)
        bone = min(st[k]["pale"] / max(1, st[k]["n"]) for k in range(8) if rest[k]["kind"] in BONES); meat = max(st[k]["pale"] / max(1, st[k]["n"]) for k in range(8) if rest[k]["kind"] in LUMPS)
        (ok if look["red"] >= 0.45 else bad)(f"they are red ({look['red']:.0%} of them; 45% or more)")
        (ok if bone >= 0.15 and meat <= 0.10 else bad)(f"and each is the shape it is meant to be: the two bones and the rib show bone (the least of the three {bone:.0%} pale; 15% or more), the three lumps none (the most {meat:.0%}; 10% or less)")
        (ok if 22 <= look["lit"] <= 140 else bad)(f"neither black nor glowing (brightness {look['lit']:.0f} of 255; 22 to 140)")
        (ok if look["lit"] >= 1.25 * look["dark"] else bad)(f"and the room's lamps light them: with the lamps left out of their light they are {look['dark']:.0f}, with them {look['lit']:.0f} ({look['lit'] / max(1e-6, look['dark']):.2f} times; 1.25 or more)")
        # the same row without the files: the balls, which must be there too, and are flat colour
        ev2, rc2 = run(DOWN + ["--shot-every", "30", "--frames", "95"], bare, dict(HALL, CHERNOBYL_GIB_ROW="40", CHERNOBYL_GIB_DRY="0"))
        A2, B2 = pic(bare, "f0030.png"), pic(bare, "f0090.png"); old = []
        if A2 and B2: old = sorted(lum(B2, (y * W + x) * 3) for y in range(y0, y1) for x in range(W) if own[x] >= 0 and differ(A2, B2, (y * W + x) * 3) and red(B2, (y * W + x) * 3))
        q2 = lambda f: old[int(f * (len(old) - 1))] if old else 1.0; flat = (q2(0.9) - q2(0.1)) / max(q2(0.5), 1.0)
        (ok if one(ev2, "gibs").get("lit") == 0 and len(old) >= 800 else bad)(f"without the files the row is the balls, and they are in the picture ({len(old)} red pixels of them; 800 or more)")
        (ok if look["spread"] >= 0.45 and old and flat < 0.25 else bad)(f"the chunks are shaded where a ball was one flat colour: their red runs from dark to bright over {look['spread']:.2f} of its middle brightness (0.45 or more; the balls: {flat:.2f}, under 0.25)")

if "pools" in PART:
    # each at the smallest it is ever thrown, flat green, on the wet mark its own landing laid; then the blood is wiped (frame 100)
    ev, rc = run(DOWN + ["--shot-every", "30", "--frames", "155"], at, dict(HALL, CHERNOBYL_GIB_ROW="40*0", CHERNOBYL_GIB_KEY="0", CHERNOBYL_GIB_DRY="100"))
    rest = sorted(all_(ev, "gibrest"), key=lambda e: e["sx"]); A, B = pic(at, "f0090.png"), pic(at, "f0150.png")
    if len(rest) != 8 or not (A and B): bad(f"the row of eight was not laid and pictured ({len(rest)} at rest)")
    else:
        own, y0, y1 = patches(rest); wet = [0] * 8; dry = [0] * 8; bloody = 0
        for y in range(y0, y1):
            for x in range(W):
                k = own[x]; i = (y * W + x) * 3
                if k < 0: continue
                wet[k] += green(A, i); dry[k] += green(B, i)
                bloody += red(A, i) and not red(B, i)
        seen = [wet[k] / max(1, dry[k]) for k in range(8)]; worst = min(range(8), key=lambda k: seen[k])
        (ok if bloody >= 1500 and min(dry) >= 60 else bad)(f"each lands in a wet mark of its own ({bloody} pixels of blood around the row, 1500 or more; the smallest chunk {min(dry)} pixels, 60 or more)")
        (ok if min(seen) >= 0.92 and max(seen) <= 1.08 else bad)(f"and lies ON it, not under it: as much is seen of each with the blood as with it wiped away (the least, {NAMES[rest[worst]['kind']]}: {seen[worst]:.0%}; 92% or more"
                                                                 + "".join(f"; {NAMES[rest[k]['kind']]} {seen[k]:.0%}" for k in range(8) if k != worst and seen[k] < 0.92) + ")")
        lows = [e["low"] for e in rest]
        (ok if all(LIFT[0] <= v <= LIFT[1] for v in lows) else bad)(f"the lowest point of each, as it is drawn, is {mm(min(lows))} to {mm(max(lows))} mm above the floor (14.5 to 16.5: on the blood, whose pools are films 7 to 14 mm up)")

if "burst" in PART:
    # how they fly and lie: a burst thrown in front of him
    ev, rc = run(["--look", "180", "-32", "--shot-every", "30", "--frames", "545"], at, dict(HALL, CHERNOBYL_GIBS="40"))
    bst, drw, fly = one(ev, "gibburst"), one(ev, "gibdraw"), one(ev, "gibfly"); rest = all_(ev, "gibrest")
    (ok if bst.get("n") == 13 and bst.get("kinds") == 8 else bad)(f"a burst throws thirteen, of the eight shapes ({bst.get('n')} thrown, {bst.get('kinds')} shapes)")
    (ok if drw.get("n") == 13 and 1 <= drw.get("lamps", 0) <= 6 else bad)(f"its lamps are looked up for the burst, not for each chunk ({drw.get('lamps')} lookups in a frame for {drw.get('n')} chunks lying where they fell: one, and one for each thrown 5 m clear of the rest; 6 at most)")
    (ok if fly.get("turned", 0) > 20 else bad)(f"they tumble in the air (a quarter of a second out, one had turned {fly.get('turned')} degrees; over 20)")
    lows = [e["low"] for e in rest] or [0.0]
    (ok if len(rest) == 13 and all(LIFT[0] <= v <= LIFT[1] for v in lows) else bad)(
        f"and all thirteen come to rest level, lying on the floor as they are drawn ({len(rest)} at rest; the lowest point of each {mm(min(lows))} to {mm(max(lows))} mm above its floor, 14.5 to 16.5 -- a tilted one would dip under that)")
    sz = sorted(e["size"] for e in rest)
    (ok if len(sz) == 13 and sum(1 for x in sz if x >= 0.105) >= 2 and sum(1 for x in sz if x < 0.07) >= 5 and 0.07 <= sum(sz) / 13 <= 0.097 else bad)(
        f"large and small: {sum(1 for x in sz if x >= 0.105)} of 0.105 m or more (two or more), {sum(1 for x in sz if x < 0.07)} under 0.07 m (five or more), {sum(sz) / max(1, len(sz)):.3f} m on average (0.07 to 0.097, which the sizes it draws from cannot leave; the balls: 0.075)")
    (ok if rest and all(20.0 <= e["life"] <= 28.0 for e in rest) else bad)(f"they stay 20 to 28 seconds ({min([e['life'] for e in rest] or [0]):.0f} to {max([e['life'] for e in rest] or [0]):.0f})")

if "end" in PART:
    # a burst that stays six seconds, each chunk flat green: pictured lying there, and again when it is gone. Which pictures:
    # by the debug log's clock, the run's speed being the machine's (it is told when each comes to rest, is half gone, is gone).
    ev, rc = run(["--look", "180", "-32", "--shot-every", "40", "--frames", "860"], at, dict(HALL, CHERNOBYL_GIBS="40", CHERNOBYL_GIB_LIFE="6", CHERNOBYL_GIB_KEY="0"))
    rest, gone, shr = all_(ev, "gibrest"), all_(ev, "gibgone"), all_(ev, "gibshrink"); shots = [(e["t"], os.path.basename(e["file"])) for e in all_(ev, "shotseq")]
    if len(rest) != 13 or len(gone) != 13 or len(shr) != 13: bad(f"the burst did not come to rest, shrink and go ({len(rest)} of 13 at rest, {len(shr)} half gone, {len(gone)} gone)")
    else:
        half = {e["i"]: e["t"] for e in shr}; fade = min(e["t"] - 3.0 * (e["t"] - half.get(e["i"], e["t"])) for e in gone)     # (the shrinking begins twice as long before the end as "half gone" is; half as much again to spare)
        lying = [f for t, f in shots if max(e["t"] for e in rest) + 0.15 < t < fade]; after = [f for t, f in shots if t > max(e["t"] for e in gone) + 0.1]
        A, B = (pic(at, lying[-1]), pic(at, after[0])) if lying and after else (None, None)
        if not (A and B): bad(f"no picture of the burst lying there and one of it gone ({len(lying)} and {len(after)} of {len(shots)})")
        else:
            pz = float(HALL["CHERNOBYL_AT"].split()[1]); view = [e for e in rest if e["at"][1] < pz - 1.0 and 0.03 < e["sx"] < 0.97 and 0.03 < e["sy"] < 0.58]   # in front of him (he looks down -z), in the picture above the gun
            def seen(D, e): return sum(green(D, (y * W + x) * 3) for y in range(max(0, int(e["sy"] * H) - 24), min(H, int(e["sy"] * H) + 25)) for x in range(max(0, int(e["sx"] * W) - 32), min(W, int(e["sx"] * W) + 33)))
            dim = [e for e in view if seen(A, e) < 8]; left = sum(green(B, i) for i in range(0, int(H * 0.62) * W * 3, 3))
            (ok if len(view) >= 2 and not dim else bad)(f"what a burst leaves is seen lying on its blood: every chunk that lies in view is there in the picture ({len(view) - len(dim)} of {len(view)}, two or more; {lying[-1]}"
                                                        + (f"; NOT seen: {', '.join(NAMES[e['kind']] for e in dim)}" if dim else "") + ")")
            (ok if left == 0 else bad)(f"and when its time is up nothing of it is left in the picture ({left} pixels of chunk in {after[0]}; the blood stays)")
        ages = [e["age"] for e in gone]; ks = [e["k"] for e in shr]; lows = [e["low"] for e in shr]
        (ok if all(0.40 <= k <= 0.5 for k in ks) and all(LIFT[0] <= v <= LIFT[1] for v in lows) else bad)(
            f"in its last second each shrinks away where it lies: half a second from the end they are drawn at {min(ks):.2f} to {max(ks):.2f} of their size (0.40 to 0.50), their lowest point {mm(min(lows))} to {mm(max(lows))} mm above the floor (14.5 to 16.5)")
        (ok if all(5.95 <= a <= 6.2 for a in ages) else bad)(f"told to stay 6 s, they are gone at {min(ages):.1f} to {max(ages):.1f}")

if "breach" in PART:
    # the row laid across breach 0's mouth, from 2.3 m back: some of the eight are over it
    ev, rc = run(["--look", "-90", "-40", "--frames", "330"], at, {"CHERNOBYL_AT": "-43.0 -34.3", "CHERNOBYL_GIB_ROW": "40*0"})
    h = one(ev, "hole"); rest = all_(ev, "gibrest"); drw = one(ev, "gibdraw")
    if len(rest) != 8 or not h: bad(f"the row of eight was not laid at the breach ({len(rest)} at rest)")
    else:
        lip = h["at"][1]; deep = lip - h["depth"]; over = [e for e in rest if abs(e["at"][0] - h["at"][0]) < h["half"][0] and abs(e["at"][1] - h["at"][2]) < h["half"][1]]; beside = [e for e in rest if e not in over]
        hung = [e for e in over if abs(e["floor"] - deep) > 0.02 or not LIFT[0] <= e["low"] <= LIFT[1]]; lost = [e for e in beside if abs(e["floor"] - lip) > 0.02 or not LIFT[0] <= e["low"] <= LIFT[1]]
        (ok if len(over) >= 2 and len(beside) >= 2 else bad)(f"laid across a breach's mouth, {len(over)} of the row are over it and {len(beside)} beside it (two or more of each)")
        (ok if over and not hung else bad)(f"a piece that comes down over the breach goes down it, and lies at the bottom ({len(over) - len(hung)} of {len(over)} lie {lip - deep:.1f} m down"
                                           + (f"; left hanging at the lip's height, on its disc of blood: {', '.join(NAMES[e['kind']] for e in hung)}" if hung else "") + ")")
        (ok if beside and not lost else bad)(f"the rest lie at its lip ({len(beside) - len(lost)} of {len(beside)} on the floor he stood on)")
        (ok if drw.get("drawn") == 8 and abs(drw.get("ly", -99) - lip) < 0.02 else bad)(f"and the lamps that light them all stay the ones that reach where he stood (looked up {drw.get('ly')} m up, his floor being at {lip}; {drw.get('drawn')} of 8 drawn)")

if "deck" in PART:
    # the low deck west of the reactor's steps (maps/chernobyl.map: x -8..-5, z -2..2, its plates 0.45 m above the hall's floor)
    DECK = (-8.0, -5.0, -2.0, 2.0, 0.45); on = lambda e: DECK[0] < e["at"][0] < DECK[1] and DECK[2] < e["at"][1] < DECK[3]
    for what, where, look, claim in (("on the floor beside it", "-9.3 2.0", "90", "a piece that comes down inside a raised deck's footprint ends on top of the deck, not inside it"),
                                     ("on top of it", "-7.6 -0.9", "0", "a piece thrown past a deck's edge falls on to the floor below, and does not hang in the air beside the deck")):
        ev, rc = run(["--look", look, "-40", "--frames", "300"], at, {"CHERNOBYL_AT": where, "CHERNOBYL_GIB_ROW": "40*0"})
        rest = all_(ev, "gibrest"); floor = one(ev, "hole").get("at", [0, 0.5, 0])[1]; top = floor + DECK[4]; stood = one(ev, "gibdraw").get("ly", floor) - floor   # (the hall's floor: the height of a breach's lip)
        up = [e for e in rest if on(e)]; off = [e for e in rest if not on(e)]
        wrong = [e for e in up if abs(e["floor"] - top) > 0.02 or not LIFT[0] <= e["low"] <= LIFT[1]] + [e for e in off if abs(e["floor"] - floor) > 0.02 or not LIFT[0] <= e["low"] <= LIFT[1]]
        (ok if len(rest) == 8 and len(up) >= 2 and len(off) >= 2 and not wrong else bad)(
            f"{claim} (the row laid with him {what}, {stood:.2f} m above the hall's floor: {len(up)} over the deck lie on its plates, {len(off)} beside it on the floor"
            + (f"; WRONG, and how high above the floor each lies: {', '.join(NAMES[e['kind']] + ' ' + format(e['floor'] - floor, '.2f') for e in wrong)}" if wrong else "") + f"; {len(rest)} of 8 at rest)")

if "balls" in PART:
    ev, rc = run(["--frames", "200"], bare, {"CHERNOBYL_GIBS": "40"})
    g, bst = one(ev, "gibs"), one(ev, "gibburst")
    (ok if g.get("lit") == 0 and bst.get("n") == 13 and rc == 0 and one(ev, "shutdown").get("ok") else bad)(f"without the files a burst throws the balls it always did, and the game runs on (lit {g.get('lit')}, {bst.get('n')} thrown, exit {rc})")
open(os.path.join(tmp, "game"), "w").write(str(fail))
PY
fail=$(cat "$T/files" 2>/dev/null || echo 1); game=$(cat "$T/game" 2>/dev/null || echo 1)
[ "$fail" = 0 ] && [ "$game" = 0 ] && echo PASS || { echo FAIL; exit 1; }
