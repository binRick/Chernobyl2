"""The claymore the player lays (src/mines.h), modelled from code like the traps.

    blender -b -P tools/mines/build_claymore.py                 # build both, write the assets
    blender -b -P tools/mines/build_claymore.py -- look OUT.png # preview renders only (look | hold | lay), nothing written

  assets/mines/claymore.gltf   the mine as it stands in the world, legs out -- drawn like a trap part, through the lit shader.
                               One mesh on one sheet of textures (claymore_c / _n / _orm.png: one_sheet below)
  assets/claymore_vm.glb       first person: the right arm of assets/grenadelauncher.glb ("Grenadelauncher animated", DJMaesen,
                               CC-BY-4.0 -- the same gloves as the launcher and the minigun) holding it. Clips: Idle (2 s loop), Lay (1 s).
                               No textures inside: the game gives it the mine's colour sheet and the launcher's own sleeve (src/mines.h)
  assets/claymore_vm.txt       where the mine sits in the viewmodel's rest pose, for the hand-off to the world (src/mines.h)

The mine is an M18A1: 216 x 83 x 35 mm, bowed (the front convex), FRONT TOWARD ENEMY raised on its face, two fuze wells
and a peep sight on top, two pairs of scissor legs. Materials and baked shading are the traps' (tools/traps/build_traps.py).
"""
import math, os, sys
import bpy, bmesh
from mathutils import Vector, Matrix, Quaternion

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools", "traps"))
import build_traps as bt                                        # (guarded: importing it builds nothing)

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
MODE = ARGS[0] if ARGS else "export"
OUT_DIR = os.path.join(REPO, "assets", "mines")

# ------------------------------------------------------------------ the mine (metres; x across, up, out = the way it faces)
W, H, D = 0.216, 0.083, 0.035          # M18A1 body
BOW = 0.011                            # how far the middle of the face stands out past its ends
LEG_H = 0.045                          # the legs hold the body this far off the ground
B0, B1 = LEG_H, LEG_H + H              # bottom and top of the body
def bow(x): return BOW * (1.0 - (2.0 * x / W) ** 2)


def soften(name, k):
    """Moulded plastic, not cast iron: the traps' bump (made for steel and concrete) taken down to k of itself."""
    import numpy as np
    for n in bt.MATS[name].node_tree.nodes:
        if n.type == "TEX_IMAGE" and n.image and n.image.name == name + "_n":
            px = np.array(n.image.pixels[:], np.float32).reshape(-1, 4); flat = np.array([0.5, 0.5, 1.0], np.float32)
            v = (px[:, :3] - 0.5) * 2.0; v[:, :2] *= k; v /= np.linalg.norm(v, axis=1, keepdims=True); px[:, :3] = v * 0.5 + 0.5
            n.image.pixels.foreach_set(px.ravel()); n.image.pack()


def materials():
    bt.material("claymore", (0.255, 0.275, 0.17), rough=0.62, spec=0.22, chips=0.05, rust=0.0, grime=0.28, tile=0.25, seed=81, under=(0.15, 0.16, 0.10))
    bt.material("claymore_text", (0.37, 0.39, 0.26), rough=0.55, spec=0.25, chips=0.0, rust=0.0, grime=0.15, tile=0.25, seed=82)   # raised letters, rubbed paler
    bt.material("claymore_dark", (0.12, 0.125, 0.085), rough=0.55, spec=0.25, chips=0.0, rust=0.0, grime=0.2, tile=0.25, seed=87)
    bt.material("claymore_steel", (0.40, 0.41, 0.42), rough=0.45, spec=0.5, chips=0.0, rust=0.25, grime=0.3, pattern="brushed", tile=0.25, seed=93)
    for m in ("claymore", "claymore_text", "claymore_dark"): soften(m, 0.3)


SHEET, DENS, PAD = 1024, 512, 32       # the mine's one sheet: its side (px), px to a tile of the materials above, the margin round each patch
def one_sheet(o):
    """The mine's four materials onto one sheet, so it is one mesh -- one draw, and three textures (colour, normal, ORM)
    where there were twelve. The traps' sheets tile, 1024 px to a tile; a tile here is 25 cm and the mine 22 cm long,
    so half that (DENS) is already more than a screen shows of it. From each material's sheet the patch its faces use
    is cut out whole (wrapping, with PAD px more of the same surface all round: nothing else bleeds in until the mips
    are a few px across) and the faces' UVs are moved onto it -- by whole px, so no texel is resampled."""
    import numpy as np
    me = o.data; n = len(me.loops)
    uv = np.empty(n * 2, np.float32); me.uv_layers[0].data.foreach_get("uv", uv); uv = uv.reshape(n, 2).astype(np.float64)
    lm = np.empty(n, np.int32)
    for p in me.polygons: lm[p.loop_start:p.loop_start + p.loop_total] = p.material_index
    names = [s.material.name for s in o.material_slots]
    patch = []                                                    # [slot, first px (u, v), size (w, h)] in px of its own sheet at DENS
    for k in range(len(names)):
        sel = lm == k
        if not sel.any(): continue
        i0 = np.floor(uv[sel].min(0) * DENS).astype(int) - PAD; i1 = np.ceil(uv[sel].max(0) * DENS).astype(int) + PAD
        patch.append([k, i0, i1 - i0])
    shelves = []; at = {}                                         # first fit, tallest first: [y, height, x used]
    for k, i0, wh in sorted(patch, key=lambda q: -q[2][1]):
        sh = next((s for s in shelves if s[2] + wh[0] <= SHEET and wh[1] <= s[1]), None)
        if sh is None:
            sh = [sum(s[1] for s in shelves), int(wh[1]), 0]; shelves.append(sh)
        assert sh[0] + wh[1] <= SHEET and sh[2] + wh[0] <= SHEET, "the mine's patches no longer fit its sheet: lower DENS or PAD"
        at[k] = (sh[2], sh[0]); sh[2] += int(wh[0])
    kinds = ("c", "n", "orm"); src = {}
    for k, i0, wh in patch:
        for kind in kinds:
            img = bpy.data.images[names[k] + "_" + kind]; s = img.size[0]
            px = np.array(img.pixels[:], np.float32).reshape(s, s, 4)[..., :3]
            if s > DENS: f = s // DENS; px = px.reshape(DENS, f, DENS, f, 3).mean(axis=(1, 3))
            elif s < DENS: px = px.repeat(DENS // s, axis=0).repeat(DENS // s, axis=1)
            src[k, kind] = px
    fill = {"c": np.mean([src[k, "c"].mean(axis=(0, 1)) for k, _, _ in patch], axis=0), "n": np.array([0.5, 0.5, 1.0]),
            "orm": np.mean([src[k, "orm"].mean(axis=(0, 1)) for k, _, _ in patch], axis=0)}
    sheet = {kind: np.empty((SHEET, SHEET, 3), np.float32) for kind in kinds}
    for kind in kinds: sheet[kind][:] = fill[kind]
    for k, i0, wh in patch:
        x0, y0 = at[k]; cols = (i0[0] + np.arange(wh[0])) % DENS; rows = (i0[1] + np.arange(wh[1])) % DENS
        for kind in kinds: sheet[kind][y0:y0 + wh[1], x0:x0 + wh[0]] = src[k, kind][np.ix_(rows, cols)]
        sel = lm == k
        uv[sel, 0] = (uv[sel, 0] * DENS - i0[0] + x0) / SHEET; uv[sel, 1] = (uv[sel, 1] * DENS - i0[1] + y0) / SHEET
    me.uv_layers[0].data.foreach_set("uv", uv.astype(np.float32).ravel())
    for kind in ("n", "orm"):                                     # the detail maps at half the colour's size (the normal re-normalised)
        h = sheet[kind].reshape(SHEET // 2, 2, SHEET // 2, 2, 3).mean(axis=(1, 3))
        if kind == "n": v = h * 2.0 - 1.0; v /= np.linalg.norm(v, axis=-1, keepdims=True); h = v * 0.5 + 0.5
        sheet[kind] = h
    keep = o.material_slots[0].material                           # everything onto the first material ("claymore"), its three images the sheet
    for p in me.polygons: p.material_index = 0
    while len(me.materials) > 1: me.materials.pop(index=len(me.materials) - 1)
    for nm in names:
        for kind in kinds: bpy.data.images[nm + "_" + kind].name = "was_" + nm + "_" + kind
    for nd in keep.node_tree.nodes:
        if nd.type == "TEX_IMAGE" and nd.image:
            kind = nd.image.name.rsplit("_", 1)[1]; nd.image = bt.to_img(keep.name + "_" + kind, sheet[kind], kind != "c")
    used = sum(int(wh[0]) * int(wh[1]) for _, _, wh in patch) / float(SHEET * SHEET)
    print("one_sheet: " + ", ".join(f"{names[k]} {int(wh[0])}x{int(wh[1])} at {at[k]}" for k, _, wh in patch) + f"; {used:.0%} of {SHEET} px used")


def bowed_box(x0, x1, u0, u1, o0, o1, mat, bevel, nx=24):
    """A box from x0..x1 (across), u0..u1 (up), o0..o1 (out), bent like the body: each section pushed out by bow(x)."""
    bm = bmesh.new(); secs = []
    for i in range(nx + 1):
        x = x0 + (x1 - x0) * i / nx; off = bow(x)
        secs.append([bm.verts.new(bt.P(x, u, o + off)) for (u, o) in ((u0, o0), (u1, o0), (u1, o1), (u0, o1))])
    for i in range(nx):
        for j in range(4): bm.faces.new((secs[i][j], secs[i][(j + 1) % 4], secs[i + 1][(j + 1) % 4], secs[i + 1][j]))
    bm.faces.new(secs[0]); bm.faces.new(secs[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    me = bpy.data.meshes.new("box"); bm.to_mesh(me); bm.free()
    o = bpy.data.objects.new("box", me); bpy.context.collection.objects.link(o)
    return bt.finish(o, mat, bevel)


def raised_text(text, size, up, mat):
    """Letters raised 0.7 mm off the face, bent with it."""
    bpy.ops.object.text_add(location=(0, 0, 0)); t = bpy.context.object
    t.data.body = text; t.data.align_x = "CENTER"; t.data.align_y = "CENTER"; t.data.size = size; t.data.extrude = 0.0007; t.data.resolution_u = 2
    t.data.offset = size * 0.045                                 # Blender's own font (DejaVu, Bitstream Vera licence -- free), its strokes thickened to the M18A1's plain bold capitals
    t.rotation_euler = (math.radians(90), 0, 0)                 # the letters face out (-y here) and read left to right from in front
    t.location = bt.P(0, up, D / 2)
    bpy.ops.object.convert(target="MESH"); o = bpy.context.object
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    for v in o.data.vertices: v.co.y -= bow(v.co.x)             # onto the bowed face (y here is -out)
    o.select_set(False)
    return bt.finish(o, mat, 0.0)


def build_mine():
    bt.PARTS = []
    bowed_box(-W / 2, W / 2, B0, B1, -D / 2, D / 2, "claymore", 0.0045)
    raised_text("FRONT TOWARD ENEMY", 0.0168, B0 + H * 0.58, "claymore_text")
    for x in (-0.046, 0.046):                                   # the two fuze wells, each with its shipping plug
        bt.cyl((x, B1 - 0.002, bow(x)), (x, B1 + 0.006, bow(x)), 0.0095, "claymore", seg=24, bevel=0.0015)
        bt.cyl((x, B1 + 0.006, bow(x)), (x, B1 + 0.0095, bow(x)), 0.0062, "claymore_dark", seg=20, bevel=0.0008)
    o0 = bow(0)                                                 # the peep sight: a base and two knife-edge posts
    bt.box(0, B1 + 0.0012, o0, 0.020, 0.003, 0.010, "claymore_dark", bevel=0.0008)
    for x in (-0.0045, 0.0045): bt.box(x, B1 + 0.0075, o0, 0.0035, 0.011, 0.0055, "claymore_dark", bevel=0.0006)
    for x in (-0.072, 0.072):                                   # scissor legs: a hinge under the body, a leg to the front and one to the back
        ob = bow(x)
        bt.box(x, B0 - 0.003, ob, 0.012, 0.006, 0.012, "claymore_steel", bevel=0.001)
        for (fx, fo) in ((-0.006, 0.030), (0.006, -0.032)):
            bt.cyl((x + fx * 0.3, B0 - 0.004, ob), (x + fx, 0.0015, ob + fo), 0.0022, "claymore_steel", seg=10, bevel=0.0)
            bt.cyl((x + fx, 0.0015, ob + fo), (x + fx, -0.012, ob + fo * 1.1), 0.0016, "claymore_steel", seg=8, r2=0.0004, bevel=0.0)   # the spike
    bpy.ops.object.select_all(action="DESELECT")
    for o in bt.PARTS: o.select_set(True)
    bpy.context.view_layer.objects.active = bt.PARTS[0]; bpy.ops.object.join(); o = bpy.context.object; o.name = "claymore"
    bt.shade(o, floor=True, drop=0.0)
    one_sheet(o)
    bt.PARTS = []
    return o


def look(o, out):
    sc = bpy.context.scene; sc.render.engine = "BLENDER_EEVEE"; sc.render.resolution_x, sc.render.resolution_y = 800, 600
    L = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); sc.collection.objects.link(L); L.data.energy = 3.0
    L.rotation_euler = (math.radians(50), 0, math.radians(30))
    w = bpy.data.worlds.new("w"); sc.world = w; w.use_nodes = True; w.node_tree.nodes["Background"].inputs[0].default_value = (0.5, 0.52, 0.55, 1)
    bpy.ops.mesh.primitive_plane_add(size=2, location=(0, 0, 0))
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); sc.collection.objects.link(cam); sc.camera = cam; cam.data.lens = 50
    paths = []
    for k, eye in enumerate((Vector((-0.22, -0.42, 0.24)), Vector((0.30, 0.34, 0.26)))):
        cam.location = eye; cam.rotation_euler = (Vector((0, 0, 0.07)) - eye).to_track_quat("-Z", "Y").to_euler()
        sc.render.filepath = out.replace(".png", f"_{k}.png"); bpy.ops.render.render(write_still=True); paths.append(sc.render.filepath)
    print("look:", " ".join(paths))



# ------------------------------------------------------------------ the arm (the launcher rig's own space: centimetres, forward -y, up +z, right -x)
GL = os.path.join(REPO, "assets", "grenadelauncher.glb")
EYE = Vector((12.0, 45.0, 14.0))                                # where the game's viewmodel camera sits in this space (assets/claymore_vm.txt "eye")
POLE_DROP = float(os.environ.get("POLE_DROP", "60"))           # cm the elbow's pole is lowered from where it leaves the rest pose alone
PREVIEW_FOV = 70.0                                              # the game draws it through the world's own lens (src/mines.h), not the guns' 55
RIGHT, UP, FWD = Vector((-1, 0, 0)), Vector((0, 0, 1)), Vector((0, -1, 0))
def V(r, u, f): return EYE + RIGHT * r + UP * u + FWD * f     # r right, u up and f ahead of the eye
UPPER, FORE, HAND, PALM = "R_arm_024", "R_elbow_025", "R_wrist_026", "R_palm_039"
FINGERS = {"thumb": ("R_thumb1_027", "R_thumb2_028", "R_thumb3_029"), "point": ("R_point1_031", "R_point2_032", "R_point3_033"),
           "middle": ("R_middle1_035", "R_middle2_036", "R_middle3_037"), "ring": ("R_ring1_040", "R_ring2_041", "R_ring3_042"),
           "pink": ("R_pink1_044", "R_pink2_045", "R_pink3_046")}


def load_arm(keep):
    bpy.ops.import_scene.gltf(filepath=GL)
    arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    me = next(o for o in bpy.data.objects if o.type == "MESH" and o.find_armature() == arm)
    for o in (arm, me): mw = o.matrix_world.copy(); o.parent = None; o.matrix_world = mw     # out of the Sketchfab hierarchy, where it is
    for o in list(bpy.data.objects):
        if o not in (arm, me) and o not in keep: bpy.data.objects.remove(o, do_unlink=True)  # the launcher and the empties
    bpy.ops.object.select_all(action="DESELECT"); arm.select_set(True); me.select_set(True); bpy.context.view_layer.objects.active = arm
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    arm.animation_data_clear()
    for a in list(bpy.data.actions): bpy.data.actions.remove(a)  # the launcher's own clip: everything here is authored afresh
    for pb in arm.pose.bones: pb.rotation_mode = "QUATERNION"; pb.matrix_basis.identity()
    # glTF stores no bone tails, so the importer guessed them -- the forearm's pointed back at the shoulder. Each bone is
    # made to point at its child (the wrist at the middle knuckle, the palm between its two fingers): the IK solves along
    # real limbs and a finger's bend is read off real joints. Heads stay put, so at rest the mesh does not move.
    bpy.context.view_layer.objects.active = arm; bpy.ops.object.mode_set(mode="EDIT"); eb = arm.data.edit_bones
    aim = {HAND: ["R_middle1_035"]}
    for b in eb:
        kids = aim.get(b.name) or [c.name for c in b.children]
        if kids:
            t = sum((eb[c].head for c in kids), Vector()) / len(kids)
            if (t - b.head).length > 0.2: b.use_connect = False; b.tail = t
    bpy.ops.object.mode_set(mode="OBJECT")
    names = {g.index: g.name for g in me.vertex_groups}                                       # the left arm goes: one hand lays it
    bm = bmesh.new(); bm.from_mesh(me.data); dl = bm.verts.layers.deform.active
    kill = [v for v in bm.verts if dl and v[dl] and names[max(v[dl].items(), key=lambda kv: kv[1])[0]].startswith("L_")]
    bmesh.ops.delete(bm, geom=kill, context="VERTS"); bm.to_mesh(me.data); bm.free()
    bpy.ops.object.select_all(action="DESELECT"); bpy.context.view_layer.update()
    return arm, me


def empty(name, loc):
    e = bpy.data.objects.new(name, None); bpy.context.collection.objects.link(e); e.location = loc; e.rotation_mode = "QUATERNION"; return e


def hand_frame(arm):
    """The hand's axes as posed: f wrist -> knuckles, k across the knuckles toward the index, d out of the back of the hand."""
    pb = arm.pose.bones; wh = pb[HAND].head
    f = (pb["R_middle1_035"].head - wh).normalized(); k = (pb["R_point1_031"].head - pb["R_pink1_044"].head)
    k = (k - f * k.dot(f)).normalized(); return f, k, f.cross(k)


def rig(arm):
    """IK on the forearm (to an empty at the wrist), the wrist's rotation copied from a second empty, the elbow kept out by a pole."""
    pb = arm.pose.bones; bpy.context.view_layer.update()
    rest_elbow = pb[FORE].head.copy(); rest_wrist = pb[HAND].head.copy()
    tgt = empty("hand_ik", pb[FORE].tail.copy()); rot = empty("hand_rot", Vector())
    rot.rotation_quaternion = (arm.matrix_world @ pb[HAND].matrix).to_quaternion()
    pole = empty("elbow_pole", rest_elbow + RIGHT * 25 + UP * -10 + FWD * -5)
    ik = pb[FORE].constraints.new("IK"); ik.target = tgt; ik.pole_target = pole; ik.chain_count = 2
    cr = pb[HAND].constraints.new("COPY_ROTATION"); cr.target = rot
    best = None                                                   # the pole angle that leaves the rest pose where it was
    for a in range(-180, 181, 5):
        ik.pole_angle = math.radians(a); bpy.context.view_layer.update()
        e = (pb[FORE].head - rest_elbow).length
        if best is None or e < best[0]: best = (e, a)
    ik.pole_angle = math.radians(best[1]); pole.location += UP * -POLE_DROP; bpy.context.view_layer.update()   # (then the elbow dropped: the forearm comes up from below, as the guns' do)
    print(f"rig: pole angle {best[1]} deg (elbow {best[0]:.2f} off rest); wrist head vs forearm tail {(rest_wrist - pb[FORE].tail).length:.3f}")
    return tgt, rot, pole


def bend(arm, k_fingers, k_thumb):
    """Open the fist: each finger joint's bend reduced by k (0 as it is, 1 straight; < 0 closes it). Pose-space, so the rig's own axes never matter."""
    pb = arm.pose.bones
    for name, chain in FINGERS.items():
        k = k_thumb if name == "thumb" else k_fingers
        for i, bn in enumerate(chain):
            b = pb[bn]; prev = (b.head - b.parent.head).normalized() if i == 0 else (pb[chain[i - 1]].tail - pb[chain[i - 1]].head).normalized()
            d = (b.tail - b.head).normalized(); ax = prev.cross(d); ang = prev.angle(d)
            if ax.length < 1e-6 or ang < 1e-4: continue
            R = Matrix.Translation(b.head) @ Matrix.Rotation(-k * ang, 4, ax.normalized()) @ Matrix.Translation(-b.head)
            b.matrix = R @ b.matrix; bpy.context.view_layer.update()


def finger_pose(arm):
    return {bn: arm.pose.bones[bn].matrix_basis.copy() for chain in FINGERS.values() for bn in chain}


def set_fingers(arm, pose):
    for bn, m in pose.items(): arm.pose.bones[bn].matrix_basis = m
    bpy.context.view_layer.update()


def aim_hand(arm, rot, f_d, k_d):
    """Turn the hand (through the rotation empty) so its knuckle line runs along f_d and the index side faces k_d."""
    bpy.context.view_layer.update()
    f0, k0, d0 = hand_frame(arm)
    k_d = (k_d - f_d * k_d.dot(f_d)).normalized(); d_d = f_d.cross(k_d)
    M0 = Matrix((f0, k0, d0)).transposed(); Md = Matrix((f_d, k_d, d_d)).transposed()
    rot.rotation_quaternion = (Md @ M0.inverted()).to_quaternion() @ rot.rotation_quaternion
    bpy.context.view_layer.update()


def preview_cam(out_png):
    sc = bpy.context.scene
    cam = bpy.data.objects.get("cam") or bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    if cam.name not in sc.collection.objects: sc.collection.objects.link(cam)
    sc.camera = cam; cam.data.sensor_fit = "VERTICAL"; cam.data.angle = math.radians(PREVIEW_FOV); cam.data.clip_start = 0.5; cam.data.clip_end = 3000
    cam.location = EYE; cam.rotation_euler = FWD.to_track_quat("-Z", "Y").to_euler()
    if not bpy.data.objects.get("sun"):
        L = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); sc.collection.objects.link(L); L.data.energy = 4.0
        L.rotation_euler = (math.radians(40), math.radians(-20), math.radians(-30))
        w = bpy.data.worlds.new("w"); sc.world = w; w.use_nodes = True; w.node_tree.nodes["Background"].inputs[0].default_value = (0.42, 0.44, 0.47, 1)
    sc.render.engine = "BLENDER_EEVEE"; sc.render.resolution_x, sc.render.resolution_y = 960, 540
    sc.render.filepath = out_png; bpy.ops.render.render(write_still=True); print("render:", out_png)


# ------------------------------------------------------------------ the hold
HOLD_W = V(17.0, -13.0, 32.0)          # the wrist, holding it
HOLD_TURN = ((UP, -18.0), (RIGHT, 16.0))  # the hand turned from the launcher's grip: to the right (it reaches forward-left into the room), then its top tipped to the eye
K_OPEN = (0.30, 0.06)                 # how far the fist opens round it (fingers, thumb: kept curled over its top)
PALM_IN, OFF_F, OFF_K = 1.6, 0.0, 0.0  # the end face this far inside the palm's bones; the mine slid along the fingers / up the index


def turn_hand(rot, turns):
    """Turn the hand about its wrist (world axes, degrees, in order) through the rotation empty."""
    for ax, deg in turns: rot.rotation_quaternion = Quaternion(ax, math.radians(deg)) @ rot.rotation_quaternion
    bpy.context.view_layer.update()


def mine_in_hand(mine, arm):
    """A centimetre copy of the mine, stood in the hand: its right end against the palm, up the index side, its face the way the fingers point."""
    pb = arm.pose.bones; f, k, d = hand_frame(arm)
    palm = (pb[HAND].head + pb["R_point1_031"].head + pb["R_pink1_044"].head) / 3 - d * PALM_IN + f * OFF_F + k * OFF_K
    anchor = bt.P(-W / 2, B0 + H * 0.5, 0.0)                    # its right end, half way up (metres, its own frame)
    R = Matrix((-d, -f, k)).transposed().to_4x4()               # its x (across) -> out of the palm, its -out -> back along the fingers, up -> the index side
    M = Matrix.Translation(palm) @ R @ Matrix.Scale(100.0, 4) @ Matrix.Translation(-anchor)
    o = bpy.data.objects.get("claymore_vm")
    if o is None: o = mine.copy(); o.data = mine.data.copy(); o.name = "claymore_vm"; bpy.context.collection.objects.link(o)
    o.hide_render = False; o.matrix_world = M; bpy.context.view_layer.update(); return o


def follow(o, arm):
    """Ride the hand while the clips are authored (it becomes skinned to the wrist at export)."""
    mw = o.matrix_world.copy(); o.parent = arm; o.parent_type = "BONE"; o.parent_bone = HAND
    bpy.context.view_layer.update(); o.matrix_world = mw; bpy.context.view_layer.update()


# ------------------------------------------------------------------ laying it
LAY_PITCH = 60.0                      # the view's dip at the kneel, degrees down    (src/mines.h MINE_DIP_PITCH)
LAY_EYE = 62.0                        # the eye's height over the floor at the kneel, cm ("dip" in assets/claymore_vm.txt)
LAY_LEAN = 25.0                       # and how far the body leans forward to set it down, cm (the game moves the eye; the clip is relative to it)
LAND_AHEAD, LAND_RIGHT = 32.0, 3.0    # where it is set down: this far ahead of the leaning, kneeling eye along the floor, and to the right, cm
REACH_MAX = 0.95 * (31.8 + 32.1)      # the furthest the wrist goes from the shoulder: an arm short of locked straight
def floor_plane():
    """The floor as the kneeling, dipped eye sees it, in this (eye-fixed) space: a point on it and its up."""
    p = math.radians(LAY_PITCH); n = UP * math.cos(p) - FWD * math.sin(p); return EYE - n * LAY_EYE, n


POSES = {                             # the wrist, the hand's turn from the launcher's grip (world axes, degrees, in order), the fist's opening
    "hold":    [HOLD_W, HOLD_TURN, K_OPEN],
    "lift":    [HOLD_W + UP * 1.6 - FWD * 1.2, HOLD_TURN, K_OPEN],
    "reach":   [V(6.0, -18.0, 48.0), ((RIGHT, LAY_PITCH),), K_OPEN],          # tipped with the view: upright on the floor the dipped eye sees
    "release": [None, ((RIGHT, LAY_PITCH),), (0.62, 0.45)],                  # (where reach ended up, a hair higher) the fingers open off it
    "away":    [V(18.0, -34.0, 28.0), ((RIGHT, 35.0),), (0.45, 0.3)],        # gone, under the bottom of the view
}
LAY_KEYS = [(0, "hold"), (3, "lift"), (10, "reach"), (12, "release"), (13, "release"), (17, "away"), (19, "away"), (24, "hold")]
LAY_FRAMES, LAY_RELEASE, LAY_HIDDEN = 24, 12, (17, 19)   # 24 fps; the game swaps the held mine for the world's at LAY_RELEASE (src/mines.h)
IDLE_KEYS = [(0, (0.0, 0.0)), (12, (0.35, 0.12)), (24, (0.0, 0.0)), (36, (-0.25, -0.08)), (48, (0.0, 0.0))]   # (up, right) cm: breathing


def pose_to(arm, tgt, rot, q0, rest, pose):
    w, turns, kopen = pose
    tgt.location = w; rot.rotation_quaternion = q0.copy(); turn_hand(rot, turns)
    set_fingers(arm, rest); bend(arm, *kopen)


def key_now(arm, tgt, rot, frame):
    tgt.keyframe_insert("location", frame=frame); rot.keyframe_insert("rotation_quaternion", frame=frame)
    for chain in FINGERS.values():
        for bn in chain: arm.pose.bones[bn].keyframe_insert("rotation_quaternion", frame=frame)


def land(arm, tgt, rot, q0, rest, cvm, clearance=0.6):
    """Bring its feet down on the floor the dipped eye sees, LAND_AHEAD out along it -- as near as the arm reaches
    (never past REACH_MAX: the IK would only stretch). Moves the reach's wrist by what is left over, a few times."""
    p0, n = floor_plane(); pr = math.radians(LAY_PITCH); h = UP * math.sin(pr) + FWD * math.cos(pr)   # along the floor, straight ahead
    target = EYE + h * LAND_AHEAD + RIGHT * LAND_RIGHT - n * (LAY_EYE - clearance)
    sh = arm.pose.bones[UPPER].head.copy()
    for _ in range(10):
        pose_to(arm, tgt, rot, q0, rest, POSES["reach"]); bpy.context.view_layer.update()
        foot = cvm.matrix_world @ Vector((0, 0, 0))
        w = POSES["reach"][0] + (target - foot); off = w - sh
        if off.length > REACH_MAX: w = sh + off.normalized() * REACH_MAX
        POSES["reach"][0] = w
    pose_to(arm, tgt, rot, q0, rest, POSES["reach"]); bpy.context.view_layer.update()
    foot = cvm.matrix_world @ Vector((0, 0, 0)); low = min((cvm.matrix_world @ v.co - p0).dot(n) for v in cvm.data.vertices)
    POSES["release"][0] = POSES["reach"][0] + n * 0.8
    print(f"land: its feet {(foot - EYE).dot(h):.1f} cm ahead (asked {LAND_AHEAD}), {(foot - p0).dot(n):.1f} cm over the floor, lowest point {low:.1f}; "
          f"shoulder to wrist {(POSES['reach'][0] - sh).length:.1f} of {31.8 + 32.1:.1f} cm")


def author_lay(arm, tgt, rot, q0, rest):
    for ob in (tgt, rot): ob.animation_data_clear()
    if arm.animation_data: arm.animation_data.action = None      # (its baked clips stay on their NLA tracks)
    for fr, name in LAY_KEYS: pose_to(arm, tgt, rot, q0, rest, POSES[name]); key_now(arm, tgt, rot, fr)


def author_idle(arm, tgt, rot, q0, rest):
    for ob in (tgt, rot): ob.animation_data_clear()
    if arm.animation_data: arm.animation_data.action = None      # (its baked clips stay on their NLA tracks)
    for fr, (u, r) in IDLE_KEYS:
        pose_to(arm, tgt, rot, q0, rest, [POSES["hold"][0] + UP * u + RIGHT * r, HOLD_TURN, K_OPEN]); key_now(arm, tgt, rot, fr)


def floor_preview():
    p0, n = floor_plane()
    bpy.ops.mesh.primitive_plane_add(size=600, location=p0); fl = bpy.context.object; fl.name = "floor_preview"
    fl.rotation_euler = n.to_track_quat("Z", "Y").to_euler()
    m = bpy.data.materials.new("floor_preview"); m.use_nodes = True; m.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.32, 0.30, 0.27, 1)
    fl.data.materials.append(m); return fl


def closeup(out_png, target, offset, fov=38):
    sc = bpy.context.scene; cam = bpy.data.objects["cam"]; keep = (cam.location.copy(), cam.rotation_euler.copy(), cam.data.angle)
    cam.location = target + offset; cam.rotation_euler = (-offset).to_track_quat("-Z", "Y").to_euler(); cam.data.angle = math.radians(fov)
    sc.render.filepath = out_png; bpy.ops.render.render(write_still=True); print("render:", out_png)
    cam.location, cam.rotation_euler, cam.data.angle = keep


# ------------------------------------------------------------------ export
VM_PATH = os.path.join(REPO, "assets", "claymore_vm.glb")
FRAME_PATH = os.path.join(REPO, "assets", "claymore_vm.txt")
def gl(v): return (v.x, v.z, -v.y)                               # Blender (z up) -> glTF / raylib (y up)


def bake(arm, name, frames):
    """The IK and copied rotation, sampled into plain keys on every bone: a clip raylib plays like any gun's. One NLA track each (the export's animations)."""
    sc = bpy.context.scene; sc.render.fps = 24; sc.frame_start, sc.frame_end = 0, frames
    bpy.ops.object.select_all(action="DESELECT"); arm.select_set(True); bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="POSE"); bpy.ops.pose.select_all(action="SELECT"); pre = arm.animation_data.action if arm.animation_data else None
    bpy.ops.nla.bake(frame_start=0, frame_end=frames, only_selected=False, visual_keying=True, clear_constraints=False,
                     use_current_action=False, bake_types={"POSE"})
    bpy.ops.object.mode_set(mode="OBJECT")
    act = arm.animation_data.action; act.name = name; act.use_fake_user = True; arm.animation_data.action = None
    tr = arm.animation_data.nla_tracks.new(); tr.name = name; tr.strips.new(name, 0, act); tr.mute = True
    if pre is not None and pre != act: bpy.data.actions.remove(pre)  # the finger keys it was baked from
    return act


def skin_to_wrist(cvm, arm):
    """Its vertices taken back into the rest pose and weighted wholly to the wrist; returns its frame (ground origin, axes) in that rest pose."""
    bpy.context.view_layer.update()
    D = arm.pose.bones[HAND].matrix @ arm.data.bones[HAND].matrix_local.inverted()        # rest -> hold, for anything on the wrist
    mw = cvm.matrix_world.copy(); Dinv = D.inverted()
    origin = Dinv @ (mw @ Vector((0, 0, 0))); R = Dinv.to_3x3() @ mw.to_3x3()
    cvm.parent = None; cvm.matrix_world = Matrix.Identity(4); cvm.data.transform(Dinv @ mw)
    vg = cvm.vertex_groups.new(name=HAND); vg.add(list(range(len(cvm.data.vertices))), 1.0, "REPLACE")
    md = cvm.modifiers.new("arm", "ARMATURE"); md.object = arm; cvm.parent = arm
    return origin, (R @ Vector((1, 0, 0))).normalized(), (R @ Vector((0, 0, 1))).normalized(), (R @ Vector((0, -1, 0))).normalized()


def export_world(mine):
    os.makedirs(OUT_DIR, exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT"); mine.select_set(True); bpy.context.view_layer.objects.active = mine
    path = os.path.join(OUT_DIR, "claymore.gltf")
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLTF_SEPARATE", use_selection=True, export_yup=True, export_apply=True, export_tangents=True,
                              export_materials="EXPORT", export_image_format="AUTO", export_texture_dir="", export_animations=False,
                              export_vertex_color="ACTIVE", export_all_vertex_colors=False, export_active_vertex_color_when_no_material=True)
    bt.no_images(path)                                            # (the sheet stays beside it as three files; the game binds them by the material's name, once)
    print(f"export: {path} ({len(mine.data.polygons)} faces)")


def export_vm(arm, me, cvm, frame):
    """No images go into the file (export_image_format NONE): the game draws the viewmodel with raylib's default shader --
    texture x vertex colour, as the guns are -- and gives it textures it has already loaded: the mine's colour sheet
    (assets/mines/claymore_c.png) and the launcher's own sleeve and glove. Its materials keep their names for that,
    and are left plain white: the game's texture is all the colour there is."""
    for o in (me, cvm):
        for slot in o.material_slots:
            nt = slot.material.node_tree if slot.material and slot.material.use_nodes else None
            b = next((n for n in nt.nodes if n.type == "BSDF_PRINCIPLED"), None) if nt else None
            if b is None: continue
            for l in list(b.inputs["Base Color"].links): nt.links.remove(l)
            b.inputs["Base Color"].default_value = (1.0, 1.0, 1.0, 1.0)
    bpy.ops.object.select_all(action="DESELECT")
    for o in (arm, me, cvm): o.select_set(True)
    bpy.context.view_layer.objects.active = arm
    for tr in arm.animation_data.nla_tracks: tr.mute = False
    me.parent = arm                                               # (the exporter wants the skin's armature as the mesh's parent)
    for tr in arm.animation_data.nla_tracks:
        print("nla:", tr.name, "mute", tr.mute, [(st.name, st.action.name if st.action else None, str(getattr(st, "action_slot", None)), st.frame_start, st.frame_end) for st in tr.strips])
    print("actions:", [(a.name, [str(sl.identifier) for sl in getattr(a, "slots", [])], tuple(a.frame_range)) for a in bpy.data.actions])
    bpy.ops.export_scene.gltf(filepath=VM_PATH, export_format="GLB", use_selection=True, export_yup=True, export_apply=False, export_skins=True,
                              export_def_bones=False, export_rest_position_armature=True, export_animations=True, export_animation_mode="NLA_TRACKS",
                              export_force_sampling=True, export_frame_step=1, export_materials="EXPORT", export_image_format="NONE",
                              export_vertex_color="ACTIVE", export_all_vertex_colors=False, export_active_vertex_color_when_no_material=True)
    o, a, u, f = frame
    with open(FRAME_PATH, "w") as fh:                              # read by src/mines.h (MinesLoad); glTF axes, the model's own units (cm)
        fh.write("claymore_vm 1\n")
        fh.write("origin %.4f %.4f %.4f\n" % gl(o)); fh.write("across %.5f %.5f %.5f\n" % gl(a)); fh.write("up %.5f %.5f %.5f\n" % gl(u)); fh.write("out %.5f %.5f %.5f\n" % gl(f))
        fh.write("eye %.3f %.3f %.3f\n" % gl(EYE)); fh.write("dip %.2f %.2f %.2f\n" % (LAY_PITCH, LAY_EYE, LAY_LEAN))
        fh.write("lay %d %d %d %d\n" % (LAY_FRAMES, LAY_RELEASE, LAY_HIDDEN[0], LAY_HIDDEN[1])); fh.write("fps 24\n")
    print(f"export: {VM_PATH} ({os.path.getsize(VM_PATH) / 1e6:.1f} MB) + {FRAME_PATH}")

if __name__ == "__main__":
    bpy.ops.wm.read_factory_settings(use_empty=True)
    if MODE != "look": arm, me = load_arm([])                     # (first: the materials below reuse its "glTF Material Output" group; the other way
    materials()                                                   #  round its importer finds the traps' one-socket group and fails)
    mine = build_mine()
    if MODE == "look": look(mine, ARGS[1])
    if MODE == "rig":
        pb = arm.pose.bones
        for n in (UPPER, FORE, HAND, PALM): b = arm.data.bones[n]; print(f"bone {n}: head {tuple(round(v,2) for v in b.head_local)} tail {tuple(round(v,2) for v in b.tail_local)} parent {b.parent.name if b.parent else None} connect {b.use_connect}")
        f, k, d = hand_frame(arm); print("hand frame f", tuple(round(v,2) for v in f), "k", tuple(round(v,2) for v in k), "d", tuple(round(v,2) for v in d))
        print("arm verts left", len(me.data.vertices), "upper arm", round((arm.data.bones[UPPER].tail_local-arm.data.bones[UPPER].head_local).length,1), "forearm", round((arm.data.bones[FORE].tail_local-arm.data.bones[FORE].head_local).length,1))
        tgt, rot, pole = rig(arm)
        rest = finger_pose(arm)
        for kf in (0.0, 0.15, 0.25, 0.35, 0.45, 0.55):
            set_fingers(arm, rest); bend(arm, kf, kf * 0.6)
            gap = (pb["Joint_3_8_038"].head - pb["Joint_3_6_030"].head).length; gi = (pb["Joint_3_7_034"].head - pb["Joint_3_6_030"].head).length
            print(f"open {kf:.2f}: middle tip - thumb tip {gap:.2f} cm, index tip - thumb tip {gi:.2f} cm")
        mine.hide_render = True; preview_cam(ARGS[1])
    if MODE in ("hold", "lay", "export"):
        pb = arm.pose.bones; tgt, rot, pole = rig(arm); rest = finger_pose(arm); q0 = rot.rotation_quaternion.copy()
        pose_to(arm, tgt, rot, q0, rest, POSES["hold"])
        mine.hide_render = True; mvm = mine_in_hand(mine, arm)
    if MODE == "lay":
        follow(mvm, arm); land(arm, tgt, rot, q0, rest, mvm); author_lay(arm, tgt, rot, q0, rest); floor_preview()
        sc = bpy.context.scene; outs = []
        for fr in (0, 3, 6, 10, 12, 15, 17, 21, 24):
            sc.frame_set(fr); o = ARGS[1].replace(".png", f"_{fr:02d}.png"); preview_cam(o); outs.append(o)
    if MODE == "export":
        export_world(mine)
        follow(mvm, arm); land(arm, tgt, rot, q0, rest, mvm)
        author_lay(arm, tgt, rot, q0, rest); bake(arm, "Lay", LAY_FRAMES)
        author_idle(arm, tgt, rot, q0, rest); bake(arm, "Idle", 48)
        for b in arm.pose.bones:
            for c in list(b.constraints): b.constraints.remove(c)
        for o in (tgt, rot, pole): bpy.data.objects.remove(o, do_unlink=True)
        arm.animation_data.nla_tracks["Idle"].mute = False; bpy.context.scene.frame_set(0); bpy.context.view_layer.update()   # the hold: frame 0 of either clip
        frame = skin_to_wrist(mvm, arm)
        export_vm(arm, me, mvm, frame)
    if MODE == "hold":
        out = ARGS[1]; preview_cam(out)
        hc = (pb[HAND].head + pb["R_middle2_036"].head) / 2
        closeup(out.replace(".png", "_top.png"), hc, UP * 26 + RIGHT * -12 + FWD * 8)
        closeup(out.replace(".png", "_front.png"), hc, FWD * 30 + RIGHT * 8 + UP * 6)
