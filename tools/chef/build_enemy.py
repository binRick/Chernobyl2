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
              "death": ("90_18", 2, 60, False, 0),
              # how he died / got hurt (the game picks by cause; tools/chef/fetch_mocap.sh fetches the takes)
              "death_blast": ("90_16", 82, 125, False, 0),    # explosion: thrown up, lands on his face
              "knock_fall": ("90_18", 18, 36, False, 0),      # knockdown = this fall...
              "knock_getup": ("139_18", 85, 142, False, 0),   # ...then up off his back (joined in join_knockdown)
              "limp": ("91_24", 252, 302, True, 6)}           # after a leg hit: one upright hurt-leg stride cycle
ZOMBIE_WALK = ("104_41", 269, 314, True, 6)          # heavier, dragging lurch
# The gait ladder. "walk" above is each type's slow creep (0.5-0.6 m/s); the game picks whichever
# of walk / stride / jog / run walks nearest the pace he is really making, so the feet keep up
# with the ground instead of a shuffle played at double speed. One stride cycle each, cut where
# the pose repeats; the speeds are the takes' own (the build writes them to <enemy>.gait).
WALK_NAT = ("35_01", 7, 41, True, 5)        # 1.2 m/s: an ordinary walk
STRIDE   = ("08_05", 37, 71, True, 5)       # 1.8 m/s: long purposeful strides
JOG      = ("02_03", 15, 39, True, 4)       # 2.65 m/s
RUN_EASY = ("09_01", 13, 35, True, 4)       # 3.45 m/s
RUN      = ("16_45", 7, 28, True, 4)        # 3.8 m/s
RUN_HARD = ("127_07", 11, 31, True, 4)      # 4.8 m/s
SPRINT   = ("127_08", 14, 33, True, 4)      # 5.3 m/s: flat out
RUN_ARMY = ("104_37", 7, 28, True, 4)       # 4.1 m/s
SPECS = {
    "cook": dict(   # canteen cook: the wave-1 staple
        phenotype=dict(gender=1.0, age=0.74, muscle=0.62, weight=0.92, proportions=0.3, height=0.55),
        targets={"stomach-pregnant-incr": 0.75, "measure-waist-circ-incr": 0.6, "torso-scale-depth-incr": 0.35,
                 "neck-double-incr": 0.7, "measure-neck-circ-incr": 0.6, "torso-muscle-dorsi-incr": 0.3},
        skin="middleage_caucasian_male", eyebrows="eyebrow007",
        clothes=["male_worksuit01", "shoes02"],
        recolor={"worksuit": ((0.93, 0.91, 0.86), 9, 7, 8)},
        props="cook", attack_synth="dual", spread_arms=(20, 20, 10),   # walk/run: blades apart, off the belly
        hunch=7, clips=dict(BASE_CLIPS, stride=WALK_NAT, jog=JOG, run=RUN_HARD)),   # cleaver + meat hook: one hand at a time
    "linecook": dict(   # line cook: young, wiry, fast -- knife
        phenotype=dict(gender=1.0, age=0.42, muscle=0.55, weight=0.25, proportions=0.6, height=0.62),
        targets={"stomach-pregnant-decr": 0.4, "neck-scale-horiz-decr": 0.3},
        skin="young_caucasian_male", eyebrows="eyebrow001",
        clothes=["male_casualsuit06", "shoes06"],
        recolor={"casualsuit06": ((0.88, 0.87, 0.84), 6, 9, 4), "shoes06": ((0.22, 0.22, 0.24), 0, 2, 0)},
        props="linecook", hunch=9, clips=dict(BASE_CLIPS, walk=STRIDE, jog=RUN_EASY, run=SPRINT)),
    "butcher": dict(   # butcher: huge, slow, hits like a truck
        phenotype=dict(gender=1.0, age=0.66, muscle=0.95, weight=1.0, proportions=0.2, height=0.85),
        targets={"stomach-pregnant-incr": 0.55, "measure-neck-circ-incr": 1.0, "neck-double-incr": 1.0,
                 "torso-scale-horiz-incr": 0.5, "torso-muscle-pectoral-incr": 0.6, "measure-shoulder-dist-incr": 0.6},
        skin="middleage_caucasian_male", eyebrows="eyebrow010",
        clothes=["male_casualsuit06", "shoes03"],
        recolor={"casualsuit06": ((0.62, 0.60, 0.56), 8, 10, 8)},
        props="butcher", attack_synth="dual", hunch=8,
        clips=dict(BASE_CLIPS, walk=ZOMBIE_WALK, stride=WALK_NAT, jog=STRIDE, run=JOG)),
    "liquidator": dict(   # liquidator: off-white cotton coverall, GP-5 mask, shovel
        phenotype=dict(gender=1.0, age=0.55, muscle=0.7, weight=0.6, proportions=0.5, height=0.6),
        targets={},
        skin="middleage_caucasian_male", eyebrows="eyebrow003",
        clothes=["male_worksuit01", "shoes03"],
        recolor={"worksuit": ((0.80, 0.80, 0.74), 5, 4, 14)},
        props="liquidator", two_hand="shaft", hunch=6,
        clips=dict(BASE_CLIPS, walk=ZOMBIE_WALK, stride=WALK_NAT, jog=JOG, run=RUN)),
    "headchef": dict(   # head chef: the giant -- tall toque, twin cleavers (drawn x1.4 in game)
        phenotype=dict(gender=1.0, age=0.85, muscle=0.75, weight=1.0, proportions=0.25, height=0.75),
        targets={"stomach-pregnant-incr": 1.0, "measure-waist-circ-incr": 0.8, "neck-double-incr": 1.0,
                 "measure-neck-circ-incr": 0.8, "torso-scale-horiz-incr": 0.4},
        skin="old_caucasian_male", eyebrows="eyebrow005",
        clothes=["male_worksuit01", "shoes03"],
        recolor={"worksuit": ((0.96, 0.95, 0.92), 18, 14, 6)},
        props="headchef", attack_synth="dual", spread_arms=(36, 8, 14),   # walk/run: keep the twin cleavers apart (left, right, wrists)
        hunch=6, clips=dict(BASE_CLIPS, walk=ZOMBIE_WALK, stride=WALK_NAT, jog=STRIDE, run=JOG)),
}
AIM_RIFLE = ("79_96", 58, 100, True, 6)            # two-handed steady aim, held (looped while engaged)
AIM_HEAVY = ("80_03", 40, 96, True, 6)
THROW     = ("124_01", 76, 118, False, 0, "release")   # wind-up + hurl (radiation bolt); a side-on pitch
CAST      = ("120_03", 92, 150, False, 0)          # arms thrust out over the dead
SPECS.update({
    "rifleman": dict(   # militia rifleman: aimed single shots
        phenotype=dict(gender=1.0, age=0.5, muscle=0.7, weight=0.45, proportions=0.6, height=0.6),
        targets={},
        skin="young_caucasian_male2", eyebrows="eyebrow003",
        clothes=["male_worksuit01", "shoes03"],
        recolor={"worksuit": ((0.40, 0.43, 0.29), 4, 3, 10)},
        props="rifleman", aim_gun="line", clips=dict(BASE_CLIPS, attack=AIM_RIFLE, stride=WALK_NAT, jog=JOG, run=RUN_ARMY),
        aim_hand_out=0.04),   # the receiver clears the gas-mask filter
    "gunner": dict(   # machine-gunner: heavy, bursts
        phenotype=dict(gender=1.0, age=0.6, muscle=0.85, weight=0.8, proportions=0.3, height=0.62),
        targets={"measure-neck-circ-incr": 0.6, "torso-scale-horiz-incr": 0.35},
        skin="middleage_caucasian_male", eyebrows="eyebrow010",
        clothes=["male_worksuit01", "shoes03"],
        recolor={"worksuit": ((0.30, 0.32, 0.26), 6, 4, 14)},
        props="gunner", aim_gun="line", clips=dict(BASE_CLIPS, attack=AIM_RIFLE, stride=WALK_NAT, jog=JOG, run=RUN_ARMY),
        aim_hand_drop=0.14, aim_hand_out=0.10, aim_hand_fwd=0.08),   # the big receiver clears the helmet, the stock the belt and his forearm   # shouldered, like the rifleman (the heavy-aim mocap held the gun at his sternum)
    "mutant": dict(   # glowing mutant: irradiated skin, throws radiation bolts
        phenotype=dict(gender=1.0, age=0.5, muscle=0.75, weight=0.3, proportions=0.4, height=0.58),
        targets={"stomach-pregnant-decr": 0.5, "torso-muscle-dorsi-incr": 0.5},
        skin="young_caucasian_male", eyebrows="eyebrow001",
        clothes=["male_casualsuit06", "shoes02"],
        recolor={"casualsuit06": ((0.42, 0.40, 0.33), 12, 8, 22)},
        skin_tint=((0.76, 0.90, 0.68), 26), skin_glow=0.55,   # sickly green + glowing lesions
        props="mutant", attack_in_place=True, hunch=8,
        clips=dict(BASE_CLIPS, attack=THROW, stride=WALK_NAT, jog=JOG, run=RUN_HARD)),
    "priest": dict(   # irradiated priest: hooded robe, lantern; raises the dead
        phenotype=dict(gender=1.0, age=0.92, muscle=0.35, weight=0.35, proportions=0.4, height=0.6),
        targets={},
        skin="old_caucasian_male", eyebrows="eyebrow005",
        clothes=["male_worksuit01", "shoes02"],
        recolor={"worksuit": ((0.10, 0.10, 0.11), 0, 0, 6)},
        props="priest", one_hand_attack=True, hunch=5,
        clips=dict(BASE_CLIPS, walk=ZOMBIE_WALK, stride=WALK_NAT, jog=STRIDE, run=JOG, cast=CAST)),   # melee = the base swing; cast = raising the dead
    "revenant": dict(   # burned firefighter: charred skin, brass-crested helmet, fire axe -- fast
        phenotype=dict(gender=1.0, age=0.45, muscle=0.8, weight=0.35, proportions=0.6, height=0.66),
        targets={"stomach-pregnant-decr": 0.6, "torso-muscle-dorsi-incr": 0.4},
        skin="young_caucasian_male", eyebrows="eyebrow001",
        clothes=["male_worksuit01", "shoes03"],
        recolor={"worksuit": ((0.26, 0.20, 0.15), 3, 2, 40)},
        skin_tint=((0.24, 0.16, 0.12), 150, (1.0, 0.42, 0.08), (0.03, 0.025, 0.02)),   # char + glowing ember cracks
        skin_glow=0.8, eye_glow=(1.0, 0.45, 0.10),
        props="revenant", two_hand="shaft", two_hand_clips=("attack",), hunch=9,
        clips=dict(BASE_CLIPS, stride=STRIDE, jog=RUN_EASY, run=SPRINT)),   # both hands on the axe for the swing
    "fiend": dict(   # mutated liquidator: bloated, cracked mask, four tentacles burst from the back
        phenotype=dict(gender=1.0, age=0.6, muscle=0.85, weight=0.75, proportions=0.3, height=0.72),
        targets={"torso-scale-horiz-incr": 0.5, "measure-shoulder-dist-incr": 0.5, "neck-double-incr": 0.6},
        skin="middleage_caucasian_male", eyebrows="eyebrow003",
        clothes=["male_worksuit01", "shoes03"],
        recolor={"worksuit": ((0.74, 0.73, 0.66), 22, 16, 26)},
        skin_tint=((0.82, 0.66, 0.70), 30, (0.55, 0.06, 0.10), (0.30, 0.18, 0.28)),
        props="fiend", tentacles=True, attack_synth="rake",   # no hand weapon: a two-handed overhead claw rake
        hunch=7, clips=dict(BASE_CLIPS, walk=ZOMBIE_WALK, stride=WALK_NAT, jog=JOG, run=RUN_EASY)),
})
# How each one looks up close. hair = (style, rgb): buzz / ring (bald on top) / thin / patchy /
# shaved / none. stubble 0..1. hands: dirt / blood / soot. gore = blood down the front of his
# clothes. face = MakeHuman face sliders, so they are eleven men and not one man eleven times
# ("cheek-" and "eye-" sliders are applied to both sides).
LOOKS = {
    "cook": dict(hair=("ring", (0.22, 0.19, 0.17)), stubble=0.6, hands="blood", gore=0.5,
                 face={"head-fat-incr": 0.5, "eye-bag-incr": 0.8, "eyebrows-trans-down": 0.5}),
    "linecook": dict(hair=("buzz", (0.06, 0.05, 0.04)), stubble=0.35, hands="blood", gore=0.35,
                     face={"cheek-volume-decr": 0.6, "cheek-bones-incr": 0.5, "eyebrows-trans-down": 0.4}),
    "butcher": dict(hair=("shaved", (0.08, 0.06, 0.05)), stubble=0.8, hands="blood", gore=0.5,
                    face={"head-square": 0.7, "chin-width-incr": 0.6, "chin-prominent-incr": 0.4, "nose-volume-incr": 0.6,
                          "nose-scale-horiz-incr": 0.4, "eyebrows-trans-down": 0.8, "cheek-volume-incr": 0.4,
                          "eye-bag-incr": 0.6, "mouth-angles-down": 0.8}),
    "liquidator": dict(hair=("buzz", (0.10, 0.08, 0.06)), hands="dirt"),
    "headchef": dict(hair=("thin", (0.42, 0.40, 0.38)), stubble=0.55, hands="blood", gore=0.85,
                     face={"head-fat-incr": 0.7, "eye-bag-incr": 1.0, "eyebrows-trans-down": 0.6, "head-age-incr": 0.5}),
    "rifleman": dict(hair=("buzz", (0.14, 0.10, 0.07)), stubble=0.3, hands="dirt",
                     face={"cheek-bones-incr": 0.5, "eyebrows-trans-down": 0.4}),
    "gunner": dict(hair=("buzz", (0.07, 0.06, 0.05)), stubble=0.8, hands="dirt",
                   face={"head-rectangular": 0.6, "chin-bones-incr": 0.6, "nose-hump-incr": 0.7, "eyebrows-trans-down": 0.6,
                         "mouth-angles-down": 0.6, "cheek-bones-incr": 0.4}),
    "mutant": dict(hair=("patchy", (0.07, 0.07, 0.05)), stubble=0.25, brows=0.5, hands="dirt", gore=0.2, skin_fx="sores",
                   face={"cheek-volume-decr": 0.9, "cheek-bones-incr": 0.7, "head-triangular": 0.4, "eye-bag-incr": 1.0,
                         "mouth-angles-down": 0.7}),
    "priest": dict(hair=("thin", (0.50, 0.48, 0.46)), stubble=0.7, hands="dirt",
                   face={"cheek-volume-decr": 1.0, "cheek-bones-incr": 0.8, "eye-bag-incr": 1.0, "nose-hump-incr": 0.8,
                         "nose-point-down": 0.6, "mouth-angles-down": 1.0, "head-age-incr": 0.6}),
    "revenant": dict(hair=("none", (0.03, 0.03, 0.03)), brows=0.0, hands="soot", skin_fx="cracks",
                     face={"cheek-volume-decr": 1.0, "cheek-bones-incr": 0.9, "eye-bag-incr": 0.5, "mouth-angles-down": 0.5}),
    "fiend": dict(hair=("none", (0.05, 0.05, 0.05)), hands="blood", gore=0.6, skin_fx="raw"),
}
for _k, _v in LOOKS.items():
    SPECS[_k].update(_v)
SPEC = SPECS[ENEMY]
if os.environ.get("CHEF_SPEC"):      # dev: try spec values without editing, e.g. CHEF_SPEC='{"aim_hand_drop": 0.2}'
    import json as _json
    SPEC = dict(SPEC, **_json.loads(os.environ["CHEF_SPEC"]))

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene

# =============================================================== 1. body
for _t, _v in SPEC.get("face", {}).items():
    if _t.startswith(("cheek-", "eye-")):
        SPEC["targets"]["l-" + _t] = _v; SPEC["targets"]["r-" + _t] = _v
    else:
        SPEC["targets"][_t] = _v
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


def gltf_output_group():
    """The node group the glTF exporter reads extra outputs (occlusion) from."""
    g = bpy.data.node_groups.get("glTF Material Output")
    if g is None:
        g = bpy.data.node_groups.new("glTF Material Output", "ShaderNodeTree")
        g.interface.new_socket("Occlusion", in_out="INPUT", socket_type="NodeSocketFloat")
    return g


def attach_detail(m, normal_img, orm_img):
    nt = m.node_tree; b = nt.nodes["Principled BSDF"]
    if normal_img is not None:          # tangent-space normal map (exported as normalTexture)
        normal_img.colorspace_settings.name = "Non-Color"
        tn = nt.nodes.new("ShaderNodeTexImage"); tn.image = normal_img
        nm = nt.nodes.new("ShaderNodeNormalMap")
        nt.links.new(tn.outputs["Color"], nm.inputs["Color"]); nt.links.new(nm.outputs["Normal"], b.inputs["Normal"])
    if orm_img is not None:             # packed R=occlusion G=roughness B=specular, rides the occlusionTexture slot
        orm_img.colorspace_settings.name = "Non-Color"
        to = nt.nodes.new("ShaderNodeTexImage"); to.image = orm_img; to.name = "ORM"
        go = nt.nodes.new("ShaderNodeGroup"); go.node_tree = gltf_output_group()
        nt.links.new(to.outputs["Color"], go.inputs["Occlusion"])
        m["orm"] = orm_img.name


def simple_material(name, image=None, rgb=(0.8, 0.8, 0.8), rough=0.8, metal=0.0, emit=None, alpha_clip=False,
                    normal_img=None, orm_img=None, emit_img=None):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    attach_detail(m, normal_img, orm_img)
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
    if emit and emit_img is None:   # raylib reads glow only from an emissive TEXTURE: a flat colour gets a tiny one
        e = np.ones((8, 8, 4), np.float32); e[..., :3] = emit
        emit_img = from_np(f"{ENEMY}_{name}_emit", e)
    if emit_img is not None:
        te = nt.nodes.new("ShaderNodeTexImage"); te.image = emit_img; te.name = "EMIT"
        nt.links.new(te.outputs["Color"], b.inputs["Emission Color"])
        b.inputs["Emission Strength"].default_value = 1.0
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


# ---- surface detail: normal maps + packed ORM (occlusion / roughness / specular) ----
DETAIL = os.environ.get("ENEMY_DETAIL", "1") != "0"


def named_image(mat, key):
    if not mat or not mat.use_nodes:
        return None
    for n in mat.node_tree.nodes:
        if n.type == "TEX_IMAGE" and n.image and key in n.image.name.lower():
            return n.image
    return None


def blur(a, r):
    """~Gaussian blur (two separable box passes) of a 2-D float array."""
    for _ in range(2):
        for ax in (0, 1):
            pad = [(r + 1, r) if i == ax else (0, 0) for i in range(2)]
            c = np.cumsum(np.pad(a, pad, mode="edge"), axis=ax)
            n = a.shape[ax]
            a = (np.take(c, range(2 * r + 1, n + 2 * r + 1), axis=ax) - np.take(c, range(0, n), axis=ax)) / (2 * r + 1)
    return a


def height_to_normal(h, strength):
    gy, gx = np.gradient(h)                       # rows run bottom-up in Blender images: +gy is +V (OpenGL/glTF)
    n = np.stack([-gx * strength, -gy * strength, np.ones_like(h)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return n * 0.5 + 0.5


def img_np(img, size=None):
    if img is None:
        return None
    if size and tuple(img.size) != (size[1], size[0]):
        img = img.copy(); img.scale(size[1], size[0])
    return to_np(img)


def save_maps(tag, nrm, orm, sss=0.0):
    """nrm: HxWx3 in 0..1 (or None), orm: HxWx3 -> Blender images (PNG in build/enemy_tex).
    ORM alpha = 1 - subsurface (skin = 0): the shader bleeds light through skin's shadow edge."""
    out = []
    for suffix, arr in (("_n", nrm), ("_orm", orm)):
        if arr is None:
            out.append(None); continue
        alpha = np.full(arr.shape[:2] + (1,), 1.0 - sss, np.float32) if suffix == "_orm" else np.ones(arr.shape[:2] + (1,), np.float32)
        rgba = np.concatenate([arr, alpha], -1)
        out.append(from_np(tag + suffix, rgba))
    return out


def splat_mask(before, after):
    d = np.abs(after[..., :3] - before[..., :3]).sum(-1)
    return np.clip(d * 6.0, 0, 1)


def vein_mask(shape, seed):
    """Branching vein network: ridges of two noise octaves, clustered by a coarse mask."""
    v = np.zeros(shape, np.float32)
    for cells, w in ((34, 1.0), (80, 0.6)):
        r = 1.0 - np.abs(smooth_noise(*shape, cells, seed + cells) * 2 - 1)
        v = np.maximum(v, np.clip((r - 0.93) * 14, 0, 1) * w)
    return v * np.clip((smooth_noise(*shape, 5, seed) - 0.35) * 3, 0, 1)


def skin_maps(tag, a, lesion_mask, img=None, hair=None):
    """Skin: pores + wrinkles + creases from the diffuse's own detail, raised lesions, veins."""
    veins = vein_mask(a.shape[:2], 404)
    if img is not None:                               # veins show faintly blue-grey through the skin
        a[..., 0] *= 1 - 0.16 * veins; a[..., 1] *= 1 - 0.10 * veins; a[..., 2] *= 1 - 0.02 * veins
        img.pixels.foreach_set(a.astype(np.float32).ravel()); img.save()
    lum = 0.3 * a[..., 0] + 0.59 * a[..., 1] + 0.11 * a[..., 2]
    fine = blur(lum, 1) - blur(lum, 10)            # wrinkles, creases, blemishes
    rng = np.random.default_rng(77)
    pores = blur(rng.standard_normal(lum.shape).astype(np.float32), 1) * 0.05
    h = fine * 1.35 + pores + blur(lesion_mask, 2) * 0.14 + blur(veins, 1) * 0.05      # negative = split open
    lesion_mask = np.clip(lesion_mask, 0, 1)
    if hair is None:
        hair = np.zeros_like(lum)
    h = h + hair * blur(rng.standard_normal(lum.shape).astype(np.float32), 1) * 0.10   # bristle
    nrm = height_to_normal(h, 7.0)
    sweat = smooth_noise(*lum.shape, 14, 5)
    orm = np.stack([np.ones_like(lum),                               # occlusion (geometry bake multiplies in)
                    np.clip(0.62 - 0.22 * sweat - 0.35 * lesion_mask + 0.25 * hair, 0.12, 1),   # roughness: sweaty / weeping = glossier
                    np.clip((0.30 + 0.15 * sweat + 0.45 * lesion_mask) * (1 - 0.6 * hair), 0, 1)], -1)  # specular strength
    return save_maps(tag, nrm, orm, sss=1.0)


def cloth_maps(tag, a, asset_n, asset_ao, blood_mask, fresh_mask):
    shape = a.shape[:2]
    nrm = None
    if asset_n is not None:                           # the asset's folds, deepened
        v = asset_n[..., :3] * 2 - 1
        v[..., :2] *= 1.7
        nrm = v / np.linalg.norm(v, axis=-1, keepdims=True) * 0.5 + 0.5
    lum = 0.3 * a[..., 0] + 0.59 * a[..., 1] + 0.11 * a[..., 2]
    weave = blur(lum, 1) - blur(lum, 4)               # fabric grain from the texture itself
    h = weave * 0.9 + blur(blood_mask, 2) * 0.06      # crusted blood stands proud a touch
    det = height_to_normal(h, 6.0)
    if nrm is None:
        nrm = det
    else:                                             # detail on top of the asset's folds/seams (whiteout blend)
        n1 = nrm * 2 - 1; n2 = det * 2 - 1
        n = np.stack([n1[..., 0] + n2[..., 0], n1[..., 1] + n2[..., 1], n1[..., 2] * n2[..., 2]], -1)
        nrm = n / np.linalg.norm(n, axis=-1, keepdims=True) * 0.5 + 0.5
    ao = asset_ao[..., 0] if asset_ao is not None else np.ones(shape, np.float32)
    orm = np.stack([ao,
                    np.clip(0.88 - 0.35 * blood_mask - 0.45 * fresh_mask, 0.15, 1),
                    np.clip(0.05 + 0.35 * blood_mask + 0.55 * fresh_mask, 0, 1)], -1)
    return save_maps(tag, nrm, orm)


# ---- painting by where a texel sits ON THE BODY (not where it happens to land in UV space) ----
def pos_map(o, size, res=2048):
    """World-space rest position of every texel of o's UV layout: (size x size x 3, coverage).
    Rasterised at `res` and stretched; islands are grown a few texels so seams get painted."""
    res = min(res, size)
    if size % res:
        res = size
    me = o.data; me.calc_loop_triangles()
    n = len(me.loop_triangles)
    lt = np.empty(n * 3, np.int32); me.loop_triangles.foreach_get("loops", lt)
    vt = np.empty(n * 3, np.int32); me.loop_triangles.foreach_get("vertices", vt)
    uvd = me.uv_layers.active.data
    uv = np.empty(len(uvd) * 2, np.float32); uvd.foreach_get("uv", uv); uv = uv.reshape(-1, 2)
    mw = o.matrix_world
    co = np.array([tuple(mw @ v.co) for v in me.vertices], np.float32)
    T = uv[lt].reshape(n, 3, 2) * res - 0.5
    V = co[vt].reshape(n, 3, 3)
    P = np.zeros((res, res, 3), np.float32); cov = np.zeros((res, res), bool)
    lo = np.floor(T.min(axis=1)).astype(int) - 1; hi = np.ceil(T.max(axis=1)).astype(int) + 1
    for k in range(n):
        xa, ya = max(0, lo[k, 0]), max(0, lo[k, 1]); xb, yb = min(res - 1, hi[k, 0]), min(res - 1, hi[k, 1])
        if xb < xa or yb < ya:
            continue
        (x0, y0), (x1, y1), (x2, y2) = T[k]
        den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
        if abs(den) < 1e-9:
            continue
        X, Y = np.meshgrid(np.arange(xa, xb + 1, dtype=np.float32), np.arange(ya, yb + 1, dtype=np.float32))
        w0 = ((y1 - y2) * (X - x2) + (x2 - x1) * (Y - y2)) / den
        w1 = ((y2 - y0) * (X - x2) + (x0 - x2) * (Y - y2)) / den
        w2 = 1 - w0 - w1
        m = (w0 >= -0.03) & (w1 >= -0.03) & (w2 >= -0.03)
        if not m.any():
            continue
        v = w0[..., None] * V[k, 0] + w1[..., None] * V[k, 1] + w2[..., None] * V[k, 2]
        P[ya:yb + 1, xa:xb + 1][m] = v[m]; cov[ya:yb + 1, xa:xb + 1][m] = True
    for _ in range(6):                                 # grow the islands into the gutters
        acc = np.zeros_like(P); cnt = np.zeros((res, res), np.float32)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            acc += np.roll(P * cov[..., None], (dy, dx), (0, 1)); cnt += np.roll(cov, (dy, dx), (0, 1))
        new = (~cov) & (cnt > 0)
        P[new] = acc[new] / cnt[new][:, None]; cov = cov | new
    k = max(1, size // res)
    return P.repeat(k, 0).repeat(k, 1)[:size, :size], cov.repeat(k, 0).repeat(k, 1)[:size, :size]


_LAT = {}


def noise3(P, freq, seed=0, stretch=(1.0, 1.0, 1.0)):
    """Gradient (Perlin) noise in 0..1 sampled at world points P (HxWx3): seamless across UV
    islands and the same grain size everywhere on the body. `freq` = features per metre. The
    lattice is turned a different way for every seed, so thresholds don't come out as boxes."""
    if seed not in _LAT:
        rng = np.random.default_rng(5000 + seed)
        g = rng.standard_normal((32, 32, 32, 3)).astype(np.float32); g /= np.linalg.norm(g, axis=-1, keepdims=True)
        R, _ = np.linalg.qr(rng.standard_normal((3, 3)))
        _LAT[seed] = (g, R.astype(np.float32), rng.random(3).astype(np.float32) * 7)
    G, R, off = _LAT[seed]
    if tuple(stretch) != (1.0, 1.0, 1.0):               # streaks must stay world-aligned (drips run DOWN)
        q = P * (np.array(stretch, np.float32) * freq) + off + 40.0
    else:
        q = (P @ R.T) * freq + off + 40.0
    i = np.floor(q).astype(np.int32); f = q - i
    u = f * f * f * (f * (f * 6 - 15) + 10)
    i0 = i & 31; i1 = (i + 1) & 31
    out = 0.0
    for dx in (0, 1):
        ix = i1[..., 0] if dx else i0[..., 0]; wx = u[..., 0] if dx else 1 - u[..., 0]
        for dy in (0, 1):
            iy = i1[..., 1] if dy else i0[..., 1]; wy = u[..., 1] if dy else 1 - u[..., 1]
            for dz in (0, 1):
                iz = i1[..., 2] if dz else i0[..., 2]; wz = u[..., 2] if dz else 1 - u[..., 2]
                g = G[ix, iy, iz]
                d = g[..., 0] * (f[..., 0] - dx) + g[..., 1] * (f[..., 1] - dy) + g[..., 2] * (f[..., 2] - dz)
                out = out + d * (wx * wy * wz)
    return np.clip(0.5 + 0.80 * out, 0, 1).astype(np.float32)


def worley3(P, freq, seed=0, jitter=0.92):
    """Cell noise at world points P: (distance to the nearest feature point, gap to the second
    nearest), in cell units. The gap falls to 0 along the walls between cells -> cracks, plates."""
    tab = np.random.default_rng(7000 + seed).random((16, 16, 16, 3)).astype(np.float32)
    q = P * freq + 40.0
    i = np.floor(q).astype(np.int32); f = q - i
    f1 = np.full(P.shape[:2], 9.0, np.float32); f2 = f1.copy()
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            for dz in (-1, 0, 1):
                c = tab[(i[..., 0] + dx) & 15, (i[..., 1] + dy) & 15, (i[..., 2] + dz) & 15]
                d = np.array((dx, dy, dz), np.float32) + 0.5 + (c - 0.5) * jitter - f
                d = np.sqrt((d * d).sum(-1))
                f2 = np.where(d < f1, f1, np.minimum(f2, d)); f1 = np.minimum(f1, d)
    return f1, f2 - f1


def fbm3(P, freq, seed=0, octaves=3):
    out = 0.0; amp = 1.0; tot = 0.0
    for k in range(octaves):
        out = out + amp * noise3(P, freq * 2 ** k, seed + k); tot += amp; amp *= 0.5
    return out / tot


def sstep(x, a, b):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def mix_to(a, col, m):
    """Blend a's rgb toward colour `col` by mask m (HxW)."""
    for ch in range(3):
        a[..., ch] = a[..., ch] * (1 - m) + col[ch] * m


def groom(a, o, P, cov):
    """Hair he still has, brows, stubble and working hands -- painted through the position map,
    so they sit where they belong on the head whatever the UV layout. Returns (hair, wet) masks."""
    h, w = a.shape[:2]
    style, hair_rgb = SPEC.get("hair", ("none", (0.08, 0.06, 0.05)))
    hair_rgb = np.array(hair_rgb, np.float32)
    gi = o.vertex_groups["Head"].index; mw = o.matrix_world
    hv = np.array([tuple(mw @ v.co) for v in o.data.vertices if any(g.group == gi and g.weight > 0.5 for g in v.groups)], np.float32)
    eo = next((x for x in meshes if "low-poly" in x.name), None)
    if eo is None or not len(hv):
        return np.zeros((h, w), np.float32), np.zeros((h, w), np.float32)
    ev = np.array([tuple(eo.matrix_world @ v.co) for v in eo.data.vertices], np.float32)
    eyes = [ev[ev[:, 0] > 0].mean(axis=0), ev[ev[:, 0] < 0].mean(axis=0)]
    E = (eyes[0] + eyes[1]) / 2
    s = float(hv[:, 2].max() - E[2])                    # eye line -> crown: the unit every landmark is measured in
    fh = hv[(hv[:, 2] > E[2] + 0.15 * s) & (hv[:, 2] < E[2] + 0.45 * s)]
    yF, yB = float(fh[:, 1].min()), float(hv[:, 1].max())
    X = P[..., 0] - E[0]; Z = (P[..., 2] - E[2]) / s
    t = np.clip((P[..., 1] - yF) / (yB - yF), 0, 1)     # 0 = forehead, 1 = back of the skull
    head = cov & (np.abs(X) < 0.14) & (Z > -1.5)
    grain = np.random.default_rng(88).random((h, w)).astype(np.float32)
    grain = np.clip(blur(grain, 1) * 2.4 - 0.7, 0, 1)   # single hairs: a texel-scale speckle
    ear = max(abs(float(hv[:, 0].max() - E[0])), abs(float(hv[:, 0].min() - E[0])))
    print(f"enemy: head s={s:.3f} depth={yB - yF:.3f} ear half-width={ear:.3f}")
    # --- scalp
    hair = np.zeros((h, w), np.float32)
    if style != "none":
        hl = np.interp(t, [0.0, 0.10, 0.26, 0.40, 0.60, 0.74, 0.88, 1.0], [0.56, 0.60, 0.40, 0.20, 0.16, -0.10, -0.42, -0.50])
        hl = hl + 0.07 * (noise3(P, 70, 41) - 0.5)
        hair = sstep(Z - hl, -0.03, 0.09) * head
        hair *= 1 - sstep(np.abs(X), ear - 0.016, ear - 0.004)          # not on the ears
        if style in ("ring", "thin"):                                     # bald on top, hair round the sides and back
            hair *= (1 - sstep(Z, 0.58, 0.76)) * sstep(t, 0.16, 0.30)
        dens = {"buzz": 0.86, "ring": 0.80, "thin": 0.50, "patchy": 0.72, "shaved": 0.30}[style]
        if style == "patchy":                                             # falling out in clumps
            wp = P + (np.stack([noise3(P, 55, 52 + k) for k in range(3)], -1) - 0.5) * 0.02
            hair *= sstep(fbm3(wp, 19, 43), 0.40, 0.72)
            hair = sstep(hair + (grain - 0.5) * 0.35, 0.10, 0.95)                       # thinning out softly at every edge
            hair *= 0.45 + 0.55 * sstep(noise3(P, 150, 44, (1, 1, 0.25)), 0.30, 0.70)     # lank strands, scalp showing through
        else:
            hair = sstep(hair + (grain - 0.5) * 0.9, 0.25, 0.80) * sstep(hair, 0.0, 0.10)   # every edge frays out into single hairs
        if style == "thin":
            hair *= 0.55 + 0.45 * sstep(noise3(P, 160, 44, (1, 1, 0.25)), 0.35, 0.65)
        d = hair * dens * (0.72 + 0.28 * grain)
        for ch in range(3):
            a[..., ch] = a[..., ch] * (1 - d) + hair_rgb[ch] * (0.7 + 0.6 * grain) * d
    # --- brows: low and heavy, drawn down toward the nose
    brow = np.zeros((h, w), np.float32)
    for c in eyes:
        sg = 1.0 if c[0] > E[0] else -1.0
        u = (P[..., 0] - c[0]) * sg / 0.030                               # -1 inner end .. +1 outer end
        zb = c[2] + s * (0.122 + 0.026 * (1 - u * u) - 0.026 * np.clip(-u, 0, 1))
        th = s * 0.046 * (1 - 0.45 * np.clip(u, 0, 1.2))
        m = sstep(1 - np.abs(P[..., 2] - zb) / th, 0.0, 0.8) * sstep(u, -0.78, -0.50) * sstep(1.25 - u, 0.0, 0.35)
        brow = np.maximum(brow, m * (P[..., 1] < c[1] + 0.012) * head)
    if SPEC.get("brows", 1.0) > 0:
        d = np.clip(brow, 0, 1) * 0.78 * SPEC.get("brows", 1.0) * (0.45 + 0.55 * grain)
        for ch in range(3):
            a[..., ch] = a[..., ch] * (1 - d) + hair_rgb[ch] * 0.8 * d
    # --- stubble: jaw, chin, upper lip, under the chin
    sb = SPEC.get("stubble", 0.0)
    if sb > 0:
        st = head * (1 - sstep(t, 0.44, 0.64)) * sstep((-0.40 + 0.50 * t) - Z + 0.10 * (noise3(P, 28, 45) - 0.5), 0.0, 0.34) * sstep(Z, -1.34, -1.02)
        st *= 1 - sstep(1.0 - ((X / 0.030) ** 2 + ((Z + 0.615) / 0.085) ** 2), 0.0, 0.35) * (t < 0.25)   # the lips stay bare
        st *= 0.75 + 0.25 * noise3(P, 45, 46)
        d = st * sb * 0.62 * (0.55 + 0.45 * grain)
        shade = hair_rgb * 0.8 + np.array((0.05, 0.055, 0.065), np.float32)
        for ch in range(3):
            a[..., ch] = a[..., ch] * (1 - d) + shade[ch] * d
        hair = np.maximum(hair, st * sb * 0.6)
    # --- lips: bloodless
    lips = sstep(1.0 - ((X / 0.030) ** 2 + ((Z + 0.615) / 0.070) ** 2), 0.0, 0.5) * (t < 0.25) * head
    for ch, k in enumerate((0.80, 0.72, 0.80)):
        a[..., ch] *= 1 - lips * (1 - k)
    # --- hands: ingrained dirt; kitchen hands are bloody to the forearm
    wet = np.zeros((h, w), np.float32)
    mode = SPEC.get("hands", "dirt")
    for side in ("Left", "Right"):
        wr = np.array(tuple(rig.matrix_world @ rig.data.bones[side + "Hand"].head_local), np.float32)
        el = np.array(tuple(rig.matrix_world @ rig.data.bones[side + "ForeArm"].head_local), np.float32)
        ax = (wr - el) / np.linalg.norm(wr - el)
        rel = P - wr
        q = rel @ ax                                                      # metres past the wrist, toward the fingers
        rad = np.linalg.norm(rel - q[..., None] * ax, axis=-1)
        arm = cov * (rad < 0.14) * (q > -0.30) * (q < 0.26)
        nz = fbm3(P, 30, 47)
        dirt = arm * sstep(q + 0.10 * (nz - 0.5), -0.06, 0.03) * (0.35 + 0.35 * noise3(P, 90, 48))
        for ch, k in enumerate((0.52, 0.44, 0.36)):
            a[..., ch] *= 1 - dirt * (1 - k)
        if mode == "blood":
            bl = arm * sstep(q + 0.22 * (nz - 0.5), -0.20, -0.07)         # ragged tide line up the forearm
            bl = np.maximum(bl, arm * (q > -0.28) * sstep(fbm3(P, 85, 49), 0.66, 0.72) * 0.9)   # spatter above it
            fresh = sstep(noise3(P, 26, 50), 0.40, 0.70)
            col = np.array((0.17, 0.022, 0.018), np.float32)[None, None, :] * (1 - fresh[..., None]) \
                + np.array((0.36, 0.030, 0.022), np.float32)[None, None, :] * fresh[..., None]
            d = bl * (0.72 + 0.2 * nz)
            for ch in range(3):
                a[..., ch] = a[..., ch] * (1 - d) + col[..., ch] * d
            wet = np.maximum(wet, bl * fresh)
        elif mode == "soot":
            d = arm * sstep(q, -0.25, 0.0) * 0.55
            for ch in range(3):
                a[..., ch] *= 1 - d * 0.8
    return hair, wet


def skin_fx(a, P, cov, kind, les_c, bruise_c):
    """What the radiation / the fire did to his skin, grown in 3-D noise so patches wander
    across the body instead of being stamped on as dots. Returns (raised+wet mask, glow mask)."""
    les_c = np.array(les_c, np.float32); bruise_c = np.array(bruise_c, np.float32)
    cv = cov.astype(np.float32)
    if kind == "cracks":                               # charred hide split into plates, embers in the splits
        warp = np.stack([noise3(P, 34, 67 + k) for k in range(3)], -1) - 0.5
        Pw = P + warp * 0.014                                                # no ruler-straight plate edges
        f1, gap = worley3(Pw, 34, 64)                                        # ~3 cm plates
        _, gap2 = worley3(Pw, 105, 65)                                       # crazing inside each plate
        heat = sstep(noise3(P, 3.6, 63), 0.36, 0.58)                         # where he is still burning inside
        ash = sstep(fbm3(P, 85, 61), 0.50, 0.62) * 0.34
        mix_to(a, (0.30, 0.28, 0.26), ash * cv)                             # flaking grey ash
        for ch in range(3):                                                  # every plate charred a little differently
            a[..., ch] *= 1 - 0.30 * sstep(f1, 0.15, 0.55) * cv
        raw = sstep(fbm3(P, 12, 62), 0.56, 0.63) * (1 - heat) * cv
        mix_to(a, (0.24, 0.045, 0.035), raw * 0.75)                          # where the hide burned through to meat
        wide = 0.035 + 0.085 * heat + 0.03 * (noise3(P, 20, 68) - 0.5)
        split = (1 - sstep(gap, wide * 0.35, wide)) * cv                     # the gaps between plates
        craze = (1 - sstep(gap2, 0.02, 0.07)) * 0.55 * cv
        mix_to(a, (0.02, 0.014, 0.012), np.maximum(split, craze) * 0.92)     # cold splits are just black
        core = (1 - sstep(gap, 0.0, wide * 0.55)) * sstep(heat, 0.25, 0.75) * cv
        core *= 0.55 + 0.45 * sstep(noise3(P, 26, 66), 0.30, 0.65)           # embers pulse along a split, never a neon tube
        halo = np.clip(blur(core, 4) * 1.5, 0, 1) * cv
        white = core * core
        col = les_c[None, None, :] * (1 - white[..., None]) + np.array((1.0, 0.72, 0.30), np.float32) * white[..., None]
        for ch in range(3):
            a[..., ch] = a[..., ch] * (1 - core) + np.clip(col[..., ch], 0, 1) * core
        glow = np.clip(col, 0, 1) * core[..., None] + np.array((0.60, 0.08, 0.015), np.float32) * (halo * (1 - core) * 0.30)[..., None]
        return np.clip(raw * 0.6 - np.maximum(split, craze * 0.6) * 1.6, -2, 1), glow
    cl = sstep(noise3(P, 5.0, 31), 0.36, 0.62)         # where on the body it has taken hold
    if kind == "sores":                                # radiation sickness: swollen, pustular, weeping
        warp = np.stack([noise3(P, 40, 36 + k) for k in range(3)], -1) - 0.5
        Pw = P + warp * 0.008
        infl = sstep(fbm3(P, 15, 32), 0.48, 0.64) * cl * cv                  # angry swollen patches, no hard edge
        mott = sstep(noise3(P, 9, 33), 0.52, 0.80) * 0.40 * cv
        mix_to(a, bruise_c, np.maximum(mott, infl * 0.45))
        mix_to(a, (0.46, 0.20, 0.16), infl * 0.30)
        f1, _ = worley3(Pw, 150, 37)                                         # pustules, a few mm each, in crops
        grp = sstep(fbm3(P, 24, 38), 0.50, 0.60) * cl * cv
        size = 0.22 + 0.22 * noise3(P, 70, 39)
        base = (1 - sstep(f1, size, size * 1.9)) * grp
        pust = (1 - sstep(f1, size * 0.35, size)) * grp
        f1b, _ = worley3(Pw, 52, 40)                                         # a few open ulcers
        big = sstep(noise3(P, 13, 41), 0.60, 0.68) * cl * cv
        rim = (1 - sstep(f1b, 0.30, 0.50)) * big
        ulc = (1 - sstep(f1b, 0.12, 0.34)) * big
        mix_to(a, (0.40, 0.11, 0.09), np.maximum(base * 0.55, rim * 0.80))   # inflamed base / raw rim
        var = 0.60 + 0.40 * noise3(P, 110, 34)
        mix_to(a, (0.17, 0.05, 0.04), ulc * 0.85)                            # the crater: dark, wet
        slough = ulc * sstep(noise3(P, 95, 42), 0.45, 0.62)
        head = np.clip(pust + slough * 0.8, 0, 1)
        for ch in range(3):
            a[..., ch] = a[..., ch] * (1 - head * 0.9) + les_c[ch] * var * head * 0.9
        v = vein_mask(a.shape[:2], 35) * cl * cv                             # poisoned veins feeding them
        mix_to(a, les_c * 0.45, np.clip(v, 0, 1) * 0.40)
        g = np.clip(head * head * (0.45 + 0.55 * var) + v * 0.18, 0, 1)
        return np.clip(pust + rim * 0.5 - ulc * 0.9, -1, 1), les_c[None, None, :] * g[..., None]
    f = fbm3(P, 17, 32)
    thr = 0.60 - 0.07 * cl
    sore = sstep(f, thr, thr + 0.05) * cl * cv
    rim = sstep(f, thr - 0.045, thr) * cl * cv
    mott = sstep(noise3(P, 9, 33), 0.52, 0.80) * 0.40 * cv
    mix_to(a, bruise_c, np.maximum(mott, rim * 0.70))
    var = 0.55 + 0.45 * noise3(P, 110, 34)
    for ch in range(3):
        a[..., ch] = a[..., ch] * (1 - sore * 0.92) + les_c[ch] * var * sore * 0.92
    return sore, np.zeros(a.shape[:2] + (3,), np.float32)


def wear(out, o):
    """Clothes get dirty where clothes get dirty: mud climbing from the hems, and for the
    kitchen staff blood down the front. Returns (dried, fresh) blood masks."""
    h, w = out.shape[:2]
    P, cov = pos_map(o, h)
    cv = cov.astype(np.float32)
    z = P[..., 2]
    nz = fbm3(P, 16, 71)
    mud = np.clip(1 - z / 0.46, 0, 1) ** 1.4 * (0.30 + 0.70 * sstep(nz, 0.30, 0.70)) * cv
    mud = np.maximum(mud, sstep(noise3(P, 60, 72), 0.70, 0.78) * np.clip(1 - z / 0.75, 0, 1) * 0.8 * cv)   # kicked-up flecks
    mix_to(out, (0.16, 0.13, 0.09), mud * 0.78)
    scuff = sstep(fbm3(P, 22, 73), 0.60, 0.72) * 0.30 * cv                   # worn-in grey patches everywhere
    for ch, k in enumerate((0.70, 0.66, 0.60)):
        out[..., ch] *= 1 - scuff * (1 - k)
    gore = SPEC.get("gore", 0.0)
    dried = np.zeros((h, w), np.float32); fresh = np.zeros((h, w), np.float32)
    if gore > 0:
        hy = (rig.matrix_world @ rig.data.bones["Hips"].head_local).y
        hz = (rig.matrix_world @ rig.data.bones["Hips"].head_local).z
        front = sstep(hy - P[..., 1], -0.02, 0.07) * sstep(z, hz - 0.45, hz - 0.10) * (1 - sstep(z, hz + 0.50, hz + 0.62)) * cv
        big = sstep(fbm3(P, 8.5, 74), 0.60 - 0.10 * gore, 0.68 - 0.10 * gore)
        spat = sstep(fbm3(P, 70, 75, 2), 0.66, 0.71) * sstep(noise3(P, 6, 76), 0.35, 0.6)
        Pup = P + np.array((0, 0, 0.05), np.float32)                         # it ran before it dried: streaks hang under a stain
        run = sstep(noise3(P, 80, 77, (1, 1, 0.06)), 0.70, 0.84) * 0.8 * sstep(fbm3(Pup, 8.5, 74), 0.58 - 0.10 * gore, 0.66 - 0.10 * gore)
        dried = np.clip(np.maximum(big, np.maximum(spat, run * 0.85)) * front, 0, 1)
        fresh = dried * sstep(noise3(P, 20, 78), 0.55, 0.75)
        col = np.array((0.20, 0.028, 0.02), np.float32)[None, None, :] * (1 - fresh[..., None]) \
            + np.array((0.38, 0.03, 0.022), np.float32)[None, None, :] * fresh[..., None]
        d = dried * 0.86
        for ch in range(3):
            out[..., ch] = out[..., ch] * (1 - d) + col[..., ch] * d * (0.8 + 0.2 * out[..., ch])
    return dried, fresh


def eye_material(src):
    """A real eyeball instead of a flat green bead: the asset's eye texture regraded --
    jaundiced, bloodshot sclera; an irradiated iris (its own fibres kept) with a dark limbal
    ring and a pinprick pupil. Only the iris glows."""
    glow = np.array(SPEC.get("eye_glow", (0.45, 1.0, 0.25)), np.float32)
    if src is None:
        return simple_material("cook_eyes", None, tuple(glow), 0.3, emit=tuple(glow * 0.6))
    a = to_np(src); h, w = a.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    u = (xx + 0.5) / w; v = (yy + 0.5) / h
    lum = 0.3 * a[..., 0] + 0.59 * a[..., 1] + 0.11 * a[..., 2]
    veins = np.clip(vein_mask((h, w), 911) * 0.9 + 0.30 * smooth_noise(h, w, 7, 5), 0, 1)[..., None] * 0.7
    out = a.copy()
    scl = np.stack([lum * 1.00, lum * 0.90, lum * 0.66], -1)           # jaundice
    out[..., :3] = scl * (1 - veins) + np.array((0.50, 0.05, 0.04), np.float32) * veins
    emi = np.zeros_like(a); emi[..., 3] = 1
    for cu, cv in ((0.292, 0.291), (0.705, 0.703)):                    # the two irises in the asset's UV layout
        r = np.sqrt((u - cu) ** 2 + (v - cv) ** 2) / 0.115
        iris = np.clip((1.0 - r) * 14, 0, 1)[..., None]
        fib = np.where(r < 0.36, 0.30, np.clip(lum * 2.8, 0, 1))[..., None]
        base = glow * 0.62 + np.array((0.42, 0.40, 0.22), np.float32) * 0.30      # a sick eye, not a neon bead
        col = base[None, None, :] * (0.28 + 0.72 * fib)
        col = col * (1 - 0.8 * np.clip((r - 0.70) / 0.30, 0, 1)[..., None] ** 2)    # limbal ring
        col = col * (1 - np.clip((0.23 - r) * 30, 0, 1)[..., None])                # pupil
        sore = np.clip(1.0 - np.abs(r - 1.25) / 0.45, 0, 1)[..., None] * 0.35       # raw red rim round the iris
        out[..., :3] = out[..., :3] * (1 - sore) + np.array((0.55, 0.10, 0.08), np.float32) * sore
        out[..., :3] = out[..., :3] * (1 - iris) + col * iris
        emi[..., :3] += col * iris * 0.6
    img = from_np(f"{ENEMY}_eyes", np.clip(out, 0, 1))
    return simple_material("cook_eyes", img, rough=0.3, emit_img=from_np(f"{ENEMY}_eyes_emit", np.clip(emi, 0, 1)))


def sick_skin(a, o):
    """The asset skins are a healthy studio pink. Drain them: pallor, livid mottling, ingrained
    dirt, and bruise-dark sunken sockets (found on the mesh, painted through its UVs)."""
    h, w = a.shape[:2]
    rgb = a[..., :3]
    lum = (0.3 * rgb[..., 0] + 0.59 * rgb[..., 1] + 0.11 * rgb[..., 2])[..., None]
    rgb = rgb * 0.60 + lum * 0.40
    rgb = rgb * np.array((0.90, 0.93, 0.87), np.float32)
    m = np.clip((smooth_noise(h, w, 22, 61) - 0.55) * 3, 0, 1)[..., None] * 0.25
    rgb = rgb * (1 - m * (1 - np.array((0.95, 0.62, 0.74), np.float32)))       # livid blotches
    d = np.clip((smooth_noise(h, w, 7, 62) - 0.42) * 2.2, 0, 1)[..., None] * 0.24
    rgb = rgb * (1 - d * (1 - np.array((0.60, 0.50, 0.38), np.float32)))       # dirt
    eo = next((x for x in meshes if "low-poly" in x.name), None)
    uvl = o.data.uv_layers.active
    if eo is not None and uvl is not None:
        ev = [eo.matrix_world @ v.co for v in eo.data.vertices]
        cs = [sum((p for p in ev if s_ * p.x > 0), Vector()) / max(1, len([p for p in ev if s_ * p.x > 0])) for s_ in (1, -1)]
        mw = o.matrix_world; near = {}
        for vx in o.data.vertices:
            p = mw @ vx.co
            dm = min((p - c).length for c in cs)
            if dm < 0.036:
                near[vx.index] = 1.0 - dm / 0.036
        mask = np.zeros((h, w), np.float32); rad = max(3, w // 64)
        for l in o.data.loops:
            k = near.get(l.vertex_index)
            if k is None:
                continue
            uv = uvl.data[l.index].uv; x = int(uv.x * w); y = int(uv.y * h)
            y0, y1, x0, x1 = max(0, y - rad), min(h, y + rad + 1), max(0, x - rad), min(w, x + rad + 1)
            if y1 <= y0 or x1 <= x0:
                continue
            gy, gx = np.ogrid[y0:y1, x0:x1]
            dd = np.sqrt((gy - y) ** 2 + (gx - x) ** 2) / rad
            mask[y0:y1, x0:x1] = np.maximum(mask[y0:y1, x0:x1], k * np.clip(1.2 - dd, 0, 1))
        mask = np.clip(blur(mask, max(2, rad // 3)), 0, 1)[..., None] * 0.62
        rgb = rgb * (1 - mask * (1 - np.array((0.50, 0.36, 0.42), np.float32)))
        print("enemy: sockets painted from", len(near), "verts")
    a[..., :3] = np.clip(rgb, 0, 1)


for o in meshes:                                   # MakeHuman clothes can arrive flat-shaded: facets on sleeves
    o.data.polygons.foreach_set("use_smooth", [True] * len(o.data.polygons))
SIZE = 2048
for o in meshes:
    src = first_image(o.data.materials[0] if o.data.materials else None)
    if src is not None and max(src.size) > SIZE:
        src.scale(SIZE, SIZE)
    if ("male_generic" in o.name or "male1591" in o.name) and src is not None and (DETAIL or SPEC.get("skin_tint")):
        a = to_np(src)
        P, cov = pos_map(o, a.shape[0])
        sick_skin(a, o)
        les = np.zeros(a.shape[:2], np.float32); eimg = None
        if SPEC.get("skin_tint"):
            tint = SPEC["skin_tint"][0]
            les_c, bruise_c = (SPEC["skin_tint"][2:] or ((0.70, 0.80, 0.30), (0.26, 0.17, 0.15)))
            for ch in range(3):
                a[..., ch] = np.clip(a[..., ch] * tint[ch], 0, 1)
            les, glow = skin_fx(a, P, cov, SPEC.get("skin_fx", "sores"), les_c, bruise_c)
            if SPEC.get("skin_glow"):                                       # the lesions / embers give off light
                e = np.zeros_like(a); e[..., 3] = 1
                e[..., :3] = glow * SPEC["skin_glow"]
                eimg = from_np(f"{ENEMY}_skin_emit", np.clip(e, 0, 1))
        hair, wet = groom(a, o, P, cov)
        a[..., :3] = np.clip(a[..., :3], 0, 1)
        img = from_np(f"{ENEMY}_skin", a)
        nimg, oimg = skin_maps(f"{ENEMY}_skin", a, np.clip(les + wet, -2, 1), img, hair) if DETAIL else (None, None)
        o.data.materials.clear(); o.data.materials.append(simple_material(f"{ENEMY}_skin", img, rough=0.6, normal_img=nimg, orm_img=oimg, emit_img=eimg))
        continue
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
        shade = (0.72 + 0.28 * lum_lp) * (0.80 + 0.12 * lum) * (0.79 + 0.21 * grime)
        out = np.empty_like(a)
        for ch in range(3):
            out[..., ch] = np.clip(base[ch] * shade * 1.12 - 0.10 * (1 - grime) * (ch == 2), 0, 1)
        out[..., 3] = a[..., 3]
        st = np.clip((smooth_noise(a.shape[0], a.shape[1], 5, 17) - 0.50) * 3.0, 0, 1) * 0.34   # old sweat / grease tide marks
        st2 = np.clip((smooth_noise(a.shape[0], a.shape[1], 31, 18) - 0.60) * 4.0, 0, 1) * 0.22  # spots
        for ch, sc in enumerate((0.80, 0.70, 0.50)):
            out[..., ch] *= (1 - st * (1 - sc)) * (1 - st2 * (1 - sc * 0.8))
        s0 = out.copy()
        if n_blood: splatter(out, n_blood, 0.006, 0.016, (0.30, 0.03, 0.02), seed=11, alpha=0.75)   # dried blood
        s1 = out.copy()
        if n_drop:  splatter(out, n_drop, 0.003, 0.008, (0.42, 0.03, 0.02), seed=12, alpha=0.85)    # fresh droplets
        s2 = out.copy()
        if n_grease: splatter(out, n_grease, 0.02, 0.05, (0.45, 0.40, 0.28), seed=13, alpha=0.22)  # grease / soot
        wd, wf = wear(out, o)
        out[..., :3] = np.clip(out[..., :3], 0, 1)
        tag = f"{ENEMY}_{o.name.split('.')[-1]}"
        img = from_np(tag, out)
        nimg = oimg = None
        if DETAIL:
            m0 = o.data.materials[0] if o.data.materials else None
            shp = out.shape[:2]
            nimg, oimg = cloth_maps(tag, out, img_np(named_image(m0, "normal"), shp), img_np(named_image(m0, "_ao"), shp),
                                    np.maximum(splat_mask(s0, s1), wd), np.maximum(splat_mask(s1, s2), wf))
        o.data.materials.clear(); o.data.materials.append(simple_material(tag, img, rough=0.9, normal_img=nimg, orm_img=oimg))
    elif "low-poly" in o.name:   # eyes: irradiated glow
        o.data.materials.clear(); o.data.materials.append(eye_material(src))
    elif src is not None:
        alpha = any(k in o.name for k in ("eyebrow", "eyelash"))
        m0 = o.data.materials[0] if o.data.materials else None
        nm = m0.name if m0 else o.name
        nimg = oimg = None
        if DETAIL and not alpha:
            sa = to_np(src); shp = sa.shape[:2]
            if o.data.uv_layers.active is not None:      # boots and the like: the same mud as the trousers
                wear(sa, o); sa[..., :3] = np.clip(sa[..., :3], 0, 1)
                src = from_np(f"{ENEMY}_{nm}_worn", sa)
            an, aa = img_np(named_image(m0, "normal"), shp), img_np(named_image(m0, "_ao"), shp)
            if an is not None or aa is not None:
                nimg, oimg = cloth_maps(f"{ENEMY}_{nm}", sa, an, aa, np.zeros(shp, np.float32), np.zeros(shp, np.float32))
        o.data.materials.clear(); o.data.materials.append(simple_material(nm, src, rough=0.85, alpha_clip=alpha,
                                                                           normal_img=nimg, orm_img=oimg))

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
def _vn(size, cy, cx, seed):
    """Value noise with its own cell count per axis (streaks: few cells one way, many the other)."""
    rng = np.random.default_rng(seed)
    small = rng.random((cy, cx)).astype(np.float32)
    ys = np.arange(size) * (cy - 1) / max(1, size - 1); xs = np.arange(size) * (cx - 1) / max(1, size - 1)
    y0 = np.floor(ys).astype(int); x0 = np.floor(xs).astype(int)
    y1 = np.minimum(y0 + 1, cy - 1); x1 = np.minimum(x0 + 1, cx - 1)
    fy = (ys - y0)[:, None]; fx = (xs - x0)[None, :]
    fy = fy * fy * (3 - 2 * fy); fx = fx * fx * (3 - 2 * fx)
    return (small[y0][:, x0] * (1 - fx) + small[y0][:, x1] * fx) * (1 - fy) + (small[y1][:, x0] * (1 - fx) + small[y1][:, x1] * fx) * fy


def _fbm2(size, cells, seed, octaves=4):
    out = np.zeros((size, size), np.float32); amp = 1.0; tot = 0.0
    for k in range(octaves):
        c = min(size // 2, cells * 2 ** k)
        out += amp * _vn(size, c, c, seed + 31 * k); tot += amp; amp *= 0.5
    out /= tot
    lo, hi = np.percentile(out, 2), np.percentile(out, 98)        # spread to 0..1 whatever the octave mix
    return np.clip((out - lo) / max(1e-6, hi - lo), 0, 1)


def _lines(size, n, lmin, lmax, seed, width=1):
    """Scratches: n straight-ish strokes (lengths as fractions of the texture), 0..1."""
    rng = np.random.default_rng(seed)
    m = np.zeros((size, size), np.float32)
    for _ in range(n):
        x, y = rng.uniform(0, size, 2); ang = rng.uniform(0, 2 * math.pi)
        L = rng.uniform(lmin, lmax) * size; bend = rng.normal(0, 0.25)
        t = np.arange(0, L, 0.6)
        aa = ang + bend * t / max(1.0, L)
        xs = (x + np.cumsum(np.cos(aa)) * 0.6).astype(int) % size; ys = (y + np.cumsum(np.sin(aa)) * 0.6).astype(int) % size
        m[ys, xs] = np.maximum(m[ys, xs], rng.uniform(0.5, 1.0) * np.sin(np.pi * t / max(1.0, L)) ** 0.5)
    return np.clip(blur(m, width) * (2.2 * width + 1.0), 0, 1) if width else m


def _worley2(size, cells, seed):
    """2-D cellular noise -> (F1, F2 - F1) in cell units; F2 - F1 -> 0 on the cell walls (cracks)."""
    rng = np.random.default_rng(seed)
    pts = rng.random((cells + 2, cells + 2, 2)).astype(np.float32)
    g = np.arange(size, dtype=np.float32) * cells / size
    gy, gx = np.meshgrid(g, g, indexing="ij")
    iy, ix = gy.astype(int), gx.astype(int)
    f1 = np.full((size, size), 9.0, np.float32); f2 = f1.copy()
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            cy_, cx_ = (iy + dy) % cells, (ix + dx) % cells
            py = iy + dy + pts[cy_, cx_, 0]; px = ix + dx + pts[cy_, cx_, 1]
            d = np.sqrt((gy - py) ** 2 + (gx - px) ** 2)
            f2 = np.where(d < f1, f1, np.minimum(f2, d)); f1 = np.minimum(f1, d)
    return f1, f2 - f1


# How each prop surface has aged. WEAR[name] = (bare-metal mask, dull dust/rust mask): the
# detail pass turns them into gloss where paint is gone and matte where dirt sits.
FINISH = [(("helmet", "axe_head", "tank", "filter"), "paint"), (("spade",), "rust"), (("rubber", "hood"), "rubber"),
          (("gunmetal", "iron"), "metal"), (("wood", "furniture"), "wood"), (("leather", "strap", "belt"), "leather"),
          (("growth", "tentacle"), None)]
WEAR = {}


def weather(a, name, base, grime, seed, size):
    fin = next((f for keys, f in FINISH if any(k in name for k in keys)), "cloth")
    if fin is None:
        return
    base = np.array(base, np.float32)
    fine = _fbm2(size, 48, seed + 12)
    dust = sstep(_fbm2(size, 6, seed + 16), 0.45, 0.85) * min(0.45, 0.8 * grime)
    zero = np.zeros((size, size), np.float32)
    bare, dull = zero, zero
    if fin == "paint":          # enamel over steel: flaked chips with a pale rim, scratches, rust creeping out
        f = _fbm2(size, 30, seed + 11, octaves=3) * 0.55 + _fbm2(size, 120, seed + 19, octaves=2) * 0.45
        f *= 0.55 + 0.45 * sstep(_fbm2(size, 5, seed + 20), 0.3, 0.7)       # chips come in clusters: where it gets knocked
        thr = 0.74 - 0.10 * grime
        chip = sstep(f, thr, thr + 0.025)
        gouge = _lines(size, 10, 0.015, 0.05, seed + 15)
        chip = np.clip(chip + sstep(gouge, 0.35, 0.6), 0, 1)
        rim = np.clip(blur(chip, 1) * 1.7 - chip, 0, 1)
        rusty = sstep(_fbm2(size, 14, seed + 13), 0.45, 0.75)
        under = np.array((0.21, 0.20, 0.19), np.float32)[None, None] * (1 - rusty[..., None]) + np.array((0.34, 0.17, 0.07), np.float32)[None, None] * rusty[..., None]
        halo = np.clip(blur(chip * rusty, 4) * 2.5, 0, 1) * (1 - chip) * 0.32
        mix_to(a, (0.36, 0.19, 0.08), halo)
        mix_to(a, np.clip(base * 1.35 + 0.08, 0, 1), rim * 0.55)
        for ch in range(3):
            a[..., ch] = a[..., ch] * (1 - chip) + under[..., ch] * chip
        mix_to(a, np.clip(base * 1.6 + 0.07, 0, 1), _lines(size, 70, 0.008, 0.06, seed + 14, width=0) * 0.5 * (1 - chip))
        mix_to(a, (0.44, 0.41, 0.35), dust * 0.55)
        bare, dull = chip * (1 - rusty), np.clip(chip * rusty + halo + dust, 0, 1)
    elif fin == "rust":         # a spade left out: scale, dark pits, bright metal only where it still digs
        mix_to(a, (0.43, 0.21, 0.08), sstep(_fbm2(size, 10, seed + 11), 0.35, 0.70) * 0.75)
        mix_to(a, (0.15, 0.09, 0.06), sstep(_fbm2(size, 40, seed + 13), 0.55, 0.80) * 0.65)
        bare = _lines(size, 60, 0.03, 0.16, seed + 14) * 0.7
        mix_to(a, (0.56, 0.55, 0.54), bare * 0.7)
        dull = 1 - bare
    elif fin == "rubber":       # scuffed pale where it rubs, fine scratches, dust settled on it
        scuff = sstep(_fbm2(size, 14, seed + 11), 0.52, 0.82) * (0.45 + 0.55 * fine)
        mix_to(a, np.clip(base * 1.5 + 0.11, 0, 1), scuff * 0.34)
        mix_to(a, np.clip(base * 1.7 + 0.12, 0, 1), _lines(size, 60, 0.01, 0.08, seed + 14) * 0.40)
        mix_to(a, (0.40, 0.38, 0.33), dust * 0.5)
        dull = np.clip(scuff * 0.6 + dust, 0, 1)
    elif fin == "metal":        # blued steel: the finish worn silver on the high spots, scratches, rust freckles
        for ch in range(3):
            a[..., ch] *= 0.82 + 0.36 * _fbm2(size, 12, seed + 11)
        worn = sstep(_fbm2(size, 8, seed + 17) * 0.6 + fine * 0.4, 0.60, 0.76)
        sc = _lines(size, 90, 0.02, 0.12, seed + 14)
        bare = np.clip(worn * 0.6 + sc * 0.7, 0, 1)
        mix_to(a, (0.52, 0.52, 0.54), bare * 0.6)
        dull = sstep(_fbm2(size, 60, seed + 13), 0.74, 0.84) * min(1.0, 2.0 * grime)
        mix_to(a, (0.33, 0.16, 0.07), dull * 0.75)
    elif fin == "wood":         # grain running the length, hand-polished pale patches, grime in the pores, dents
        g = _vn(size, 5, 150, seed + 11) * 0.6 + _vn(size, 11, 420, seed + 12) * 0.4
        for ch in range(3):
            a[..., ch] *= 0.70 + 0.60 * g
        worn = sstep(_fbm2(size, 6, seed + 17), 0.55, 0.85)
        mix_to(a, np.clip(base * 1.55 + 0.09, 0, 1), worn * 0.38)
        mix_to(a, (0.07, 0.05, 0.04), _lines(size, 26, 0.01, 0.05, seed + 14, width=2) * 0.6)
        bare, dull = worn * 0.25, dust
    elif fin == "leather":      # cracked where it flexes, scuffed pale on the edges
        _, wall = _worley2(size, 46, seed + 11)
        crack = (1 - sstep(wall, 0.0, 0.09)) * sstep(_fbm2(size, 8, seed + 17), 0.35, 0.65)
        for ch in range(3):
            a[..., ch] *= 1 - 0.6 * crack
        scuff = sstep(_fbm2(size, 12, seed + 18), 0.58, 0.85)
        mix_to(a, np.clip(base * 1.6 + 0.10, 0, 1), scuff * 0.32)
        dull = np.clip(scuff + crack, 0, 1)
    else:                        # cloth: old stains, sun-faded patches, lint
        mix_to(a, base * 0.55, sstep(_fbm2(size, 7, seed + 11), 0.50, 0.82) * 0.40)
        mix_to(a, np.clip(base * 1.25 + 0.05, 0, 1), sstep(_fbm2(size, 5, seed + 17), 0.55, 0.88) * 0.22)
        for ch in range(3):
            a[..., ch] *= 0.93 + 0.14 * fine
    WEAR[name] = (np.clip(bare, 0, 1), np.clip(dull, 0, 1))


def paint(name, base, grime=0.25, blood=0, drops=0, size=1024, seed=1, stripes=None):
    a = np.zeros((size, size, 4), np.float32); a[..., 3] = 1
    n1 = smooth_noise(size, size, 6, seed); n2 = smooth_noise(size, size, 23, seed + 1)
    shade = 1.0 - grime * (0.6 * n1 + 0.4 * n2)
    for ch in range(3):
        a[..., ch] = base[ch] * shade
    weather(a, name, base, grime, seed, size)
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
M_lens = simple_material("lens", None, (0.30, 0.62, 0.18), 0.15, emit=(0.30, 0.68, 0.17))
M_dark = paint("darkcloth", (0.13, 0.13, 0.15), grime=0.3, seed=6)
M_hood = paint("hood_rubber", (0.33, 0.35, 0.33), grime=0.45, seed=15) if SPEC["props"] in ("liquidator", "fiend") else M_rubber
M_rim = paint("rim_gunmetal", (0.17, 0.17, 0.18), grime=0.4, seed=16) if SPEC["props"] in ("liquidator", "fiend") else M_dark
M_strap = paint("strap_leather", (0.20, 0.14, 0.09), grime=0.4, seed=14)
M_rust = paint("spade", (0.36, 0.24, 0.15), grime=0.6, seed=7)
M_brass = simple_material("brass", None, (0.70, 0.52, 0.22), 0.35, metal=0.9)
M_red = paint("neckerchief", (0.55, 0.07, 0.06), grime=0.25, seed=8, stripes=(16, (0.9, 0.85, 0.8)))


HANDB = {}


def hand_basis(side):
    """The real fist, from the hand mesh: palm normal = the thin axis of the hand's vertices
    (signed toward where the relaxed fingertips droop), `side` = across the palm toward the
    thumb, `hole` = where a handle sits once the fingers close (just palm-side of the knuckles).
    A handle runs along `side` through `hole`; a pistol grip's barrel runs along `along`."""
    if side in HANDB:
        return HANDB[side]
    pb = rig.data.bones; mw = rig.matrix_world
    hand = mw @ pb[f"{side}Hand"].head_local
    fb = mw @ pb[f"{side}FingerBase"].tail_local       # the KNUCKLES (the Hand bone is a 4 cm stub)
    th = mw @ pb[f"{side[0]}Thumb"].tail_local
    gi = body.vertex_groups.get(f"{side}FingerBase")      # the palm is skinned to FingerBase
    pts = []
    for v in body.data.vertices:
        g = max(v.groups, key=lambda g: g.weight, default=None)
        if gi is not None and g is not None and g.group == gi.index:
            pts.append(tuple(body.matrix_world @ v.co))
    X = np.array(pts); c = X.mean(0)
    n = Vector(tuple(np.linalg.svd(X - c)[2][2]))
    along = (fb - hand).normalized()
    n = (n - along * n.dot(along)).normalized()
    # palm side = where the relaxed fingertips droop (the thumb rests BACK-of-hand on this rig;
    # signing by it curled every fist backwards and put handles on the back of the hand)
    tip = mw @ pb[f"{side}HandFinger1"].tail_local
    drift = tip - (hand + along * (tip - hand).dot(along))
    if drift.dot(n) < 0:
        n = -n
    sd = along.cross(n).normalized()
    if (th - hand).dot(sd) < 0:
        sd = -sd
    # one finger bone bends only at the knuckles, so the fingers close as straight pieces: the
    # handle sits in the wedge between palm and bent fingers, ~3 cm back from the knuckles
    HANDB[side] = dict(hand=hand, hole=fb - along * 0.032 + n * 0.027, along=along, n=n, side=sd)
    print(f"enemy: {side} hand: palm n {tuple(round(x, 2) for x in n)} along {tuple(round(x, 2) for x in along)}")
    return HANDB[side]


def hand_frame(side):
    """Fist hole + the handle direction (across the palm, out past the thumb)."""
    b = hand_basis(side)
    return b["hole"].copy(), b["side"].copy()


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


def ellipse_band(name, z0, z1, mat, pad=0.010, flare=0.022, verts=32, rings=1):
    c, rx, ry = skull_slice(z0)
    bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=1, radius2=1 + flare / max(rx, ry), depth=z1 - z0,
                                    location=(c.x, c.y, (z0 + z1) / 2), end_fill_type="NOTHING")
    o = bpy.context.active_object; o.name = name; o.scale = (rx + pad, ry + pad, 1)
    if rings > 1:                       # horizontal loops, so a shape can vary up the band
        bm = bmesh.new(); bm.from_mesh(o.data)
        bmesh.ops.subdivide_edges(bm, edges=[e for e in bm.edges if abs(e.verts[0].co.z - e.verts[1].co.z) > 1e-5],
                                  cuts=rings - 1, use_grid_fill=True)
        bm.to_mesh(o.data); bm.free()
    m = o.modifiers.new("solid", "SOLIDIFY"); m.thickness = 0.006
    bpy.context.view_layer.objects.active = o; bpy.ops.object.modifier_apply(modifier="solid")
    bpy.ops.object.shade_smooth()
    o.data.materials.append(mat); props.append((o, "Head")); return o, c, rx, ry


def toque(height=0.10, puff=1.0):
    band, c, rx, ry = ellipse_band("hat_band", brow_z, brow_z + height, M_hat, verts=120, rings=8)
    for v in band.data.vertices:        # starched pleats: 20 soft folds that die out into a flat doubled hem
        a = math.atan2(v.co.y, v.co.x)
        t = v.co.z / height + 0.5
        env = min(1.0, max(0.0, (t - 0.14) / 0.2)); env = env * env * (3 - 2 * env)
        k = 1.0 + 0.024 * (0.5 - 0.5 * math.cos(20 * a)) * env + (0.016 if t < 0.14 else 0.0)
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
    orig = {v.index: (v.co.copy(), v.normal.copy()) for v in bm.verts}
    edge = {v.index for e in bm.edges if e.is_boundary for v in e.verts}
    for _ in range(14):                 # rubber spans the hollows of a face (eye sockets, lips, round the ears)...
        bmesh.ops.smooth_vert(bm, verts=[v for v in bm.verts if v.index not in edge], factor=0.5, use_axis_x=True, use_axis_y=True, use_axis_z=True)
    for v in bm.verts:                  # ...but never sinks into it
        co, no = orig[v.index]
        d = (v.co - co).dot(no)
        if d < 0.007: v.co += no * (0.007 - d)
    bm.to_mesh(hood.data); bm.free()
    hood.vertex_groups.clear()
    hood.data.materials.clear(); hood.data.materials.append(M_hood)
    props.append((hood, "Head"))
    for s, e in ((-1, eyeR), (1, eyeL)):
        a = Vector((e.x, nose.y + 0.025, e.z))
        cyl(f"eye_rim_{s}", a, a + Vector((0, -0.022, 0)), 0.024, M_rim, "Head", verts=24)
        cyl(f"eye_seat_{s}", a + Vector((0, 0.004, 0)), a + Vector((0, -0.006, 0)), 0.030, M_hood, "Head", verts=24)   # the moulded boss it sits in
        cyl(f"eye_lens_{s}", a + Vector((0, -0.021, 0)), a + Vector((0, -0.024, 0)), 0.019, M_lens, "Head", verts=24)
    v = Vector((eye_c.x, nose.y + 0.012, mouth_z))
    cyl("valve", v, v + Vector((0, -0.035, -0.008)), 0.026, M_rim, "Head", verts=20)
    for i in range(3):   # ribbed canister, screwed on below the valve, angled down-forward
        a = v + Vector((0, -0.03 - i * 0.022, -0.035 - i * 0.03))
        cyl(f"canister_{i}", a, a + Vector((0, -0.02, -0.028)), 0.04 if i != 1 else 0.042, M_filter, "Head", verts=24)


def cleaver(side, bone, scale=1.0):
    f, w = hand_frame(side)
    side_v = w.cross(Vector((0, 0, 1))).normalized()
    # handle: oval wooden grip with three brass rivets, steel bolster where it meets the blade
    h = cyl(f"cleaver_handle_{side}", f - w * 0.05 * scale, f + w * 0.07 * scale, 0.019 * scale, M_wood, bone, verts=16)
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
    cyl(f"knife_handle_{side}", f - w * 0.05, f + w * 0.065, 0.017, M_dark, bone, verts=10)
    side_v = w.cross(Vector((0, 0, 1)))
    c = f + w * 0.215
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
    # smooth on the mesh itself: the shade_smooth operator only touches SELECTED objects, and
    # the apron is never selected -- it shipped flat-shaded, a mosaic under the glossy rubber
    o.data.polygons.foreach_set("use_smooth", [True] * len(o.data.polygons))
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


def forearm_frame(side):
    """Pistol grip in the fist: grip axis across the palm (thumb end = top), barrel along the
    knuckles. Returns grip centre, barrel dir, up (toward the thumb), side."""
    b = hand_basis(side)
    up = b["side"].copy()
    d = (b["along"] - up * b["along"].dot(up)).normalized()
    return b["hole"].copy(), d, up, d.cross(up).normalized()


GUN = {}   # rifle(): its parts + rest-pose grip frame, so the aim pass can seat it on the aim pose


def curved_mag(name, top, d, sv, up, mat, bone, S=1.0, length=0.21, radius=0.42, depth=0.05, width=0.03, n=14):
    """A single closed magazine: a depth x width box section swept from `top` (inside the
    receiver) down an arc that curves forward (towards d), slightly deeper at the base."""
    d, sv, up = d.normalized(), sv.normalized(), up.normalized()
    L, R = length * S, radius * S
    bm = bmesh.new()
    rings = []
    for i in range(n + 1):
        ph = (i / n) * (L / R)
        c = top + (-up) * (R * math.sin(ph)) + d * (R * (1 - math.cos(ph)))
        nv = d * math.cos(ph) + up * math.sin(ph)          # in-plane, across the section
        dep = depth * S * (1.0 + 0.12 * i / n)               # flares a touch toward the floorplate
        rings.append([bm.verts.new(c + nv * (a * dep / 2) + sv * (b * width * S / 2))
                      for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1))])
    for r0, r1 in zip(rings, rings[1:]):
        for k in range(4):
            bm.faces.new((r0[k], r0[(k + 1) % 4], r1[(k + 1) % 4], r1[k]))
    bm.faces.new(list(reversed(rings[0]))); bm.faces.new(rings[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    o = bpy.data.objects.new(name, me); bpy.context.collection.objects.link(o)
    bpy.context.view_layer.objects.active = o
    m = o.modifiers.new("bevel", "BEVEL"); m.width = 0.002; m.segments = 2
    m.limit_method = "ANGLE"                                  # edges only, not the gentle arc steps
    bpy.ops.object.modifier_apply(modifier="bevel")
    o.data.materials.append(mat); props.append((o, bone)); return o


def rifle(scale=1.0, heavy=False, lift=0.085):
    g, d, up, sv = forearm_frame("Right")
    n0 = len(props)
    GUN.update(start=n0, grip=g.copy(), d=d.copy(), up=up.copy(), lift=lift * scale, scale=scale)
    # the fist holds the PISTOL GRIP; the receiver/stock line rides above the wrist (at fist
    # height the stock sat inside the forearm in every pose)
    g0 = g; g = g + up * lift * scale
    M_gun = paint("gunmetal", (0.12, 0.12, 0.13), grime=0.3, seed=21)
    M_furn = paint("furniture", (0.34, 0.20, 0.10), grime=0.4, seed=22)
    S = scale
    box_basis("receiver", g + d * 0.10 * S + up * 0.04 * S, d, sv, up, (0.30 * S, 0.05 * S, 0.075 * S), M_gun, "RightHand")
    box_basis("dust_cover", g + d * 0.11 * S + up * 0.083 * S, d, sv, up, (0.26 * S, 0.044 * S, 0.012 * S), M_gun, "RightHand")
    # the grip runs THROUGH the fist (g0 is the fist's centre) and out its underside; it used to
    # stop 2 cm below g0, so the hand closed on its very tip and looked like it held nothing
    box_basis("pistol_grip", g0 + up * ((lift - 0.035) * 0.5) * S, d, sv, up, (0.04 * S, 0.035 * S, (lift + 0.075) * S), M_furn, "RightHand")
    # short (folded-style) stock: a full one, modelled along the forearm, ran through the
    # forearm in every carry and through the raised upper arm in the aim
    sl = 0.05 if heavy else 0.08         # the heavy gun's longer reach put its stock in the raised upper arm
    box_basis("stock", g - d * (0.07 + sl / 2) * S + up * 0.02 * S, d, sv, up, (sl * S, 0.042 * S, 0.08 * S), M_furn, "RightHand")
    box_basis("handguard", g + d * 0.33 * S + up * 0.045 * S, d, sv, up, (0.20 * S, 0.055 * S, 0.06 * S), M_furn, "RightHand")
    blen = 0.62 if heavy else 0.42
    cyl("barrel", g + d * 0.25 * S + up * 0.05 * S, g + d * (0.25 + blen) * S + up * 0.05 * S, 0.012 * S, M_gun, "RightHand", verts=12)
    cyl("muzzle", g + d * (0.25 + blen) * S + up * 0.05 * S, g + d * (0.29 + blen) * S + up * 0.05 * S, 0.018 * S, M_gun, "RightHand", verts=12)
    cyl("front_sight", g + d * (0.20 + blen) * S + up * 0.06 * S, g + d * (0.20 + blen) * S + up * 0.10 * S, 0.005 * S, M_gun, "RightHand", verts=6)
    if heavy:   # drum + bipod + carry handle
        # pan magazine ON TOP of the receiver (Degtyaryov-style): a drum underneath sat in the
        # path of the supporting forearm in every aim
        pc = g + d * 0.36 * S + up * 0.105 * S         # forward, clear of the cheek at the aim
        cyl("drum", pc - up * 0.018 * S, pc + up * 0.018 * S, 0.09 * S, M_gun, "RightHand", verts=32)
        cyl("drum_hub", pc + up * 0.018 * S, pc + up * 0.03 * S, 0.03 * S, M_gun, "RightHand", verts=16)
        for k in (-1, 1):
            cyl(f"bipod_{k}", g + d * (0.25 + blen * 0.8) * S + up * 0.04 * S, g + d * (0.32 + blen * 0.8) * S - up * 0.22 * S + sv * 0.07 * k * S, 0.006 * S, M_gun, "RightHand", verts=6)
        # (no carry handle: the pan sits where it was)
    else:       # the curved magazine: ONE piece, a box section swept down a forward-curving arc
        curved_mag("magazine", g + d * 0.16 * S + up * 0.012 * S, d, sv, up, M_gun, "RightHand", S)
    GUN["parts"] = [o for o, _ in props[n0:]]


def steel_helmet():
    c, rx, ry = skull_slice(brow_z + 0.01)
    M_helm = paint("helmet", (0.30, 0.34, 0.22), grime=0.35, seed=23)
    h = blob("helmet", (c.x, c.y + 0.008, brow_z - 0.005), 1.0, M_helm, "Head", scale=(rx + 0.03, ry + 0.035, crown - brow_z + 0.07), sub=4)
    import bmesh as _bm
    bm = _bm.new(); bm.from_mesh(h.data)
    _bm.ops.delete(bm, geom=[v for v in bm.verts if v.co.z < -0.12], context="VERTS")   # open dome (local coords, unit sphere)
    bm.to_mesh(h.data); bm.free()
    for v in h.data.vertices:            # flared skirt at the rim, SSh-68 style
        if v.co.z < 0.25:
            k = 1.0 + (0.25 - v.co.z) * 0.35
            v.co.x *= k; v.co.y *= k
    m = h.modifiers.new("solid", "SOLIDIFY"); m.thickness = 0.008
    bpy.context.view_layer.objects.active = h; bpy.ops.object.modifier_apply(modifier="solid")
    # chin strap: down each cheek in front of the ear and under the jaw, laid on the face by ray-casts
    dg = bpy.context.evaluated_depsgraph_get(); tree = BVHTree.FromObject(body, dg); mwb = body.matrix_world; inv = mwb.inverted()
    top = Vector((c.x, c.y - 0.035, brow_z - 0.012)); low = Vector((c.x, nose.y + 0.060, mouth_z - 0.075))
    D = (low - top).normalized(); mid = top + D * ((low - top).length * 0.55)
    pts = [Vector((c.x + rx + 0.022, top.y, top.z))]
    for k in range(15):
        th = math.radians(8 + 164 * k / 14)
        d = Vector((1, 0, 0)) * math.cos(th) + D * math.sin(th)
        hit = tree.ray_cast(inv @ (mid + d * 0.5), (inv.to_3x3() @ -d).normalized())
        if hit[0] is not None:
            pts.append(mwb @ hit[0] + d * 0.0045)
    pts.append(Vector((c.x - rx - 0.022, top.y, top.z)))
    for k in range(len(pts) - 1):
        st = cyl(f"chinstrap_{k}", pts[k] - (pts[k + 1] - pts[k]) * 0.08, pts[k + 1] + (pts[k + 1] - pts[k]) * 0.08, 0.0042, M_strap, "Head", verts=6)
    bz_ = pts[len(pts) // 2 - 3]                         # the buckle, on his left cheek
    box_basis("strap_buckle", bz_ + Vector((0.003, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1)), Vector((1, 0, 0)), (0.016, 0.014, 0.004), M_steel, "Head", bevel=0.001)


def bandolier():
    """Ammo belt slung from the right shoulder to the left hip. It is fitted to the torso by
    ray-casts and skinned from the body under it: the old one was a fixed-size hoop rigid on one
    spine bone, so it stood off his back and swung clear of him whenever he bent or twisted."""
    sp1 = rig.matrix_world @ rig.data.bones["Spine1"].head_local
    M_belt = paint("ammo_belt", (0.32, 0.26, 0.14), grime=0.4, seed=24)
    C = Vector((sp1.x, sp1.y - 0.01, sp1.z + 0.02))
    R = Matrix.Rotation(math.radians(35), 3, "Y")
    U, V, N = R @ Vector((1, 0, 0)), R @ Vector((0, 1, 0)), R @ Vector((0, 0, 1))
    dg = bpy.context.evaluated_depsgraph_get()
    trees = [(BVHTree.FromObject(o, dg), o.matrix_world.copy()) for o in meshes if "low-poly" not in o.name]
    NP = 64
    dirs = [U * math.cos(2 * math.pi * k / NP) + V * math.sin(2 * math.pi * k / NP) for k in range(NP)]
    rad = []
    for dv in dirs:                       # from inside the chest outward: the first wall of each layer he wears
        hits = []
        for t, mw in trees:
            inv = mw.inverted()
            h = t.ray_cast(inv @ C, (inv.to_3x3() @ dv).normalized())
            if h[0] is not None:
                hits.append(((mw @ h[0]) - C).length)
        hits = [x for x in hits if x <= min(hits) * 1.35 + 0.03] if hits else [0.16]    # (not an arm seen through a gap)
        rad.append(min(max(max(hits), 0.09), 0.32))
    for _ in range(3):
        rad = [(rad[k] * 2 + rad[k - 1] + rad[(k + 1) % NP]) / 4 for k in range(NP)]
    path = [C + dirs[k] * (rad[k] + 0.009) for k in range(NP)]
    # skin: each point takes the torso weights of the body under it
    from mathutils.kdtree import KDTree
    names = {g.index: g.name for g in body.vertex_groups}
    torso = lambda n: any(k in n for k in ("Hips", "Spine", "Neck", "Shoulder"))
    kd = KDTree(len(body.data.vertices))
    for v in body.data.vertices:
        kd.insert(body.matrix_world @ v.co, v.index)
    kd.balance()
    def weights(pt):
        acc = {}
        for _, idx, _d in kd.find_n(pt, 4):
            for g in body.data.vertices[idx].groups:
                n = names[g.group]
                if torso(n) and g.weight > 0.01: acc[n] = acc.get(n, 0.0) + g.weight
        tot = sum(acc.values())
        return {n: w / tot for n, w in acc.items()} if tot > 0 else {"Spine1": 1.0}
    bm = bmesh.new(); uvl = bm.loops.layers.uv.new(); SD = 8
    rings = []
    for k in range(NP):                   # a flat strap: 6 cm wide, lying on him
        tan = (path[(k + 1) % NP] - path[k - 1]).normalized(); o_ = (dirs[k] - tan * dirs[k].dot(tan)).normalized(); w_ = tan.cross(o_)
        rings.append([bm.verts.new(path[k] + o_ * (0.009 * math.cos(2 * math.pi * q / SD)) + w_ * (0.030 * math.sin(2 * math.pi * q / SD))) for q in range(SD)])
    for k in range(NP):
        for q in range(SD):
            f = bm.faces.new((rings[k][q], rings[k][(q + 1) % SD], rings[(k + 1) % NP][(q + 1) % SD], rings[(k + 1) % NP][q]))
            for lp, (u, v) in zip(f.loops, ((k, q), (k, q + 1), (k + 1, q + 1), (k + 1, q))):
                lp[uvl].uv = (u / NP * 8, v / SD)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new("bandolier"); bm.to_mesh(me); bm.free()
    t = bpy.data.objects.new("bandolier", me); bpy.context.collection.objects.link(t)
    me.polygons.foreach_set("use_smooth", [True] * len(me.polygons)); me.materials.append(M_belt)
    grp = {}
    for v in me.vertices:
        for n, w in weights(v.co).items():
            if n not in grp: grp[n] = t.vertex_groups.new(name=n)
            grp[n].add([v.index], w, "REPLACE")
    props.append((t, None))
    for k in range(NP):                   # a row of brass cartridges across the chest
        if dirs[k].y > -0.25:
            continue
        tan = (path[(k + 1) % NP] - path[k - 1]).normalized(); o_ = (dirs[k] - tan * dirs[k].dot(tan)).normalized(); w_ = tan.cross(o_)
        q0 = path[k] + o_ * 0.014
        wts = weights(q0)
        cyl(f"cartridge_{k}", q0 - w_ * 0.03, q0 + w_ * 0.03, 0.007, M_brass, max(wts, key=wts.get), verts=8)


def growths():
    M_tum = paint("growth", (0.62, 0.70, 0.45), grime=0.5, blood=3, seed=25)
    rnd = random.Random(9)
    sh = rig.matrix_world @ rig.data.bones["LeftShoulder"].tail_local
    for k in range(5):                    # clustered on the skull + one shoulder
        if k < 3:
            p = Vector((hc.x + rnd.uniform(-0.05, 0.06), hc.y + rnd.uniform(0.0, 0.05), crown - rnd.uniform(0.0, 0.05)))
            bone = "Head"
        else:
            p = sh + Vector((rnd.uniform(-0.04, 0.04), rnd.uniform(-0.02, 0.04), rnd.uniform(0.0, 0.05)))
            bone = "LeftShoulder"
        blob(f"growth_{k}", p, rnd.uniform(0.025, 0.045), M_tum, bone, scale=(1, 0.85, 0.8), sub=3, noise=0.18, seed=k)


def robe():
    """Hooded robe: a flared tube shaped by radial ray-casts round the body (chest down),
    plus a cowl moulded from the back/top of the head (as the gas-mask hood)."""
    dg = bpy.context.evaluated_depsgraph_get()
    trees = [(BVHTree.FromObject(o, dg), o.matrix_world.copy()) for o in meshes if "low-poly" not in o.name]
    def radius_at(cx, cy, z, ang):
        org = Vector((cx + math.cos(ang) * 1.0, cy + math.sin(ang) * 1.0, z)); dirn = Vector((-math.cos(ang), -math.sin(ang), 0))
        best = 0.08
        for t, mw in trees:
            inv = mw.inverted()
            h = t.ray_cast(inv @ org, (inv.to_3x3() @ dirn).normalized())
            if h[0] is not None:
                w = mw @ h[0]; r = math.hypot(w.x - cx, w.y - cy)
                best = max(best, r)
        return best
    bz = lambda n: (rig.matrix_world @ rig.data.bones[n].head_local)
    hips = bz("Hips"); top = min(bz("LeftArm").z, bz("RightArm").z) - 0.02; bot = 0.06   # the tube stops under the shoulders
    cx, cy = hips.x, hips.y
    NA, NZ = 56, 30
    R = [[0.0] * NA for _ in range(NZ + 1)]
    for j in range(NZ + 1):
        z = top + (bot - top) * j / NZ
        for i in range(NA):
            ang = 2 * math.pi * i / NA
            zz = max(z, hips.z - 0.05)
            r = min(radius_at(cx, cy, zz, ang), 0.24 if z > hips.z else 0.30) + 0.035   # torso only: the A-posed arms don't count
            if z < hips.z - 0.05:
                r += 0.06 + (hips.z - 0.05 - z) * 0.12       # gentle flare below the hips (legs swing inside)
            R[j][i] = r
    for _ in range(6):                    # smooth into cloth
        R = [[(R[j][i] * 2 + R[j][(i + 1) % NA] + R[j][i - 1] + (R[j - 1][i] if j > 0 else R[j][i]) + (R[j + 1][i] if j < NZ else R[j][i])) / 6
              for i in range(NA)] for j in range(NZ + 1)]
    bm = bmesh.new(); uvl = bm.loops.layers.uv.new()
    rings = []
    for j in range(NZ + 1):
        z = top + (bot - top) * j / NZ
        fall = (j / NZ) ** 1.3                       # folds deepen toward the hem; they only ever push OUT (legs swing inside)
        fold = [1 + fall * (0.034 * (0.5 + 0.5 * math.sin(7 * 2 * math.pi * i / NA + 0.6))
                            + 0.020 * (0.5 + 0.5 * math.sin(12 * 2 * math.pi * i / NA + 2.1))) for i in range(NA)]
        rings.append([bm.verts.new((cx + math.cos(2 * math.pi * i / NA) * R[j][i] * fold[i],
                                    cy + math.sin(2 * math.pi * i / NA) * R[j][i] * fold[i], z)) for i in range(NA)])
    for j in range(NZ):
        for i in range(NA):
            i2 = (i + 1) % NA
            f = bm.faces.new((rings[j][i], rings[j][i2], rings[j + 1][i2], rings[j + 1][i]))
            for lp, (u, w) in zip(f.loops, ((i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1))):
                lp[uvl].uv = (u / NA, (1 - w / NZ) * 0.88)
    # yoke: the cloth lies over the shoulders and climbs to a loose collar at the neck (the
    # tube used to end in a flat barrel top at chin height)
    def height_at(x, y, z0):
        best = None
        for t, mw in trees:
            inv = mw.inverted()
            h = t.ray_cast(inv @ Vector((x, y, z0)), (inv.to_3x3() @ Vector((0, 0, -1))).normalized())
            if h[0] is not None:
                wz = (mw @ h[0]).z; best = wz if best is None else max(best, wz)
        return best
    nk = bz("Neck"); zc = (nk.z + bz("Head").z) / 2; zcol = max(nk.z - 0.01, top + 0.05); MY = 7
    rn = [min(radius_at(nk.x, nk.y, nk.z + 0.02, 2 * math.pi * i / NA), 0.092) + 0.024 for i in range(NA)]   # clear of the jaw
    YR = [[0.0] * NA for _ in range(MY + 1)]; YZ = [[top] * NA for _ in range(MY + 1)]; YC = []
    for m in range(MY + 1):
        f = m / MY; YC.append((cx + (nk.x - cx) * f, cy + (nk.y - cy) * f))
        for i in range(NA):
            ang = 2 * math.pi * i / NA
            YR[m][i] = R[0][i] + (rn[i] - R[0][i]) * f
            hz = height_at(YC[m][0] + math.cos(ang) * YR[m][i], YC[m][1] + math.sin(ang) * YR[m][i], zc + 0.02)
            fr = max(0.0, -math.sin(ang)); zn = zcol - 0.035 * fr                         # the neckline dips under the chin
            z = top + (zn - top) * f ** 1.4
            fq = min(1.0, max(0.0, (fr - 0.3) / 0.5)) * f                                 # (under the chin the ray finds the jaw, not the chest)
            if m and hz is not None: z = max(z, min(hz + 0.030, (zcol + 0.035) * (1 - fq) + (zn + 0.004) * fq))
            YZ[m][i] = max(z, YZ[m - 1][i]) if m else top
    for _ in range(3):                    # cloth, not a cast of the shoulders
        YZ = [YZ[0]] + [[(YZ[m][i] * 2 + YZ[m][(i + 1) % NA] + YZ[m][i - 1]) / 4 for i in range(NA)] for m in range(1, MY + 1)]
    yoke = [[bm.verts.new((YC[m][0] + math.cos(2 * math.pi * i / NA) * YR[m][i], YC[m][1] + math.sin(2 * math.pi * i / NA) * YR[m][i], YZ[m][i]))
             for i in range(NA)] for m in range(1, MY + 1)]
    yoke.append([bm.verts.new((YC[MY][0] + math.cos(2 * math.pi * i / NA) * (rn[i] + 0.004), YC[MY][1] + math.sin(2 * math.pi * i / NA) * (rn[i] + 0.004),
                               YZ[MY][i] + 0.014)) for i in range(NA)])                  # a rolled neck hem
    seq = yoke[::-1] + [rings[0]]
    for k in range(len(seq) - 1):
        for i in range(NA):
            i2 = (i + 1) % NA
            f = bm.faces.new((seq[k][i], seq[k][i2], seq[k + 1][i2], seq[k + 1][i]))
            for lp, (u, w) in zip(f.loops, ((i, k), (i + 1, k), (i + 1, k + 1), (i, k + 1))):
                lp[uvl].uv = (u / NA, 1 - 0.12 * w / (len(seq) - 1))
    print(f"enemy: robe tube top {top:.3f}, collar {zcol:.3f}..{max(YZ[MY]) + 0.014:.3f}, neck r {min(rn):.3f}..{max(rn):.3f}")
    me = bpy.data.meshes.new("robe"); bm.to_mesh(me); bm.free()
    o = bpy.data.objects.new("robe", me); bpy.context.collection.objects.link(o)
    bpy.context.view_layer.objects.active = o
    m = o.modifiers.new("solid", "SOLIDIFY"); m.thickness = 0.01
    bpy.ops.object.modifier_apply(modifier="solid")
    o.data.polygons.foreach_set("use_smooth", [True] * len(o.data.polygons))   # (the operator only smooths SELECTED objects)
    o.data.materials.append(paint("robe", (0.07, 0.07, 0.08), grime=0.45, blood=4, seed=26))
    sp = bz("Spine").z; sp1 = bz("Spine1").z
    def drape(ob):                        # skin by height, so everything on the robe moves as the robe does
        g = {n: ob.vertex_groups.new(name=n) for n in ("Spine1", "Spine", "Hips")}
        for v in ob.data.vertices:
            z = v.co.z
            if z >= sp1: g["Spine1"].add([v.index], 1.0, "REPLACE")
            elif z >= sp:
                t = (z - sp) / (sp1 - sp); g["Spine1"].add([v.index], t, "REPLACE"); g["Spine"].add([v.index], 1 - t, "REPLACE")
            else:
                t = max(0.0, min(1.0, (z - hips.z) / (sp - hips.z))); g["Spine"].add([v.index], t, "REPLACE"); g["Hips"].add([v.index], 1 - t, "REPLACE")
        props.append((ob, None))
    drape(o)
    # cincture: a knotted rope round the waist, its two ends hanging down the front
    def surf(z, ang, out):                # a point `out` metres off the robe at height z, bearing ang
        fj = max(0.0, min(NZ - 1e-6, (top - z) / (top - bot) * NZ)); j = int(fj); fi = (ang % (2 * math.pi)) / (2 * math.pi) * NA
        i = int(fi) % NA; i2 = (i + 1) % NA; u = fi - int(fi); w = fj - j
        fall = (fj / NZ) ** 1.3
        fold = 1 + fall * (0.034 * (0.5 + 0.5 * math.sin(7 * ang + 0.6)) + 0.020 * (0.5 + 0.5 * math.sin(12 * ang + 2.1)))
        r = ((R[j][i] * (1 - u) + R[j][i2] * u) * (1 - w) + (R[j + 1][i] * (1 - u) + R[j + 1][i2] * u) * w) * fold + out
        return Vector((cx + math.cos(ang) * r, cy + math.sin(ang) * r, z))
    def tube(name, path, rad, closed, sides=8):
        vs, fs = [], []
        n = len(path)
        for k, c in enumerate(path):
            a, b = path[(k - 1) % n if closed else max(0, k - 1)], path[(k + 1) % n if closed else min(n - 1, k + 1)]
            tan = (b - a).normalized(); nx = tan.cross(Vector((0, 0, 1)))
            nx = nx.normalized() if nx.length > 1e-4 else Vector((1, 0, 0)); ny = tan.cross(nx)
            tw = k * 0.9                                                    # the lay of the rope
            for q in range(sides):
                an = 2 * math.pi * q / sides + tw
                vs.append(tuple(c + (nx * math.cos(an) + ny * math.sin(an)) * rad * (1.0 + 0.16 * math.cos(2 * (an - tw) + tw * 2))))
        for k in range(n if closed else n - 1):
            k2 = (k + 1) % n
            for q in range(sides):
                fs.append((k * sides + q, k * sides + (q + 1) % sides, k2 * sides + (q + 1) % sides, k2 * sides + q))
        me2 = bpy.data.meshes.new(name); me2.from_pydata(vs, [], fs)
        me2.polygons.foreach_set("use_smooth", [True] * len(me2.polygons))
        ob = bpy.data.objects.new(name, me2); bpy.context.collection.objects.link(ob)
        me2.materials.append(M_rope); return ob
    M_rope = paint("cincture_cloth", (0.46, 0.39, 0.27), grime=0.5, seed=27)
    zw = sp + 0.02; A0 = -math.pi / 2 + 0.22      # the knot sits just off-centre at the front
    sag = lambda ang: 0.022 * (0.5 - 0.5 * math.cos(ang - A0))               # rides higher at the back
    drape(tube("cincture", [surf(zw + sag(2 * math.pi * k / 72), 2 * math.pi * k / 72, 0.011) for k in range(72)], 0.011, True))
    for e, (da, ln) in enumerate(((-0.05, 0.34), (0.07, 0.27))):
        path = [surf(zw - ln * k / 9, A0 + da * (0.3 + k / 9), 0.012 + 0.008 * math.sin(math.pi * min(1, k / 3) / 2)) for k in range(10)]
        drape(tube(f"cincture_end{e}", path, 0.009, False))
        kb = blob(f"cincture_tassel{e}", path[-1], 0.016, M_rope, None, scale=(1, 1, 1.5), sub=2)
        props.pop(); bpy.context.view_layer.update()
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True); drape(kb)
    kn = blob("cincture_knot", surf(zw, A0, 0.016), 0.024, M_rope, None, scale=(1.2, 0.8, 1.0), sub=2, noise=0.12, seed=28)
    props.pop(); bpy.ops.object.transform_apply(location=True, rotation=True, scale=True); drape(kn)
    # cowl: a smooth hood shaped round the head -- open face, deep at the back, falling to
    # the shoulders (copying the scalp surface came out as a ragged frame)
    c, rx, ry = skull_slice(eye_c.z)
    hood = blob("cowl", (c.x, c.y + 0.03, (crown + mouth_z) / 2 - 0.01), 1.0, o.data.materials[0], "Head",
                scale=(rx + 0.05, ry + 0.07, (crown - mouth_z) / 2 + 0.09), sub=4)
    bmh = bmesh.new(); bmh.from_mesh(hood.data)
    fco, fno = Vector((0, -0.66, 0.45)), Vector((0, 1, 0.42)).normalized()      # the face opening: one slanted cut, so
    for co, no in ((fco, fno), (Vector((0, 0, -0.80)), Vector((0, 0, 1)))):       # the rim is an oval peaking over the brow
        bmesh.ops.bisect_plane(bmh, geom=bmh.verts[:] + bmh.edges[:] + bmh.faces[:], dist=1e-5,
                               plane_co=co, plane_no=no)
    gone = [f for f in bmh.faces if (f.calc_center_median() - fco).dot(fno) < 0
            or f.calc_center_median().z < -0.80]
    bmesh.ops.delete(bmh, geom=gone, context="FACES")
    bmh.to_mesh(hood.data); bmh.free()
    rnd = random.Random(5)
    for v in hood.data.vertices:          # soft cloth folds + the drape pooling at the back of the neck
        v.co *= 1.0 + 0.03 * math.sin(9 * math.atan2(v.co.y, v.co.x)) * max(0.0, -v.co.z)
        if v.co.z < -0.3 and v.co.y > 0: v.co.y += 0.12 * (-v.co.z - 0.3)
    mm = hood.modifiers.new("solid", "SOLIDIFY"); mm.thickness = 0.012
    bpy.context.view_layer.objects.active = hood; bpy.ops.object.modifier_apply(modifier="solid")
    hood.data.polygons.foreach_set("use_smooth", [True] * len(hood.data.polygons))


def iron_cross():
    """Priest's weapon: a heavy iron processional cross, gripped low on the shaft, swung as a club."""
    f, w = hand_frame("Right")
    M_iron = paint("cross_iron", (0.16, 0.15, 0.14), grime=0.45, seed=29)
    a, b = f - w * 0.10, f + w * 0.58
    cyl("cross_shaft", a, b, 0.016, M_iron, "RightHand", verts=10)
    cyl("cross_butt", a - w * 0.03, a, 0.022, M_iron, "RightHand", verts=10)
    side_v = w.cross(Vector((0, 0, 1)))
    if side_v.length < 1e-6:
        side_v = w.cross(Vector((1, 0, 0)))
    side_v.normalize()
    c = b - w * 0.08                                  # the crossbar, near the head
    box_basis("cross_bar", c, side_v, w, side_v.cross(w), (0.30, 0.035, 0.035), M_iron, "RightHand", bevel=0.004)
    box_basis("cross_head", b + w * 0.02, w, side_v, side_v.cross(w), (0.07, 0.05, 0.035), M_iron, "RightHand", bevel=0.004)
    for k in (-1, 1):                                 # flared ends on the bar
        box_basis(f"cross_tip_{k}", c + side_v * 0.15 * k, side_v, w, side_v.cross(w), (0.025, 0.06, 0.045), M_iron, "RightHand", bevel=0.004)


def lantern():
    f, w = hand_frame("Left")
    M_iron = paint("iron", (0.10, 0.10, 0.10), grime=0.4, seed=27)
    top = f + Vector((0, 0, -0.03)); body_c = top + Vector((0, 0, -0.17))
    cyl("lantern_bail", f + Vector((0, 0, 0.01)), top, 0.004, M_iron, "LeftHand", verts=6)
    cyl("lantern_cap", top, top + Vector((0, 0, -0.04)), 0.045, M_iron, "LeftHand", verts=16, r2=0.06)
    cyl("lantern_glass", top + Vector((0, 0, -0.04)), body_c + Vector((0, 0, -0.05)), 0.05, M_lens, "LeftHand", verts=16)
    cyl("lantern_base", body_c + Vector((0, 0, -0.05)), body_c + Vector((0, 0, -0.08)), 0.06, M_iron, "LeftHand", verts=16)
    for k in range(4):
        a = k * math.pi / 2
        o2 = Vector((math.cos(a) * 0.052, math.sin(a) * 0.052, 0))
        cyl(f"lantern_bar_{k}", top + Vector((0, 0, -0.04)) + o2, body_c + Vector((0, 0, -0.05)) + o2, 0.004, M_iron, "LeftHand", verts=6)


# ---------------------------------------------------------------- revenant + fiend props
def back_point(x, z):
    """Where a ray from behind (+Y, the enemy faces -Y) first meets the body or clothes."""
    dg = bpy.context.evaluated_depsgraph_get()
    best = None
    for o in meshes:
        if "low-poly" in o.name:
            continue
        inv = o.matrix_world.inverted()
        org = inv @ Vector((x, 1.5, z)); d = (inv.to_3x3() @ Vector((0, -1, 0))).normalized()
        ok, loc, nrm, _ = o.evaluated_get(dg).ray_cast(org, d)
        if ok:
            w = o.matrix_world @ loc
            if best is None or w.y > best.y:
                best = w
    return best


def fire_helmet():
    """Soviet fire-brigade helmet: red-lacquered steel dome, brass comb front to back,
    leather neck curtain."""
    c, rx, ry = skull_slice(brow_z + 0.01)
    M_lac = paint("fire_helmet", (0.48, 0.07, 0.04), grime=0.55, seed=41)
    sx, sy, sz = rx + 0.03, ry + 0.035, crown - brow_z + 0.07
    h = blob("fire_helmet", (c.x, c.y + 0.008, brow_z - 0.005), 1.0, M_lac, "Head", scale=(sx, sy, sz), sub=4)
    bm = bmesh.new(); bm.from_mesh(h.data)
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.co.z < -0.10], context="VERTS")
    bm.to_mesh(h.data); bm.free()
    for v in h.data.vertices:            # short brim, longer at the back
        if v.co.z < 0.2:
            k = 1.0 + (0.2 - v.co.z) * (0.5 if v.co.y > 0 else 0.3)
            v.co.x *= k; v.co.y *= k
    m = h.modifiers.new("solid", "SOLIDIFY"); m.thickness = 0.008
    bpy.context.view_layer.objects.active = h; bpy.ops.object.modifier_apply(modifier="solid")
    o = Vector((c.x, c.y + 0.008, brow_z - 0.005))
    pts = [o + Vector((0, sy * math.sin(a) * 1.04, sz * math.cos(a) * 1.04 + 0.006))
           for a in (math.radians(d) for d in range(-75, 80, 10))]
    # the brass comb: a solid fin riding the dome front to back
    vs, fs = [], []
    for i, q in enumerate(pts):
        nrm = (q - o); nrm.x = 0; nrm.normalize()
        vs += [tuple(q - nrm * 0.012), tuple(q + nrm * 0.022)]
        if i:
            fs.append((2 * i - 2, 2 * i - 1, 2 * i + 1, 2 * i))
    me = bpy.data.meshes.new("comb"); me.from_pydata(vs, [], fs)
    cb = bpy.data.objects.new("comb", me); bpy.context.collection.objects.link(cb)
    bpy.context.view_layer.objects.active = cb
    m = cb.modifiers.new("solid", "SOLIDIFY"); m.thickness = 0.008; m.offset = 0; bpy.ops.object.modifier_apply(modifier="solid")
    m = cb.modifiers.new("bevel", "BEVEL"); m.width = 0.002; m.segments = 2; bpy.ops.object.modifier_apply(modifier="bevel")
    me.materials.append(M_brass); props.append((cb, "Head"))
    cyl("badge", o + Vector((0, -sy * 0.93, sz * 0.38)), o + Vector((0, -sy * 0.99, sz * 0.40)), 0.022, M_brass, "Head", verts=16)
    # neck curtain: leather arc behind the head, flaring down to the collar
    M_leather = paint("leather", (0.16, 0.11, 0.07), grime=0.5, seed=42)
    vs, fs, R, Z = [], [], 14, 5
    z_top, z_bot = brow_z - 0.02, mouth_z - 0.075
    for j in range(Z):
        t = j / (Z - 1); z = z_top + (z_bot - z_top) * t; fl = 1.0 + 0.35 * t
        for i in range(R):
            a = math.radians(35 + 110 * i / (R - 1))
            vs.append((c.x + math.cos(a) * sx * fl, c.y + 0.008 + math.sin(a) * sy * fl, z))
    for j in range(Z - 1):
        for i in range(R - 1):
            fs.append((j * R + i, j * R + i + 1, (j + 1) * R + i + 1, (j + 1) * R + i))
    me = bpy.data.meshes.new("curtain"); me.from_pydata(vs, [], fs)
    cur = bpy.data.objects.new("curtain", me); bpy.context.collection.objects.link(cur)
    bpy.context.view_layer.objects.active = cur
    m = cur.modifiers.new("solid", "SOLIDIFY"); m.thickness = 0.006; bpy.ops.object.modifier_apply(modifier="solid")
    cur.data.polygons.foreach_set("use_smooth", [True] * len(cur.data.polygons))
    me.materials.append(M_leather); props.append((cur, "Head"))


def fire_axe():
    """Pick-head fire axe: wood haft, red head, ground steel edge."""
    fR, wR = hand_frame("Right")
    side_v = wR.cross(Vector((0, 0, 1))).normalized()
    e = wR.cross(side_v).normalized()                 # the chop direction (edge points this way)
    if e.z < 0:
        e = -e
    a, b = fR - wR * 0.14, fR + wR * 0.72
    cyl("axe_haft", a, b + wR * 0.05, 0.019, M_wood, "RightHand", verts=10, r2=0.016)
    cyl("axe_knob", a - wR * 0.03, a, 0.024, M_wood, "RightHand", verts=10)
    M_axe = paint("axe_head", (0.60, 0.07, 0.04), grime=0.4, seed=43)
    hd = box_basis("axe_head", b + e * 0.09, e, wR, side_v, (0.22, 0.09, 0.026), M_axe, "RightHand")
    for v in hd.data.vertices:           # wedge to the edge, edge flares along the haft
        t = v.co.dot(e) / 0.11
        if t > 0:
            s_off = v.co.dot(side_v); v.co -= side_v * s_off * min(0.85, t * 0.85)
            h_off = v.co.dot(wR); v.co += wR * h_off * t * 0.9
    ed = box_basis("axe_edge", b + e * 0.196, e, wR, side_v, (0.014, 0.17, 0.007), M_steel, "RightHand", bevel=0.001)
    cyl("axe_pick", b - e * 0.01, b - e * 0.20, 0.024, M_axe, "RightHand", verts=10, r2=0.003)


def air_tank():
    """Breathing-apparatus cylinder strapped high on the back."""
    sp1 = rig.matrix_world @ rig.data.bones["Spine1"].head_local
    z0, z1 = sp1.z - 0.18, sp1.z + 0.26
    p = back_point(sp1.x, (z0 + z1) / 2) or (sp1 + Vector((0, 0.15, 0)))
    M_tank = paint("air_tank", (0.55, 0.53, 0.48), grime=0.55, seed=44)
    ax = Vector((p.x, p.y + 0.085, 0))
    cyl("tank", ax + Vector((0, 0, z0)), ax + Vector((0, 0, z1)), 0.075, M_tank, "Spine1", verts=20)
    blob("tank_dome", ax + Vector((0, 0, z1)), 0.075, M_tank, "Spine1", scale=(1, 1, 0.55), sub=3)
    cyl("tank_valve", ax + Vector((0, 0, z1 + 0.035)), ax + Vector((0, 0, z1 + 0.08)), 0.018, M_brass, "Spine1", verts=10)
    cyl("tank_wheel", ax + Vector((0, 0, z1 + 0.07)), ax + Vector((0, 0, z1 + 0.085)), 0.032, M_dark, "Spine1", verts=12)
    for z in (z0 + 0.08, z1 - 0.08):      # straps
        bpy.ops.mesh.primitive_torus_add(major_radius=0.079, minor_radius=0.008, location=ax + Vector((0, 0, z)))
        st = bpy.context.active_object; st.name = "tank_strap"; st.data.materials.append(M_dark); props.append((st, "Spine1"))


TENT_N, TENT_SEG = 4, 6
TENT_BONES = []                           # [[bone names root->tip] per tentacle]


def tentacles():
    """Four tentacles burst out of the back. Each is a real 6-bone chain added to the rig
    (31 + 24 bones <= the game's 64-bone skin shader), smooth-weighted along its length,
    so the clips can make them writhe (tentacle_motion below)."""
    sp = rig.matrix_world @ rig.data.bones["Spine1"].head_local
    sp2 = rig.matrix_world @ rig.data.bones["Spine1"].tail_local
    roots = [(+0.09, sp2.z - 0.05, "Spine1", (0.9, 0.7)), (-0.09, sp2.z - 0.05, "Spine1", (-0.9, 0.7)),
             (+0.07, sp.z - 0.02, "Spine", (0.7, -0.1)), (-0.07, sp.z - 0.02, "Spine", (-0.7, -0.1))]
    chains = []
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode="EDIT")
    eb = rig.data.edit_bones
    inv = rig.matrix_world.inverted()
    for ti, (x, z, parent, (sx, up)) in enumerate(roots):
        p = back_point(sp.x + x, z) or Vector((sp.x + x, sp.y + 0.12, z))
        p = p - Vector((0, 0.03, 0))      # start just inside the coverall
        d = Vector((sx * 0.55, 1.0, up)).normalized()
        L = 0.20 if ti < 2 else 0.16
        pts = [p]
        names = []
        for k in range(TENT_SEG):
            d = (Matrix.Rotation(math.radians(-14 if ti < 2 else 10), 3, Vector((1, 0, 0))) @ d).normalized()   # arch over / droop
            pts.append(pts[-1] + d * L * (1.0 - 0.07 * k))
        for k in range(TENT_SEG):
            nm = f"Tent{ti}_{k}"
            b = eb.new(nm); b.head = inv @ pts[k]; b.tail = inv @ pts[k + 1]
            b.parent = eb[parent] if k == 0 else eb[names[-1]]
            b.use_connect = k > 0; b.use_deform = True
            names.append(nm)
        TENT_BONES.append(names); chains.append(pts)
    bpy.ops.object.mode_set(mode="OBJECT")
    M_flesh = paint("tentacle", (0.46, 0.20, 0.24), grime=0.55, blood=6, drops=10, seed=45)
    RING, PER = 14, 6
    for ti, pts in enumerate(chains):
        vs, fs, uvs, wts = [], [], [], []
        nseg = len(pts) - 1
        rows = nseg * PER + 1
        tot = sum((pts[i + 1] - pts[i]).length for i in range(nseg))
        acc = 0.0
        prev_n = None
        for r in range(rows):
            k = min(nseg - 1, r // PER); f = (r - k * PER) / PER if r < rows - 1 else 1.0
            c = pts[k].lerp(pts[k + 1], f)
            t = (k + f) / nseg
            tan = (pts[k + 1] - pts[k]).normalized()
            n = prev_n if prev_n is not None else tan.orthogonal().normalized()
            n = (n - tan * n.dot(tan)).normalized(); prev_n = n; bnorm = tan.cross(n)
            rad = (0.072 if ti < 2 else 0.058) * (1 - t) ** 0.75 + 0.007
            rad *= 1.0 + 0.10 * math.sin(t * 38)          # ringed, segmented flesh
            for i in range(RING):
                a = 2 * math.pi * i / RING
                bump = 1.0 + (0.18 if (i in (0, 1) and r % 2 == 0) else 0.0)   # sucker row on the underside
                vs.append(c + (n * math.cos(a) + bnorm * math.sin(a)) * rad * bump)
                uvs.append((i / RING, t * 4.0))
                # blend between the bones whose centres bracket this ring
                u = (k + f) - 0.5
                k0 = max(0, min(nseg - 1, math.floor(u))); k1 = min(nseg - 1, k0 + 1); w1 = max(0.0, min(1.0, u - k0))
                wts.append(((k0, 1 - w1), (k1, w1)))
        for r in range(rows - 1):
            for i in range(RING):
                j = (i + 1) % RING
                fs.append((r * RING + i, r * RING + j, (r + 1) * RING + j, (r + 1) * RING + i))
        tip = len(vs); vs.append(pts[-1] + (pts[-1] - pts[-2]).normalized() * 0.02); uvs.append((0.5, 4.0)); wts.append(((nseg - 1, 1.0),))
        for i in range(RING):
            fs.append(((rows - 1) * RING + i, (rows - 1) * RING + (i + 1) % RING, tip))
        me = bpy.data.meshes.new(f"tentacle_{ti}"); me.from_pydata([tuple(v) for v in vs], [], fs)
        uvl = me.uv_layers.new(name="UVMap")
        for poly in me.polygons:
            for li in poly.loop_indices:
                uvl.data[li].uv = uvs[me.loops[li].vertex_index]
        ob = bpy.data.objects.new(f"tentacle_{ti}", me); bpy.context.collection.objects.link(ob)
        groups = [ob.vertex_groups.new(name=nm) for nm in TENT_BONES[ti]]
        for vi, ws in enumerate(wts):
            for (k, w) in ws:
                if w > 0:
                    groups[k].add([vi], w, "ADD")
        me.polygons.foreach_set("use_smooth", [True] * len(me.polygons))
        me.materials.append(M_flesh); props.append((ob, None))
        # torn-open wound where it bursts out of the back
        blob(f"wound_{ti}", pts[0] + (pts[1] - pts[0]) * 0.2, 0.07 if ti < 2 else 0.06, M_flesh, roots[ti][2],
             scale=(1.0, 0.55, 0.85), sub=3, noise=0.22, seed=50 + ti)


PROPS_EXTRA = {
    "revenant": lambda: (fire_helmet(), fire_axe(), air_tank()),
    "fiend": lambda: (gas_mask(), growths(), tentacles()),
}


PROPS = {
    "rifleman": lambda: (steel_helmet(), respirator(), rifle()),
    "gunner": lambda: (steel_helmet(), bandolier(), rifle(1.15, heavy=True)),
    "mutant": lambda: (growths(),),
    "priest": lambda: (robe(), lantern(), iron_cross()),
    "cook": lambda: (toque(), respirator(), cleaver("Right", "RightHand"), meat_hook("Left", "LeftHand")),
    "linecook": lambda: (kitchen_cap(), respirator(), knife("Right", "RightHand")),
    "butcher": lambda: (apron(None, None, 0.25, paint("rubber_apron", (0.22, 0.10, 0.08), grime=0.35, blood=10, drops=14, seed=9), bib=True),
                        cleaver("Right", "RightHand", 1.35), meat_hook("Left", "LeftHand", 1.3)),
    "liquidator": lambda: (gas_mask(), shovel()),
    "headchef": lambda: (toque(0.20, 1.25), respirator(), neckerchief(), jacket_buttons(),
                         cleaver("Right", "RightHand", 1.25), cleaver("Left", "LeftHand", 1.25)),
}
PROPS.update(PROPS_EXTRA)
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

if os.environ.get("ENEMY_LOOK"):     # dev: look-dev stills of the rest pose (skin, clothes, props), then stop -- no mocap
    LK = os.environ["ENEMY_LOOK"]; os.makedirs(LK, exist_ok=True)
    scene.render.engine = "BLENDER_EEVEE"
    w = bpy.data.worlds.new("w"); scene.world = w; w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = (0.16, 0.16, 0.18, 1)
    w.node_tree.nodes["Background"].inputs[1].default_value = 0.7
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); sun.data.energy = 3.5
    sun.rotation_euler = (math.radians(50), 0, math.radians(30)); scene.collection.objects.link(sun)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); scene.collection.objects.link(cam); scene.camera = cam
    ez = eye_c.z
    for nm, loc, rot, lens, res in (("face", (0.0, -0.95, ez - 0.01), (90, 0, 0), 70, (640, 720)),
                                    ("face34", (0.62, -0.72, ez), (90, 0, 40.7), 70, (640, 720)),
                                    ("headside", (0.95, 0.02, ez + 0.01), (90, 0, 90), 70, (640, 720)),
                                    ("headback", (-0.45, 0.85, ez + 0.02), (90, 0, 208), 70, (640, 720)),
                                    ("front", (0.0, -4.6, 0.95), (90, 0, 0), 60, (760, 900)),
                                    ("back", (0.0, 4.6, 0.95), (90, 0, 180), 60, (760, 900)),
                                    ("arm", (0.75, -1.1, ez - 0.45), (90, 0, 10), 60, (640, 720))):
        if os.environ.get("ENEMY_LOOK_VIEWS") and nm not in os.environ["ENEMY_LOOK_VIEWS"].split(","):
            continue
        cam.location = loc; cam.rotation_euler = tuple(math.radians(a) for a in rot); cam.data.lens = lens
        scene.render.resolution_x, scene.render.resolution_y = res
        scene.render.filepath = os.path.join(LK, f"{ENEMY}_{nm}.png")
        bpy.ops.render.render(write_still=True)
    print("enemy: look stills in", LK)
    sys.exit(0)

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


def sample(src, frames, loop, yaw_fix=True, face=None):
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
    if face == "release":   # throws: aim the fastest hand's release, not the (side-on) hips
        best, prev = (0.0, None), {}
        for f in frames:
            scene.frame_set(f)
            for hb in ("LeftHand", "RightHand"):
                p = (src.matrix_world @ src.pose.bones[hb].head).copy()
                if hb in prev:
                    v = p - prev[hb]; v.z = 0
                    if v.length > best[0]:
                        best = (v.length, v.normalized())
                prev[hb] = p
        hs = [best[1]]
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
    global LAST_TRAVEL      # how far the take really went: a loop's own ground speed (take() records it)
    LAST_TRAVEL = math.hypot(last.x - first.x, last.y - first.y)
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
LAST_TRAVEL = 0.0
GAIT_MPS = {}       # role -> m/s the loop walks (the game plays loops at 30 keys/s, wrapping after the last)
def take(role):
    bvh, d0, d1, loop, xf, *face = SPEC["clips"][role]
    src = import_bvh(bvh)
    poses = fist(sample(src, bvh_frames(src, d0, d1), loop=loop, yaw_fix=loop, face=(face or [None])[0]))
    if loop and role != "idle":
        GAIT_MPS[role] = LAST_TRAVEL * 30.0 / len(poses)
    bpy.data.objects.remove(src, do_unlink=True)
    return crossfade(poses, xf) if xf else poses
idle, walk, run, attack, death = (take(r) for r in ("idle", "walk", "run", "attack", "death"))
if SPEC.get("attack_in_place"):     # the throw stepped ~1.7 m sideways, then the model snapped back
    RH = Rt0["Hips"]                 # hips location is stored in the hips' rest frame
    w0 = RH @ attack[0]["Hips"][1]
    for p in attack:
        w = RH @ p["Hips"][1]
        p["Hips"][1] = RH.inverted() @ Vector((w0.x, w0.y, w.z))   # keep the bob, drop the travel

# attack_synth="grab" (fiend): no weapon to swing, so a procedural lunge -- lean back and
# draw the arms in (wind-up), then lunge, both arms reaching out with clawed hands at the
# strike (~0.45, matching ATK_STRIKE_FRAC and the tentacle whip), hold, recover.
def world_rot(bn, axis, deg):
    return Quaternion(Rt0[bn].inverted() @ Vector(axis), math.radians(deg))


def curl_quats(side, angles):
    """Finger-bone rotations curling `side`'s hand toward its palm by `angles` (degrees per
    FingerBase / HandFinger1 / Thumb). The mocap has NO finger channels: without these keys
    every hand stayed flat and splayed at the rest pose."""
    n = hand_basis(side)["n"]; out = {}
    for key, deg in angles.items():
        bn = f"{side}{key}" if key != "Thumb" else f"{side[0]}Thumb"
        if bn not in tbones:
            continue
        d = (rig.matrix_world @ tbones[bn].tail_local) - (rig.matrix_world @ tbones[bn].head_local)
        ax = d.cross(n)                                      # about d x n the tip moves palm-ward
        if ax.length > 1e-9:
            out[bn] = Quaternion(Rt0[bn].inverted() @ ax.normalized(), math.radians(deg))
    return out


RELAX = {"FingerBase": 6, "HandFinger1": 32, "Thumb": 12}    # an empty hand at rest: softly curled


def synth_grab(base, n=45):
    out = []
    for i in range(n):
        t = i / (n - 1)
        wind = math.sin(math.pi * min(1.0, t / 0.32)) if t < 0.32 else 0.0
        reach = 0.0 if t < 0.28 else (min(1.0, (t - 0.28) / 0.16) if t < 0.62 else max(0.0, 1.0 - (t - 0.62) / 0.38))
        reach = reach * reach * (3 - 2 * reach)                       # smoothstep
        src = base[i % len(base)]
        p = {b: [q.copy(), (l.copy() if l is not None else None)] for b, (q, l) in src.items()}
        lean = -10 * wind + 26 * reach                                # torso: rock back, then lunge
        for bn, k in (("Spine", 0.35), ("Spine1", 0.45), ("Spine2", 0.20)):
            if bn in p:
                p[bn][0] = p[bn][0] @ Quaternion((1, 0, 0), math.radians(lean * k))
        if "Neck" in p:
            p["Neck"][0] = p["Neck"][0] @ Quaternion((1, 0, 0), math.radians(-14 * reach))   # eyes stay on the target
        for side, sx in (("Left", 1), ("Right", -1)):
            arm, fore, hand = f"{side}Arm", f"{side}ForeArm", f"{side}Hand"
            if arm in p:   # wind-up: elbows back; strike: arms swing forward + up, slightly inward
                p[arm][0] = p[arm][0] @ world_rot(arm, (1, 0, 0), 28 * wind - 102 * reach) @ world_rot(arm, (0, 1, 0), sx * 14 * reach)
            if fore in p:
                p[fore][0] = p[fore][0] @ world_rot(fore, (1, 0, 0), -40 * wind + 25 * reach)
            if hand in p:  # clawed: wrists cocked back as the hands open to grab
                p[hand][0] = p[hand][0] @ world_rot(hand, (1, 0, 0), 30 * reach)
        p["Hips"][1] = p["Hips"][1] + Vector((0, -0.10 * reach + 0.03 * wind, -0.03 * reach))   # step into it
        # claws: splayed wide as the arms come forward, SNAP shut at the strike (the grab reads)
        close = 0.0 if t < 0.40 else min(1.0, (t - 0.40) / 0.06)
        close *= 1.0 if t < 0.70 else max(0.0, 1.0 - (t - 0.70) / 0.25)
        for side in ("Left", "Right"):
            for bn, q in curl_quats(side, {"FingerBase": -4 + 14 * close, "HandFinger1": 8 + 95 * close,
                                           "Thumb": 5 + 40 * close}).items():
                p[bn] = [q, None]
        out.append(p)
    return out


def fk_world(p, bn):
    """World rotation of bone bn in pose p (same convention as sample(): W = Wpar @ L0 @ Q)."""
    Q = p[bn][0].to_matrix()
    par = tbones[bn].parent
    if par is None:
        return Rt0[bn] @ Q
    return fk_world(p, par.name) @ (Rt0[par.name].inverted() @ Rt0[bn]) @ Q


def turn_world(p, bn, axis, deg):
    """Rotate bone bn by deg about a WORLD axis, from where the pose has it now (children follow)."""
    par = tbones[bn].parent
    B = (fk_world(p, par.name) @ (Rt0[par.name].inverted() @ Rt0[bn])) if par else Rt0[bn]
    R = Matrix.Rotation(math.radians(deg), 3, Vector(axis))
    p[bn][0] = (B.inverted() @ R @ B @ p[bn][0].to_matrix()).to_quaternion()


def _swing(t, t0, t_hit, t1):
    """0 before t0, wind-up 0->1 by t_hit-0.08, chop back through to -1 at t_hit, recover to 0 by t1."""
    if t <= t0 or t >= t1:
        return 0.0, 0.0
    tw = t_hit - 0.10
    if t < tw:                                   # wind-up: raise
        k = (t - t0) / (tw - t0); return k * k * (3 - 2 * k), 0.0
    if t < t_hit:                                # the chop: fast
        k = (t - tw) / (t_hit - tw); return 1.0 - k, k
    k = (t - t_hit) / (t1 - t_hit)               # follow-through, then settle
    return 0.0, 1.0 - k * k * (3 - 2 * k)


def synth_rake(base, n=45):
    """Fiend: a two-handed overhead claw rake. Both arms rise high (claws splayed, body rearing
    back), then rake down through the front as he lunges -- landing at 0.45 (ATK_STRIKE_FRAC) --
    claws snapping shut, then recover. The tentacles whip forward with it (tentacle_motion)."""
    out = []
    for i in range(n):
        t = i / (n - 1)
        src = base[i % len(base)]
        p = {b: [q.copy(), (l.copy() if l is not None else None)] for b, (q, l) in src.items()}
        up, down = _swing(t, 0.02, 0.45, 0.92)
        for bn, k in (("Spine", 0.4), ("Spine1", 0.6)):          # rear back, then hunch into the rake
            turn_world(p, bn, (1, 0, 0), (-14 * up + 26 * down) * k)
        for side, sx in (("Left", 1), ("Right", -1)):
            arm, fore, hand = f"{side}Arm", f"{side}ForeArm", f"{side}Hand"
            turn_world(p, arm, (1, 0, 0), -(160 * up + 60 * down))      # up over the head, down through the front
            turn_world(p, arm, (0, 1, 0), sx * (25 * up + 10 * down))   # spread wide on the way up
            turn_world(p, fore, (1, 0, 0), -45 * up + 10 * down)
            turn_world(p, hand, (1, 0, 0), -35 * up + 25 * down)        # claws cocked back, then flicked
            close = min(1.0, down * 1.6)
            for bn, q in curl_quats(side, {"FingerBase": -6 + 16 * close, "HandFinger1": 5 + 85 * close,
                                           "Thumb": 4 + 36 * close}).items():
                p[bn] = [q, None]
        p["Hips"][1] = p["Hips"][1] + Vector((0, -0.12 * down + 0.04 * up, -0.04 * down))   # lunge
        out.append(p)
    return out


def synth_dual(base, n=60):
    """Two-weapon attack: right-hand overhand chop (lands at 0.28), then the left (0.78).
    Each arm raises high and behind the head, then chops down across the body while the
    torso twists into it. DUAL_STRIKES in main.c lands damage at those two moments."""
    out = []
    for i in range(n):
        t = i / (n - 1)
        src = base[i % len(base)]
        p = {b: [q.copy(), (l.copy() if l is not None else None)] for b, (q, l) in src.items()}
        for side, sx, (t0, th, t1) in (("Right", -1, (0.00, 0.28, 0.50)), ("Left", 1, (0.50, 0.78, 1.00))):
            up, down = _swing(t, t0, th, t1)
            arm, fore, hand = f"{side}Arm", f"{side}ForeArm", f"{side}Hand"
            for bn, k in (("Spine", 0.4), ("Spine1", 0.6)):     # twist into the blow + a forward drop
                turn_world(p, bn, (0, 0, 1), sx * (-16 * up + 22 * down) * k)
                turn_world(p, bn, (1, 0, 0), (-8 * up + 18 * down) * k)
            # (model faces -Y) raise: arm swings forward and up over the head, slightly back;
            # chop: down through the front to waist height. Elbow cocks the blade behind the head.
            turn_world(p, arm, (1, 0, 0), -(185 * up + 70 * down))
            turn_world(p, arm, (0, 1, 0), sx * (10 * up - 14 * down))
            turn_world(p, fore, (1, 0, 0), -75 * up + 5 * down)
            turn_world(p, hand, (1, 0, 0), -20 * up + 30 * down)
            p["Hips"][1] = p["Hips"][1] + Vector((0, -0.05 * down, 0))
        out.append(p)
    return out


# hit: the idle pose with a sharp recoil layered on (no clean CMU flinch exists). The arms
# are turned about WORLD axes from wherever the idle has them (a local-axis twist drove some
# rigs' arms back into the torso): they fly out to the sides and a little forward, elbows
# bending, as the torso and head snap back.
def synth_hit(base, n=14):
    out = []
    for i in range(n):
        t = i / (n - 1)
        env = math.sin(math.pi * min(1.0, t * 1.6)) * (1.0 - t * 0.35)
        p = {b: [q.copy(), (l.copy() if l is not None else None)] for b, (q, l) in base[0].items()}
        for bn, deg in (("Spine1", -16), ("Spine", -8), ("Neck", -14), ("Head", -18)):
            p[bn][0] = p[bn][0] @ Quaternion((1, 0, 0), math.radians(deg * env))
        for side, sx in (("Left", -1), ("Right", 1)):      # model faces -Y; its left is +X
            arm, fore = f"{side}Arm", f"{side}ForeArm"
            turn_world(p, arm, (0, 1, 0), sx * 28 * env)    # out to the side
            turn_world(p, arm, (1, 0, 0), -18 * env)        # and forward
            turn_world(p, fore, (1, 0, 0), -22 * env)       # elbows bend
        p["Hips"][1] = p["Hips"][1] + Vector((0, -0.04 * env, 0))   # knocked back
        out.append(p)
    return out


hit = synth_hit(idle)


# body-part flinches (the game picks one from where the shot landed; "hit" above = centre mass).
# All layered on the idle's first pose about WORLD axes; model faces -Y, its left is +X.
#   +X rotation bends the upper body FORWARD; +Z turns its left shoulder BACK.
def _flinch_env(t, attack=1.6):
    return math.sin(math.pi * min(1.0, t * attack)) * (1.0 - t * 0.35)


def synth_hit_zone(base, zone, n=14):
    # both fists on one shaft in the flinches (liquidator's shovel): the arms ride the body --
    # flinging one out would tear the other hand off the handle
    two_hand = SPEC.get("two_hand") == "shaft" and any(c.startswith("hit") for c in SPEC.get("two_hand_clips", ("hit",)))
    af = 0.0 if two_hand else 1.0
    out = []
    for i in range(n):
        t = i / (n - 1)
        p = {b: [q.copy(), (l.copy() if l is not None else None)] for b, (q, l) in base[0].items()}
        if zone == "head":                  # the head snaps back, the body follows a little
            e = _flinch_env(t, 2.4)         # sharper
            for bn, deg in (("Spine1", -9), ("Neck", -16), ("Head", -34)):
                turn_world(p, bn, (1, 0, 0), deg * e)
            turn_world(p, "Head", (0, 1, 0), 9 * e)                 # and twists off the impact
            for side, sx in (("Left", -1), ("Right", 1)):
                turn_world(p, f"{side}Arm", (0, 1, 0), af * (sx * 14 * e))
                turn_world(p, f"{side}ForeArm", (1, 0, 0), af * (-16 * e))
            p["Hips"][1] = p["Hips"][1] + Vector((0, -0.025 * e, 0))
        elif zone in ("left", "right"):     # spun by a shot to one side: that shoulder goes back,
            e = _flinch_env(t)              # the body leans away, that arm flings out
            k = 1 if zone == "left" else -1                         # +1: his left (+X) side was hit
            for bn, deg in (("Spine", 9), ("Spine1", 18), ("Neck", 6)):
                turn_world(p, bn, (0, 0, 1), k * deg * e)           # hit shoulder back
            for bn, deg in (("Spine", 5), ("Spine1", 7)):
                turn_world(p, bn, (0, 1, 0), k * deg * e)           # lean away from the hit
            turn_world(p, "Head", (0, 1, 0), -k * 10 * e)
            side = "Left" if k > 0 else "Right"; sx = -1 if k > 0 else 1
            turn_world(p, f"{side}Arm", (0, 1, 0), af * (sx * 34 * e))     # the hit arm flies out...
            turn_world(p, f"{side}Arm", (0, 0, 1), af * (k * 18 * e))      # ...and back
            turn_world(p, f"{side}ForeArm", (1, 0, 0), af * (-20 * e))
            other = "Right" if k > 0 else "Left"
            turn_world(p, f"{other}Arm", (1, 0, 0), af * (-12 * e))        # the other arm comes up a little
            p["Hips"][1] = p["Hips"][1] + Vector((0, -0.02 * e, 0))
        elif zone == "leg":                 # the left leg is knocked out: knee buckles, he pitches
            e = _flinch_env(t, 1.4)         # forward and drops, arms out for balance, then rights himself
            turn_world(p, "LeftUpLeg", (1, 0, 0), -22 * e)          # thigh forward
            turn_world(p, "LeftLeg", (1, 0, 0), 48 * e)             # knee buckles
            turn_world(p, "RightUpLeg", (1, 0, 0), -10 * e)         # the standing leg gives a little too
            turn_world(p, "RightLeg", (1, 0, 0), 20 * e)
            for bn, deg in (("Spine", 10), ("Spine1", 14)):
                turn_world(p, bn, (1, 0, 0), deg * e)               # pitch forward
            turn_world(p, "Spine1", (0, 1, 0), 6 * e)               # dip toward the hurt leg
            for side, sx in (("Left", -1), ("Right", 1)):
                turn_world(p, f"{side}Arm", (0, 1, 0), af * (sx * 30 * e)) # arms out for balance
                turn_world(p, f"{side}ForeArm", (1, 0, 0), af * (-14 * e))
            p["Hips"][1] = p["Hips"][1] + Vector((0, 0, -0.07 * e))  # drops
        out.append(p)
    return out


HITX = {f"hit_{z}": synth_hit_zone(idle, z) for z in ("head", "left", "right", "leg")}


def _rot_root(poses, Rz, shift):
    """Turn whole poses about world Z by Rz (3x3) and shift the hips (world offset), per frame lists."""
    RH = Rt0["Hips"]
    for i, p in enumerate(poses):
        R = Rz[i] if isinstance(Rz, list) else Rz
        q = p["Hips"][0].to_matrix()
        p["Hips"][0] = (RH.inverted() @ R @ RH @ q).to_quaternion()
        w = RH @ p["Hips"][1]
        sh = shift[i] if isinstance(shift, list) else shift
        p["Hips"][1] = RH.inverted() @ (R @ w + sh)
    return poses


def _hips_fwd(p):
    """World facing of the pelvis (its forward axis flattened onto the floor)."""
    W = Rt0["Hips"] @ p["Hips"][0].to_matrix()
    f = W @ (Rt0["Hips"].inverted() @ Vector((0, -1, 0)))
    f.z = 0
    return f.normalized() if f.length > 1e-6 else Vector((0, -1, 0))


def join_knockdown(fall, getup, hold=6, xf=6):
    """Knocked flat, then back up: the get-up is turned + shifted so it starts exactly where the
    fall left the body, easing back to facing -Y at the origin by the time he's standing (so the
    run that follows doesn't pop). A short hold on the floor, then a crossfade into the get-up."""
    end = fall[-1]
    n = len(getup)
    a0 = math.atan2(_hips_fwd(end).x, -_hips_fwd(end).y) - math.atan2(_hips_fwd(getup[0]).x, -_hips_fwd(getup[0]).y)
    RH = Rt0["Hips"]
    e_w, g0 = RH @ end["Hips"][1], None
    Rs, shifts = [], []
    for i in range(n):
        t = i / max(1, n - 1)
        k = t * t * (3 - 2 * t)
        R = Matrix.Rotation(a0 * (1 - k), 3, Vector((0, 0, 1)))
        Rs.append(R)
    # start offset: where the turned get-up's hips are vs. where the fall ended (horizontal only)
    gw0 = Rs[0] @ (RH @ getup[0]["Hips"][1])
    d0 = Vector((e_w.x - gw0.x, e_w.y - gw0.y, 0))
    for i in range(n):
        t = i / max(1, n - 1); k = t * t * (3 - 2 * t)
        gwi = Rs[i] @ (RH @ getup[i]["Hips"][1])
        tgt = Vector((-gwi.x, -gwi.y, 0))                     # standing at the origin by the end
        shifts.append(d0.lerp(tgt, k))
    getup = _rot_root(getup, Rs, shifts)
    # height: sample() measures hips against the take's FIRST frame, taken to be standing -- but
    # this take opens with him flat on his back, so the whole get-up came out ~0.8 m up in the
    # air (he stood up on nothing). Ground it on its end: standing, the planted ankle at rest height.
    def ankle(p, side):
        bn, v = side + "Foot", Vector((0, 0, 0))
        while tbones[bn].parent is not None:
            par = tbones[bn].parent.name
            v = v + fk_world(p, par) @ (Rt0[par].inverted() @ (Ht0[bn] - Ht0[par])); bn = par
        return (Ht0[bn] + v + RH @ p["Hips"][1]).z
    low = min(min(ankle(p, "Left"), ankle(p, "Right")) for p in getup[-8:])
    dz = min(Ht0["LeftFoot"].z, Ht0["RightFoot"].z) - low
    getup = _rot_root(getup, Matrix.Identity(3), Vector((0, 0, dz)))
    print(f"enemy: knockdown get-up moved {dz:+.3f} m to stand on the floor; hips lying {(Ht0['Hips'] + RH @ end['Hips'][1]).z:.2f} -> "
          f"{(Ht0['Hips'] + RH @ getup[0]['Hips'][1]).z:.2f}, standing {(Ht0['Hips'] + RH @ getup[-1]['Hips'][1]).z:.2f} (rest {Ht0['Hips'].z:.2f})")
    out = fall + [{b: [q.copy(), (l.copy() if l is not None else None)] for b, (q, l) in end.items()} for _ in range(hold)]
    for i in range(xf):                                       # floor pose -> get-up start
        w = (i + 1) / (xf + 1)
        g = getup[0]
        out.append({b: [q.slerp(g[b][0] if q.dot(g[b][0]) >= 0 else -g[b][0], w),
                        (l.lerp(g[b][1], w) if l is not None and g[b][1] is not None else l)]
                    for b, (q, l) in end.items()})
    return out + getup


def synth_crumple(base, end, mode, n=40):
    """Procedural deaths that end on a mocap floor pose. Phase A (to 35%): the legs give --
    mode "head": both knees at once, straight down, head lolling (a dead drop); mode "leg": the
    hit (left) knee first, the body twisting down onto it. Phase B: topple from that slump into
    `end` (a lying pose from a fall take), every bone slerped, the hips carried across."""
    slump = None
    out = []
    nA = int(n * 0.35)
    for i in range(nA):
        k = (i + 1) / nA
        k = k * k * (3 - 2 * k) if mode == "leg" else 1 - (1 - k) ** 3      # a headshot drops fast
        p = {b: [q.copy(), (l.copy() if l is not None else None)] for b, (q, l) in base.items()}
        if mode == "head":
            for side in ("Left", "Right"):
                turn_world(p, f"{side}UpLeg", (1, 0, 0), -78 * k)
                turn_world(p, f"{side}Leg", (1, 0, 0), 112 * k)
            for bn, deg in (("Spine", 12), ("Spine1", 16), ("Neck", 20), ("Head", 26)):
                turn_world(p, bn, (1, 0, 0), deg * k)                    # folds and lolls forward
            turn_world(p, "Head", (0, 1, 0), 18 * k)
            for side, sx in (("Left", -1), ("Right", 1)):
                turn_world(p, f"{side}Arm", (0, 1, 0), sx * 16 * k)      # arms fall limp, a little out (blades clear the thighs)
            p["Hips"][1] = p["Hips"][1] + Vector((0, 0, -0.48 * k))
        else:
            turn_world(p, "LeftUpLeg", (1, 0, 0), -80 * k)
            turn_world(p, "LeftLeg", (1, 0, 0), 115 * k)                 # down onto the hurt knee
            turn_world(p, "RightUpLeg", (1, 0, 0), -30 * k)
            turn_world(p, "RightLeg", (1, 0, 0), 45 * k)
            for bn, deg in (("Spine", 14), ("Spine1", 18)):
                turn_world(p, bn, (1, 0, 0), deg * k)
            turn_world(p, "Spine1", (0, 1, 0), 10 * k)                   # twists down toward it
            for side, sx in (("Left", -1), ("Right", 1)):
                turn_world(p, f"{side}Arm", (0, 1, 0), sx * 22 * k)      # arms out to catch himself
            p["Hips"][1] = p["Hips"][1] + Vector((0.05 * k, 0, -0.38 * k))
        out.append(p)
        slump = p
    for i in range(n - nA):
        k = (i + 1) / (n - nA)
        k = k * k * (3 - 2 * k)
        p = {}
        for b, (q, l) in slump.items():
            qe, le = end[b]
            p[b] = [q.slerp(qe if q.dot(qe) >= 0 else -qe, k), (l.lerp(le, k) if l is not None and le is not None else l)]
        out.append(p)
    return out


def synth_riddled(death, n=18, seed=7):
    """Minigun / LMG kill: stood there shuddering under the stream for ~0.6 s, then the fall."""
    rnd = random.Random(seed)
    base = death[0]
    out = []
    for i in range(n):
        p = {b: [q.copy(), (l.copy() if l is not None else None)] for b, (q, l) in base.items()}
        a = 1.0 - 0.3 * i / n
        for bn, amp in (("Spine", 5), ("Spine1", 8), ("Neck", 7), ("Head", 9)):
            turn_world(p, bn, (1, 0, 0), rnd.uniform(-amp, amp) * a)
            turn_world(p, bn, (0, 0, 1), rnd.uniform(-amp, amp) * a)
        for side in ("Left", "Right"):
            turn_world(p, f"{side}Arm", (rnd.uniform(-1, 1), rnd.uniform(-1, 1), rnd.uniform(-1, 1)), rnd.uniform(-14, 14) * a)
        p["Hips"][1] = p["Hips"][1] + Vector((rnd.uniform(-0.015, 0.015), rnd.uniform(-0.01, 0.03), 0))
        out.append(p)
    return out + [{b: [q.copy(), (l.copy() if l is not None else None)] for b, (q, l) in f.items()} for f in death]


# EXTRA clips: name -> (poses, kind, bake step). kind picks how later passes treat it
# ("death": de-clipped + gripped like the death; "walk": a loop, like the walk; "hit": like the flinch)
EXTRA = {
    "death_blast": (take("death_blast"), "death", 2),
    "death_riddled": (synth_riddled(death), "death", 2),
    "knockdown": (join_knockdown(take("knock_fall"), take("knock_getup")), "hit", 2),
    "limp": (take("limp"), "walk", 1),
}
EXTRA["death_head"] = (synth_crumple(idle[0], death[-1], "head"), "death", 2)            # ends on his back
EXTRA["death_leg"] = (synth_crumple(idle[0], EXTRA["death_blast"][0][-1], "leg"), "death", 2)   # ends on his face

# spread_arms=(left arm, right arm, wrists) degrees: the zombie lurch reaches both hands forward and IN, which
# crossed the head chef's twin cleavers through each other. Swing each arm (and wrist) out
# about the world vertical on the walk/run loops -- the same offset every frame, so the
# loops still close.
def spread_arms(poses, left_deg, right_deg, hand_deg):
    for p in poses:   # per side: the lurch is lopsided (right arm already wide, left across the belly)
        for side, sx, arm_deg in (("Left", 1, left_deg), ("Right", -1, right_deg)):   # model faces -Y; its left is +X
            turn_world(p, f"{side}Arm", (0, 0, 1), sx * arm_deg)
            turn_world(p, f"{side}Hand", (0, 0, 1), sx * hand_deg)
    return poses


if SPEC.get("spread_arms"):
    walk, run = (spread_arms(ps, *SPEC["spread_arms"]) for ps in (walk, run))

# the rest of the gait ladder (stride / jog between the creep and the run), loops like the walk
for _g in ("stride", "jog"):
    if _g in SPEC["clips"]:
        _ps = take(_g)
        EXTRA[_g] = (spread_arms(_ps, *SPEC["spread_arms"]) if SPEC.get("spread_arms") else _ps, "walk", 1)


# hunch: the mocap runners are upright athletes; these are not. Tip the chest and neck forward
# and hang the head on the quicker gaits (the same offset every frame, so the loops still close).
def hunch(poses, deg):
    for p in poses:
        turn_world(p, "Spine", (1, 0, 0), deg * 0.5)
        turn_world(p, "Spine1", (1, 0, 0), deg * 0.5)
        turn_world(p, "Neck", (1, 0, 0), deg * 0.35)
        turn_world(p, "Head", (1, 0, 0), -deg * 0.9)      # eyes stay on the prey
    return poses


if SPEC.get("hunch"):
    run = hunch(run, SPEC["hunch"])
    EXTRA = {k: ((hunch(v, SPEC["hunch"]) if k in ("stride", "jog") else v), kind, st) for k, (v, kind, st) in EXTRA.items()}
print("enemy: gait m/s " + " ".join(f"{k}={v:.2f}" for k, v in GAIT_MPS.items()))

# one_hand_attack (priest): the base melee swing is two-handed -- his lantern arm keeps its idle
# pose (local rotations, frame by frame), so only the right hand strikes
if SPEC.get("one_hand_attack"):
    keep = [b for b in tbones.keys() if b.startswith(("LeftShoulder", "LeftArm", "LeftForeArm", "LeftHand"))]
    for i, p in enumerate(attack):
        src = idle[i % len(idle)]
        for bn in keep:
            if bn in src and bn in p:
                p[bn][0] = src[bn][0].copy()

if SPEC.get("attack_synth") == "grab":
    attack = synth_grab(idle)
elif SPEC.get("attack_synth") == "rake":
    attack = synth_rake(idle)
elif SPEC.get("attack_synth") == "dual":
    attack = synth_dual(idle)

# aim_gun (rifleman, gunner): the gun is modelled along the right forearm at rest, but in the
# aim clip that forearm runs ACROSS the face (high elbow, hand at the cheek) -- the barrel
# pointed off to his left and the forward left hand held nothing. Seat it properly:
#  1. yaw the whole aim clip so the line right hand -> left hand points straight ahead
#     (the shooter's bladed stance; the game aims the enemy's facing at the player),
#  2. rotate the gun in the right hand (rest space, about the grip) so that, at mid-clip,
#     the barrel lies along that line, out over the supporting left hand, sights up.
def fk_pos(p, bn):
    par = tbones[bn].parent
    if par is None:
        return Ht0[bn].copy()
    return fk_pos(p, par.name) + fk_world(p, par.name) @ (Rt0[par.name].inverted() @ (Ht0[bn] - Ht0[par.name]))


def ik_arm(p, side, T, pole=None):
    """Two-bone IK: bend the elbow so shoulder->wrist spans |T - shoulder|, swing the upper arm
    so the wrist lands on T, then (with a pole) roll the arm about shoulder->T so the elbow
    points along the pole. Returns the miss in metres."""
    arm, fore, hand = f"{side}Arm", f"{side}ForeArm", f"{side}Hand"
    S_, E, W = fk_pos(p, arm), fk_pos(p, fore), fk_pos(p, hand)
    l1, l2 = (E - S_).length, (W - E).length
    D = min((T - S_).length, (l1 + l2) * 0.999)
    want = math.acos(max(-1.0, min(1.0, (l1 * l1 + l2 * l2 - D * D) / (2 * l1 * l2))))
    have = (S_ - E).angle(W - E)
    n = (E - S_).cross(W - E)
    if n.length < 1e-6:
        n = (pole if pole is not None else Vector((0, 0, 1))).cross(E - S_)
    n.normalize()
    best = None
    for sgn in (1, -1):                                 # the bend direction that reaches D
        q = p[fore][0].copy()
        turn_world(p, fore, n, sgn * math.degrees(have - want))
        dd = abs((fk_pos(p, hand) - S_).length - D)
        if best is None or dd < best[0]:
            best = (dd, p[fore][0].copy())
        p[fore][0] = q
    p[fore][0] = best[1]
    W = fk_pos(p, hand)
    a0, a1 = (W - S_), (T - S_)
    ax = a0.cross(a1)
    if ax.length > 1e-6:
        turn_world(p, arm, ax.normalized(), math.degrees(a0.angle(a1)))
    if pole is not None:                                # roll the elbow toward the pole
        axis = (T - S_).normalized()
        e = fk_pos(p, fore) - S_; e -= axis * e.dot(axis)
        pl = pole - axis * pole.dot(axis)
        if e.length > 1e-6 and pl.length > 1e-6:
            ang = e.angle(pl)
            if e.cross(pl).dot(axis) < 0:
                ang = -ang
            turn_world(p, arm, axis, math.degrees(ang))
    return (fk_pos(p, hand) - T).length


def seat_hand(p, side, point, axis, palm_dir, pole=None, along_dir=None):
    """Close `side`'s fist on a handle: orient the hand so the handle (`axis`) runs across the
    palm with the palm facing `palm_dir`, IK the wrist so the fist hole sits on `point`."""
    hb = hand_basis(side); hn = f"{side}Hand"
    axis = axis.normalized()
    Rcur = fk_world(p, hn) @ Rt0[hn].inverted()
    sd = axis if (Rcur @ hb["side"]).dot(axis) >= 0 else -axis
    hand_sign = 1.0 if hb["side"].cross(hb["n"]).dot(hb["along"]) > 0 else -1.0
    if along_dir is not None:      # knuckles this way (the forearm comes into the handle from the side)
        ad = (along_dir - sd * along_dir.dot(sd)).normalized()
        n = (ad.cross(sd) * hand_sign).normalized()
    else:
        n = (palm_dir - sd * palm_dir.dot(sd)).normalized()
    al = sd.cross(n).normalized() * hand_sign
    Fr = Matrix((hb["along"], hb["side"], hb["n"])).transposed()
    Fd = Matrix((al, sd, n)).transposed()
    Wrot = Fd @ Fr.inverted()                                # rest world -> posed world, for the hand
    off = Wrot @ (hb["hole"] - hb["hand"])
    ik_arm(p, side, point - off, pole)
    par = tbones[hn].parent.name
    B = fk_world(p, par) @ (Rt0[par].inverted() @ Rt0[hn])
    p[hn][0] = (B.inverted() @ Wrot @ Rt0[hn]).to_quaternion()
    return (fk_pos(p, hn) + off - point).length


# aim_gun="line": rifle shouldered, left hand out along the barrel -> steps 1 + 2.
# aim_gun="forward": heavy gun gripped with both hands together (no hand-to-hand line) ->
#     step 2 only, barrel level and straight ahead.
if SPEC.get("aim_gun") and GUN.get("parts"):
    if SPEC["aim_gun"] == "hip":
        # heavy gun from the hip: the idle (breathing, weight shifts) with the right fist on the
        # grip at belt height ~30 cm out front, elbow back and down -- the mocap aim held the
        # fist at his sternum and the drum went through his chest
        attack = [{b: [q.copy(), (l.copy() if l is not None else None)] for b, (q, l) in fr.items()} for fr in idle]
        yaw = -30.0                    # bladed, left shoulder forward: the left hand can reach the guard
        for p in attack:
            turn_world(p, "Hips", (0, 0, 1), yaw)
        for p in attack:
            Hp, Sp = fk_pos(p, "Hips"), fk_pos(p, "RightArm")
            Ry = Matrix.Rotation(math.radians(yaw), 3, Vector((0, 0, 1)))     # body-relative: -X right, -Y ahead
            ik_arm(p, "Right", Hp + Ry @ Vector((-0.20, -0.22, 0.17)), pole=Ry @ Vector((-0.5, 0.6, -0.6)))
    mid = attack[len(attack) // 2]
    yaw = yaw if SPEC["aim_gun"] == "hip" else 0.0
    if SPEC.get("aim_drop"):        # hands at the chin put the heavy gun's receiver in his face
        for p in attack:
            for side in ("Left", "Right"):
                turn_world(p, f"{side}Arm", (1, 0, 0), SPEC["aim_drop"])
    if SPEC["aim_gun"] == "line":
        a = fk_pos(mid, "LeftHand") - fk_pos(mid, "RightHand")
        yaw = math.degrees(math.atan2(-1.0, 0.0) - math.atan2(a.y, a.x))     # model faces -Y
        for p in attack:
            turn_world(p, "Hips", (0, 0, 1), yaw)
        # legs: this mocap locks the left knee straight and bends the right one BACKWARDS
        # (goat-legged). Stand him on the idle's relaxed legs instead, turned with the yaw so
        # the feet follow the bladed stance (world-space match, so the aim's pelvis tilt
        # doesn't swing them)
        Ryaw = Matrix.Rotation(math.radians(yaw), 3, Vector((0, 0, 1)))
        ref = idle[0]
        for p in attack:
            for side in ("Left", "Right"):
                up = f"{side}UpLeg"
                par = tbones[up].parent.name
                B = fk_world(p, par) @ (Rt0[par].inverted() @ Rt0[up])
                p[up][0] = (B.inverted() @ Ryaw @ fk_world(ref, up)).to_quaternion()
                for bn in (f"{side}Leg", f"{side}Foot", f"{side}ToeBase"):
                    if bn in p and bn in ref:
                        p[bn][0] = ref[bn][0].copy()
        # the gun rides above the fist: drop the cheek hand so it passes UNDER the helmet brim;
        # a big gun (gunner's) also comes out to the right, off the cheek and the chest belt
        a0 = fk_pos(mid, "LeftHand") - fk_pos(mid, "RightHand")
        a0 = Vector((a0.x, a0.y, 0)).normalized()
        right = a0.cross(Vector((0, 0, 1))).normalized()
        off = (Vector((0, 0, -SPEC.get("aim_hand_drop", 0.12))) + right * SPEC.get("aim_hand_out", 0.0)
               + a0 * SPEC.get("aim_hand_fwd", 0.0))
        for p in attack:
            ik_arm(p, "Right", fk_pos(p, "RightHand") + off)
        a = (fk_pos(mid, "LeftHand") - fk_pos(mid, "RightHand")).normalized()
    else:
        a = Vector((0, -1, 0))
    R = fk_world(mid, "RightHand") @ Rt0["RightHand"].inverted()           # rest world -> posed world
    def frame(d, up):
        d = d.normalized(); up = (up - d * up.dot(d)).normalized()
        return Matrix((d, up, d.cross(up))).transposed()
    cur = frame(R @ GUN["d"], R @ GUN["up"])
    want = frame(a, Vector((0, 0, 1)))
    # the gun stays seated in the fist; the WRIST turns it onto the aim line, every frame
    turned = math.degrees((want @ cur.inverted()).to_quaternion().angle)
    for p in attack:
        Rp = fk_world(p, "RightHand") @ Rt0["RightHand"].inverted()
        Wd = want @ frame(Rp @ GUN["d"], Rp @ GUN["up"]).inverted()
        par = tbones["RightHand"].parent.name
        B = fk_world(p, par) @ (Rt0[par].inverted() @ Rt0["RightHand"])
        p["RightHand"][0] = (B.inverted() @ Wd @ B @ p["RightHand"][0].to_matrix()).to_quaternion()
    Q = Matrix.Identity(3)
    print(f"enemy: aim_gun yaw {yaw:.1f} deg, wrist turned {turned:.1f} deg")
    # left hand onto the handguard: the mocap's support hand reaches ~0.57 m out, past the muzzle
    # of our shorter rifle (hip mode: it starts at his side) -- IK puts it under the wood
    if SPEC["aim_gun"] in ("line", "hip"):
        d_r, up_r = Q @ GUN["d"], Q @ GUN["up"]
        gl = GUN["grip"] - Ht0["RightHand"]                  # grip offset from the right wrist, rest
        err = 0.0
        for p in attack:
            Rp = fk_world(p, "RightHand") @ Rt0["RightHand"].inverted()
            grip = fk_pos(p, "RightHand") + Rp @ gl
            dp, upp = (Rp @ d_r).normalized(), (Rp @ up_r).normalized()
            # handguard centre is grip + 0.33 d + (lift + 0.045) up; the wrist sits a hand's length
            # back along it and below, so the palm cups the wood from underneath
            hold = grip + dp * 0.33 * GUN["scale"] + upp * (GUN["lift"] + 0.045 * GUN["scale"])
            err = max(err, seat_hand(p, "Left", hold, dp, upp))   # palm up under the wood
        print(f"enemy: aim_gun left hand on the handguard, max miss {err * 100:.1f} cm")
    # carry (gunner): a big turn of the gun in the hand leaves it sticking out sideways/backwards
    # in the other clips -- turn the right wrist there so it is carried low, muzzle ahead and down
    # hold (gunner): a heavy gun stays in both hands -- idle/walk/run/hit take the hip hold's arms
    # (local rotations, so it rides the upper body), the gun carried across him
    if SPEC.get("gun_hold"):
        ARMS = [b for b in tbones.keys() if b.startswith(("LeftShoulder", "RightShoulder", "LeftArm", "RightArm",
                "LeftForeArm", "RightForeArm", "LeftHand", "RightHand", "LeftFinger", "RightFinger", "LThumb", "RThumb"))]
        ref = attack[len(attack) // 2]
        ref = {bn: ref[bn][0].copy() for bn in ARMS if bn in ref}
        for clip in (idle, walk, run, hit, attack, *HITX.values(), EXTRA["limp"][0]):
            for p in clip:
                for bn, q in ref.items():
                    if bn in p:
                        p[bn][0] = q.copy()
        print(f"enemy: gun_hold arms copied into idle/walk/run/hit/attack ({len(ARMS)} bones)")
    elif SPEC.get("gun_carry"):
        d_r, up_r = Q @ GUN["d"], Q @ GUN["up"]
        carry = Vector((0.25, -0.75, -0.6)).normalized()      # ahead, down, a little across the body
        worst = 0.0
        for clip in (idle, walk, run, hit, *HITX.values(), EXTRA["limp"][0], EXTRA["knockdown"][0]):
            for p in clip:
                Rp = fk_world(p, "RightHand") @ Rt0["RightHand"].inverted()
                W = frame(carry, Vector((0, 0, 1))) @ frame(Rp @ d_r, Rp @ up_r).inverted()
                par = tbones["RightHand"].parent.name
                B = fk_world(p, par) @ (Rt0[par].inverted() @ Rt0["RightHand"])
                p["RightHand"][0] = (B.inverted() @ W @ B @ p["RightHand"][0].to_matrix()).to_quaternion()
                worst = max(worst, math.degrees(W.to_quaternion().angle))
        print(f"enemy: gun_carry wrist turn up to {worst:.0f} deg")

# tentacles (fiend): a travelling wave down each chain on every clip -- loops close exactly
# (whole cycles per clip), the attack whips them all forward over the shoulders, death
# lets them thrash then go limp. Rotations are about WORLD axes mapped into each bone's
# rest frame, so "forward" means forward whatever the chain's rest direction.
def tentacle_motion(poses, role):
    if not TENT_BONES:
        return poses
    n = len(poses)
    loop = role in ("idle", "walk", "run")
    cycles = max(1, round(n / 30.0 * (0.55 if role == "idle" else 0.9)))
    X, Z = Vector((1, 0, 0)), Vector((0, 0, 1))
    for i, p in enumerate(poses):
        t = i / max(1, n - 1)
        ph = 2 * math.pi * (i / n if loop else t * max(1, n / 30.0) * 0.9) * (cycles if loop else 1)
        amp = {"idle": 0.13, "walk": 0.17, "run": 0.20, "hit": 0.30, "attack": 0.16, "death": 0.30}[role]
        if role == "death":
            amp *= max(0.0, 1.0 - t * 1.3)
        lash = 0.0
        if role == "attack":                # coil back, then whip forward, then recover
            lash = -0.22 * math.sin(math.pi * min(1, t / 0.35)) if t < 0.35 else 0.62 * math.sin(math.pi * min(1.0, (t - 0.35) / 0.5))
        for ti, chain in enumerate(TENT_BONES):
            side = 1 if ti % 2 == 0 else -1
            for k, bn in enumerate(chain):
                lag = k * 0.75 + ti * 1.7
                ax = amp * math.sin(ph - lag) + lash * (0.6 + 0.12 * k)
                az = side * amp * 0.8 * math.cos(ph * (1 if loop else 1.3) - lag * 0.8)
                if role == "death":
                    ax -= 0.10 * t                       # sag to the floor
                R0 = Rt0[bn]
                q = (Quaternion(R0.inverted() @ X, ax) @ Quaternion(R0.inverted() @ Z, az))
                p[bn][0] = p[bn][0] @ q
    return poses


# ---------------------------------------------------------------- de-clip: weapons out of bodies
# Mocap arms were never holding OUR props: cleavers swung through thighs, lanterns hung inside
# robes, two blades crossed through each other. Per frame, every hand-held prop is sampled as
# points and posed rigidly with its hand; the body is a set of capsules fitted to the skinned
# meshes (body, clothes, apron, robe...). Anything inside a capsule -- or too close to the other
# hand's prop -- swings that upper arm away, a few degrees at a time, until clear. Corrections
# are then smoothed through time (no jitter), and a second pass mops up what smoothing gave back.
# tools/chef/check_clips.py verifies the exported glb against the real meshes.
def _declip_setup():
    import numpy as _np
    wpn = {}
    for side in ("Left", "Right"):
        objs = [o for o, b in props if b == f"{side}Hand"]
        if objs:
            vs, fs = [], []
            for o in objs:
                base = len(vs)
                vs += [tuple(o.matrix_world @ v.co) for v in o.data.vertices]
                fs += [tuple(base + i for i in pl.vertices) for pl in o.data.polygons]
            wpn[side] = (_np.array(vs), fs)
    if not wpn:
        return None
    # body = skinned body + clothes + draped props (apron, robe, growths, belts...)
    body_objs = [o for o in meshes if "low-poly" not in o.name] + \
                [o for o, b in props if b not in ("LeftHand", "RightHand")]      # incl. hats, masks, helmets
    # each face's bone (most-weighted group of its first vertex; rigid props: their bone)
    rigid = {o.name: b for o, b in props}
    fbone = {}
    for o in body_objs:
        gname = {g.index: g.name for g in o.vertex_groups}
        rb = rigid.get(o.name)
        vb = []
        for v in o.data.vertices:
            g = max(v.groups, key=lambda g: g.weight, default=None)
            vb.append(rb if rb else (gname.get(g.group) if g else ""))
        fbone[o.name] = [vb[pl.vertices[0]] for pl in o.data.polygons]
    return wpn, body_objs, fbone


DECLIP = _declip_setup() if not TENT_BONES else None


def _apply_pose(p):
    for n, (q, l) in p.items():
        pb = rig.pose.bones[n]
        pb.rotation_quaternion = q
        if l is not None:
            pb.location = l
    bpy.context.view_layer.update()


def declip(poses, loop, label="", deep=False):
    if DECLIP is None or not poses:
        return poses
    import numpy as _np
    wpn, body_objs, _ = DECLIP
    gun = bool(GUN.get("parts"))
    if rig.animation_data:
        rig.animation_data.action = None
    topo, fbone = {}, {}      # from the EVALUATED meshes (mask modifiers drop hidden skin)
    rigid = {o.name: b for o, b in props}

    def ignore(bn, side):
        own = (f"{side}Hand", f"{side}Finger", f"{side}HandFinger", f"{side[0]}Thumb", f"{side}ForeArm")
        if bn.startswith(own):
            return True                       # the fist holds the grip; the wrist sits on the handle
        if gun and bn.startswith(("LeftHand", "RightHand", "LeftFinger", "RightFinger", "LThumb", "RThumb",
                                  "LeftForeArm", "RightForeArm", "LeftArm", "RightArm")):
            return True                       # a gun is held by both arms: the aim owns that
        return False

    def body_trees():
        dg = bpy.context.evaluated_depsgraph_get()
        out = []
        for o in body_objs:
            ev = o.evaluated_get(dg); me = ev.to_mesh()
            if o.name not in topo:
                topo[o.name] = [tuple(pl.vertices) for pl in me.polygons]
                gname = {g.index: g.name for g in o.vertex_groups}
                rb = rigid.get(o.name); vb = []
                for v in me.vertices:
                    g = max(v.groups, key=lambda g: g.weight, default=None)
                    vb.append(rb if rb else (gname.get(g.group, "") if g else ""))
                fbone[o.name] = [vb[f[0]] for f in topo[o.name]]
            co = _np.empty(len(me.vertices) * 3); me.vertices.foreach_get("co", co); co = co.reshape(-1, 3)
            M = _np.array([tuple(r) for r in ev.matrix_world])
            co = co @ M[:3, :3].T + M[:3, 3]
            ev.to_mesh_clear()
            out.append((o.name, co, BVHTree.FromPolygons(co.tolist(), topo[o.name])))
        return out

    def weapon(side):
        pb = rig.pose.bones[f"{side}Hand"]
        M = rig.matrix_world @ pb.matrix @ pb.bone.matrix_local.inverted() @ rig.matrix_world.inverted()
        X, fs = wpn[side]
        Mn = _np.array([tuple(r) for r in M])
        Y = X @ Mn[:3, :3].T + Mn[:3, 3]
        return Y, fs, BVHTree.FromPolygons(Y.tolist(), fs)

    def contacts(Wt, trees, side):
        """(weapon-side centroid, outward body normal sum, count) over overlapping triangle pairs."""
        Y, fs, tw = Wt
        cen, push, n = Vector((0, 0, 0)), Vector((0, 0, 0)), 0
        for nm, co, tb in trees:
            fb = fbone[nm]; tp = topo[nm]
            for iw, ib in tw.overlap(tb):
                if ignore(fb[ib], side):
                    continue
                f = tp[ib]
                a0, a1, a2 = (Vector(tuple(co[f[k]])) for k in (0, 1, 2))
                nrm = (a1 - a0).cross(a2 - a0)
                if nrm.length > 1e-12:
                    push += nrm.normalized()
                cen += Vector(tuple(Y[list(fs[iw])].mean(0))); n += 1
        return (cen / n if n else None), push, n

    def swing(side, where, push, deg, bone="Arm"):
        S = rig.matrix_world @ rig.pose.bones[f"{side}{bone}"].head
        lever = where - S
        ax = lever.cross(push)
        if ax.length < 1e-9 or lever.length < 1e-3:
            return None
        return (side, deg, ax.normalized(), bone)

    def solve(p, iters=20, wrist_after=10, fore_after=None, margin=0.0):
        # upper-arm swings first; if a long prop (shovel, cross) is still in the head after
        # wrist_after rounds, the wrist joins in and tips the prop itself away
        moved = 0
        last = {}
        for it in range(iters):
            _apply_pose(p)
            trees = body_trees()
            W = {s: weapon(s) for s in wpn}
            turns = []
            for side, Wt in W.items():
                where, push, n = contacts(Wt, trees, side)
                if n and push.length > 1e-9:
                    t = swing(side, where, push.normalized(), min(6.0, 1.5 + 0.08 * n))
                    if t: turns.append(t)
                    if it >= wrist_after and not gun:
                        t = swing(side, where, push.normalized(), min(8.0, 2.0 + 0.1 * n), bone="Hand")
                        if t: turns.append(t)
                    if fore_after is not None and it >= fore_after and not gun:   # deep: the elbow too
                        t = swing(side, where, push.normalized(), min(8.0, 2.0 + 0.1 * n), bone="ForeArm")
                        if t: turns.append(t)
            if len(W) == 2:
                ov = W["Left"][2].overlap(W["Right"][2])
                if ov:
                    cL = Vector(tuple(W["Left"][0].mean(0))); cR = Vector(tuple(W["Right"][0].mean(0)))
                    sep = cL - cR
                    sep = sep.normalized() if sep.length > 1e-6 else Vector((1, 0, 0))
                    for side, sv in (("Left", sep), ("Right", -sep)):
                        t = swing(side, Vector(tuple(W[side][0].mean(0))), sv, 3.0)
                        if t: turns.append(t)
                        if it >= wrist_after and not gun:      # props locked together: tip them apart too
                            t = swing(side, Vector(tuple(W[side][0].mean(0))), sv, 4.0, bone="Hand")
                            if t: turns.append(t)
            if not turns:
                # just clear is not clear enough: the baked keys are a few frames apart and the
                # blend between two touching poses dips back in -- carry on a little further out
                if moved and margin:
                    for (side, bone), ax in last.items():
                        turn_world(p, f"{side}{bone}", ax, margin)
                break
            for side, deg, ax, bone in turns:
                turn_world(p, f"{side}{bone}", ax, deg)
                last[(side, bone)] = ax
            moved += 1
        return moved

    arms = [f"{s}{b}" for s in ("Left", "Right") for b in ("Arm", "Hand")]
    before = [{a: p[a][0].copy() for a in arms} for p in poses]
    n_fixed = sum(1 for p in poses if solve(p) > 0)
    n = len(poses)
    deltas = [{a: poses[i][a][0] @ before[i][a].inverted() for a in arms} for i in range(n)]
    for i in range(n):          # smooth the corrections through time (wrap for loops)...
        for a in arms:
            ref = deltas[i][a]; acc = [0.0, 0.0, 0.0, 0.0]
            for k in range(-3, 4):
                j = (i + k) % n if loop else min(n - 1, max(0, i + k))
                q = deltas[j][a]; q = q if q.dot(ref) >= 0 else -q
                # bigger corrections weigh more: they pull their neighbours, not the reverse
                w = (1.0 / (1 + abs(k))) * (1.0 + 4.0 * (1.0 - abs(q.w)))
                acc = [acc[0] + q.w * w, acc[1] + q.x * w, acc[2] + q.y * w, acc[3] + q.z * w]
            qa = Quaternion(acc); qa.normalize()
            poses[i][a][0] = qa @ before[i][a]
    left = sum(1 for p in poses if solve(p, iters=45, wrist_after=3) > 0)   # ...then mop up the residue
    if left and not deep:     # a frame the mop-up ran out of rounds on (a cross against a swinging robe):
        for _ in range(3):    # keep at it, wrist and all, until a pass finds nothing left to move
            again = sum(1 for p in poses if solve(p, iters=60, wrist_after=1, margin=2.5) > 0)
            if not again:
                break
        left = again
    if deep:      # falls (deaths, knockdown): fast motion, body folding over the weapon -- a harder last pass
        left = sum(1 for p in poses if solve(p, iters=90, wrist_after=1, fore_after=6) > 0)
    stuck = []
    if left and not deep:
        for i, p in enumerate(poses):
            _apply_pose(p); trees = body_trees()
            for side in wpn:
                _, _, c = contacts(weapon(side), trees, side)
                if c: stuck.append((i, side, c))
    print(f"enemy: declip {label}: {n_fixed}/{n} frames corrected, {left} re-touched after smoothing"
          + (f", still touching {stuck[:8]}" if stuck else ""))
    return poses


# knees: retargeted mocap sometimes bends a knee BACKWARDS (goat legs -- the base melee attack,
# the rifle aim). Measure each knee's bend about its hinge (rest X: + flexes the shin back);
# a negative bend is mirrored to a forward one, the thigh swings so the ankle lands where it
# was, and the foot keeps its world angle (stays planted).
def fix_knees(poses, label):
    nfix = 0
    for side in ("Left", "Right"):
        up, lg, ft = f"{side}UpLeg", f"{side}Leg", f"{side}Foot"
        if lg not in tbones or ft not in tbones:
            continue
        hinge = (Rt0[lg].inverted() @ Vector((1, 0, 0))).normalized()
        for p in poses:
            q = p[lg][0]
            v = Vector((q.x, q.y, q.z))
            ang = 2 * math.atan2(v.dot(hinge), q.w)
            if ang > math.radians(-1.0):
                continue
            A0, Wf = fk_pos(p, ft), fk_world(p, ft)
            p[lg][0] = Quaternion(hinge, -ang + math.radians(4))
            H, A1 = fk_pos(p, up), fk_pos(p, ft)
            a0, a1 = A1 - H, A0 - H
            ax = a0.cross(a1)
            if ax.length > 1e-9:
                turn_world(p, up, ax.normalized(), math.degrees(a0.angle(a1)))
            B = fk_world(p, lg) @ (Rt0[lg].inverted() @ Rt0[ft])
            p[ft][0] = (B.inverted() @ Wf).to_quaternion()
            nfix += 1
    if nfix:
        print(f"enemy: knees {label}: {nfix} backward knees flexed forward")
    return poses


idle, walk, run, attack, hit, death = (fix_knees(ps, lb) for ps, lb in
    ((idle, "idle"), (walk, "walk"), (run, "run"), (attack, "attack"), (hit, "hit"), (death, "death")))
EXTRA = {k: (fix_knees(v, k), kind, st) for k, (v, kind, st) in EXTRA.items()}

if DECLIP is not None:
    idle, walk, run, hit, death = (declip(ps, lp, label=lb) for ps, lp, lb in
                                   ((idle, True, "idle"), (walk, True, "walk"), (run, True, "run"),
                                    (hit, False, "hit"), (death, False, "death")))
    if not GUN.get("parts"):        # gun aims are hand-built (aim_gun); melee swings get de-clipped
        attack = declip(attack, False, label="attack")
    HITX = {k: declip(v, False, label=k) for k, v in HITX.items()}
    EXTRA = {k: (declip(v, kind == "walk", label=k, deep=(k.startswith("death") or k == "knockdown")), kind, st)
             for k, (v, kind, st) in EXTRA.items()}

# grip: a hand that holds a prop is a closed fist on EVERY frame. fist() adds a curl on top of
# the mocap, so open-handed clips (the run's flat palms) left knives floating off open
# fingers; here the finger bones are SET to a fixed grip, whatever the mocap did.
GRIP = {"FingerBase": 10, "HandFinger1": 108, "Thumb": 45}   # FingerBase = the PALM: barely bend it
GRIP_SIDES = sorted({b[:-4] for _, b in props if b in ("LeftHand", "RightHand")} |
                    ({"Left"} if (GUN.get("parts") or SPEC.get("two_hand")) else set()))


GRIPQ = {}
for _sd in GRIP_SIDES:
    GRIPQ.update(curl_quats(_sd, GRIP))
RELAXQ = {}
for _sd in ("Left", "Right"):
    if _sd not in GRIP_SIDES and f"{_sd}HandFinger1" in tbones:
        RELAXQ.update(curl_quats(_sd, RELAX))


def grip(poses):
    for p in poses:          # the mocap has no finger channels: ADD them (they stayed flat at rest)
        for bn, q in GRIPQ.items():
            p[bn] = [q.copy(), None]
        for bn, q in RELAXQ.items():                     # empty hands: relaxed, unless a synth set them
            if bn not in p:
                p[bn] = [q.copy(), None]
    return poses


# two_hand="shaft" (liquidator's shovel, revenant's axe swing): the left fist rides the shaft,
# nearest to where the mocap had it (clamped along the shaft); two_hand_clips picks the clips
FORE_CLEAR = 0.06       # shaft axis to left forearm axis (elbow .. 80% to the wrist): no closer than this
POLES_L = (None, Vector((0, 0, -1)), Vector((1, 0, -0.5)), Vector((1, -0.5, 0)))   # elbow: as is, down, out-down, out-forward


def fore_clear(p, hole, ax):
    """Closest the left forearm (elbow .. 80% of the way to the wrist) comes to the shaft's axis."""
    e, w = fk_pos(p, "LeftForeArm"), fk_pos(p, "LeftHand")
    best = 9.0
    for k in range(9):
        q = e.lerp(w, k / 10)
        along = max(-0.15, min(1.4, (q - hole).dot(ax)))
        best = min(best, (q - (hole + ax * along)).length)
    return best


if SPEC.get("two_hand") == "shaft":
    hbR = hand_basis("Right"); worst = 0.0; worst_clear = 9.0
    TH = {"idle": idle, "walk": walk, "run": run, "hit": hit, "attack": attack, **HITX, "limp": EXTRA["limp"][0]}
    for cn in SPEC.get("two_hand_clips", ("idle", "walk", "run", "hit", "attack", *HITX.keys(), "limp")):
        for p in TH[cn]:
            Rr = fk_world(p, "RightHand") @ Rt0["RightHand"].inverted()
            hole = fk_pos(p, "RightHand") + Rr @ (hbR["hole"] - hbR["hand"])
            ax = (Rr @ hbR["side"]).normalized()
            hbL = hand_basis("Left")
            Rl = fk_world(p, "LeftHand") @ Rt0["LeftHand"].inverted()
            lh = fk_pos(p, "LeftHand") + Rl @ (hbL["hole"] - hbL["hand"])
            t = max(0.30, min(0.70, (lh - hole).dot(ax)))
            pt = hole + ax * t
            pd = pt - lh
            pd = pd if pd.length > 1e-4 else Rl @ hbL["n"]
            fa = fk_pos(p, "LeftHand") - fk_pos(p, "LeftForeArm")     # forearm comes in across the shaft
            miss = seat_hand(p, "Left", pt, ax, pd.normalized(), along_dir=fa)
            clear = fore_clear(p, hole, ax)
            if miss > 0.01 or clear < FORE_CLEAR:
                # out of reach (a de-clipped wind-up tips the shaft away), or the forearm lies along
                # the shaft and it runs through the wrist: try other grip points along the shaft and
                # other elbow directions; score = reach + forearm clearance + staying near the mocap
                def score(m_, c_, tt_):
                    return m_ * 5 + max(0.0, FORE_CLEAR - c_) * 3 + abs(tt_ - t) * 0.02
                best = (score(miss, clear, t), miss, clear, None)
                for tt in (0.12, 0.18, 0.24, 0.30, 0.36, 0.42, 0.48, 0.54, 0.60, 0.66, 0.70):
                    for pole in POLES_L:
                        q = {b: [v[0].copy(), (v[1].copy() if v[1] is not None else None)] for b, v in p.items()}
                        pt2 = hole + ax * tt
                        pd2 = pt2 - lh
                        pd2 = pd2.normalized() if pd2.length > 1e-4 else pd.normalized()
                        m2 = seat_hand(q, "Left", pt2, ax, pd2, pole=pole, along_dir=fa)
                        c2 = fore_clear(q, hole, ax)
                        sc = score(m2, c2, tt)
                        if sc < best[0]:
                            best = (sc, m2, c2, q)
                if best[3] is not None:
                    p.update(best[3]); miss, clear = best[1], best[2]
            if miss > 0.02 or clear < FORE_CLEAR:
                print(f"enemy: two_hand {cn} frame {TH[cn].index(p)} miss {miss * 100:.1f} cm, forearm clear {clear * 100:.1f} cm")
            worst_clear = min(worst_clear, clear)
            worst = max(worst, miss)
    print(f"enemy: two_hand left fist on the shaft, max miss {worst * 100:.1f} cm, "
          f"min forearm clearance {worst_clear * 100:.1f} cm")

idle, walk, run, attack, hit, death = (grip(ps) for ps in (idle, walk, run, attack, hit, death))
HITX = {k: grip(v) for k, v in HITX.items()}
EXTRA = {k: (grip(v), kind, st) for k, (v, kind, st) in EXTRA.items()}
print(f"enemy: grip {GRIP_SIDES} closed, relaxed {sorted({b[:4] for b in RELAXQ})} on every frame")

idle, walk, run, attack, hit, death = (tentacle_motion(ps, r) for ps, r in
    ((idle, "idle"), (walk, "walk"), (run, "run"), (attack, "attack"), (hit, "hit"), (death, "death")))
HITX = {k: tentacle_motion(v, "hit") for k, v in HITX.items()}
EXTRA = {k: (tentacle_motion(v, kind), kind, st) for k, (v, kind, st) in EXTRA.items()}

for name, poses, step in (("idle", idle, 1), ("walk", walk, 1), ("run", run, 1),
                          ("attack", attack, 2), ("hit", hit, 1), ("death", death, 2),
                          *((k, v, 1) for k, v in HITX.items()),
                          *((k, v, st) for k, (v, kind, st) in EXTRA.items())):
    clips.append(bake(name, poses, step))
if "cast" in SPEC["clips"]:     # optional spell clip (priest), separate from the melee swing
    clips.append(bake("cast", grip(declip(fix_knees(take("cast"), "cast"), False, label="cast")), 2))
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

# =============================================================== 5b. prop surface detail
# Every remaining material (props, eyes, gear) gets a procedural normal map + ORM by what it
# is made of: brushed steel, pebbled rubber, cotton weave, wood grain, chipped paint, wet flesh.
PROFILES = [   # (name keywords, pattern, roughness, specular, normal strength)
    (("steel", "iron", "gunmetal", "blade", "cleaver", "knife"), "brushed", 0.22, 0.95, 3.0),
    (("brass",), "brushed", 0.30, 0.75, 3.0),
    (("spade", "rust"), "pitted", 0.80, 0.15, 7.0),
    (("helmet", "axe_head", "air_tank", "tank"), "chipped", 0.45, 0.45, 5.0),
    (("rubber_apron", "apron"), "smooth", 0.28, 0.55, 2.0),     # big wet rubber sheet: glossy, faint texture
    (("rubber", "filter", "mask"), "pebble", 0.50, 0.30, 3.5),
    (("leather", "strap", "belt"), "pebble", 0.55, 0.25, 4.0),
    (("hat", "cloth", "neckerchief", "robe", "canvas", "apron", "toque"), "weave", 0.90, 0.04, 1.6),
    (("wood", "furniture", "haft", "handle"), "grain", 0.65, 0.12, 3.0),
    (("growth", "tentacle", "flesh", "tumour", "tumor"), "blobby", 0.30, 0.60, 5.0),
    (("blood",), "blobby", 0.10, 0.90, 2.5),
    (("eye", "lens"), "flat", 0.05, 1.00, 0.0),
]


def blur_x(a, r):
    c = np.cumsum(np.pad(a, [(0, 0), (r + 1, r)], mode="wrap"), axis=1)
    n = a.shape[1]
    return (c[:, 2 * r + 1:n + 2 * r + 1] - c[:, 0:n]) / (2 * r + 1)


def pattern_height(kind, S, seed):
    rng = np.random.default_rng(seed)
    nz = lambda: rng.standard_normal((S, S)).astype(np.float32)
    yy, xx = np.mgrid[0:S, 0:S].astype(np.float32)
    if kind == "brushed":     # long streaks + a few scratches
        h = blur_x(nz(), 28) * 3.0
        sc = (rng.random((S, S)) > 0.9993).astype(np.float32)
        h -= blur_x(blur(sc, 1), 40) * 18.0
        return h
    if kind == "smooth":
        return blur(nz(), 3) * 0.3 + smooth_noise(S, S, 6, seed) * 1.5
    if kind == "pebble":
        return blur(nz(), 2) * 1.2 + blur(nz(), 6) * 0.8
    if kind == "pitted":
        pits = (rng.random((S, S)) > 0.996).astype(np.float32)
        return blur(nz(), 2) * 1.0 + blur(nz(), 8) * 1.5 - blur(pits, 2) * 6.0
    if kind == "chipped":     # smooth paint with flaked chips
        chips = (smooth_noise(S, S, 40, seed) > 0.78).astype(np.float32)
        return blur(nz(), 3) * 0.4 - blur(chips, 1) * 0.6
    if kind == "weave":
        p = 2.6                                       # fine cotton: a felt texture, not a grid
        return 0.10 * (np.sin(2 * np.pi * xx / p) * np.sin(2 * np.pi * yy / p)) + blur(nz(), 1) * 0.35 + blur(nz(), 5) * 0.4
    if kind == "grain":
        return np.sin(2 * np.pi * (xx / S * 36.0 + 5.0 * smooth_noise(S, S, 7, seed))) * 0.5 + blur(nz(), 1) * 0.2
    if kind == "blobby":
        return smooth_noise(S, S, 12, seed) * 2.0 + smooth_noise(S, S, 36, seed + 1) * 1.0
    return np.zeros((S, S), np.float32)


def material_image(m):
    if not m or not m.use_nodes:
        return None
    for n in m.node_tree.nodes:
        if n.type == "TEX_IMAGE" and n.image and n.name not in ("ORM", "EMIT"):
            return n.image
    return None


# geometry AO is baked only into body + clothes (clean, non-overlapping UVs); props share
# materials across overlapping islands, so a bake there would be garbage
BAKE_SET = {m.name for m in bpy.data.materials if "orm" in m.keys()}
if DETAIL:
    done = 0
    for m in list(bpy.data.materials):
        if not m.use_nodes or "orm" in m.keys() or m.users == 0:
            continue
        if getattr(m, "blend_method", "") == "CLIP":
            continue                                     # eyebrows / lashes: alpha cards
        key = m.name.lower()
        prof = next((p for p in PROFILES if any(k in key for k in p[0])), (None, "pebble", 0.70, 0.12, 4.0))
        _, kind, rough, spec, strength = prof
        img = material_image(m)
        S = min(512, img.size[0]) if img is not None else 512
        h = pattern_height(kind, S, abs(hash(m.name)) % 1000)
        mask = np.zeros((S, S), np.float32)
        if img is not None:                              # the painted texture's own grime + any blood on it
            a = img_np(img, (S, S))
            lum = 0.3 * a[..., 0] + 0.59 * a[..., 1] + 0.11 * a[..., 2]
            h = h + (blur(lum, 1) - blur(lum, 6)) * 2.0
            base_red = float(np.median(a[..., 0] - np.maximum(a[..., 1], a[..., 2])))
            if base_red < 0.15:                          # wet blood on a non-red surface
                mask = np.clip((a[..., 0] - np.maximum(a[..., 1], a[..., 2]) - 0.15) * 5.0, 0, 1)
        nrm = height_to_normal(h, strength) if strength > 0 else None
        var = smooth_noise(S, S, 16, 9)
        orm = np.stack([np.ones((S, S), np.float32),
                        np.clip(rough + 0.12 * (var - 0.5) - 0.5 * mask, 0.05, 1),
                        np.clip(spec * (0.8 + 0.4 * var) + 0.6 * mask, 0, 1)], -1)
        wr = WEAR.get(m.name)
        if wr is not None:
            k = max(1, wr[0].shape[0] // S)
            bare, dull = (x[::k, ::k][:S, :S] for x in wr)
            orm[..., 1] = np.clip(orm[..., 1] * (1 - bare) * (1 - dull) + 0.26 * bare * (1 - dull) + 0.92 * dull, 0.05, 1)
            orm[..., 2] = np.clip(orm[..., 2] * (1 - bare) * (1 - dull) + 0.95 * bare * (1 - dull) + 0.04 * dull, 0, 1)
        nimg, oimg = save_maps(f"{ENEMY}_{m.name}", nrm, orm)
        attach_detail(m, nimg, oimg)
        done += 1
    print("enemy: prop detail on", done, "materials")

# =============================================================== 6. export
# One mesh object for the whole enemy (body + clothes + every prop): glTF then holds one
# primitive per MATERIAL instead of one per object -- a cook drops from 35 draws to 12,
# a gunner (26 cartridges) from 45 to 10. Bake any non-armature modifiers first so the
# join can't drop them.
parts = [o for o in bpy.data.objects if o.type == "MESH"]
for o in parts:
    bpy.context.view_layer.objects.active = o
    for md in list(o.modifiers):
        if md.type != "ARMATURE":
            try:
                bpy.ops.object.modifier_apply(modifier=md.name)
            except RuntimeError as ex:
                print("enemy: could not apply", md.name, "on", o.name, ex); o.modifiers.remove(md)
# join merges UV layers BY NAME: the MakeHuman meshes and the props name theirs
# differently, so unify (one layer each, same name) or most parts lose their UVs
for o in parts:
    uv = o.data.uv_layers
    if not uv:
        uv.new(name="UVMap")
    keep = uv.active or uv[0]
    for l in list(uv):
        if l != keep:
            uv.remove(l)
    keep.name = "UVMap"
bpy.ops.object.select_all(action="DESELECT")
for o in parts:
    o.select_set(True)
bpy.context.view_layer.objects.active = body
bpy.ops.object.join()
arms = [md for md in body.modifiers if md.type == "ARMATURE"]
for md in arms[1:]:
    body.modifiers.remove(md)
if not arms:
    md = body.modifiers.new("rig", "ARMATURE"); md.object = rig
else:
    arms[0].object = rig
if os.environ.get("ENEMY_DECIMATE"):
    dm = body.modifiers.new("lod", "DECIMATE"); dm.ratio = float(os.environ["ENEMY_DECIMATE"])
    bpy.context.view_layer.objects.active = body
    bpy.ops.object.modifier_move_to_index(modifier="lod", index=0)
    bpy.ops.object.modifier_apply(modifier="lod")
    print("enemy: decimated to", len(body.data.polygons), "faces")
if DETAIL:
    # Bake ambient occlusion from the real geometry (helmet over the face, collar on the neck,
    # arms against the torso) in the rest pose, and multiply it into each ORM's R channel.
    rig.data.pose_position = "REST"
    scene.render.engine = "CYCLES"; scene.cycles.samples = 48; scene.cycles.device = "CPU"
    bakes, scratch = [], []
    for m in body.data.materials:
        if m is None:
            continue
        if "orm" not in m.keys() or m.name not in BAKE_SET:
            # Blender bakes into the ACTIVE image node of EVERY material on the mesh: give the
            # others a throwaway target, or their own textures (hat, mask, shoes) get overwritten
            if m.use_nodes:
                si = bpy.data.images.new(m.name + "_scratch", 8, 8)
                n = m.node_tree.nodes.new("ShaderNodeTexImage"); n.image = si
                for x in m.node_tree.nodes: x.select = False
                n.select = True; m.node_tree.nodes.active = n
                scratch.append((m, n, si))
            continue
        orm = bpy.data.images[m["orm"]]
        bi = bpy.data.images.new(m.name + "_aobake", 1024, 1024, alpha=False)
        bi.colorspace_settings.name = "Non-Color"
        n = m.node_tree.nodes.new("ShaderNodeTexImage"); n.image = bi
        for x in m.node_tree.nodes: x.select = False
        n.select = True; m.node_tree.nodes.active = n
        bakes.append((m, n, bi, orm))
    if bakes:
        bpy.ops.object.select_all(action="DESELECT"); body.select_set(True); bpy.context.view_layer.objects.active = body
        scene.render.bake.margin = 8
        bpy.ops.object.bake(type="AO")
        # curvature (Cycles pointiness), baked as emission into a second image per material
        curv = {}
        for m, n, bi, orm in bakes:
            ci = bpy.data.images.new(m.name + "_curv", 1024, 1024, alpha=False); ci.colorspace_settings.name = "Non-Color"
            n.image = ci
            nt = m.node_tree; out = next(x for x in nt.nodes if x.type == "OUTPUT_MATERIAL")
            prev = out.inputs["Surface"].links[0].from_socket if out.inputs["Surface"].links else None
            geo = nt.nodes.new("ShaderNodeNewGeometry"); em = nt.nodes.new("ShaderNodeEmission")
            nt.links.new(geo.outputs["Pointiness"], em.inputs["Color"]); nt.links.new(em.outputs["Emission"], out.inputs["Surface"])
            curv[m.name] = (ci, prev, geo, em, out)
        bpy.ops.object.bake(type="EMIT")
        for m, n, bi, orm in bakes:
            ci, prev, geo, em, out = curv[m.name]; nt = m.node_tree
            if prev is not None: nt.links.new(prev, out.inputs["Surface"])
            nt.nodes.remove(geo); nt.nodes.remove(em)
            n.image = bi
        for m, n, bi, orm in bakes:
            ao = to_np(bi)[..., 0]
            o_ = to_np(orm)
            ky, kx = o_.shape[0] // ao.shape[0], o_.shape[1] // ao.shape[1]
            ao = np.kron(ao, np.ones((ky, kx), np.float32))[:o_.shape[0], :o_.shape[1]]
            ao = blur(np.clip(ao, 0, 1), 2)
            # curvature -> cavities (grime, occlusion) and worn ridges (lighter edges)
            ci = curv[m.name][0]
            pt = to_np(ci)[..., 0]
            c = pt - blur(pt, 8)
            k = 1.0 / (np.percentile(np.abs(c), 96) + 1e-6)
            cav = np.kron(np.clip(-c * k, 0, 1), np.ones((ky, kx), np.float32))[:o_.shape[0], :o_.shape[1]]
            edge = np.kron(np.clip(c * k, 0, 1), np.ones((ky, kx), np.float32))[:o_.shape[0], :o_.shape[1]]
            cav, edge = blur(cav, 2), blur(edge, 2)
            skin = "_skin" in m.name
            o_[..., 0] = np.clip(o_[..., 0] * (0.35 + 0.65 * ao) * (1 - (0.20 if skin else 0.35) * cav), 0, 1)
            o_[..., 1] = np.clip(o_[..., 1] + (0.0 if skin else 0.08) * cav, 0, 1)   # grime is duller
            orm.pixels.foreach_set(o_.astype(np.float32).ravel()); orm.save()
            base = next((l.from_node.image for l in m.node_tree.nodes["Principled BSDF"].inputs["Base Color"].links
                         if l.from_node.type == "TEX_IMAGE"), None)
            if base is not None:
                d_ = to_np(base)
                if d_.shape[:2] != o_.shape[:2]:
                    cy_, cx_ = d_.shape[0] // o_.shape[0] or 1, d_.shape[1] // o_.shape[1] or 1
                    cav2, edge2 = np.kron(cav, np.ones((cy_, cx_), np.float32)), np.kron(edge, np.ones((cy_, cx_), np.float32))
                    cav2, edge2 = cav2[:d_.shape[0], :d_.shape[1]], edge2[:d_.shape[0], :d_.shape[1]]
                else:
                    cav2, edge2 = cav, edge
                grime = np.array((0.20, 0.16, 0.11), np.float32)          # oily soot settles in folds
                g = (0.18 if skin else 0.45) * cav2
                for ch in range(3):
                    d_[..., ch] = d_[..., ch] * (1 - g) + grime[ch] * g * d_[..., ch] * 2.0
                    if not skin:
                        d_[..., ch] = np.clip(d_[..., ch] + 0.07 * edge2 * (1 - d_[..., ch]), 0, 1)   # worn, lighter ridges
                base.pixels.foreach_set(d_.astype(np.float32).ravel()); base.save()
            bpy.data.images.remove(ci)
            m.node_tree.nodes.remove(n); bpy.data.images.remove(bi)
        print("enemy: baked AO into", len(bakes), "materials")
    for m, n, si in scratch:
        m.node_tree.nodes.remove(n); bpy.data.images.remove(si)
    rig.data.pose_position = "POSE"
bpy.ops.object.select_all(action="DESELECT")
for o in bpy.data.objects:
    if o.type in ("MESH", "ARMATURE"):
        o.select_set(True)
bpy.ops.export_scene.gltf(export_tangents=DETAIL, filepath=OUT, export_format="GLB", use_selection=True, export_yup=True,
                          export_animations=True, export_animation_mode="NLA_TRACKS", export_skins=True,
                          export_apply=True, export_materials="EXPORT", export_image_format="AUTO",
                          export_frame_range=False, export_force_sampling=True)
with open(os.path.splitext(OUT)[0] + ".gait", "w") as _f:      # the game reads this: role, m/s (at scale 1)
    _f.write("".join(f"{k} {v:.3f}\n" for k, v in GAIT_MPS.items()))
print("enemy: wrote", OUT, os.path.getsize(OUT) // 1024, "KB;",
      sum(len(o.data.vertices) for o in bpy.data.objects if o.type == "MESH"), "verts total")

# =============================================================== 7. distance LOD
# <out>_lod.glb: the same figure decimated to ~30% of its triangles, mesh + skin ONLY (no
# animations, placeholder materials). The game draws it with the full model's materials and
# bone palette, so it must keep one primitive per material, in the same order -- checked
# here and again at load.
LOD_RATIO = float(os.environ.get("ENEMY_LOD", "0.3"))
if LOD_RATIO > 0:
    used_before = sorted({p.material_index for p in body.data.polygons})
    dm = body.modifiers.new("lod", "DECIMATE"); dm.ratio = LOD_RATIO
    bpy.context.view_layer.objects.active = body
    bpy.ops.object.modifier_move_to_index(modifier="lod", index=0)
    bpy.ops.object.modifier_apply(modifier="lod")
    used_after = sorted({p.material_index for p in body.data.polygons})
    if used_after != used_before:
        print("enemy: LOD dropped materials", sorted(set(used_before) - set(used_after)), "-- no LOD written")
    else:
        lod_out = OUT[:-4] + "_lod.glb"
        bpy.ops.object.select_all(action="DESELECT")
        for o in bpy.data.objects:
            if o.type in ("MESH", "ARMATURE"):
                o.select_set(True)
        bpy.ops.export_scene.gltf(filepath=lod_out, export_format="GLB", use_selection=True, export_yup=True,
                                  export_animations=False, export_skins=True, export_apply=True,
                                  export_materials="PLACEHOLDER")
        print("enemy: wrote LOD", lod_out, os.path.getsize(lod_out) // 1024, "KB;", len(body.data.vertices), "verts")
