#!/bin/bash
# One arm for the guns (owner, 2026-11-17: "check if we can make the arm model the same for each weapon ... go ahead with
# minigun and remington, use the launcher arm"; 2026-11-18: "flamethrower doesnt show the same arm model. is it possible
# to make flame thrower arm consistent?"). Every gun came with its own arms, so switching guns changed the player's
# clothes. tools/arms/refit_arms.py puts the grenade launcher's arm mesh on the minigun's, the Remington's and the
# flamethrower's own skeletons (their clips move it), and the game gives it the colour map the launcher loaded.
#   1. the files: the launcher's arm on each, nothing of the gun's own skeleton or clips touched, no arm picture inside,
#      nothing left unreferenced, and the tool builds the same bytes again; the arm is whole on the gun's skeleton (no
#      part of it drawn out into a spike), and each sleeve is closed at the shoulder by a cap that wears the sleeve's
#      cloth at the cloth's own scale (the Remington's camera looks straight at that end); the new skin hangs on every
#      bone the gun's own arm hung on (the Remington carries its forearm on three twist bones, the flamethrower on four);
#   2. the game: each of the three takes the launcher's sleeve at load, and is drawn with it (not white, not missing); no
#      other gun's mesh is taken for such an arm; and with no launcher to take the sleeve from (its file gone) the arms
#      are kept and drawn plain, not hidden as an untextured mesh;
#   2b. the view: with the look lagging as far as it goes, each gun's new arms take little more of the picture than its
#      own did and do not hide the gun (a capped sleeve end under the lens rises into view as a dome);
#   3. the clips: posed through every one of the gun's clips, the new arm's wrist is not wrung thinner than the gun's
#      own arm was (the Remington's reload rolls the left hand half a turn about the forearm).
cd "$(dirname "$0")/.." || exit 1
make -s all >/dev/null || { echo "FAIL build"; exit 1; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
NP="uv run --quiet --no-project --with numpy==2.5.5 python"      # (pinned: the tool's bytes must not drift with a numpy release)
$NP tools/arms/refit_arms.py --out "$T" >/dev/null 2>"$T/err" || { echo "FAIL tools/arms/refit_arms.py did not run: $(tail -1 "$T/err")"; exit 1; }
python3 - "$T" <<'PY'
import hashlib, io, json, math, os, shutil, struct, subprocess, sys
from PIL import Image
tmp = sys.argv[1]; fail = 0
def ok(t): print("ok   " + t)
def bad(t):
    global fail; print("FAIL " + t); fail = 1
def glb(b):
    jl = struct.unpack_from("<I", b, 12)[0]; j = json.loads(b[20:20 + jl]); bo = 20 + jl; return j, b[bo + 8:bo + 8 + struct.unpack_from("<I", b, bo)[0]]
SZ = {5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4}; NC = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
def raw(j, b, i):                                   # an accessor's bytes (none of these files interleave)
    a = j["accessors"][i]; bv = j["bufferViews"][a["bufferView"]]; o = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    return b[o:o + a["count"] * SZ[a["componentType"]] * NC[a["type"]]]
FMT = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}
def arr(j, b, i):                                   # an accessor's values, a tuple for each element
    a = j["accessors"][i]; n = NC[a["type"]]; v = struct.unpack("<%d%s" % (a["count"] * n, FMT[a["componentType"]]), raw(j, b, i))
    return [v[k:k + n] for k in range(0, len(v), n)]
def dist(a, b): return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))
def area(a, b, c):                                  # of a triangle, in 2 or 3 dimensions
    u = [x - y for x, y in zip(b, a)]; v = [x - y for x, y in zip(c, a)]
    if len(u) == 2: return abs(u[0] * v[1] - u[1] * v[0]) / 2
    return math.sqrt((u[1] * v[2] - u[2] * v[1]) ** 2 + (u[2] * v[0] - u[0] * v[2]) ** 2 + (u[0] * v[1] - u[1] * v[0]) ** 2) / 2
def median(v): v = sorted(v); return v[len(v) // 2]
def clips(j, b): return hashlib.md5(b"".join(raw(j, b, s[k]) for a in j.get("animations", []) for s in a["samplers"] for k in ("input", "output"))).hexdigest()
dj, db = glb(open("assets/grenadelauncher.glb", "rb").read())
dp = dj["meshes"][0]["primitives"][0]                                                  # the launcher's arm: mesh 0
duv = raw(dj, db, dp["attributes"]["TEXCOORD_0"]); dP = arr(dj, db, dp["attributes"]["POSITION"]); nd = len(dP)
dI = [x[0] for x in arr(dj, db, dp["indices"])]; dT = [dI[k:k + 3] for k in range(0, len(dI), 3)]
dim = dj["images"][dj["textures"][dj["materials"][dp["material"]]["pbrMetallicRoughness"]["baseColorTexture"]["index"]]["source"]]
dbv = dj["bufferViews"][dim["bufferView"]]; sleeve = Image.open(io.BytesIO(db[dbv.get("byteOffset", 0):dbv.get("byteOffset", 0) + dbv["byteLength"]])).convert("RGB")
def texel(uv): return sleeve.getpixel((min(sleeve.size[0] - 1, max(0, int(uv[0] * sleeve.size[0]))), min(sleeve.size[1] - 1, max(0, int(uv[1] * sleeve.size[1])))))
def rim_edges(P, T):                                # the edges of a mesh that one triangle only uses (vertices welded by position: a seam in the picture is not a rim)
    size = max(max(v[k] for v in P) - min(v[k] for v in P) for k in range(3)); weld = {}
    W = [weld.setdefault(tuple(round(c / size, 4) for c in v), len(weld)) for v in P]; edges = {}
    for t in T:
        for k in range(3):
            if W[t[k]] != W[t[k - 1]]: e = (min(W[t[k]], W[t[k - 1]]), max(W[t[k]], W[t[k - 1]])); edges[e] = edges.get(e, 0) + 1
    return sum(1 for c in edges.values() if c == 1), W
drims, _ = rim_edges(dP, dT)                        # the launcher's own: its two sleeves end open
def wears(jj, bb, q):                               # what a primitive is drawn with: its material's name, colour and the bytes of the pictures it names
    m = jj["materials"][q["material"]]; pbr = m.get("pbrMetallicRoughness", {}); out = [m.get("name"), pbr.get("baseColorFactor"), m.get("alphaMode")]
    for t in (pbr.get("baseColorTexture"), pbr.get("metallicRoughnessTexture"), m.get("normalTexture"), m.get("occlusionTexture"), m.get("emissiveTexture")):
        if not t: out.append(None); continue
        bv = jj["bufferViews"][jj["images"][jj["textures"][t["index"]]["source"]]["bufferView"]]; out.append(hashlib.md5(bb[bv.get("byteOffset", 0):bv.get("byteOffset", 0) + bv["byteLength"]]).hexdigest())
    return out
# gun, where its arm is in the rebuilt file, the pictures left in it, the meshes of the file as it came that went with its
# old arms (the flamethrower's sleeves were a mesh of their own, and five more made a wristwatch on its bare forearm), and
# whether its sleeves are capped at the shoulder: where the gun's own arms were closed there. The flamethrower's own
# sleeves end open, just under the lens: capped, the end rose into the picture as a dome at every turn of the head.
for gun, arm, nimg, gone, shut in (("minigun", 6, 3, (), True), ("remington", 0, 6, (), True), ("flamethrower", 0, 3, (1, 2, 3, 4, 5, 6), False)):
    j, b = glb(open(f"assets/{gun}.glb", "rb").read())
    oj, ob = glb(subprocess.run(["git", "show", f"b75ba60:assets/{gun}.glb"], capture_output=True, check=True).stdout)
    kept = [k for k in range(len(oj["meshes"])) if k not in gone]             # the rebuilt file's mesh k is its own mesh kept[k]
    if len(j["meshes"]) != len(kept): bad(f"{gun}: {len(j['meshes'])} meshes in the file (want {len(kept)}: its own {len(oj['meshes'])} less the {len(gone)} that went with its old arms)"); continue
    p = j["meshes"][arm]["primitives"][0]; uv = raw(j, b, p["attributes"]["TEXCOORD_0"])
    (ok if uv[:len(duv)] == duv else bad)(f"{gun}: mesh {arm} is the launcher's arm ({j['accessors'][p['attributes']['POSITION']]['count']} vertices)")
    m = j["materials"][p["material"]]; tex = [k for k in ("normalTexture", "occlusionTexture", "emissiveTexture") if k in m] + [k for k in m.get("pbrMetallicRoughness", {}) if k.endswith("Texture")]
    (ok if m.get("name") == "arms" and not tex else bad)(f"{gun}: its arm names no picture of its own ({m.get('name')}, {tex or 'none'})")
    (ok if len(j.get("images", [])) == nimg else bad)(f"{gun}: {len(j.get('images', []))} images left (the gun's own; want {nimg})")
    same = [x.get("name") for x in j["meshes"]] == [oj["meshes"][k].get("name") for k in kept] and j["skins"][0]["joints"] == oj["skins"][0]["joints"] \
        and raw(j, b, j["skins"][0]["inverseBindMatrices"]) == raw(oj, ob, oj["skins"][0]["inverseBindMatrices"]) \
        and [a.get("name") for a in j["animations"]] == [a.get("name") for a in oj["animations"]] and clips(j, b) == clips(oj, ob)
    (ok if same else bad)(f"{gun}: its meshes' order, skeleton, bind pose and clips are as they came")
    parts = [(q, oq) for k, x in enumerate(j["meshes"]) if k != arm for q, oq in zip(x["primitives"], oj["meshes"][kept[k]]["primitives"])]      # the gun itself: every other mesh
    diff = sum(1 for q, oq in parts for key in list(oq["attributes"]) + ["indices"] if (raw(j, b, q["attributes"][key]) if key != "indices" else raw(j, b, q["indices"])) != (raw(oj, ob, oq["attributes"][key]) if key != "indices" else raw(oj, ob, oq["indices"])))
    (ok if parts and diff == 0 and all(sorted(q["attributes"]) == sorted(oq["attributes"]) for q, oq in parts) else bad)(f"{gun}: the gun's own meshes are byte for byte as they came ({len(parts)} of them; attributes that differ: {diff})")
    worn = sum(1 for q, oq in parts if wears(j, b, q) != wears(oj, ob, oq))
    (ok if parts and worn == 0 else bad)(f"{gun}: and wear what they wore: the same material, colour and pictures (that differ: {worn})")
    ex = j["asset"].get("extras", {}); arms = ex.get("arms", {}); dex = dj["asset"]["extras"]                 # CC-BY: whose work is in the file, and that it was changed
    (ok if {k: v for k, v in ex.items() if k != "arms"} == oj["asset"].get("extras", {}) and all(arms.get(k) == dex[k] for k in ("title", "author", "license", "source")) and "changes" in arms
        else bad)(f"{gun}: the file names its own author and licence as it came, and the arm's beside them ({arms.get('author')})")
    used = {v for x in j["meshes"] for q in x["primitives"] for v in list(q["attributes"].values()) + [q.get("indices")] if v is not None}
    used |= {s["inverseBindMatrices"] for s in j["skins"]} | {s[k] for a in j["animations"] for s in a["samplers"] for k in ("input", "output")}
    bvs = {j["accessors"][i]["bufferView"] for i in used} | {im["bufferView"] for im in j.get("images", [])}
    (ok if len(used) == len(j["accessors"]) and len(bvs) == len(j["bufferViews"]) else bad)(f"{gun}: nothing in the file is left unreferenced ({len(j['accessors'])} accessors, {len(j['bufferViews'])} views)")
    spans = [(bv.get("byteOffset", 0), bv.get("byteOffset", 0) + bv["byteLength"]) for bv in (j["bufferViews"][im["bufferView"]] for im in j.get("images", []))]
    for i in used:                                  # ... and no bytes either: a view kept whole would keep the old arm's vertices under the accessors that went
        a = j["accessors"][i]; bv = j["bufferViews"][a["bufferView"]]; st = bv.get("byteStride", 0); el = SZ[a["componentType"]] * NC[a["type"]]
        o = bv.get("byteOffset", 0) + a.get("byteOffset", 0); spans.append((o, o + ((a["count"] - 1) * st + el if st else a["count"] * el)))
    end = dead = 0
    for s0, s1 in sorted(spans): dead += max(0, s0 - end); end = max(end, s1)
    dead += max(0, len(b) - end)
    (ok if dead < 4096 else bad)(f"{gun}: no data is left in the file that nothing uses ({dead} bytes outside every accessor and picture; a little is padding)")
    built = os.path.join(tmp, gun + ".glb")
    (ok if os.path.exists(built) and open(f"assets/{gun}.glb", "rb").read() == open(built, "rb").read() else bad)(f"{gun}: the shipped file is what the tool builds")
    # the arm's shape. Its first triangles are the launcher's own, vertex for vertex, so each edge can be measured against
    # the launcher's: a joint the refit placed wrongly (the minigun's fingertips carry no bind pose) draws its edges out
    # many times further than the rest of the arm's.
    P = arr(j, b, p["attributes"]["POSITION"]); UV = arr(j, b, p["attributes"]["TEXCOORD_0"]); I = [x[0] for x in arr(j, b, p["indices"])]
    T = [I[k:k + 3] for k in range(0, len(I), 3)]
    if T[:len(dT)] != dT: bad(f"{gun}: the arm's first triangles are not the launcher's"); continue
    dl = [dist(dP[t[k]], dP[t[k - 1]]) for t in dT for k in range(3)]; least = 0.25 * median(dl)      # (a crease's tiny edges may open many times over and still be tiny)
    st = [dist(P[t[k]], P[t[k - 1]]) / max(l, least) for (t, k), l in zip(((t, k) for t in dT for k in range(3)), dl)]
    worst = max(st) / median(st)
    (ok if worst < 6.0 else bad)(f"{gun}: no part of the arm is drawn out into a spike (its most stretched edge: {worst:.1f} times the arm's usual stretch; under 6)")
    low = sorted(s_ for s_, l in zip(st, dl) if l >= least); squash = low[len(low) // 1000] / median(st)      # (not a crease's own tiny edges; one edge in a thousand may fold)
    (ok if squash > 0.2 else bad)(f"{gun}: and none is squashed flat (its thousandth least stretched edge: {squash:.2f} of the arm's usual stretch; over 0.2 -- a fingertip aimed at a joint that is not the finger's end comes out a tenth as long: 0.08)")
    rims, W = rim_edges(P, T)
    if shut: (ok if rims == 0 else bad)(f"{gun}: the sleeves are closed (edges of the arm left open: {rims})")
    else: (ok if rims == drims and len(T) == len(dT) else bad)(f"{gun}: its sleeves end open as its own did, with no cap under the lens (triangles past the launcher's own: {len(T) - len(dT)}; edges left open: {rims}, the launcher's own {drims})")
    # the skin. A bone of the gun's own arm may go unused only between two bones the new skin does hang on (a knuckle's own
    # bone in the palm, between the hand and the finger): a run of unused bones is a part of the arm the new skin does not
    # follow -- the Remington's forearm is carried by three twist bones, and a skin left on the one bone above them is
    # wrung at the wrist when the hand rolls.
    JJ = arr(j, b, p["attributes"]["JOINTS_0"]); WW = arr(j, b, p["attributes"]["WEIGHTS_0"])
    def hung(jj, joints, weights):                  # the nodes a skin puts real weight on
        m = {}
        for jv, wv in zip(joints, weights):
            for k, w in zip(jv, wv):
                if w > 0: m[k] = m.get(k, 0.0) + w
        return {jj["skins"][0]["joints"][k] for k, w in m.items() if w > 0.5}
    own = set(); new = hung(j, JJ, WW)                                          # its own arm: the mesh the new one took the place of, and those that went with it
    for k in [kept[arm]] + list(gone): oa = oj["meshes"][k]["primitives"][0]["attributes"]; own |= hung(oj, arr(oj, ob, oa["JOINTS_0"]), arr(oj, ob, oa["WEIGHTS_0"]))
    par = {c: i for i, nd in enumerate(j["nodes"]) for c in nd.get("children", [])}
    bare = sorted(j["nodes"][n].get("name", "?") for n in own - new if not (par.get(n) in new and any(c in new for c in j["nodes"][n].get("children", []))))
    (ok if not bare else bad)(f"{gun}: the new arm hangs on every bone its own arm hung on, or on the bones either side of it (left bare: {', '.join(bare) or 'none'})")
    twice = sum(1 for jv, wv in zip(JJ, WW) if len({k for k, w in zip(jv, wv) if w > 0}) < sum(1 for w in wv if w > 0)); loose = sum(1 for wv in WW if abs(sum(wv) - 1.0) > 1e-4)
    (ok if twice == 0 and loose == 0 else bad)(f"{gun}: every vertex's weights name different bones and sum to one ({twice} name a bone twice, {loose} do not sum to one)")
    # the caps: every triangle past the launcher's own. Each must wear the cloth at the scale the sleeve beside it does
    # (a fan drawn from the rim's own texture coordinates smears one line of the picture over the whole end), and in
    # the colour of the cloth at the rim.
    if not shut: continue
    cap = T[len(dT):]; rimpos = {W[v] for t in cap for v in t}; beside = [t for t in dT if any(W[v] in rimpos for v in t)]
    if not cap or not beside: bad(f"{gun}: no caps on the sleeves ({len(cap)} triangles past the launcher's own)"); continue
    dens = lambda t: area(UV[t[0]], UV[t[1]], UV[t[2]]) / max(area(P[t[0]], P[t[1]], P[t[2]]), 1e-30)
    ref = median([dens(t) for t in beside]); off = sum(1 for t in cap if not ref / 2.5 < dens(t) < ref * 2.5)
    (ok if off == 0 else bad)(f"{gun}: the caps wear the cloth at the sleeve's own scale ({off} of {len(cap)} cap triangles do not, within 2.5 times)")
    rimc = [texel(UV[v]) for t in beside for v in t if W[v] in rimpos]; mean = [sum(c[k] for c in rimc) / len(rimc) for k in range(3)]
    capc = [texel([sum(UV[v][k] for v in t) / 3 for k in range(2)]) for t in cap] + [texel(UV[v]) for t in cap for v in t]
    far = max(max(abs(c[k] - mean[k]) for k in range(3)) for c in capc)
    (ok if far < 40 else bad)(f"{gun}: the caps are the colour of the cloth at the rim (the furthest cap texel is {far:.0f} levels from the rim's mean {tuple(round(x) for x in mean)}; under 40)")
# ---- the game: each takes the launcher's sleeve at load, and is drawn with it
def run(args, cwd=".", env=None):                   # the game, hidden, from `cwd` (where it finds assets/ and writes its log and its picture)
    for f in ("chernobyl2-shot.png", "chernobyl2-debug.log"):
        if os.path.exists(os.path.join(cwd, f)): os.remove(os.path.join(cwd, f))
    subprocess.run(["perl", "-e", "alarm 180; exec @ARGV", os.path.abspath("build/chernobyl2"), "--god", "--no-enemies", "--no-intro", "--mute", "--debug"] + args,
                   env=dict(os.environ, CHERNOBYL_BACKGROUND="1", CHERNOBYL_CLEAN_HUD="1", **(env or {})), cwd=cwd, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ev = []; log = os.path.join(cwd, "chernobyl2-debug.log")
    for line in open(log) if os.path.exists(log) else []:
        try: ev.append(json.loads(line))
        except Exception: pass
    return ev
def white(path):                                    # how much of the picture's lower half is near white (an untextured mesh is drawn white)
    im = Image.open(path).convert("RGB"); W, H = im.size; px = im.load()
    low = [px[x, y] for y in range(H // 2, H, 2) for x in range(0, W, 2)]; return sum(1 for p in low if min(p) > 235) / len(low)
def loads(ev): return {e["slot"]: e for e in ev if e.get("ev") == "weapon" and "bones" in e}, {(e["slot"], e["culledUntexturedMesh"]) for e in ev if e.get("ev") == "weapon" and "culledUntexturedMesh" in e}
load, culled = loads(run(["--frames", "40"]))
(ok if load.get(0, {}).get("sleeve") == 0 else bad)(f"the launcher wears its own sleeve (sleeve: {load.get(0, {}).get('sleeve')})")
for slot, gun, arm in ((2, "minigun", 6), (1, "remington", 0), (5, "flamethrower", 0)):
    (ok if load.get(slot, {}).get("sleeve") == 1 else bad)(f"{gun}: its arms are given the launcher's sleeve at load (sleeve: {load.get(slot, {}).get('sleeve')})")
    (ok if (slot, arm) not in culled else bad)(f"{gun}: its arms are drawn (not hidden as an untextured mesh)")
    run(["--weapon", str(slot), "--shot", "200", "--frames", "215"])
    if not os.path.exists("chernobyl2-shot.png"): bad(f"{gun}: no screenshot"); continue
    wh = white("chernobyl2-shot.png")
    (ok if wh < 0.03 else bad)(f"{gun}: no white, untextured arm in the picture ({wh:.1%} of the lower half is white)")
if os.path.exists("chernobyl2-shot.png"): os.remove("chernobyl2-shot.png")
others = sorted(s for s, e in load.items() if s not in (1, 2, 5) and e.get("sleeve"))
(ok if not others else bad)(f"no other gun's mesh is taken for a refitted arm (slots given a sleeve besides 1, 2 and 5: {others or 'none'})")
# ---- no launcher to take the sleeve from: the game started where every asset but the launcher's file is
alt = os.path.join(tmp, "nolauncher"); os.makedirs(os.path.join(alt, "assets")); os.makedirs(os.path.join(alt, "build"))
for f in os.listdir("assets"):
    if not f.startswith("grenadelauncher.glb"): os.symlink(os.path.abspath(os.path.join("assets", f)), os.path.join(alt, "assets", f))
os.symlink(os.path.abspath("maps"), os.path.join(alt, "maps"))
for f in os.listdir("."):
    if f.endswith(".txt") and (f.startswith("vm_tune") or f == "options.txt"): shutil.copy(f, alt)          # (copies: nothing the game writes there reaches the repo)
l2, c2 = loads(run(["--weapon", "1", "--shot", "200", "--frames", "215"], cwd=alt)); got = l2.get(1, {}).get("sleeve")
(ok if got == -1 and (1, 0) not in c2 else bad)(f"with no launcher to take the sleeve from, the Remington's arms are kept and given a plain one (sleeve: {got}; -1 is plain)")
shot = os.path.join(alt, "chernobyl2-shot.png")
if os.path.exists(shot): wh = white(shot); (ok if wh < 0.03 else bad)(f"and they are not drawn white ({wh:.1%} of the lower half is white)")
else: bad("no screenshot of the Remington without the launcher")
# ---- the view: the look lagging as far as it goes. A turn of the head lifts the gun and its arms up the picture (the gun
# lags the look: VmPlace, g_vmSway), and what stood just under the frame comes into it. Each gun is drawn with its own arms
# and with the new ones, the arms flat magenta and the gun flat green (CHERNOBYL_VM_KEY), the lag held at rest, straight up,
# up and left, up and right in turn (CHERNOBYL_SWAY), and the pictures are read for how much each takes. Arms that are
# merely fuller take a quarter more than the gun's own did. A sleeve's end standing just under the lens does not: capped
# as the other two guns' are, the flamethrower's rose as a dome over twice, then six times, what its own arms showed,
# and hid the gun (its own sleeves end open there, and all that showed of them was a rim).
def stage(name, own):                               # a folder to run the game from (nothing it writes reaches the repo): the repo's assets, the three guns as they came if `own`
    d = os.path.join(tmp, name); os.makedirs(os.path.join(d, "assets")); os.makedirs(os.path.join(d, "build", "shots"))
    for f in os.listdir("assets"):
        if own and f in ("minigun.glb", "remington.glb", "flamethrower.glb"): open(os.path.join(d, "assets", f), "wb").write(subprocess.run(["git", "show", "b75ba60:assets/" + f], capture_output=True, check=True).stdout)
        else: os.symlink(os.path.abspath(os.path.join("assets", f)), os.path.join(d, "assets", f))
    os.symlink(os.path.abspath("maps"), os.path.join(d, "maps"))
    for f in os.listdir("."):
        if f.endswith(".txt") and (f.startswith("vm_tune") or f == "options.txt"): shutil.copy(f, d)
    return d
LAG = (("at rest", "0 0"), ("up", "0 40"), ("up and left", "-40 40"), ("up and right", "40 40"))
def cover(cwd, slot):                               # [(arms, gun)]: the share of the picture each takes, at each lag in turn (120 frames a lag: it has settled)
    run(["--weapon", str(slot), "--shot-every", "120", "--frames", str(120 * len(LAG) + 5)], cwd=cwd, env={"CHERNOBYL_VM_KEY": "1", "CHERNOBYL_SWAY": ";".join(v for _, v in LAG)})
    out = []
    for k in range(len(LAG)):
        f = os.path.join(cwd, "build", "shots", "f%04d.png" % (120 * (k + 1)))
        if not os.path.exists(f): return None
        b = Image.open(f).convert("RGB").resize((480, 310), Image.NEAREST).tobytes(); os.remove(f); n = len(b) // 3
        out.append((sum(1 for i in range(0, len(b), 3) if b[i] > 170 and b[i + 2] > 170 and b[i + 1] < 110) / n, sum(1 for i in range(0, len(b), 3) if b[i + 1] > 170 and b[i] < 110 and b[i + 2] < 110) / n))
    return out
was_at, now_at = stage("own", True), stage("new", False)
for slot, gun, arm in ((2, "minigun", 6), (1, "remington", 0), (5, "flamethrower", 0)):
    was, now = cover(was_at, slot), cover(now_at, slot)
    if not was or not now: bad(f"{gun}: no pictures of it with the look lagging"); continue
    say = lambda k: "; ".join(f"{nm} {n[k]:.1%} (own {w[k]:.1%})" for (nm, _), w, n in zip(LAG, was, now))
    (ok if all(n[0] <= 1.5 * w[0] + 0.005 for w, n in zip(was, now)) else bad)(f"{gun}: the look lagging any way, its arms take no more of the picture than half as much again as its own did: {say(0)}")
    (ok if all(n[1] >= 0.5 * w[1] for w, n in zip(was, now) if w[1] > 0.005) else bad)(f"{gun}: and do not hide the gun where its own did not (the gun seen: {say(1)})")
    (ok if now[0][1] > 0.01 and abs(now[0][1] - was[0][1]) < 0.15 * was[0][1] else bad)(f"{gun}: the gun itself is drawn as it was (at rest {now[0][1]:.1%} of the picture; with its own arms {was[0][1]:.1%})")
open(os.path.join(tmp, "static"), "w").write(str(fail))
PY
# ---- the clips: each arm posed through every clip of its gun, the wrist's skin measured against the bind pose
$NP - "$T" <<'PY'
import json, os, subprocess, sys
import numpy as np
sys.path.insert(0, "tools/arms"); import refit_arms as ra
def qmat(q):
    x, y, z, w = q[:, 0], q[:, 1], q[:, 2], q[:, 3]; R = np.empty((len(q), 3, 3))
    R[:, 0, 0] = 1 - 2 * (y * y + z * z); R[:, 0, 1] = 2 * (x * y - z * w); R[:, 0, 2] = 2 * (x * z + y * w)
    R[:, 1, 0] = 2 * (x * y + z * w); R[:, 1, 1] = 1 - 2 * (x * x + z * z); R[:, 1, 2] = 2 * (y * z - x * w)
    R[:, 2, 0] = 2 * (x * z - y * w); R[:, 2, 1] = 2 * (y * z + x * w); R[:, 2, 2] = 1 - 2 * (x * x + y * y); return R
class Clips:                                        # a .glb's skeleton, posed by its clips (glTF's own rules: linear keys, the shorter way round)
    def __init__(self, js, bin_):
        self.js, self.bin = js, bin_; nodes = js["nodes"]; n = len(nodes); self.par = [-1] * n; self.keys = {}
        for i, nd in enumerate(nodes):
            for c in nd.get("children", []): self.par[c] = i
        self.order = []; seen = set()
        def visit(i):
            if i in seen: return
            if self.par[i] >= 0: visit(self.par[i])
            seen.add(i); self.order.append(i)
        for i in range(n): visit(i)
        self.T = np.array([nd.get("translation", [0, 0, 0]) for nd in nodes], np.float64); self.R = np.array([nd.get("rotation", [0, 0, 0, 1]) for nd in nodes], np.float64)
        self.S = np.array([nd.get("scale", [1, 1, 1]) for nd in nodes], np.float64); self.M = [np.array(nd["matrix"], np.float64).reshape(4, 4).T if "matrix" in nd else None for nd in nodes]
        sk = js["skins"][0]; self.joints = sk["joints"]; self.ibm = ra.acc(js, bin_, sk["inverseBindMatrices"]).astype(np.float64).reshape(-1, 4, 4).transpose(0, 2, 1)
    def key(self, i):
        if i not in self.keys: self.keys[i] = ra.acc(self.js, self.bin, i).astype(np.float64)
        return self.keys[i]
    def length(self, ai): return max(float(self.key(s["input"]).max()) for s in self.js["animations"][ai]["samplers"])
    def pose(self, ai, t):                          # every joint's skinning matrix at time t of clip ai
        T, R, S = self.T.copy(), self.R.copy(), self.S.copy(); hit = set(); a = self.js["animations"][ai]
        for ch in a["channels"]:
            sm = a["samplers"][ch["sampler"]]; node, path = ch["target"].get("node"), ch["target"]["path"]
            if node is None or path not in ("translation", "rotation", "scale"): continue
            ti = self.key(sm["input"]).ravel(); vo = self.key(sm["output"]); kind = sm.get("interpolation", "LINEAR")
            if kind == "CUBICSPLINE": vo = vo.reshape(len(ti), 3, -1)[:, 1]
            if t <= ti[0]: v = vo[0]
            elif t >= ti[-1]: v = vo[-1]
            else:
                k = int(np.searchsorted(ti, t, side="right")) - 1; u = (t - ti[k]) / (ti[k + 1] - ti[k]); v0, v1 = vo[k], vo[k + 1]
                if kind == "STEP": v = v0
                else: v = v0 + ((-v1 if path == "rotation" and v0 @ v1 < 0 else v1) - v0) * u
            if path == "translation": T[node] = v
            elif path == "rotation": R[node] = v / np.linalg.norm(v)
            else: S[node] = v
            hit.add(node)
        Rm = qmat(R); G = np.zeros((len(T), 4, 4))
        for i in self.order:
            if self.M[i] is not None and i not in hit: L = self.M[i]
            else: L = np.eye(4); L[:3, :3] = Rm[i] * S[i][None, :]; L[:3, 3] = T[i]
            G[i] = L if self.par[i] < 0 else G[self.par[i]] @ L
        return G[self.joints] @ self.ibm
def wrung(js, bin_, kind):
    """For each side: how thin the wrist's skin gets at its worst moment in any clip -- the 5th percentile of the areas of
    its triangles (those by the last third of the forearm and the heel of the hand) against the bind pose."""
    r = ra.Rig(js, bin_); c = Clips(js, bin_); mi, pi = r.arm(); p = js["meshes"][mi]["primitives"][pi]; at = p["attributes"]
    P = ra.acc(js, bin_, at["POSITION"]).astype(np.float64); J = ra.acc(js, bin_, at["JOINTS_0"]).astype(np.int64); W = ra.acc(js, bin_, at["WEIGHTS_0"]).astype(np.float64)
    W /= np.maximum(W.sum(1, keepdims=True), 1e-9); I = ra.acc(js, bin_, p["indices"]).astype(np.int64).reshape(-1, 3); P4 = np.concatenate([P, np.ones((len(P), 1))], 1)
    area = lambda Q: 0.5 * np.linalg.norm(np.cross(Q[I[:, 1]] - Q[I[:, 0]], Q[I[:, 2]] - Q[I[:, 0]]), axis=1)
    A0 = area(P); real = A0 > 1e-12 * A0.max(); cen = P[I].mean(1); band = {}
    for S in ("L", "R"):
        R = ra.ROLES[kind](r, S); el, wr = r.pos[R["elbow"]], r.pos[R["wrist"]]; ax = wr - el; s = ((cen - el) @ ax) / (ax @ ax)
        band[S] = real & (s > 0.70) & (s < 1.10) & (np.linalg.norm((cen - el) - np.outer(s, ax), axis=1) < 0.5 * np.linalg.norm(ax))
    worst = {S: (9.0, "") for S in band}
    for ai, a in enumerate(js["animations"]):
        for t in np.arange(0.0, c.length(ai) + 1e-9, 1.0 / 30.0):
            SM = c.pose(ai, t); Q = sum(W[:, k:k + 1] * np.einsum("nij,nj->ni", SM[J[:, k]], P4)[:, :3] for k in range(4))
            ar = area(Q)[real] / A0[real]; ar /= np.median(ar)                    # (the rig's own scale goes with the median)
            for S in band:
                v = float(np.percentile(ar[band[S][real]], 5))
                if v < worst[S][0]: worst[S] = (v, "%s at %.2f s" % (a.get("name", "clip %d" % ai), t))
    return worst, {S: int(m.sum()) for S, m in band.items()}
fail = 0
for gun, kind in ra.GUNS.items():
    new = ra.parse(open("assets/%s.glb" % gun, "rb").read()); own = ra.parse(subprocess.run(["git", "show", "%s:assets/%s.glb" % (ra.ORIGIN, gun)], capture_output=True, check=True).stdout)
    (wn, nn), (wo, no) = wrung(*new, kind), wrung(*own, kind)
    for S, side in (("L", "left"), ("R", "right")):
        good = wn[S][0] >= 0.6 * wo[S][0]; fail |= not good      # (left on the forearm's one bone the Remington's left wrist kept 0.21, four tenths of its own arm's; shared along the twist bones, more than its own arm's)
        print("%s %s: its %s wrist is not wrung through its clips (its skin there keeps %.2f of its area at worst, %s; the gun's own arm kept %.2f; at least six tenths of that)" % (
            "ok  " if good else "FAIL", gun, side, wn[S][0], wn[S][1], wo[S][0]))
open(os.path.join(sys.argv[1], "posed"), "w").write(str(int(fail)))
PY
fail=$(cat "$T/static" 2>/dev/null || echo 1); posed=$(cat "$T/posed" 2>/dev/null || echo 1)
[ "$fail" = 0 ] && [ "$posed" = 0 ] && echo PASS || { echo FAIL; exit 1; }
