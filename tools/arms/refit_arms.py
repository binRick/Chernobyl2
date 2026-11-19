#!/usr/bin/env python3
"""One arm for the guns: the grenade launcher's arm mesh, refitted to another gun's own skeleton.

    uv run --no-project --with numpy==2.5.5 python tools/arms/refit_arms.py [minigun remington flamethrower] [--out DIR] [-v]

Every gun came with its own arms (the minigun bare forearms and fingerless gloves, the Remington grey sleeves, the
flamethrower bare forearms, black gloves and a wristwatch), so switching guns changed the player's clothes. Each gun keeps its skeleton and its clips -- the hands still move exactly as
its animator made them -- and only the mesh that is skinned to that skeleton is replaced by the launcher's arm
("Grenadelauncher animated", DJMaesen, CC-BY-4.0: the claymore's arm too, tools/mines/build_claymore.py).

How the arm is carried over. The rigs are built differently (bone names, bone axes, proportions), so nothing is matched
by name or by a bone's own axes. Each rig's joints are given ROLES by anatomy: shoulder, elbow, wrist, and three joints
of each of five fingers. For every role a frame is built from joint positions alone: its origin at the joint, x towards
the next joint, y from a second landmark (the arm's bend; the knuckle line for the hand and fingers). A donor vertex
weighted to a role is carried by

        F_target @ diag(len_target / len_donor, k, k, 1) @ inverse(F_donor)

onto the target's bone -- stretched along it by the two bones' lengths, and across it by k, the two hands' width ratio --
and the vertex's weights then name the target's joints. Blended by those weights this is the donor arm standing in the
target's bind pose, so the target's own clips move it.

Where a rig carries its forearm on a chain of twist bones (Cransh's: three of them between the elbow and the wrist, each
taking a share of the hand's roll, and no skin at all on the forearm bone itself), the forearm's weight goes to that
chain by how far down the forearm the vertex is. Left on the one bone, the whole of a rolled hand's turn falls across
the wrist and wrings it to a stalk (the Remington's reload rolls the left hand half a turn).

A rig may not say everything (the flamethrower's, a Counter-Strike arm skeleton). Joints nothing hangs on carry no bind
pose -- its forearms among them: such a joint stands where its parent's bind and its own rest offset put it. Its finger
chains end in an exporter's stub, a tenth of a fingertip long and pointing across the finger: there a fingertip is where
the skin on the last joint ends, and the donor's is taken the same way, so the glove's tip lands on the gun's fingertip.
And its arms are more than one mesh (hands; sleeves) with a wristwatch on the bare forearm (five more): the new arm
takes the place of the largest, the rest go, and so does what the old arms wore (WORN) -- the launcher's sleeve is
thicker than a bare wrist, and a watch worn with one gun only is the change of clothes this tool is here to end.

The launcher's sleeve is cut off a quarter of the way down the upper arm, where its own camera never looks; a gun framed
differently (the Remington) looks straight into the open end. Each gun's own arms say how a sleeve must end for ITS
camera: they reach further up the arm and are closed there. So the upper sleeve is drawn out along the arm -- the whole
of it, evenly, so its cloth is stretched a quarter or a third and nothing is smeared -- until its rim is where that
gun's own skin began (no further: past the shoulder it would stand in front of the lens), and the rim is closed by a
shallow dome. The Remington's camera looks straight at that end, so the dome has vertices of its own and wears a round
patch of the sleeve's cloth -- the cloth beside the rim in the picture, at the cloth's own scale (a fan drawn from the
rim's own texture coordinates smears one line of the picture over the whole end).

Closed, that is, where the gun's own sleeve was (the minigun's, the Remington's). The flamethrower's own sleeves end
open at the shoulder (open_top), and stand just under the lens: the game lifts a gun up the picture when the look lags,
and a dome there rose into view at every turn of the head, twice and six times the size of anything its own arms showed,
hiding the gun. An open end shows a rim and no more (its inside faces away and is not drawn). So a new sleeve ends as
the gun's own did.

The file is rewritten with only what is still referenced: the gun's own arm mesh and its three textures go, and the new
arm's material ("arms") names no texture -- the game gives it the colour map the launcher already loaded (main.c
WeaponSleeve), so the picture is held once for every gun that wears it. The file keeps the author and licence block it
came with and gains the arm's beside it (both are CC-BY: docs/LICENSES.md).

The inputs are the guns as they stood before this tool (commit ORIGIN), read back out of git, so the repo rebuilds
them without carrying a second copy of them -- and so needs a clone that has that commit. The donor is read from the
working file: it is the launcher as the game loads it, whose picture the arms will wear. The result is the same bytes
on every run (tests/arms.sh; numpy is pinned there so that it stays so)."""
import json, os, re, struct, subprocess, sys
import numpy as np

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ORIGIN = "b75ba60a54bb20d8f4fa1a432bbf322a8a9bb845"  # the guns as they came, their own arms on them
DONOR = "grenadelauncher"
GUNS = {"minigun": "maesen", "remington": "cransh", "flamethrower": "aug"}  # gun -> how its rig names its joints
WORN = {"flamethrower": ("glass", "watch_diffuse", "blue_light.001", "green_light.001", "red_light.001")}  # gun -> the materials of what its old arms wore (a wristwatch): it goes with them
STUBS = ("aug",)                                    # rigs whose finger chains end in an exporter's stub, not at a fingertip (skin_tips)
FINGERS = ("thumb", "index", "middle", "ring", "pinky")
CAP_RINGS = 3                                       # rings of vertices in a sleeve's cap, the one on the rim included
CAP_DOME = 0.35                                     # how far the cap stands out of the rim, in rim radii
CT = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
NC = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


# ------------------------------------------------------------------ .glb in and out
def parse(b):
    total = struct.unpack_from("<I", b, 8)[0]; off = 12; js = None; bin_ = b""
    while off < total:
        n, kind = struct.unpack_from("<I4s", b, off); off += 8
        if kind == b"JSON": js = json.loads(b[off:off + n])
        elif kind == b"BIN\0": bin_ = b[off:off + n]
        off += n
    return js, bytes(bin_)


def acc(js, bin_, i):
    a = js["accessors"][i]; bv = js["bufferViews"][a["bufferView"]]; dt = CT[a["componentType"]]; nc = NC[a["type"]]
    o = bv.get("byteOffset", 0) + a.get("byteOffset", 0); st = bv.get("byteStride", 0); isz = np.dtype(dt).itemsize * nc
    if st and st != isz:
        raw = np.frombuffer(bin_, np.uint8, (a["count"] - 1) * st + isz, o)
        idx = (np.arange(a["count"])[:, None] * st + np.arange(isz)[None, :]).ravel()
        return np.frombuffer(raw[idx].tobytes(), dt).reshape(a["count"], nc).copy()
    return np.frombuffer(bin_, dt, a["count"] * nc, o).reshape(a["count"], nc).copy()


def write(js, views, path):
    """views: the bytes of every bufferView, in order. Laid out 4-byte aligned; nothing else goes in the file."""
    out = bytearray()
    for bv, data in zip(js["bufferViews"], views):
        while len(out) % 4: out.append(0)
        bv["byteOffset"] = len(out); bv["byteLength"] = len(data); out += data
    while len(out) % 4: out.append(0)
    js["buffers"] = [{"byteLength": len(out)}]
    j = json.dumps(js, separators=(",", ":")).encode()
    while len(j) % 4: j += b" "
    with open(path, "wb") as f:
        f.write(b"glTF" + struct.pack("<II", 2, 12 + 8 + len(j) + 8 + len(out)) + struct.pack("<I", len(j)) + b"JSON" + j + struct.pack("<I", len(out)) + b"BIN\x00" + bytes(out))
    return 28 + len(j) + len(out)


def compact(js, bin_, extra):
    """Keep only what is referenced. extra: {new accessor index: (array, componentType, type, target, minmax)} already
    named by the meshes; the other accessors keep their own bytes. Returns the views' bytes, in order."""
    used = set()
    for m in js["meshes"]:
        for p in m["primitives"]:
            used |= set(p["attributes"].values())
            if "indices" in p: used.add(p["indices"])
            for t in p.get("targets", []): used |= set(t.values())
    for s in js.get("skins", []):
        if "inverseBindMatrices" in s: used.add(s["inverseBindMatrices"])
    for a in js.get("animations", []):
        for s in a["samplers"]: used |= {s["input"], s["output"]}
    mats = sorted({p["material"] for m in js["meshes"] for p in m["primitives"] if "material" in p}); mmap = {o: n for n, o in enumerate(mats)}
    js["materials"] = [js["materials"][o] for o in mats]
    for m in js["meshes"]:
        for p in m["primitives"]:
            if "material" in p: p["material"] = mmap[p["material"]]
    def texrefs(m):
        r = [m.get(k) for k in ("normalTexture", "occlusionTexture", "emissiveTexture")] + [m.get("pbrMetallicRoughness", {}).get(k) for k in ("baseColorTexture", "metallicRoughnessTexture")]
        return [x for x in r if x]
    texs = sorted({r["index"] for m in js["materials"] for r in texrefs(m)}); tmap = {o: n for n, o in enumerate(texs)}
    for m in js["materials"]:
        for r in texrefs(m): r["index"] = tmap[r["index"]]
    old_tex = js.get("textures", []); js["textures"] = [old_tex[o] for o in texs]
    imgs = sorted({t["source"] for t in js["textures"] if "source" in t}); imap = {o: n for n, o in enumerate(imgs)}
    smps = sorted({t["sampler"] for t in js["textures"] if "sampler" in t}); smap = {o: n for n, o in enumerate(smps)}
    for t in js["textures"]:
        if "source" in t: t["source"] = imap[t["source"]]
        if "sampler" in t: t["sampler"] = smap[t["sampler"]]
    old_img = js.get("images", []); js["images"] = [old_img[o] for o in imgs]
    if "samplers" in js: js["samplers"] = [js["samplers"][o] for o in smps]
    for k in ("textures", "images", "samplers"):
        if k in js and not js[k]: del js[k]
    # An old bufferView is carried over once, cut down to the runs of bytes its kept accessors still use (a picture's
    # view whole). Carried whole, a view the old arm shared with the gun would keep the old arm's vertices in the file.
    old_bv = js["bufferViews"]; views = []; new_bv = []; bmap = {}; keep = sorted(used); runs = {}
    for o in keep:
        if o in extra: continue
        a = js["accessors"][o]; assert "sparse" not in a and "bufferView" in a, "accessor %d has no plain bufferView" % o
        st = old_bv[a["bufferView"]].get("byteStride", 0); el = np.dtype(CT[a["componentType"]]).itemsize * NC[a["type"]]; at = a.get("byteOffset", 0)
        runs.setdefault(a["bufferView"], []).append([at, at + ((a["count"] - 1) * st + el if st else a["count"] * el)])
    def view(o):
        if o not in bmap:
            bv = dict(old_bv[o]); s = bv.pop("byteOffset", 0); data = bytearray(); table = []
            if o in runs:
                for a0, a1 in sorted(runs[o]):
                    if table and a0 <= table[-1][1]: table[-1][1] = max(table[-1][1], a1); continue
                    table.append([a0, a1, 0])
                for run in table:                    # each run starts where its accessors stay as aligned as they were
                    while len(data) % 4 != run[0] % 4: data.append(0)
                    run[2] = len(data); data += bin_[s + run[0]:s + run[1]]
            else: data += bin_[s:s + bv["byteLength"]]
            bv["byteLength"] = len(data); bmap[o] = (len(new_bv), table); new_bv.append(bv); views.append(bytes(data))
        return bmap[o]
    amap = {o: n for n, o in enumerate(keep)}; new_acc = []
    for o in keep:
        if o in extra:
            arr, ctype, typ, target, minmax = extra[o]; raw = np.ascontiguousarray(arr).tobytes()
            bv = {"buffer": 0, "byteLength": len(raw)}
            if target: bv["target"] = target
            new_bv.append(bv); views.append(raw)
            a = {"bufferView": len(new_bv) - 1, "componentType": ctype, "count": len(arr), "type": typ}
            if minmax: a["min"] = [float(x) for x in arr.min(0)]; a["max"] = [float(x) for x in arr.max(0)]
        else:
            a = dict(js["accessors"][o]); at = a.get("byteOffset", 0); a["bufferView"], table = view(a["bufferView"])
            a["byteOffset"] = next(new + at - a0 for a0, a1, new in table if a0 <= at < a1)
        new_acc.append(a)
    for im in js.get("images", []):
        if "bufferView" in im: im["bufferView"] = view(im["bufferView"])[0]
    js["accessors"] = new_acc; js["bufferViews"] = new_bv
    for m in js["meshes"]:
        for p in m["primitives"]:
            p["attributes"] = {k: amap[v] for k, v in p["attributes"].items()}
            if "indices" in p: p["indices"] = amap[p["indices"]]
            for t in p.get("targets", []):
                for k in list(t): t[k] = amap[t[k]]
    for s in js.get("skins", []):
        if "inverseBindMatrices" in s: s["inverseBindMatrices"] = amap[s["inverseBindMatrices"]]
    for a in js.get("animations", []):
        for s in a["samplers"]: s["input"] = amap[s["input"]]; s["output"] = amap[s["output"]]
    return views


# ------------------------------------------------------------------ a rig, and its joints by anatomy
def local(n):                                       # a node's own transform
    if "matrix" in n: return np.array(n["matrix"], np.float64).reshape(4, 4).T
    t = np.array(n.get("translation", [0, 0, 0]), np.float64); x, y, z, w = n.get("rotation", [0, 0, 0, 1]); sc = np.array(n.get("scale", [1, 1, 1]), np.float64)
    R = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
    M = np.eye(4); M[:3, :3] = R * sc; M[:3, 3] = t; return M


class Rig:
    def __init__(self, js, bin_):
        self.js, self.bin = js, bin_; nodes = js["nodes"]; s = js["skins"][0]; self.joints = s["joints"]
        self.names = [nodes[j].get("name", "?") for j in self.joints]
        ibm = acc(js, bin_, s["inverseBindMatrices"]).astype(np.float64).reshape(-1, 4, 4).transpose(0, 2, 1)
        B = np.linalg.inv(ibm)                              # each joint's bind matrix (mesh space)
        parent = {}
        for i, n in enumerate(nodes):
            for c in n.get("children", []): parent[c] = i
        self.par = [self.joints.index(parent[j]) if parent.get(j) in self.joints else -1 for j in self.joints]
        self.kids = {k: [c for c in range(len(self.joints)) if self.par[c] == k] for k in range(len(self.joints))}
        # A joint no vertex hangs on (a fingertip) may carry no usable inverse bind matrix -- the minigun's are all at the
        # origin, and a last finger bone aimed there is a spike across the screen. Such a joint stands where its parent's
        # bind and its own rest transform put it.
        used = set()
        for m in js["meshes"]:
            for p in m["primitives"]:
                if "JOINTS_0" in p["attributes"]:
                    jj = acc(js, bin_, p["attributes"]["JOINTS_0"]); ww = acc(js, bin_, p["attributes"]["WEIGHTS_0"]).astype(np.float64)
                    used |= set(jj[ww > 0].tolist())
        def depth(k):
            n = 0
            while self.par[k] >= 0: k = self.par[k]; n += 1
            return n
        for k in sorted(range(len(self.joints)), key=depth):
            if k not in used and self.par[k] >= 0: B[k] = B[self.par[k]] @ local(nodes[self.joints[k]])
        self.pos = B[:, :3, 3]                              # where each joint stands in the bind pose

    def find(self, pat):
        m = [k for k, n in enumerate(self.names) if re.fullmatch(pat, n)]
        assert len(m) == 1, (pat, [self.names[k] for k in m]); return m[0]

    def chain(self, k, n):                          # k and the joints below it down a single line, n of them
        out = [k]
        while len(out) < n and self.kids[out[-1]]: out.append(self.kids[out[-1]][0])
        return out

    def arms(self):                                 # [(mesh, primitive)] of the arms: every blended-skinned mesh, the one with the most vertices first
        out = []
        for mi, m in enumerate(self.js["meshes"]):
            for pi, p in enumerate(m["primitives"]):
                at = p["attributes"]
                if "JOINTS_0" not in at: continue
                w = acc(self.js, self.bin, at["WEIGHTS_0"]).astype(np.float64)
                if (w.max(1) > 0.99 * np.maximum(w.sum(1), 1e-9)).all(): continue      # every vertex on one bone: a gun part
                out.append((self.js["accessors"][at["POSITION"]]["count"], mi, pi))
        out.sort(key=lambda a: -a[0]); return [(mi, pi) for _, mi, pi in out]

    def arm(self): return self.arms()[0]            # the one the new arm takes the place of

    def skin(self):                                 # the arms' vertices: where they stand, their joints, their weights (each vertex's summing to one)
        P, J, W = [], [], []
        for mi, pi in self.arms():
            at = self.js["meshes"][mi]["primitives"][pi]["attributes"]
            P.append(acc(self.js, self.bin, at["POSITION"]).astype(np.float64)); J.append(acc(self.js, self.bin, at["JOINTS_0"]).astype(np.int64))
            W.append(acc(self.js, self.bin, at["WEIGHTS_0"]).astype(np.float64))
        P, J, W = np.vstack(P), np.vstack(J), np.vstack(W); return P, J, W / np.maximum(W.sum(1, keepdims=True), 1e-9)


def roles_maesen(r, S):                             # DJMaesen (grenade launcher, minigun): L_arm, L_elbow, L_wrist, L_thumb1.., point, middle, ring, pink
    R = {"shoulder": r.find(S + r"_arm_\d+"), "elbow": r.find(S + r"_elbow_\d+"), "wrist": r.find(S + r"_wrist_\d+")}
    for f, nm in zip(FINGERS, ("thumb", "point", "middle", "ring", "pink")): R[f] = r.chain(r.find(S + "_" + nm + r"1_\d+"), 4)
    pal = [k for k, n in enumerate(r.names) if re.fullmatch(r"(%s_)?palm(_2)?_\d+" % S, n) and r.par[k] == R["wrist"]]
    R["palm"] = pal[0] if pal else None; R["twist"] = []                        # (the forearm is one bone: the wrist hangs straight off it)
    return R


def roles_cransh(r, S):                             # Cransh (Remington): UpArm_L, Forearm_L, Hand_L and five unnamed chains under the hand
    R = {"shoulder": r.find(r"UpArm_%s_\d+" % S), "elbow": r.find(r"Forearm_%s_\d+" % S), "wrist": r.find(r"Hand_%s_\d+" % S)}
    ch = [r.chain(k, 5) for k in r.kids[R["wrist"]]]
    thumb = min(ch, key=len); rest = [c for c in ch if c is not thumb]          # the thumb has no bone of its own in the palm: the short chain
    rest.sort(key=lambda c: np.linalg.norm(r.pos[c[1]] - r.pos[thumb[0]]))      # the index is the knuckle nearest the thumb's root
    R["thumb"] = thumb[:4]
    for f, c in zip(FINGERS[1:], rest): R[f] = c[1:5]
    R["palm"] = None
    R["twist"] = r.chain(R["elbow"], 4)[1:]                                     # BoneTwist_03, _02, _01 down the forearm: its skin hangs on these, none on Forearm itself
    return R


def roles_aug(r, S):                                # a Counter-Strike arm rig (the flamethrower's): l_upperarm, l_forearm, l_wrist, l_index_low / _mid / _tip ...
    s = S.lower(); R = {"shoulder": r.find(s + r"_upperarm_\d+"), "elbow": r.find(s + r"_forearm_\d+"), "wrist": r.find(s + r"_wrist_\d+")}
    for f in FINGERS: R[f] = r.chain(r.find(s + "_" + f + r"_low_\d+"), 4)        # (the fourth is the exporter's stub: STUBS)
    R["palm"] = None
    R["twist"] = r.chain(r.find(s + r"_armtwist_1_\d+"), 4)                     # l_armtwist_1 .. _4 down the forearm, the first at the elbow: its skin hangs on these, none on l_forearm
    return R


ROLES = {"maesen": roles_maesen, "cransh": roles_cransh, "aug": roles_aug}


def skin_tips(r, R):
    """finger -> where its skin ends: out from its last joint, towards the furthest of the skin that hangs on that joint."""
    P, J, W = r.skin(); out = {}
    for f in FINGERS:
        k = R[f][2]; on = (W * (J == k)).sum(1) > 0.5; assert on.any(), "no skin on %s" % r.names[k]
        Q = P[on] - r.pos[k]; far = np.linalg.norm(Q, axis=1); d = Q[far >= np.percentile(far, 95)].mean(0); d /= np.linalg.norm(d)
        out[f] = r.pos[k] + d * float((Q @ d).max())
    return out


def frames(r, R, tips=None):
    """role -> (joint, frame, length along the frame's x). Also the hand's width: index knuckle to pinky knuckle.
    tips: {finger: where it ends} for a rig whose chains do not end at the fingertip (skin_tips)."""
    P = r.pos; unit = lambda v: v / max(np.linalg.norm(v), 1e-12)
    across = P[R["index"][0]] - P[R["pinky"][0]]
    def fr(o, to, second):
        x = unit(to - o); y = second - x * np.dot(second, x)
        if np.linalg.norm(y) < 1e-6 * max(1.0, np.linalg.norm(second)): y = np.cross(x, [0.3, 0.9, 0.1])
        y = unit(y); F = np.eye(4); F[:3, 0] = x; F[:3, 1] = y; F[:3, 2] = np.cross(x, y); F[:3, 3] = o; return F
    sh, el, wr, mid = P[R["shoulder"]], P[R["elbow"]], P[R["wrist"]], P[R["middle"][0]]
    out = {"shoulder": (R["shoulder"], fr(sh, el, wr - el), np.linalg.norm(el - sh)),
           "elbow": (R["elbow"], fr(el, wr, sh - el), np.linalg.norm(wr - el)),
           "wrist": (R["wrist"], fr(wr, mid, across), np.linalg.norm(mid - wr))}
    if R.get("palm") is not None: out["palm"] = (R["palm"], fr(wr, mid, across), np.linalg.norm(mid - wr))
    for f in FINGERS:
        c = R[f]; at = [P[k] for k in c]
        if tips is not None: at = at[:3] + [tips[f]]
        for i in range(min(3, len(at) - 1)):
            ln = np.linalg.norm(at[i + 1] - at[i])
            assert 0.02 * np.linalg.norm(across) < ln < 1.2 * np.linalg.norm(across), "a %s bone %.3f long on a hand %.3f wide (%s): a joint without a bind pose?" % (f, ln, np.linalg.norm(across), r.names[c[i]])
            out[f + str(i + 1)] = (c[i], fr(at[i], at[i + 1], across), ln)
    return out, np.linalg.norm(across)


def twist_knots(r, R):
    """A rig's forearm twist joints as [(how far down the forearm, joint)], elbow end first; [] where it has none."""
    el = r.pos[R["elbow"]]; ax = r.pos[R["wrist"]] - el; knots = []
    for k in R["twist"]:
        x = float((r.pos[k] - el) @ ax / (ax @ ax)); off = np.linalg.norm((r.pos[k] - el) - x * ax) / np.linalg.norm(ax)
        assert off < 0.05 and -0.05 < x < 0.95 and (not knots or x > knots[-1][0]), "%s is not a twist joint down the forearm (at %.2f, %.2f off its line)" % (r.names[k], x, off)
        knots.append((x, k))
    return knots


def along(x, knots):
    """A forearm's weight shared along its twist joints: all on the first above its place, all on the last below its
    place, and between two of them in proportion."""
    if x <= knots[0][0]: return [(knots[0][1], 1.0)]
    for (x0, k0), (x1, k1) in zip(knots, knots[1:]):
        if x <= x1: u = (x - x0) / (x1 - x0); return [(k0, 1.0 - u), (k1, u)]
    return [(knots[-1][1], 1.0)]


# ------------------------------------------------------------------ the donor arm
def donor_arm(verbose):
    js, bin_ = parse(open(os.path.join(REPO, "assets", DONOR + ".glb"), "rb").read()); d = Rig(js, bin_)
    mi, pi = d.arm(); p = js["meshes"][mi]["primitives"][pi]; at = p["attributes"]
    A = {"P": acc(js, bin_, at["POSITION"]).astype(np.float64), "N": acc(js, bin_, at["NORMAL"]).astype(np.float64),
         "UV": acc(js, bin_, at["TEXCOORD_0"]).astype(np.float32), "J": acc(js, bin_, at["JOINTS_0"]).astype(np.int64),
         "W": acc(js, bin_, at["WEIGHTS_0"]).astype(np.float64), "I": acc(js, bin_, p["indices"]).astype(np.int64).reshape(-1, 3)}
    A["W"] = A["W"] / np.maximum(A["W"].sum(1, keepdims=True), 1e-9)
    # the sleeve's open ends at the shoulders: rims of the mesh (edges with one triangle, vertices welded by position:
    # a UV seam is not a rim) whose vertices hang on the upper arm, pushed back along it
    key = {}; weld = np.array([key.setdefault(tuple(np.round(v, 3)), len(key)) for v in A["P"]])
    cnt = {}
    for t in A["I"]:
        for a, b in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
            k = (min(weld[a], weld[b]), max(weld[a], weld[b])); cnt.setdefault(k, []).append((a, b))
    rim = [v[0] for v in cnt.values() if len(v) == 1]
    comp = {}                                        # welded vertex -> its rim (union-find)
    def top(x):
        while comp.setdefault(x, x) != x: comp[x] = comp[comp[x]]; x = comp[x]
        return x
    for a, b in rim: comp[top(weld[a])] = top(weld[b])
    loops = {}
    for a, b in rim: loops.setdefault(top(weld[a]), []).append((a, b))
    P, J, W = A["P"], A["J"], A["W"]; A["rims"] = {}
    for S in ("L", "R"):
        R = roles_maesen(d, S); sh, el = d.pos[R["shoulder"]], d.pos[R["elbow"]]; L = np.linalg.norm(el - sh); ax = (el - sh) / L
        best = None
        for edges in loops.values():
            vs = sorted({v for e in edges for v in e}); wsh = np.mean([W[v][J[v] == R["shoulder"]].sum() for v in vs])
            if wsh < 0.6: continue                   # not on this upper arm
            x = np.mean([(P[v] - sh) @ ax for v in vs])
            if best is None or x < best[0]: best = (x, edges, vs)
        assert best is not None, "no open sleeve end found on the %s upper arm" % S
        A["rims"][S] = (best[1], best[2])
        if verbose: print("  donor %s sleeve: a rim of %d vertices, %.2f of the way down the upper arm" % (S, len(best[2]), best[0] / L))
    return d, A


def skin_start(r, R):
    """How far down the upper arm (0 the shoulder joint, 1 the elbow) a rig's own arm skin begins."""
    P, J, W = r.skin(); sh, el = r.pos[R["shoulder"]], r.pos[R["elbow"]]; L = np.linalg.norm(el - sh)
    on = np.array([W[v][J[v] == R["shoulder"]].sum() for v in range(len(P))]) > 0.5
    return float((((P[on] - sh) @ (el - sh)) / (L * L)).min())


def open_top(r, R):
    """Whether a rig's own sleeve ends open at the shoulder: its arms' skin (vertices welded by position, across its
    meshes) has an edge that one triangle only uses, hanging on the upper arm's joint."""
    P, J, W = r.skin(); I = []; base = 0
    for mi, pi in r.arms():
        p = r.js["meshes"][mi]["primitives"][pi]; I.append(acc(r.js, r.bin, p["indices"]).astype(np.int64).reshape(-1, 3) + base)
        base += r.js["accessors"][p["attributes"]["POSITION"]]["count"]
    size = float((P.max(0) - P.min(0)).max()); key = {}; weld = [key.setdefault(tuple(np.round(v / size, 4)), len(key)) for v in P]; cnt = {}
    for t in np.vstack(I):
        for a, b in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
            if weld[a] != weld[b]: cnt.setdefault((min(weld[a], weld[b]), max(weld[a], weld[b])), []).append((a, b))
    wsh = (W * (J == R["shoulder"])).sum(1)
    return any(len(v) == 1 and wsh[v[0][0]] > 0.5 and wsh[v[0][1]] > 0.5 for v in cnt.values())


def extended(d, A, starts, shut, verbose):
    """The donor arm with each upper sleeve drawn out along the arm until its open rim is `starts[side]` of the way
    down it (0 the shoulder joint, 1 the elbow), and, where `shut[side]`, the rim closed with a shallow dome in the
    sleeve's cloth."""
    P = A["P"].copy(); N, UV, J, W, I = A["N"], A["UV"], A["J"], A["W"], A["I"]; newP, newN, newUV, newJ, newW, newI = [], [], [], [], [], []
    for S in ("L", "R"):
        R = roles_maesen(d, S); sh, el = d.pos[R["shoulder"]], d.pos[R["elbow"]]; L = np.linalg.norm(el - sh); ax = (el - sh) / L
        edges, vs = A["rims"][S]; at = np.mean([(A["P"][v] - sh) @ ax for v in vs]) / L; to = max(starts[S], 0.0)
        if to < at - 1e-6:                           # (else that gun's sleeve began no higher than the launcher's ends)
            wsh = np.array([W[v][J[v] == R["shoulder"]].sum() for v in range(len(P))]); x = ((A["P"] - sh) @ ax) / L
            on = (wsh > 0) & (x < 1.0); k = (1.0 - to) / (1.0 - at)                 # the elbow stays; everything above it moves away from it by k
            P[on] += np.outer((1.0 - x[on]) * (1.0 - k) * L * wsh[on], ax)
        if not shut[S]:                              # (the gun's own sleeve ended open here: so does this one)
            if verbose: print("  %s sleeve: drawn out from %.2f to %.2f of the way down the upper arm, and left open as the gun's own was" % (S, at, np.mean([(P[v] - sh) @ ax for v in vs]) / L))
            continue
        # The cap: rings of the rim's own outline drawn in towards a point CAP_DOME rim radii up the arm. In the picture
        # it is a disc as long round its edge as the rim is, laid on the cloth just inside the rim's line, each ring as
        # far in from the disc's edge as it is over the dome from the rim (so the cloth is not finer in one band).
        mid = P[vs].mean(0); rad = np.linalg.norm(P[vs] - mid, axis=1).mean()
        e1 = P[vs[0]] - mid; e1 -= ax * (e1 @ ax); e1 /= np.linalg.norm(e1); e2 = np.cross(ax, e1)
        near = sorted(set(I[np.isin(I, vs).any(1)].ravel().tolist()) - set(vs))     # the sleeve's vertices one row in from the rim
        r_uv = sum(np.linalg.norm(UV[a] - UV[b]) for a, b in edges) / (2.0 * np.pi)
        inward = UV[near].mean(0) - UV[vs].mean(0); c_uv = UV[vs].mean(0) + inward / np.linalg.norm(inward) * (1.25 * r_uv)
        f = 0.5 * np.pi * np.arange(CAP_RINGS + 1) / CAP_RINGS                       # the rings' angles up the dome, the point last
        run = np.concatenate([[0.0], np.cumsum(np.hypot(np.diff(np.cos(f)), CAP_DOME * np.diff(np.sin(f))))]); rho = 1.0 - run / run[-1]
        col = {}
        for v in vs:
            col[v] = len(P) + len(newP); q = P[v] - mid; th = np.arctan2(q @ e2, q @ e1)
            for k in range(CAP_RINGS):
                n = q / np.linalg.norm(q) * np.cos(f[k]) - ax * np.sin(f[k])
                newP.append(mid + q * np.cos(f[k]) - ax * (CAP_DOME * rad * np.sin(f[k]))); newN.append(n / np.linalg.norm(n))
                newUV.append(c_uv + r_uv * rho[k] * np.array([np.cos(th), np.sin(th)])); newJ.append(J[v]); newW.append(W[v])
        pole = len(P) + len(newP); v0 = max(vs, key=lambda v: W[v][J[v] == R["shoulder"]].sum())
        newP.append(mid - ax * (CAP_DOME * rad)); newN.append(-ax); newUV.append(c_uv); newJ.append(J[v0]); newW.append(W[v0])
        for a, b in edges:                           # (a, b) runs as the sleeve's own triangle does, so the cap's run back along it
            for k in range(CAP_RINGS - 1): newI += [(col[b] + k, col[a] + k, col[a] + k + 1), (col[b] + k, col[a] + k + 1, col[b] + k + 1)]
            newI.append((col[b] + CAP_RINGS - 1, col[a] + CAP_RINGS - 1, pole))
        if verbose: print("  %s sleeve: drawn out from %.2f to %.2f of the way down the upper arm; its cap wears the cloth about (%.3f, %.3f), radius %.3f" % (
            S, at, np.mean([(P[v] - sh) @ ax for v in vs]) / L, c_uv[0], c_uv[1], r_uv))
    if not newP: return {"n0": len(P), "P": P, "N": N, "UV": UV, "J": J, "W": W, "I": I}
    return {"n0": len(P), "P": np.vstack([P, newP]), "N": np.vstack([N, newN]), "UV": np.vstack([UV, np.array(newUV, np.float32)]),
            "J": np.vstack([J, newJ]), "W": np.vstack([W, newW]), "I": np.vstack([I, np.array(newI, np.int64)])}


# ------------------------------------------------------------------ one gun
def refit(gun, d, A, out_dir, verbose):
    got = subprocess.run(["git", "-C", REPO, "show", "%s:assets/%s.glb" % (ORIGIN, gun)], capture_output=True)
    if got.returncode: sys.exit("refit_arms: cannot read assets/%s.glb as it stood at commit %s (%s).\nThe guns' own arms are kept only in git's history: this needs a clone that has that commit." % (
        gun, ORIGIN[:7], (got.stderr.decode(errors="replace").strip().splitlines() or ["git failed"])[-1]))
    raw = got.stdout; js, bin_ = parse(raw); t = Rig(js, bin_); T = {}; mp = {}; split = {}; stub = GUNS[gun] in STUBS
    assert len(t.joints) <= 256, "%s has %d joints: its skin's joint numbers are written as bytes" % (gun, len(t.joints))
    A = extended(d, A, {S: skin_start(t, ROLES[GUNS[gun]](t, S)) for S in ("L", "R")}, {S: not open_top(t, ROLES[GUNS[gun]](t, S)) for S in ("L", "R")}, verbose)
    # what goes with the old arms: the rest of them (meshes besides the one the new arm takes), and what they wore
    mi, pi = t.arm(); gone = [m for m, _ in t.arms() if m != mi]; armj = set()
    assert all(len(js["meshes"][m]["primitives"]) == 1 for m in gone + [mi]), "%s: an arm mesh of several primitives (a mesh goes whole)" % gun
    for S in ("L", "R"):
        Rt = ROLES[GUNS[gun]](t, S); armj |= {Rt["shoulder"], Rt["elbow"], Rt["wrist"]} | set(Rt["twist"]) | {k for f in FINGERS for k in Rt[f]}
    worn = [k for k, m in enumerate(js["meshes"]) if k != mi and any(js["materials"][q["material"]].get("name") in WORN.get(gun, ()) for q in m["primitives"] if "material" in q)]
    for k in worn:
        for q in js["meshes"][k]["primitives"]:
            on = acc(js, bin_, q["attributes"]["JOINTS_0"])[acc(js, bin_, q["attributes"]["WEIGHTS_0"]) > 0]
            assert set(np.unique(on).tolist()) <= armj, "%s hangs on %s: not worn on an arm" % (js["meshes"][k].get("name"), sorted({t.names[x] for x in np.unique(on)} - {t.names[x] for x in armj}))
    assert len(worn) == len(WORN.get(gun, ())), "%s: %d meshes wear %s" % (gun, len(worn), WORN.get(gun))
    for S in ("L", "R"):
        Rd, Rt = roles_maesen(d, S), ROLES[GUNS[gun]](t, S)
        Fd, wd = frames(d, Rd, skin_tips(d, Rd) if stub else None); Ft, wt = frames(t, Rt, skin_tips(t, Rt) if stub else None); k = wt / wd
        for role, (jd, fd, ld) in Fd.items():
            jt, ft, lt = Ft[role] if role in Ft else Ft["wrist"]                # (no bone in the palm there: the hand carries it)
            T[jd] = ft @ np.diag([lt / ld, k, k, 1.0]) @ np.linalg.inv(fd); mp[jd] = jt
            if verbose: print("  %s %-9s %-16s -> %-20s length x%.3f, across x%.4f" % (S, role, d.names[jd], t.names[jt], lt / ld, k))
        knots = twist_knots(t, Rt)
        if knots:                                    # the donor's forearm joint -> where its forearm runs, and the target's twist joints along it
            el = d.pos[Rd["elbow"]]; ax = d.pos[Rd["wrist"]] - el; split[Rd["elbow"]] = (el, ax / (ax @ ax), knots)
            if verbose: print("  %s forearm   its weight shared along %s" % (S, ", ".join("%s (at %.2f)" % (t.names[j], x) for x, j in knots)))
    P, N, J, W = A["P"], A["N"], A["J"], A["W"]
    miss = sorted({d.names[k] for k in np.unique(J[W > 1e-4]) if k not in T})
    assert not miss, "donor joints with no role: %s" % miss
    P4 = np.concatenate([P, np.ones((len(P), 1))], 1); nP = np.zeros((len(P), 3)); nN = np.zeros((len(P), 3))
    for c in range(4):
        for k in np.unique(J[:, c]):
            sel = (J[:, c] == k) & (W[:, c] > 0)
            if sel.any(): M = T[k]; nP[sel] += W[sel, c:c + 1] * (P4[sel] @ M.T)[:, :3]; nN[sel] += W[sel, c:c + 1] * (N[sel] @ np.linalg.inv(M[:3, :3]))
    nN /= np.maximum(np.linalg.norm(nN, axis=1, keepdims=True), 1e-9)
    # The weights, naming the target's joints: a forearm's share goes along the twist chain where there is one; weights
    # that come to name one joint are one weight (the palm's bone and the hand's are the same joint on a rig with no
    # bone in the palm); the four largest are kept. The bind shape above does not depend on any of this.
    nJ = np.zeros((len(P), 4), np.int64); nW = np.zeros((len(P), 4))
    for v in range(len(P)):
        on = {}
        for c in range(4):
            if W[v, c] <= 0: continue
            if J[v, c] in split:
                el, axn, knots = split[J[v, c]]
                for jt, f in along(float((P[v] - el) @ axn), knots):
                    if f > 0: on[jt] = on.get(jt, 0.0) + W[v, c] * f
            else: on[mp[J[v, c]]] = on.get(mp[J[v, c]], 0.0) + W[v, c]
        top = list(on.items()) if len(on) <= 4 else sorted(on.items(), key=lambda jw: -jw[1])[:4]; tot = sum(w for _, w in top)
        for c, (jt, w) in enumerate(top): nJ[v, c] = jt; nW[v, c] = w / tot
    p = js["meshes"][mi]["primitives"][pi]; n = len(js["accessors"])
    extra = {n: (nP.astype(np.float32), 5126, "VEC3", 34962, True), n + 1: (nN.astype(np.float32), 5126, "VEC3", 34962, False),
             n + 2: (A["UV"], 5126, "VEC2", 34962, False), n + 3: (nJ.astype(np.uint8), 5121, "VEC4", 34962, False),
             n + 4: (nW.astype(np.float32), 5126, "VEC4", 34962, False), n + 5: (A["I"].astype(np.uint32).reshape(-1, 1), 5125, "SCALAR", 34963, False)}
    js["accessors"] += [None] * 6
    p["attributes"] = {"POSITION": n, "NORMAL": n + 1, "TEXCOORD_0": n + 2, "JOINTS_0": n + 3, "WEIGHTS_0": n + 4}; p["indices"] = n + 5; p.pop("targets", None)
    js["materials"][p["material"]] = {"name": "arms", "doubleSided": True, "pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 1.0}}
    changes = "the arms of this model are replaced by this one's, refitted to this skeleton (tools/arms/refit_arms.py)"
    if gone or worn:                                 # their meshes leave the file (their nodes stay, holding nothing)
        names = lambda ks: ", ".join(js["meshes"][k].get("name", "mesh %d" % k) for k in ks)
        if verbose: print("  with its old arms go: %s%s" % (names(gone) or "no other mesh", "; and what they wore: " + names(worn) if worn else ""))
        if worn: changes += "; what its own arms wore is removed (%s)" % ", ".join(WORN[gun])
        keep = [k for k in range(len(js["meshes"])) if k not in gone and k not in worn]; mm = {o: i for i, o in enumerate(keep)}
        js["meshes"] = [js["meshes"][o] for o in keep]; mi = mm[mi]
        for nd in js["nodes"]:
            if "mesh" in nd and nd["mesh"] in mm: nd["mesh"] = mm[nd["mesh"]]
            elif "mesh" in nd: del nd["mesh"]; nd.pop("skin", None)
        still = {e for m in js["meshes"] for q in m["primitives"] if "material" in q for e in js["materials"][q["material"]].get("extensions", {})} | {e for q in (js["materials"][p["material"]],) for e in q.get("extensions", {})}
        for k in ("extensionsUsed", "extensionsRequired"):   # an extension only a material that went used (the watch's glass: clearcoat) is no longer used
            if k in js:
                js[k] = [e for e in js[k] if not e.startswith("KHR_materials_") or e in still]
                if not js[k]: del js[k]
    js["asset"].setdefault("extras", {})["arms"] = dict(d.js["asset"].get("extras", {}), changes=changes)
    views = compact(js, bin_, extra)
    path = os.path.join(out_dir, gun + ".glb"); size = write(js, views, path)
    print("%-10s arms: mesh %d, %d vertices (%d of them the sleeves' caps); %d images left; %.1f MB (was %.1f)" % (
        gun, mi, len(nP), len(nP) - A["n0"], len(js.get("images", [])), size / 1e6, len(raw) / 1e6))


def main():
    args = sys.argv[1:]; verbose = "-v" in args
    out_dir = args[args.index("--out") + 1] if "--out" in args else os.path.join(REPO, "assets")
    guns = [a for a in args if a in GUNS] or list(GUNS)
    os.makedirs(out_dir, exist_ok=True)
    d, A = donor_arm(verbose)
    for g in guns: refit(g, d, A, out_dir, verbose)


if __name__ == "__main__":
    main()
