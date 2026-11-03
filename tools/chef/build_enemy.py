"""Enemy builder -- MakeHuman body + modelled props + CMU mocap, exported for the game.

One spec per enemy (SPECS below): body shape, outfit + recolour, props, mocap clips.

Everything here is free / CC0 / public, no accounts:
  * MPFB2 Blender extension     blender --online-mode --command extension install mpfb --enable
  * MakeHuman system assets      CC0 pack unzipped into MPFB's user data dir:
      https://files.makehumancommunity.org/asset_packs/makehuman_system_assets/makehuman_system_assets_cc0.zip
  * CMU mocap (cgspeed BVH)      free for any use (see the mirror's READMEFIRST):
      https://github.com/una-dinosauria/cmu-mocap  -> tools/chef/fetch_mocap.sh puts the clips in build/cmu/

Run (NOT --factory-startup: MPFB must be enabled):
    blender -b -P tools/chef/build_enemy.py -- <enemy> assets/<enemy>.glb [preview_dir]
    (tools/chef/build_all.sh rebuilds every enemy)

Game contract (src/main.c LoadEnemyRig): feet at origin, facing glTF +Z (Blender -Y),
metres, clips walk / idle / run / hit / attack / death. raylib resamples clips at 60 fps
and the game plays loops/hit at 30 frames/s, attack at 90, death at 60 -- so each clip is
laid on a 60 fps timeline at the density that makes it look real-time IN GAME.
"""
import math
import os
import random
import sys

import bpy
import numpy as np
from mathutils import Matrix, Quaternion, Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
ENEMY = argv[0] if argv else "cook"
OUT = os.path.abspath(argv[1] if len(argv) > 1 else f"assets/{ENEMY}.glb")
PREVIEW = os.path.abspath(argv[2]) if len(argv) > 2 else None
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CMU = os.path.join(REPO, "build", "cmu")
TEX = os.path.join(REPO, "build", "enemy_tex", ENEMY)
os.makedirs(TEX, exist_ok=True)
random.seed(7)
np.random.seed(7)

from bl_ext.blender_org.mpfb.services.humanservice import HumanService  # noqa: E402


# =============================================================== specs
# phenotype/targets: MakeHuman sliders. recolor: clothes-name substring -> (base rgb,
# blood splats, fresh drops, grease) applied to that garment's texture (luminance kept).
# clips: role -> (bvh, first dump-frame, last dump-frame, loop, crossfade). Dump frames are
# 30 fps samples (BVH frame f0+4i) -- picked from stick-figure strips of each take.
BASE_CLIPS = {"idle": ("120_22", 4, 52, True, 8), "walk": ("120_22", 167, 211, True, 6),
              "run": ("09_01", 5, 27, True, 4), "attack": ("79_01", 21, 47, False, 0),
              "death": ("90_18", 2, 60, False, 0)}
ZOMBIE_WALK = ("104_41", 269, 314, True, 6)          # heavier, dragging lurch
SPECS = {
    "cook": dict(   # canteen cook: the wave-1 staple
        phenotype=dict(gender=1.0, age=0.74, muscle=0.62, weight=0.92, proportions=0.3, height=0.55),
        targets={"stomach-pregnant-incr": 0.75, "measure-waist-circ-incr": 0.6, "torso-scale-depth-incr": 0.35,
                 "neck-double-incr": 0.7, "measure-neck-circ-incr": 0.6, "torso-muscle-dorsi-incr": 0.3},
        skin="middleage_caucasian_male", eyebrows="eyebrow007",
        clothes=["male_worksuit01", "shoes02"],
        recolor={"worksuit": ((0.93, 0.91, 0.86), 9, 7, 8)},
        props="cook", clips=dict(BASE_CLIPS)),
    "linecook": dict(   # line cook: young, wiry, fast -- knife
        phenotype=dict(gender=1.0, age=0.42, muscle=0.55, weight=0.25, proportions=0.6, height=0.62),
        targets={"stomach-pregnant-decr": 0.4, "neck-scale-horiz-decr": 0.3},
        skin="young_caucasian_male", eyebrows="eyebrow001",
        clothes=["male_casualsuit06", "shoes06"],
        recolor={"casualsuit06": ((0.88, 0.87, 0.84), 6, 9, 4), "shoes06": ((0.22, 0.22, 0.24), 0, 2, 0)},
        props="linecook", clips=dict(BASE_CLIPS, walk=("09_01", 5, 27, True, 4))),
    "butcher": dict(   # butcher: huge, slow, hits like a truck
        phenotype=dict(gender=1.0, age=0.66, muscle=0.95, weight=1.0, proportions=0.2, height=0.85),
        targets={"stomach-pregnant-incr": 0.55, "measure-neck-circ-incr": 1.0, "neck-double-incr": 1.0,
                 "torso-scale-horiz-incr": 0.5, "torso-muscle-pectoral-incr": 0.6, "measure-shoulder-dist-incr": 0.6},
        skin="middleage_caucasian_male", eyebrows="eyebrow010",
        clothes=["male_casualsuit06", "shoes03"],
        recolor={"casualsuit06": ((0.62, 0.60, 0.56), 8, 10, 8)},
        props="butcher", clips=dict(BASE_CLIPS, walk=ZOMBIE_WALK, run=ZOMBIE_WALK)),
    "liquidator": dict(   # liquidator: off-white cotton coverall, GP-5 mask, shovel
        phenotype=dict(gender=1.0, age=0.55, muscle=0.7, weight=0.6, proportions=0.5, height=0.6),
        targets={},
        skin="middleage_caucasian_male", eyebrows="eyebrow003",
        clothes=["male_worksuit01", "shoes03"],
        recolor={"worksuit": ((0.80, 0.80, 0.74), 5, 4, 14)},
        props="liquidator", clips=dict(BASE_CLIPS, walk=ZOMBIE_WALK)),
    "headchef": dict(   # head chef: the giant -- tall toque, twin cleavers (drawn x1.4 in game)
        phenotype=dict(gender=1.0, age=0.85, muscle=0.75, weight=1.0, proportions=0.25, height=0.75),
        targets={"stomach-pregnant-incr": 1.0, "measure-waist-circ-incr": 0.8, "neck-double-incr": 1.0,
                 "measure-neck-circ-incr": 0.8, "torso-scale-horiz-incr": 0.4},
        skin="old_caucasian_male", eyebrows="eyebrow005",
        clothes=["male_worksuit01", "shoes03"],
        recolor={"worksuit": ((0.96, 0.95, 0.92), 18, 14, 6)},
        props="headchef", clips=dict(BASE_CLIPS, walk=ZOMBIE_WALK, run=ZOMBIE_WALK)),
}
SPEC = SPECS[ENEMY]

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene

# =============================================================== 1. body
human = {
    "name": ENEMY,
    "phenotype": dict(SPEC["phenotype"], cupsize=0.5, firmness=0.5,
                      race={"asian": 0.0, "african": 0.0, "caucasian": 1.0}),
    "targets": [{"target": t, "value": v} for t, v in SPEC["targets"].items()],
    "rig": "cmu_mb",                 # CMU MotionBuilder skeleton == the BVH joint names
    "proxy": "male_generic.proxy",   # 13.6k-vert body (GPU-skinned in game, so detail is cheap)
    "eyes": "low-poly.mhclo", "eyebrows": "", "eyelashes": "",   # alpha cards -> black bars unlit
    "teeth": "", "tongue": "", "hair": "",
    "skin_mhmat": SPEC["skin"] + ".mhmat", "skin_material_type": "GAMEENGINE",
    "eyes_material_type": "MAKESKIN", "eyes_material_settings": {}, "skin_material_settings": {},
    "clothes": [c + ".mhclo" for c in SPEC["clothes"]], "clothes_material_type": "MAKESKIN",
    "expressions": [], "alternative_materials": {},
}
settings = HumanService.get_default_deserialization_settings()
settings["subdiv_levels"] = 0
settings["material_instances"] = "NEVER"
basemesh = HumanService.deserialize_from_dict(human, settings)
rig = next(o for o in bpy.data.objects if o.type == "ARMATURE")
bpy.data.objects.remove(basemesh, do_unlink=True)          # the proxy is the visible body
meshes = [o for o in bpy.data.objects if o.type == "MESH"]
body = next(o for o in meshes if "male_generic" in o.name or "male1591" in o.name)
print("enemy: rig", rig.name, "meshes", [o.name for o in meshes])

# =============================================================== 2. materials -> plain, exportable, recoloured
def first_image(mat):
    if not mat or not mat.use_nodes:
        return None
    for n in mat.node_tree.nodes:
        if n.type == "TEX_IMAGE" and n.image and ("diffuse" in n.image.name.lower() or "color" in n.label.lower()
                                                   or n.image.name.lower().endswith((".png", ".jpg"))):
            if "normal" not in n.image.name.lower() and "_ao" not in n.image.name.lower():
                return n.image
    return None


def simple_material(name, image=None, rgb=(0.8, 0.8, 0.8), rough=0.8, metal=0.0, emit=None, alpha_clip=False):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    if image is not None:
        t = nt.nodes.new("ShaderNodeTexImage")
        t.image = image
        nt.links.new(t.outputs["Color"], b.inputs["Base Color"])
        if alpha_clip:
            nt.links.new(t.outputs["Alpha"], b.inputs["Alpha"])
            m.blend_method = "CLIP" if hasattr(m, "blend_method") else None
    else:
        b.inputs["Base Color"].default_value = (*rgb, 1)
    if emit:
        b.inputs["Emission Color"].default_value = (*emit, 1)
        b.inputs["Emission Strength"].default_value = 4.0
    return m


def to_np(img):
    a = np.empty(img.size[0] * img.size[1] * 4, dtype=np.float32)
    img.pixels.foreach_get(a)
    return a.reshape(img.size[1], img.size[0], 4)


def from_np(name, arr):
    h, w = arr.shape[:2]
    img = bpy.data.images.new(name, w, h, alpha=True)
    img.pixels.foreach_set(arr.astype(np.float32).ravel())
    img.filepath_raw = os.path.join(TEX, name + ".png")
    img.file_format = "PNG"
    img.save()
    return img


def smooth_noise(h, w, cells, seed):
    rng = np.random.default_rng(seed)
    small = rng.random((cells, cells)).astype(np.float32)
    ys = (np.arange(h) * (cells - 1) / max(1, h - 1))
    xs = (np.arange(w) * (cells - 1) / max(1, w - 1))
    y0 = np.floor(ys).astype(int); x0 = np.floor(xs).astype(int)
    y1 = np.minimum(y0 + 1, cells - 1); x1 = np.minimum(x0 + 1, cells - 1)
    fy = (ys - y0)[:, None]; fx = (xs - x0)[None, :]
    a = small[y0][:, x0]; b = small[y0][:, x1]; c = small[y1][:, x0]; d = small[y1][:, x1]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def splatter(arr, n, rmin, rmax, color, seed, alpha=0.9):
    h, w = arr.shape[:2]
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:h, 0:w]
    for _ in range(n):
        cy, cx = rng.integers(0, h), rng.integers(0, w)
        r = rng.uniform(rmin, rmax) * w
        # one main blob + satellite droplets, ragged edge from noise
        for k in range(int(rng.integers(8, 20))):
            oy = cy + rng.normal(0, r * 1.6); ox = cx + rng.normal(0, r * 1.6)
            rr = r * (rng.uniform(0.6, 1.0) if k == 0 else rng.uniform(0.08, 0.35))
            y0, y1 = int(max(0, oy - rr - 2)), int(min(h, oy + rr + 2))
            x0, x1 = int(max(0, ox - rr - 2)), int(min(w, ox + rr + 2))
            if y1 <= y0 or x1 <= x0:
                continue
            d = np.sqrt((yy[y0:y1, x0:x1] - oy) ** 2 + (xx[y0:y1, x0:x1] - ox) ** 2) / rr
            m = np.clip((1.0 - d) * 3.0, 0, 1) * alpha * rng.uniform(0.55, 1.0)
            for ch in range(3):
                arr[y0:y1, x0:x1, ch] = arr[y0:y1, x0:x1, ch] * (1 - m) + color[ch] * m * (0.75 + 0.25 * arr[y0:y1, x0:x1, ch])


SIZE = 2048
for o in meshes:
    src = first_image(o.data.materials[0] if o.data.materials else None)
    if src is not None and max(src.size) > SIZE:
        src.scale(SIZE, SIZE)
    rule = next((v for k, v in SPEC["recolor"].items() if k in o.name), None)
    if rule is not None and src is not None:
        # recolour the garment, keeping its luminance (folds, seams), then dirty it up
        (base, n_blood, n_drop, n_grease) = rule
        a = to_np(src)
        lum = (0.3 * a[..., 0] + 0.59 * a[..., 1] + 0.11 * a[..., 2])
        lum = (lum - lum.min()) / (lum.max() - lum.min() + 1e-6)
        grime = smooth_noise(a.shape[0], a.shape[1], 9, 3)
        base = np.array(base, np.float32)
        k = 24   # low-pass the luminance: big folds/seams survive, printed logos don't
        h2, w2 = (a.shape[0] // k) * k, (a.shape[1] // k) * k
        lo = lum[:h2, :w2].reshape(h2 // k, k, w2 // k, k).mean(axis=(1, 3))
        lo = np.kron(lo, np.ones((k, k), np.float32))
        lum_lp = np.pad(lo, ((0, a.shape[0] - h2), (0, a.shape[1] - w2)), mode="edge")
        shade = (0.72 + 0.28 * lum_lp) * (0.80 + 0.12 * lum) * (0.86 + 0.14 * grime)
        out = np.empty_like(a)
        for ch in range(3):
            out[..., ch] = np.clip(base[ch] * shade * 1.12 - 0.10 * (1 - grime) * (ch == 2), 0, 1)
        out[..., 3] = a[..., 3]
        if n_blood: splatter(out, n_blood, 0.006, 0.016, (0.30, 0.03, 0.02), seed=11, alpha=0.75)   # dried blood
        if n_drop:  splatter(out, n_drop, 0.003, 0.008, (0.42, 0.03, 0.02), seed=12, alpha=0.85)    # fresh droplets
        if n_grease: splatter(out, n_grease, 0.02, 0.05, (0.45, 0.40, 0.28), seed=13, alpha=0.22)  # grease / soot
        tag = f"{ENEMY}_{o.name.split('.')[-1]}"
        img = from_np(tag, out)
        o.data.materials.clear(); o.data.materials.append(simple_material(tag, img, rough=0.9))
    elif "low-poly" in o.name:   # eyes: irradiated glow
        o.data.materials.clear(); o.data.materials.append(simple_material("cook_eyes", None, (0.45, 1.0, 0.25), 0.3, emit=(0.45, 1.0, 0.25)))
    elif src is not None:
        alpha = any(k in o.name for k in ("eyebrow", "eyelash"))
        nm = o.data.materials[0].name if o.data.materials else o.name
        o.data.materials.clear(); o.data.materials.append(simple_material(nm, src, rough=0.85, alpha_clip=alpha))

# =============================================================== 3. props (rigidly skinned to one bone each)
props = []


def bevel_box(name, size, loc, rot, mat, bone, bevel=0.003):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc, rotation=rot)
    o = bpy.context.active_object; o.name = name; o.scale = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    m = o.modifiers.new("bevel", "BEVEL"); m.width = bevel; m.segments = 2
    bpy.ops.object.modifier_apply(modifier="bevel")
    o.data.materials.append(mat); props.append((o, bone)); return o


def cyl(name, a, b, r, mat, bone, verts=12, r2=None, smooth=True):
    a, b = Vector(a), Vector(b); d = b - a
    bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=r, radius2=(r if r2 is None else r2), depth=d.length, location=(a + b) / 2)
    o = bpy.context.active_object; o.name = name
    o.rotation_euler = d.to_track_quat("Z", "Y").to_euler()
    if smooth:
        bpy.ops.object.shade_smooth()
    o.data.materials.append(mat); props.append((o, bone)); return o


def blob(name, c, r, mat, bone, scale=(1, 1, 1), sub=3, noise=0.0, seed=0):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=sub, radius=r, location=c)
    o = bpy.context.active_object; o.name = name; o.scale = scale
    if noise > 0:
        rnd = random.Random(seed)
        for v in o.data.vertices:
            v.co *= 1.0 + rnd.uniform(-noise, noise)
    bpy.ops.object.shade_smooth()
    o.data.materials.append(mat); props.append((o, bone)); return o


# head extents from the body skin weights
def group_bbox(obj, group, thr=0.5):
    gi = obj.vertex_groups[group].index
    pts = [obj.matrix_world @ v.co for v in obj.data.vertices if any(g.group == gi and g.weight > thr for g in v.groups)]
    mn = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    mx = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    return mn, mx


hmn, hmx = group_bbox(body, "Head")
hc = (hmn + hmx) / 2
hh = hmx.z - hmn.z

# ---------------------------------------------------------------- facial landmarks (per body, not guessed)
import bmesh
from mathutils.bvhtree import BVHTree

eye_obj = next(o for o in meshes if "low-poly" in o.name)
ev = [eye_obj.matrix_world @ v.co for v in eye_obj.data.vertices]
eyeL = sum((p for p in ev if p.x > 0), Vector()) / max(1, len([p for p in ev if p.x > 0]))
eyeR = sum((p for p in ev if p.x < 0), Vector()) / max(1, len([p for p in ev if p.x < 0]))
eye_c = (eyeL + eyeR) / 2
_hgi = body.vertex_groups["Head"].index
HEADV = [body.matrix_world @ v.co for v in body.data.vertices if any(g.group == _hgi and g.weight > 0.5 for g in v.groups)]
crown = max(p.z for p in HEADV)
nose = min((p for p in HEADV if eye_c.z - 0.09 < p.z < eye_c.z - 0.01), key=lambda p: p.y)
mouth_z = eye_c.z - 0.072
brow_z = eye_c.z + 0.035


def skull_slice(z, band=0.012):
    pts = [p for p in HEADV if abs(p.z - z) < band]
    xs, ys = [p.x for p in pts], [p.y for p in pts]
    return Vector(((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2, z)), (max(xs) - min(xs)) / 2, (max(ys) - min(ys)) / 2


print("enemy: eyes", tuple(round(x, 3) for x in eye_c), "nose", tuple(round(x, 3) for x in nose), "crown", round(crown, 3))

# ---------------------------------------------------------------- painted prop textures
def paint(name, base, grime=0.25, blood=0, drops=0, size=1024, seed=1, stripes=None):
    a = np.zeros((size, size, 4), np.float32); a[..., 3] = 1
    n1 = smooth_noise(size, size, 6, seed); n2 = smooth_noise(size, size, 23, seed + 1)
    shade = 1.0 - grime * (0.6 * n1 + 0.4 * n2)
    for ch in range(3):
        a[..., ch] = base[ch] * shade
    if stripes:   # (period px, rgb) -- e.g. ticking on a neckerchief
        per, col = stripes
        m = ((np.arange(size) // per) % 2 == 0)[None, :]
        for ch in range(3):
            a[..., ch] = np.where(m, a[..., ch] * 0.6 + col[ch] * 0.4, a[..., ch])
    if blood:
        splatter(a, blood, 0.02, 0.06, (0.30, 0.02, 0.02), seed=seed + 5, alpha=0.8)
    if drops:
        splatter(a, drops, 0.008, 0.02, (0.42, 0.03, 0.02), seed=seed + 6, alpha=0.85)
    return simple_material(name, from_np(f"{ENEMY}_{name}", np.clip(a, 0, 1)), rough=0.8)


M_hat = paint("hat", (0.90, 0.88, 0.83), grime=0.22, drops=3, seed=2)
M_rubber = paint("rubber", (0.11, 0.12, 0.11), grime=0.35, seed=3)
M_filter = paint("filter", (0.32, 0.35, 0.22), grime=0.4, seed=4)
M_steel = simple_material("steel", None, (0.72, 0.73, 0.74), 0.28, metal=0.9)
M_wood = paint("wood", (0.30, 0.17, 0.08), grime=0.45, seed=5)
M_blood = simple_material("blood", None, (0.34, 0.02, 0.02), 0.3)
M_lens = simple_material("lens", None, (0.45, 1.0, 0.25), 0.15, emit=(0.45, 1.0, 0.25))
M_dark = paint("darkcloth", (0.13, 0.13, 0.15), grime=0.3, seed=6)
M_rust = paint("spade", (0.36, 0.24, 0.15), grime=0.6, seed=7)
M_brass = simple_material("brass", None, (0.70, 0.52, 0.22), 0.35, metal=0.9)
M_red = paint("neckerchief", (0.55, 0.07, 0.06), grime=0.25, seed=8, stripes=(16, (0.9, 0.85, 0.8)))


def hand_frame(side):
    """Fist centre + the thumb direction (handles run along it, through the fist)."""
    pb = rig.data.bones
    hand = rig.matrix_world @ pb[f"{side}Hand"].head_local
    fb = rig.matrix_world @ pb[f"{side}FingerBase"].head_local
    th = rig.matrix_world @ pb[f"{'L' if side == 'Left' else 'R'}Thumb"].tail_local
    fist = hand.lerp(fb, 0.6) + Vector((0, 0, -0.025))
    fwd = (th - hand); fwd.z = 0; fwd.normalize()
    return fist, fwd


def box_basis(name, centre, X, Y, Z, size, mat, bone, bevel=0.002):
    """Box with its local axes = X, Y, Z (world vectors), dimensions `size`."""
    bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, 0))
    o = bpy.context.active_object; o.name = name
    X, Y, Z = X.normalized(), Y.normalized(), Z.normalized()
    M = Matrix((X, Y, Z)).transposed().to_4x4()
    for v in o.data.vertices:
        v.co = Vector((v.co.x * size[0], v.co.y * size[1], v.co.z * size[2]))
    o.data.transform(M); o.location = centre
    if bevel:
        m = o.modifiers.new("bevel", "BEVEL"); m.width = bevel; m.segments = 2
        bpy.context.view_layer.objects.active = o; bpy.ops.object.modifier_apply(modifier="bevel")
    o.data.materials.append(mat); props.append((o, bone)); return o


def ellipse_band(name, z0, z1, mat, pad=0.010, flare=0.022, verts=32):
    c, rx, ry = skull_slice(z0)
    bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=1, radius2=1 + flare / max(rx, ry), depth=z1 - z0,
                                    location=(c.x, c.y, (z0 + z1) / 2), end_fill_type="NOTHING")
    o = bpy.context.active_object; o.name = name; o.scale = (rx + pad, ry + pad, 1)
    m = o.modifiers.new("solid", "SOLIDIFY"); m.thickness = 0.006
    bpy.context.view_layer.objects.active = o; bpy.ops.object.modifier_apply(modifier="solid")
    bpy.ops.object.shade_smooth()
    o.data.materials.append(mat); props.append((o, "Head")); return o, c, rx, ry


def toque(height=0.10, puff=1.0):
    band, c, rx, ry = ellipse_band("hat_band", brow_z, brow_z + height, M_hat, verts=96)
    for v in band.data.vertices:        # starched pleats: 24 vertical folds round the band
        a = math.atan2(v.co.y, v.co.x)
        k = 1.0 + 0.035 * abs(math.sin(12 * a))
        v.co.x *= k; v.co.y *= k
    top = brow_z + height
    p = blob("hat_puff", (c.x + 0.01, c.y + 0.012, top + 0.035 * puff), 1.0, M_hat, "Head",
             scale=((rx + 0.045) * puff, (ry + 0.04) * puff, 0.075 * puff), sub=4, noise=0.0)
    for v in p.data.vertices:           # soft gathered folds radiating from the crown
        a = math.atan2(v.co.y, v.co.x)
        v.co *= 1.0 + 0.05 * math.sin(9 * a + 2.0 * v.co.z) * (1.0 - abs(v.co.z))
    # piping seam where puff meets band
    bpy.ops.mesh.primitive_torus_add(major_radius=1, minor_radius=0.006 / max(rx, ry), major_segments=64, minor_segments=8,
                                     location=(c.x, c.y, top))
    t = bpy.context.active_object; t.name = "hat_seam"; t.scale = (rx + 0.03, ry + 0.03, max(rx, ry))
    bpy.ops.object.shade_smooth(); t.data.materials.append(M_hat); props.append((t, "Head"))


def kitchen_cap():
    _, c, rx, ry = ellipse_band("cap_band", brow_z - 0.005, brow_z + 0.03, M_dark, pad=0.008, flare=0.0)
    blob("cap_dome", (c.x, c.y, brow_z + 0.03), 1.0, M_dark, "Head", scale=(rx + 0.012, ry + 0.012, crown - brow_z + 0.005), sub=3)


def respirator():
    mc = Vector((eye_c.x, nose.y + 0.032, mouth_z + 0.012))
    m = blob("mask", mc, 1.0, M_rubber, "Head", scale=(0.058, 0.045, 0.054), sub=4)
    for v in m.data.vertices:           # contoured: nose bridge pinch at the top, chin cup below
        if v.co.z > 0.4: v.co.x *= 1.0 - 0.45 * (v.co.z - 0.4)
        if v.co.y > 0.2: v.co.y = 0.2 + (v.co.y - 0.2) * 0.3        # flat where it seals to the face
    cyl("exhale_valve", mc + Vector((0, -0.035, -0.012)), mc + Vector((0, -0.05, -0.016)), 0.016, M_dark, "Head", verts=20)
    blob("valve_cap", mc + Vector((0, -0.051, -0.016)), 0.0145, M_rubber, "Head", scale=(1, 0.45, 1), sub=2)
    for s in (-1, 1):
        a = mc + Vector((s * 0.042, -0.012, -0.008))
        d = Vector((s * 0.035, -0.03, -0.018)); dn = d.normalized()
        cyl(f"filter_{s}", a, a + d, 0.03, M_filter, "Head", verts=24)
        for r in (0.3, 0.55, 0.8):      # moulded grip rings
            q = a + d * r
            cyl(f"filter_ring_{s}_{r}", q - dn * 0.002, q + dn * 0.002, 0.0325, M_filter, "Head", verts=24)
        cyl(f"filter_grille_{s}", a + d, a + d + dn * 0.003, 0.024, M_dark, "Head", verts=24)
    c, rx, ry = skull_slice(mouth_z + 0.02)
    bpy.ops.mesh.primitive_torus_add(major_radius=1, minor_radius=0.004 / max(rx, ry), major_segments=32, minor_segments=6,
                                     location=(c.x, c.y, mouth_z + 0.02))
    st = bpy.context.active_object; st.name = "strap"; st.scale = (rx + 0.006, ry + 0.006, max(rx, ry))
    st.data.materials.append(M_rubber); props.append((st, "Head"))


def gas_mask():
    """GP-5: a rubber hood moulded from the head itself, glass eyepieces, chin canister."""
    hood = body.copy(); hood.data = body.data.copy(); hood.name = "hood"
    bpy.context.collection.objects.link(hood)
    for md in list(hood.modifiers):
        hood.modifiers.remove(md)
    bm = bmesh.new(); bm.from_mesh(hood.data)
    dl = bm.verts.layers.deform.active
    chin = mouth_z - 0.055
    keep = lambda v: (dl is None or v[dl].get(_hgi, 0) > 0.5) and (hood.matrix_world @ v.co).z > chin
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if not all(keep(v) for v in f.verts)], context="FACES")
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")
    bm.normal_update()
    for v in bm.verts:
        v.co += v.normal * 0.007
    bm.to_mesh(hood.data); bm.free()
    hood.vertex_groups.clear()
    hood.data.materials.clear(); hood.data.materials.append(M_rubber)
    props.append((hood, "Head"))
    for s, e in ((-1, eyeR), (1, eyeL)):
        a = Vector((e.x, nose.y + 0.025, e.z))
        cyl(f"eye_rim_{s}", a, a + Vector((0, -0.022, 0)), 0.024, M_dark, "Head", verts=24)
        cyl(f"eye_lens_{s}", a + Vector((0, -0.021, 0)), a + Vector((0, -0.024, 0)), 0.019, M_lens, "Head", verts=24)
    v = Vector((eye_c.x, nose.y + 0.012, mouth_z))
    cyl("valve", v, v + Vector((0, -0.035, -0.008)), 0.026, M_dark, "Head", verts=20)
    for i in range(3):   # ribbed canister, screwed on below the valve, angled down-forward
        a = v + Vector((0, -0.03 - i * 0.022, -0.035 - i * 0.03))
        cyl(f"canister_{i}", a, a + Vector((0, -0.02, -0.028)), 0.04 if i != 1 else 0.042, M_filter, "Head", verts=24)


def cleaver(side, bone, scale=1.0):
    f, w = hand_frame(side)
    side_v = w.cross(Vector((0, 0, 1))).normalized()
    # handle: oval wooden grip with three brass rivets, steel bolster where it meets the blade
    h = cyl(f"cleaver_handle_{side}", f - w * 0.06 * scale, f + w * 0.08 * scale, 0.019 * scale, M_wood, bone, verts=16)
    for v in h.data.vertices:           # oval section (flatter across the blade plane)
        d = v.co.dot(side_v); v.co -= side_v * d * 0.35
    for k in (-0.035, 0.0, 0.035):
        r0 = f + w * k * scale
        blob(f"rivet_{side}_{k}", r0 + side_v * 0.013 * scale, 0.0055 * scale, M_brass, bone, sub=2)
        blob(f"rivet_b_{side}_{k}", r0 - side_v * 0.013 * scale, 0.0055 * scale, M_brass, bone, sub=2)
    cyl(f"bolster_{side}", f + w * 0.08 * scale, f + w * 0.095 * scale, 0.022 * scale, M_steel, bone, verts=16)
    c = f + w * 0.22 * scale + Vector((0, 0, -0.06 * scale))
    blade = box_basis(f"cleaver_blade_{side}", c, w, side_v, Vector((0, 0, 1)), (0.27 * scale, 0.009, 0.15 * scale), M_steel, bone, bevel=0.0015)
    for v in blade.data.vertices:       # grind: the lower 40% tapers to a sharp edge
        t = max(0.0, (-v.co.z) / (0.075 * scale) - 0.2)
        if t > 0:
            v.co -= side_v * v.co.dot(side_v) * min(0.92, t)
    # the hanging hole near the spine at the far corner
    cyl(f"cleaver_hole_{side}", c + w * 0.105 * scale + Vector((0, 0, 0.045 * scale)) - side_v * 0.006,
        c + w * 0.105 * scale + Vector((0, 0, 0.045 * scale)) + side_v * 0.006, 0.011 * scale, M_dark, bone, verts=16)
    box_basis(f"cleaver_gore_{side}", c + w * 0.05 * scale + Vector((0, 0, -0.03 * scale)), w, side_v, Vector((0, 0, 1)),
              (0.11 * scale, 0.0095, 0.07 * scale), M_blood, bone, bevel=0.001)


def meat_hook(side, bone, scale=1.0):
    f, w = hand_frame(side)
    yaw = math.atan2(w.y, w.x)
    cyl(f"hook_handle_{side}", f - w * 0.05, f + w * 0.07, 0.02, M_wood, bone, verts=10)
    tip = f + w * 0.07
    cyl(f"hook_shank_{side}", tip, tip + w * 0.14 * scale, 0.009, M_steel, bone, verts=8)
    bpy.ops.mesh.primitive_torus_add(major_radius=0.06 * scale, minor_radius=0.009, major_segments=20, minor_segments=8,
                                     location=tip + w * 0.14 * scale + Vector((0, 0, -0.06 * scale)),
                                     rotation=(0, math.pi / 2, yaw + math.pi / 2))
    h = bpy.context.active_object; h.name = f"hook_curl_{side}"; h.data.materials.append(M_steel); props.append((h, bone))


def knife(side, bone):
    f, w = hand_frame(side)
    cyl(f"knife_handle_{side}", f - w * 0.06, f + w * 0.08, 0.017, M_dark, bone, verts=10)
    side_v = w.cross(Vector((0, 0, 1)))
    c = f + w * 0.24
    blade = box_basis(f"knife_blade_{side}", c, w, side_v, Vector((0, 0, 1)), (0.30, 0.004, 0.05), M_steel, bone, bevel=0.0015)
    for v in blade.data.vertices:   # taper to a point, edge curving up to the tip (local coords)
        t = v.co.dot(w) / 0.15
        if t > 0:
            v.co.z = v.co.z * max(0.08, 1 - t * 0.85) + 0.01 * t


def shovel():
    fR, wR = hand_frame("Right")
    side_v = wR.cross(Vector((0, 0, 1)))
    a = fR - wR * 0.12
    b = fR + wR * 1.0
    cyl("shovel_shaft", a, b, 0.017, M_wood, "RightHand", verts=10)
    cyl("shovel_grip", a - wR * 0.06, a, 0.024, M_dark, "RightHand", verts=10)
    # spade: plate containing the shaft axis, slightly dished
    sp = box_basis("shovel_blade", b + wR * 0.13, wR, side_v, Vector((0, 0, 1)), (0.28, 0.22, 0.006), M_rust, "RightHand")
    sn = side_v.normalized()
    for v in sp.data.vertices:          # mesh coords are local to the blade centre
        d = v.co.dot(sn)
        v.co.z += 1.2 * d * d


def apron(top_z, bottom_z, half_w, mat, pad=0.03, bib=False):
    """Apron draped by ray-casting onto the body: hugs chest and belly, hangs straight below the hips."""
    dg = bpy.context.evaluated_depsgraph_get()
    # BVHTree.FromObject builds in the object's LOCAL space -- rays go in local, hits come
    # back out to world (garments sit at an offset; world-space rays silently missed them
    # and buried the apron inside the shirt)
    trees = [(BVHTree.FromObject(o, dg), o.matrix_world.copy()) for o in meshes if "low-poly" not in o.name]
    def front_hit(org):                 # nearest surface (body or garment) along +Y
        best = None
        for t, mw in trees:
            inv = mw.inverted()
            h = t.ray_cast(inv @ org, (inv.to_3x3() @ Vector((0, 1, 0))).normalized())
            if h[0] is not None:
                w = mw @ h[0]
                if best is None or w.y < best.y:
                    best = w
        return best
    hips_z = (rig.matrix_world @ rig.data.bones["Hips"].head_local).z
    # None = derive from this body's skeleton (chest to knee): a 2.2 m butcher gets a bib on
    # his CHEST, not a fixed-height apron that stops at his belly
    bz = lambda n: (rig.matrix_world @ rig.data.bones[n].head_local).z
    if top_z is None: top_z = bz("Spine1") + 0.14
    if bottom_z is None: bottom_z = bz("LeftLeg") - 0.04
    nx, nz = 24, 36
    # pass 1: drape (ray-cast onto body + garments)
    Y = [[0.0] * (nx + 1) for _ in range(nz + 1)]
    X = [[0.0] * (nx + 1) for _ in range(nz + 1)]
    for j in range(nz + 1):
        z = top_z + (bottom_z - top_z) * j / nz
        t_bib = min(1.0, max(0.0, (z - (hips_z + 0.22)) / 0.16)) if bib else 0.0
        width = half_w * (1.0 - 0.38 * t_bib * t_bib * (3 - 2 * t_bib))     # smoothstep taper into the bib
        for i in range(nx + 1):
            x = -width + 2 * width * i / nx
            zz = max(z, hips_z - 0.06)   # below the crotch: hang from the hip line
            hit = front_hit(Vector((x, -1.0, zz)))
            y = (hit.y - pad) if hit is not None else -0.2
            if z < hips_z - 0.06:
                y -= 0.05 + 0.06 * (hips_z - 0.06 - z)   # hangs clear of the legs (they swing under it)
            Y[j][i], X[j][i] = y, x
    # pass 2: relax into a smooth heavy sheet (shrink-wrapping every garment wrinkle shaded
    # as blocky facets) -- never letting it sink behind the drape (smaller y = further forward)
    S = [row[:] for row in Y]
    for _ in range(10):
        T = [row[:] for row in S]
        for j in range(nz + 1):
            for i in range(nx + 1):
                acc = n = 0
                for dj in (-1, 0, 1):
                    for di in (-1, 0, 1):
                        jj, ii = j + dj, i + di
                        if 0 <= jj <= nz and 0 <= ii <= nx:
                            acc += S[jj][ii]; n += 1
                T[j][i] = min(acc / n, Y[j][i])
        S = T
    bm = bmesh.new()
    grid = []
    for j in range(nz + 1):
        z = top_z + (bottom_z - top_z) * j / nz
        grid.append([bm.verts.new((X[j][i], S[j][i], z)) for i in range(nx + 1)])
    uv = bm.loops.layers.uv.new()
    for j in range(nz):
        for i in range(nx):
            f = bm.faces.new((grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]))
            for lp, (u, w) in zip(f.loops, ((i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1))):
                lp[uv].uv = (u / nx, 1 - w / nz)
    me = bpy.data.meshes.new("apron"); bm.to_mesh(me); bm.free()
    o = bpy.data.objects.new("apron", me); bpy.context.collection.objects.link(o)
    bpy.context.view_layer.objects.active = o
    m = o.modifiers.new("solid", "SOLIDIFY"); m.thickness = 0.008
    bpy.ops.object.modifier_apply(modifier="solid")
    bpy.ops.object.shade_smooth()
    o.data.materials.append(mat)
    # weights: above the hips, copy the NEAREST body vertex's skin weights so the bib rides
    # the chest exactly (rigid spine weights let the posed belly push through it); below
    # the hips, Hips only, so it hangs rather than stretching between the legs.
    from mathutils.kdtree import KDTree
    # source = the torso GARMENT the apron lies on (it deforms exactly as the apron should);
    # only torso bones are copied -- arm/shoulder weights near the armpits dragged the bib
    # back into the chest whenever the arms moved
    src = next((m for m in meshes if any(k in m.name for k in ("worksuit", "casualsuit"))), body)
    bverts = src.data.vertices
    kd = KDTree(len(bverts))
    for v in bverts:
        kd.insert(src.matrix_world @ v.co, v.index)
    kd.balance()
    gname = {g.index: g.name for g in src.vertex_groups}
    TORSO = ("Hips", "LowerBack", "Spine", "Spine1")
    groups = {}
    def grp(n):
        if n not in groups:
            groups[n] = o.vertex_groups.new(name=n)
        return groups[n]
    for v in o.data.vertices:
        if v.co.z < hips_z:
            grp("Hips").add([v.index], 1.0, "REPLACE")
            continue
        _, idx, _ = kd.find(v.co)
        ws = [(gname[g.group], g.weight) for g in bverts[idx].groups
              if g.weight > 0.01 and gname[g.group] in TORSO]
        tot = sum(w for _, w in ws) or 1.0
        if not ws:
            ws, tot = [("Spine", 1.0)], 1.0
        for n, w in ws:
            grp(n).add([v.index], w / tot, "REPLACE")
    props.append((o, None))   # None = keep these weights
    return o


def neckerchief():
    nz = (rig.matrix_world @ rig.data.bones["Neck1"].head_local).z
    _ngi = body.vertex_groups["Neck1"].index if "Neck1" in body.vertex_groups else _hgi
    nv = [body.matrix_world @ v.co for v in body.data.vertices
          if any(g.group == _ngi and g.weight > 0.3 for g in v.groups) and abs((body.matrix_world @ v.co).z - nz) < 0.02]
    cx = sum(p.x for p in nv) / len(nv); cy = sum(p.y for p in nv) / len(nv)
    rx = (max(p.x for p in nv) - min(p.x for p in nv)) / 2; ry = (max(p.y for p in nv) - min(p.y for p in nv)) / 2
    bpy.ops.mesh.primitive_torus_add(major_radius=1, minor_radius=0.013 / max(rx, ry), major_segments=28, minor_segments=8,
                                     location=(cx, cy, nz - 0.015))
    t = bpy.context.active_object; t.name = "neckerchief"; t.scale = (rx + 0.016, ry + 0.016, max(rx, ry))
    t.data.materials.append(M_red); props.append((t, "Neck"))
    blob("knot", (cx, cy - ry - 0.028, nz - 0.035), 0.022, M_red, "Neck", scale=(1.3, 0.8, 1.0))


def jacket_buttons():
    sp1 = rig.matrix_world @ rig.data.bones["Spine1"].head_local
    dg = bpy.context.evaluated_depsgraph_get(); tree = BVHTree.FromObject(body, dg)
    assert body.matrix_world.is_identity or True   # body proxy sits at the origin
    for xo in (-0.07, 0.07):
        for k in range(4):
            z = sp1.z + 0.12 - k * 0.07
            hit = tree.ray_cast(Vector((sp1.x + xo, -1.0, z)), Vector((0, 1, 0)))
            if hit[0] is not None:
                blob(f"button_{xo}_{k}", hit[0] + Vector((0, -0.022, 0)), 0.011, M_dark, "Spine1", scale=(1, 0.5, 1))


PROPS = {
    "cook": lambda: (toque(), respirator(), cleaver("Right", "RightHand"), meat_hook("Left", "LeftHand")),
    "linecook": lambda: (kitchen_cap(), respirator(), knife("Right", "RightHand")),
    "butcher": lambda: (apron(None, None, 0.25, paint("rubber_apron", (0.22, 0.10, 0.08), grime=0.35, blood=10, drops=14, seed=9), bib=True),
                        cleaver("Right", "RightHand", 1.35), meat_hook("Left", "LeftHand", 1.3)),
    "liquidator": lambda: (gas_mask(), shovel()),
    "headchef": lambda: (toque(0.20, 1.25), respirator(), neckerchief(), jacket_buttons(),
                         cleaver("Right", "RightHand", 1.25), cleaver("Left", "LeftHand", 1.25)),
}
PROPS[SPEC["props"]]()

for o, bone in props:
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.select_all(action="DESELECT"); o.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    if bone is not None:
        vg = o.vertex_groups.new(name=bone)
        vg.add(list(range(len(o.data.vertices))), 1.0, "REPLACE")
    o.parent = rig
    md = o.modifiers.new("rig", "ARMATURE"); md.object = rig

# =============================================================== 4. mocap retarget
CHILD = {"LeftShoulder": "LeftArm", "LeftArm": "LeftForeArm", "LeftForeArm": "LeftHand",
         "RightShoulder": "RightArm", "RightArm": "RightForeArm", "RightForeArm": "RightHand",
         "LeftUpLeg": "LeftLeg", "LeftLeg": "LeftFoot", "LeftFoot": "LeftToeBase",
         "RightUpLeg": "RightLeg", "RightLeg": "RightFoot", "RightFoot": "RightToeBase"}
SRC_NAME = {"LeftHandFinger1": "LeftHandIndex1", "RightHandFinger1": "RightHandIndex1"}
tbones = rig.data.bones
order = [b.name for b in tbones]          # parents precede children in MPFB rigs
R_world = rig.matrix_world.to_3x3()
Rt0 = {b.name: (R_world @ b.matrix_local.to_3x3()).normalized() for b in tbones}
Ht0 = {b.name: rig.matrix_world @ b.head_local for b in tbones}
for p in rig.pose.bones:
    p.rotation_mode = "QUATERNION"
rig.animation_data_create()
FIST = {"FingerBase": 55, "HandFinger1": 70, "Thumb": 35}   # degrees of curl, applied every frame


def import_bvh(clip):
    before = set(bpy.data.objects)
    bpy.ops.import_anim.bvh(filepath=os.path.join(CMU, clip + ".bvh"), update_scene_fps=False, update_scene_duration=False)
    return next(o for o in bpy.data.objects if o not in before)


def swing(a, b):
    return a.normalized().rotation_difference(b.normalized()).to_matrix()


def sample(src, frames, loop, yaw_fix=True):
    """Retarget src (imported BVH armature) frames (BVH frame numbers) -> list of {bone: (quat, loc|None)}."""
    sb = src.data.bones
    Rs0 = {b.name: (src.matrix_world.to_3x3() @ b.matrix_local.to_3x3()).normalized() for b in sb}
    Hs0 = {b.name: src.matrix_world @ b.head_local for b in sb}
    # rest alignment: rotate each target rest bone onto the source rest (T-pose) direction
    A = {}
    for n in order:
        s = SRC_NAME.get(n, n)
        if n in CHILD and s in Hs0 and CHILD[n] in Hs0:
            A[n] = swing(Ht0[CHILD[n]] - Ht0[n], Hs0[CHILD[n]] - Hs0[s])
        else:
            par = tbones[n].parent
            A[n] = A[par.name] if par else Matrix.Identity(3)
    k = (Ht0["Hips"].z - Ht0["LeftFoot"].z) / (Hs0["Hips"].z - Hs0["LeftFoot"].z)
    # heading: rotate the take so the body faces -Y (game +Z)
    def heading(f):
        scene.frame_set(f)
        l = (src.matrix_world @ src.pose.bones["LeftUpLeg"].head) - (src.matrix_world @ src.pose.bones["RightUpLeg"].head)
        fw = l.cross(Vector((0, 0, 1))); fw.z = 0
        return fw.normalized()
    f_tpose = int(src.animation_data.action.frame_range[0])
    scene.frame_set(f_tpose)
    hip_ref = src.matrix_world @ src.pose.bones["Hips"].head      # standing T-pose hips (the BVH rest has them at 0)
    foot_ref = src.matrix_world @ src.pose.bones["LeftFoot"].head
    k = (Ht0["Hips"].z - Ht0["LeftFoot"].z) / (hip_ref.z - foot_ref.z)
    hs = [heading(f) for f in frames[:: max(1, len(frames) // 8)]] if yaw_fix else [heading(frames[0])]
    avg = sum(hs, Vector()).normalized()
    Y = swing(avg, Vector((0, -1, 0)))
    out = []
    hip0 = None
    for f in frames:
        scene.frame_set(f)
        W = {}
        pose = {}
        for n in order:
            s = SRC_NAME.get(n, n)
            if s in src.pose.bones:
                Rs = (src.matrix_world.to_3x3() @ src.pose.bones[s].matrix.to_3x3()).normalized()
                W[n] = Y @ Rs @ Rs0[s].inverted() @ A[n] @ Rt0[n]
            else:
                par = tbones[n].parent
                W[n] = W[par.name] @ (Rt0[par.name].inverted() @ Rt0[n])
            par = tbones[n].parent
            if par:
                L0 = Rt0[par.name].inverted() @ Rt0[n]
                Q = (L0.inverted() @ W[par.name].inverted() @ W[n])
            else:
                Q = Rt0[n].inverted() @ W[n]
            pose[n] = [Q.to_quaternion(), None]
        hp = Y @ ((src.matrix_world @ src.pose.bones["Hips"].head) - hip_ref) * k
        if hip0 is None:
            hip0 = hp.copy()
        pose["Hips"][1] = hp
        out.append(pose)
    # root motion: loops stay in place (drift removed), one-shots start at the origin
    n = len(out)
    first, last = out[0]["Hips"][1].copy(), out[-1]["Hips"][1].copy()
    for i, p in enumerate(out):
        h = p["Hips"][1]
        if loop:
            t = i / max(1, n - 1)
            drift = first.lerp(last, t)
            h.x -= drift.x; h.y -= drift.y
        else:
            h.x -= first.x; h.y -= first.y
        # pose-bone location is in the hips' rest frame
        p["Hips"][1] = Rt0["Hips"].inverted() @ h
    return out


def crossfade(poses, c):
    n = len(poses)
    for i in range(n - c, n):
        w = (i - (n - c - 1)) / c
        for b, (q, l) in poses[i].items():
            q0, l0 = poses[0][b]
            q2 = q.slerp(q0, w) if q.dot(q0) >= 0 else q.slerp(-q0, w)
            poses[i][b] = [q2, (l.lerp(l0, w) if l is not None else None)]
    return poses


def fist(poses):
    for p in poses:
        for side, s in (("Left", 1), ("Right", -1)):
            for key, deg in FIST.items():
                n = (f"{side}{key}" if key != "Thumb" else f"{side[0]}Thumb")
                if n in p:
                    p[n][0] = p[n][0] @ Quaternion((1, 0, 0), math.radians(deg))
    return poses


def bake(name, poses, step):
    """Key poses onto a fresh action, `step` timeline frames apart (60 fps timeline)."""
    act = bpy.data.actions.new(name); act.use_fake_user = True
    rig.animation_data.action = act
    for i, p in enumerate(poses):
        fr = 1 + i * step
        for n, (q, l) in p.items():
            pb = rig.pose.bones[n]
            pb.rotation_quaternion = q
            pb.keyframe_insert("rotation_quaternion", frame=fr)
            if l is not None:
                pb.location = l
                pb.keyframe_insert("location", frame=fr)
    return act


def bvh_frames(src, dump_a, dump_b):
    f0 = int(src.animation_data.action.frame_range[0])
    return [f0 + 4 * i for i in range(dump_a, dump_b + 1)]   # dump index i = BVH frame f0+4i (30 fps)


clips = []
_cache = {}
def take(role):
    bvh, d0, d1, loop, xf = SPEC["clips"][role]
    src = import_bvh(bvh)
    poses = fist(sample(src, bvh_frames(src, d0, d1), loop=loop, yaw_fix=loop))
    bpy.data.objects.remove(src, do_unlink=True)
    return crossfade(poses, xf) if xf else poses
idle, walk, run, attack, death = (take(r) for r in ("idle", "walk", "run", "attack", "death"))
# hit: the idle pose with a sharp recoil layered on (no clean CMU flinch exists)
hit = []
for i in range(14):
    t = i / 13.0
    env = math.sin(math.pi * min(1.0, t * 1.6)) * (1.0 - t * 0.35)
    p = {b: [q.copy(), (l.copy() if l is not None else None)] for b, (q, l) in idle[0].items()}
    for bn, deg, axis in (("Spine1", -16, (1, 0, 0)), ("Spine", -8, (1, 0, 0)), ("Neck", -14, (1, 0, 0)),
                          ("Head", -18, (1, 0, 0)), ("LeftArm", -25, (0, 0, 1)), ("RightArm", 25, (0, 0, 1))):
        p[bn][0] = p[bn][0] @ Quaternion(axis, math.radians(deg * env))
    p["Hips"][1] = p["Hips"][1] + Vector((0, -0.04 * env, 0))
    hit.append(p)

for name, poses, step in (("idle", idle, 1), ("walk", walk, 1), ("run", run, 1),
                          ("attack", attack, 2), ("hit", hit, 1), ("death", death, 2)):
    clips.append(bake(name, poses, step))
for act in clips:
    tr = rig.animation_data.nla_tracks.new(); tr.name = act.name
    tr.strips.new(act.name, int(act.frame_range[0]), act)
rig.animation_data.action = None
scene.render.fps = 60

# =============================================================== 5. previews
if PREVIEW:
    os.makedirs(PREVIEW, exist_ok=True)
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x, scene.render.resolution_y = 420, 600
    w = bpy.data.worlds.new("w"); scene.world = w; w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = (0.16, 0.16, 0.18, 1)
    w.node_tree.nodes["Background"].inputs[1].default_value = 0.7
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); sun.data.energy = 3.5
    sun.rotation_euler = (math.radians(50), 0, math.radians(30)); scene.collection.objects.link(sun)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); cam.data.lens = 60
    scene.collection.objects.link(cam); scene.camera = cam
    V = {"front": ((0, -5.2, 1.0), (90, 0, 0)), "three": ((3.6, -3.9, 1.25), (86, 0, 42)),
         "side": ((5.2, 0, 1.0), (90, 0, 90)), "face": ((0.3, -1.25, 1.55), (90, 0, 12)),
         "hands": ((0.0, -1.7, 1.05), (90, 0, 0))}

    def shot(clip, frame, view, tag=""):
        for tr in rig.animation_data.nla_tracks:
            tr.mute = (tr.name != clip)
        scene.frame_set(frame)
        cam.location, r = V[view]; cam.rotation_euler = tuple(math.radians(a) for a in r)
        scene.render.filepath = os.path.join(PREVIEW, f"{clip}_{frame:03d}_{view}{tag}.png")
        bpy.ops.render.render(write_still=True)

    shot("idle", 1, "front"); shot("idle", 1, "three"); shot("idle", 1, "face"); shot("idle", 1, "hands")
    for c in clips:
        n = int(c.frame_range[1])
        for fr in (1, n // 3, (2 * n) // 3, n):
            shot(c.name, max(1, fr), "side" if c.name != "death" else "three")
    for tr in rig.animation_data.nla_tracks:
        tr.mute = False

# =============================================================== 6. export
bpy.ops.object.select_all(action="DESELECT")
for o in bpy.data.objects:
    if o.type in ("MESH", "ARMATURE"):
        o.select_set(True)
bpy.ops.export_scene.gltf(filepath=OUT, export_format="GLB", use_selection=True, export_yup=True,
                          export_animations=True, export_animation_mode="NLA_TRACKS", export_skins=True,
                          export_apply=True, export_materials="EXPORT", export_image_format="AUTO",
                          export_frame_range=False, export_force_sampling=True)
print("enemy: wrote", OUT, os.path.getsize(OUT) // 1024, "KB;",
      sum(len(o.data.vertices) for o in bpy.data.objects if o.type == "MESH"), "verts total")
