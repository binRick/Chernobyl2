"""The plant's traps, modelled: assets/traps/<part>.glb (src/traps.h draws and animates them).

    blender -b -P tools/traps/build_traps.py            # all parts
    blender -b -P tools/traps/build_traps.py -- crane   # only parts whose name starts with this

Everything is built here from code -- bevelled, split-normal geometry, and tileable weathered materials painted in
numpy (colour, tangent-space normal, packed R occlusion / G roughness / B specular, as the game's lit shader reads
them from a glTF's occlusion slot). No downloaded assets. A part is modelled in its trap's own frame, the one traps.h
uses: x across, y up, z OUT from the middle of the ring; moving parts (valve wheel, crane load, door slab, rotor)
are separate files modelled about their own pivot.
"""
import math, os, random, sys
import bpy, bmesh
import numpy as np
from mathutils import Vector, Matrix, Euler

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(REPO, "assets", "traps"); os.makedirs(OUT, exist_ok=True)
ONLY = sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv and len(sys.argv) > sys.argv.index("--") + 1 else ""
S = 1024
rnd = random.Random(11)


# ------------------------------------------------------------------ textures
def fbm(beta, seed, aniso=(1.0, 1.0)):
    """Tileable noise in 0..1: random phases under a 1/f^beta spectrum (aniso stretches it along x / y)."""
    rng = np.random.default_rng(seed)
    fy = np.fft.fftfreq(S)[:, None] * aniso[1]; fx = np.fft.rfftfreq(S)[None, :] * aniso[0]
    f = np.sqrt(fx * fx + fy * fy); f[0, 0] = 1.0
    spec = (rng.standard_normal((S, S // 2 + 1)) + 1j * rng.standard_normal((S, S // 2 + 1))) / f ** beta
    spec[0, 0] = 0
    n = np.fft.irfft2(spec, s=(S, S)); n -= n.min(); return (n / n.max()).astype(np.float32)


def sstep(x, a, b):
    t = np.clip((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t)


def to_img(name, arr, data=False):
    n = arr.shape[0]; img = bpy.data.images.new(name, n, n, alpha=False)
    rgba = np.ones((n, n, 4), np.float32); rgba[..., :arr.shape[-1]] = np.clip(arr, 0, 1)
    img.pixels.foreach_set(rgba.ravel()); img.pack()
    if data: img.colorspace_settings.name = "Non-Color"
    return img


def normal_of(h, k):
    gy, gx = np.gradient(h); n = np.stack([-gx * k, -gy * k, np.ones_like(h)], -1)
    return n / np.linalg.norm(n, axis=-1, keepdims=True) * 0.5 + 0.5


MATS = {}
def material(name, base, rough=0.55, spec=0.35, chips=0.25, rust=0.3, grime=0.35, pattern=None, tile=2.0, seed=1, under=(0.20, 0.19, 0.18)):
    """Painted (or bare) weathered metal / concrete. chips: how much paint is gone to `under`; rust: brown bloom and streaks."""
    yy, xx = np.mgrid[0:S, 0:S].astype(np.float32) / S
    n_lo, n_mid, n_hi = fbm(2.2, seed), fbm(1.5, seed + 1), fbm(0.7, seed + 2)
    streak = fbm(1.8, seed + 3, aniso=(0.5, 7.0))                                     # runs down the surface (v is up on every side face)
    n_fine = fbm(0.25, seed + 4)
    col = np.empty((S, S, 3), np.float32); col[:] = base
    h = n_hi * 0.06; r = np.full((S, S), rough, np.float32); sp = np.full((S, S), spec, np.float32)
    if pattern == "hazard":                                                           # yellow / black diagonals, worn
        band = ((xx + yy) * 4.0) % 1.0 < 0.5
        col[band] = (0.82, 0.62, 0.05); col[~band] = (0.05, 0.05, 0.055); h += np.where(band, 0.02, 0.0)
    elif pattern == "diamond":                                                        # tread plate
        u, v = (xx * 16) % 1.0 - 0.5, (yy * 16) % 1.0 - 0.5; odd = ((np.floor(xx * 16) + np.floor(yy * 16)) % 2) > 0
        a, b = np.where(odd, u + v, u - v), np.where(odd, u - v, u + v)
        loz = np.clip(1.0 - np.maximum(np.abs(a) / 0.34, np.abs(b) / 0.10), 0, 1)
        h += sstep(loz, 0.0, 0.5) * 0.55; col *= (0.9 + 0.25 * sstep(loz, 0.2, 0.7))[..., None]
    elif pattern == "brushed":
        h += fbm(1.0, seed + 5, aniso=(0.15, 6.0)) * 0.10
    elif pattern == "concrete":
        pits = sstep(fbm(0.4, seed + 6), 0.74, 0.80); h -= pits * 0.5; col *= (1.0 - 0.35 * pits)[..., None]
        col *= (0.75 + 0.5 * n_mid)[..., None]
    elif pattern == "canvas":
        weave = 0.5 + 0.25 * np.sin(xx * 6.2832 * 90) + 0.25 * np.sin(yy * 6.2832 * 90); h += weave * 0.12; col *= (0.85 + 0.2 * weave)[..., None]
    col *= (0.84 + 0.22 * n_mid + 0.10 * n_fine)[..., None]; h += n_fine * 0.05; r = np.clip(r + (n_fine - 0.5) * 0.18, 0.05, 1.0)
    if chips > 0:                                                                     # paint gone in patches: bare, darker metal underneath, a hair lower
        mix_ = n_mid * 0.45 + n_hi * 0.55; q = float(np.quantile(mix_, 1.0 - chips * 0.45))      # this share of the paint, in flakes of every size
        m = sstep(mix_, q, q + 0.012)
        col = col * (1 - m[..., None]) + np.array(under, np.float32) * m[..., None]; h -= m * 0.22
        r = r * (1 - m) + 0.42 * m; sp = sp * (1 - m) + 0.5 * m
    if rust > 0:                                                                      # rust: blooms where the low noise is high, and streaks below them
        ql, qs = float(np.quantile(n_lo * 0.7 + n_mid * 0.3, 1.0 - rust * 0.45)), float(np.quantile(streak, 1.0 - rust * 0.5))
        m = np.clip(sstep(n_lo * 0.7 + n_mid * 0.3, ql, ql + 0.10) + sstep(streak, qs, qs + 0.12) * 0.75, 0, 1) * (0.5 + 0.5 * n_hi)
        rc = np.stack([0.34 + 0.16 * n_hi, 0.15 + 0.07 * n_hi, 0.05 + 0.02 * n_hi], -1)
        col = col * (1 - m[..., None]) + rc * m[..., None]; r = r * (1 - m) + 0.92 * m; sp = sp * (1 - m) + 0.08 * m; h += m * n_hi * 0.10
    g = sstep(n_lo * 0.5 + streak * 0.5, 1.0 - grime * 0.8, 1.0)                        # grime: soot and oil in the low places
    col *= (1.0 - 0.55 * g)[..., None]; r = np.clip(r + 0.2 * g, 0, 1)
    occ = np.clip(0.80 + 0.20 * sstep(h, h.mean() - 0.12, h.mean() + 0.05), 0, 1)
    m = bpy.data.materials.new(name); m.use_nodes = True; nt = m.node_tree; b = nt.nodes["Principled BSDF"]
    tc = nt.nodes.new("ShaderNodeTexImage"); tc.image = to_img(name + "_c", col); nt.links.new(tc.outputs["Color"], b.inputs["Base Color"])
    tn = nt.nodes.new("ShaderNodeTexImage"); tn.image = to_img(name + "_n", normal_of(h, 34.0), True)
    nm = nt.nodes.new("ShaderNodeNormalMap"); nt.links.new(tn.outputs["Color"], nm.inputs["Color"]); nt.links.new(nm.outputs["Normal"], b.inputs["Normal"])
    g_ = bpy.data.node_groups.get("glTF Material Output")
    if g_ is None:
        g_ = bpy.data.node_groups.new("glTF Material Output", "ShaderNodeTree"); g_.interface.new_socket("Occlusion", in_out="INPUT", socket_type="NodeSocketFloat")
    to = nt.nodes.new("ShaderNodeTexImage"); to.image = to_img(name + "_orm", np.stack([occ, r, sp], -1)[::2, ::2], True)
    go = nt.nodes.new("ShaderNodeGroup"); go.node_tree = g_; nt.links.new(to.outputs["Color"], go.inputs["Occlusion"])
    b.inputs["Roughness"].default_value = rough; b.inputs["Metallic"].default_value = 0.0
    m["tile"] = tile; MATS[name] = m; return m


def build_materials():
    material("yellow", (0.78, 0.56, 0.06), rough=0.5, chips=0.30, rust=0.35, grime=0.3, seed=3)                # crane paint
    material("grey", (0.30, 0.32, 0.34), rough=0.55, chips=0.22, rust=0.28, grime=0.4, seed=9)                 # machine grey
    material("dark", (0.10, 0.105, 0.115), rough=0.5, chips=0.18, rust=0.2, grime=0.3, seed=15, under=(0.3, 0.3, 0.31))
    material("bare", (0.46, 0.47, 0.48), rough=0.52, spec=0.42, chips=0.0, rust=0.22, grime=0.3, pattern="brushed", seed=21)   # pipe, cable, rotor
    material("red", (0.55, 0.06, 0.04), rough=0.45, chips=0.28, rust=0.22, grime=0.3, seed=27)
    material("teal", (0.08, 0.36, 0.33), rough=0.5, chips=0.25, rust=0.4, grime=0.35, seed=33)
    material("hazard", (0.8, 0.6, 0.05), rough=0.55, chips=0.30, rust=0.2, grime=0.35, pattern="hazard", tile=1.0, seed=39)
    material("plate", (0.26, 0.27, 0.29), rough=0.5, spec=0.42, chips=0.0, rust=0.3, grime=0.45, pattern="diamond", tile=1.6, seed=45)
    material("concrete", (0.42, 0.41, 0.39), rough=0.9, spec=0.05, chips=0.0, rust=0.0, grime=0.5, pattern="concrete", tile=3.0, seed=51)
    material("copper", (0.62, 0.30, 0.14), rough=0.45, spec=0.55, chips=0.0, rust=0.0, grime=0.35, pattern="brushed", seed=57)
    material("canvas", (0.50, 0.44, 0.30), rough=0.95, spec=0.03, chips=0.0, rust=0.0, grime=0.45, pattern="canvas", tile=0.6, seed=63)
    material("ceramic", (0.62, 0.50, 0.36), rough=0.2, spec=0.8, chips=0.0, rust=0.0, grime=0.3, seed=69)


# ------------------------------------------------------------------ geometry (trap frame: x across, up, out)
PARTS = []
def P(x, up, out): return Vector((x, -out, up))


def finish(o, mat, bevel):
    bpy.context.view_layer.objects.active = o; o.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    if bevel > 0:
        md = o.modifiers.new("b", "BEVEL"); md.width = bevel; md.segments = 2; md.limit_method = "ANGLE"; md.angle_limit = math.radians(40)
        bpy.ops.object.modifier_apply(modifier="b")
    for p in o.data.polygons: p.use_smooth = True
    md = o.modifiers.new("e", "EDGE_SPLIT"); md.split_angle = math.radians(42); bpy.ops.object.modifier_apply(modifier="e")
    m = MATS[mat]; o.data.materials.append(m); tile = m["tile"]
    me = o.data; uv = me.uv_layers.new(name="UVMap") if not me.uv_layers else me.uv_layers[0]            # box projection, in metres: the weathering runs unbroken over a part
    for poly in me.polygons:
        n = poly.normal; ax = max(range(3), key=lambda i: abs(n[i]))
        for li in poly.loop_indices:
            co = me.vertices[me.loops[li].vertex_index].co
            u, v = (co.y, co.z) if ax == 0 else (co.x, co.z) if ax == 1 else (co.x, co.y)
            uv.data[li].uv = (u / tile + 0.137 * ax, v / tile + 0.291 * ax)
    o.select_set(False); PARTS.append(o); return o


def box(x, up, out, sx, sup, sout, mat="grey", bevel=0.012, rot=(0, 0, 0)):
    """Centre (x, up, out), full sizes; rot = (about across, about up, about out) in degrees."""
    bpy.ops.mesh.primitive_cube_add(size=1, location=P(x, up, out)); o = bpy.context.object
    o.scale = (sx, sout, sup); o.rotation_euler = Euler((math.radians(rot[0]), -math.radians(rot[2]), math.radians(rot[1])))
    return finish(o, mat, min(bevel, 0.45 * min(sx, sup, sout)))


def cyl(a, b, r, mat="bare", seg=28, r2=None, bevel=0.006, wall=0.0):
    """A round bar / cone from a to b (trap-frame points). wall > 0: a hollow tube of that thickness, open at both ends."""
    A, B = P(*a), P(*b); d = B - A; L = d.length
    bpy.ops.mesh.primitive_cone_add(vertices=seg, radius1=r, radius2=(r if r2 is None else r2), depth=L, location=(A + B) / 2,
                                    end_fill_type=("NOTHING" if wall > 0 else "NGON")); o = bpy.context.object
    o.rotation_euler = Vector((0, 0, 1)).rotation_difference(d.normalized()).to_euler()
    if wall > 0:
        bpy.context.view_layer.objects.active = o; md = o.modifiers.new("s", "SOLIDIFY"); md.thickness = wall; md.offset = -1.0; bpy.ops.object.modifier_apply(modifier="s")
    return finish(o, mat, bevel if min(r, r2 or r) > 0.02 else 0.0)


def torus(c, major, minor, axis, mat="bare", seg=36):
    bpy.ops.mesh.primitive_torus_add(major_radius=major, minor_radius=minor, major_segments=seg, minor_segments=12, location=P(*c)); o = bpy.context.object
    o.rotation_euler = Vector((0, 0, 1)).rotation_difference((P(*axis)).normalized()).to_euler()
    return finish(o, mat, 0.0)


def dome(c, r, squash, mat):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=12, radius=r, location=P(*c)); o = bpy.context.object; o.scale = (1, 1, squash)
    return finish(o, mat, 0.0)


def bolts(points, r=0.022, h=0.03, axis=(0, 1, 0), mat="dark"):
    for p in points:
        e = tuple(p[i] + axis[i] * h for i in range(3)); cyl(p, e, r, mat, seg=6, bevel=0.003)


def ibeam(a, b, w=0.2, d=0.26, mat="yellow"):
    """An I-section from a to b: two flanges and a web. (Runs along one axis: up, across or out.)"""
    A, B = Vector(a), Vector(b); mid = (A + B) / 2; ext = B - A; ax = max(range(3), key=lambda i: abs(ext[i])); L = abs(ext[ax]); tf = 0.03
    if ax == 1:    # vertical
        box(mid.x, mid.y, mid.z - d / 2 + tf / 2, w, L, tf, mat); box(mid.x, mid.y, mid.z + d / 2 - tf / 2, w, L, tf, mat); box(mid.x, mid.y, mid.z, tf, L, d - 2 * tf, mat)
    elif ax == 0:  # across
        box(mid.x, mid.y + d / 2 - tf / 2, mid.z, L, tf, w, mat); box(mid.x, mid.y - d / 2 + tf / 2, mid.z, L, tf, w, mat); box(mid.x, mid.y, mid.z, L, d - 2 * tf, tf, mat)
    else:          # out
        box(mid.x, mid.y + d / 2 - tf / 2, mid.z, w, tf, L, mat); box(mid.x, mid.y - d / 2 + tf / 2, mid.z, w, tf, L, mat); box(mid.x, mid.y, mid.z, tf, d - 2 * tf, L, mat)


def baseplate(x, out, s=0.42, mat="dark"):
    box(x, 0.012, out, s, 0.024, s, mat, 0.004)
    bolts([(x + i * s * 0.36, 0.024, out + j * s * 0.36) for i in (-1, 1) for j in (-1, 1)])


def flange(c, r, axis, n=8, mat="bare"):
    """A pipe flange at c: a disc pair with a ring of bolts. axis: unit vector the pipe runs along."""
    a = Vector(axis); u = Vector((0, 1, 0)) if abs(a.y) < 0.9 else Vector((1, 0, 0)); u = (u - a * u.dot(a)).normalized(); v = a.cross(u)
    C = Vector(c); cyl(tuple(C - a * 0.03), tuple(C + a * 0.03), r, mat, seg=28, bevel=0.004)
    bolts([tuple(C + (u * math.cos(k * 6.2832 / n) + v * math.sin(k * 6.2832 / n)) * (r * 0.82) - a * 0.045) for k in range(n)], r=0.014, h=0.09, axis=tuple(a))


def shade(o, floor, drop):
    """Bake the part's own shading into its vertex colours (the game multiplies the texture by them): ray-traced
    occlusion -- dark where parts meet, under flanges, against the floor -- and wear: the convex edges, where paint goes
    first, left brighter than the faces. Long edges are cut first so big flat faces carry enough points to shade."""
    me = o.data; bm = bmesh.new(); bm.from_mesh(me)
    for _ in range(2):
        long_ = [e for e in bm.edges if e.calc_length() > 0.55]
        if not long_: break
        bmesh.ops.subdivide_edges(bm, edges=long_, cuts=1, use_grid_fill=True)
    bmesh.ops.triangulate(bm, faces=bm.faces[:]); bm.verts.ensure_lookup_table(); bm.normal_update()
    wear = np.zeros(len(bm.verts), np.float32)
    for e in bm.edges:
        if len(e.link_faces) == 2:
            a = e.calc_face_angle_signed(0.0)
            if a > 0.22:
                for v in e.verts: wear[v.index] = max(wear[v.index], min(1.0, a / 0.6))
    for v in bm.verts:                                         # not every edge alike: broken up by where it is
        h = math.sin(v.co.x * 37.1 + v.co.y * 23.7 + v.co.z * 51.3) * 43758.5453; wear[v.index] *= 0.35 + 0.65 * (h - math.floor(h))
    bm.to_mesh(me); bm.free()
    col = me.color_attributes.new("Col", "BYTE_COLOR", "CORNER"); me.color_attributes.active_color = col; me.color_attributes.render_color_index = 0
    plane = None
    if floor:                                                  # the floor it stands on shades its feet
        bpy.ops.mesh.primitive_plane_add(size=40, location=(0, 0, -drop - 0.001)); plane = bpy.context.object
    bpy.ops.object.select_all(action="DESELECT"); o.select_set(True); bpy.context.view_layer.objects.active = o
    sc = bpy.context.scene; sc.render.engine = "CYCLES"; sc.cycles.device = "CPU"; sc.cycles.samples = 48; sc.render.bake.target = "VERTEX_COLORS"
    if sc.world is None: sc.world = bpy.data.worlds.new("w")
    bpy.ops.object.bake(type="AO")
    if plane is not None: bpy.data.objects.remove(plane, do_unlink=True)
    n = len(me.loops); c = np.empty(n * 4, np.float32); col.data.foreach_get("color", c); c = c.reshape(n, 4)
    vi = np.empty(n, np.int32); me.loops.foreach_get("vertex_index", vi)
    ao = np.clip(c[:, 0], 0, 1) ** 1.6
    v = np.clip(0.80 * (0.22 + 0.78 * ao) + 0.26 * wear[vi] * (0.4 + 0.6 * ao), 0, 1)       # 0.8 = a plain open face; edges up to 1, crevices down to a fifth
    c[:, 0] = c[:, 1] = c[:, 2] = v; c[:, 3] = 1.0; col.data.foreach_set("color", c.ravel())
    bpy.ops.object.select_all(action="DESELECT"); o.select_set(True); bpy.context.view_layer.objects.active = o


def export(name, pivot=(0, 0, 0)):
    global PARTS
    if ONLY and not name.startswith(ONLY):
        for o in PARTS: bpy.data.objects.remove(o, do_unlink=True)
        PARTS = []; return
    bpy.ops.object.select_all(action="DESELECT")
    for o in PARTS: o.select_set(True)
    bpy.context.view_layer.objects.active = PARTS[0]; bpy.ops.object.join(); o = bpy.context.object
    o.location = -P(*pivot); bpy.ops.object.transform_apply(location=True); o.name = name
    shade(o, floor=(name not in ("steam_wheel", "crane_load", "door_slab", "turbine_rotor")), drop=P(*pivot).z)
    path = os.path.join(OUT, name + ".gltf")          # separate files: the textures are written once (by image name) and shared by every part. PNG: the desktop raylib reads no JPEG
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLTF_SEPARATE", use_selection=True, export_yup=True, export_apply=True, export_tangents=True,
                              export_materials="EXPORT", export_image_format="AUTO", export_texture_dir="", export_animations=False,
                              export_vertex_color="ACTIVE", export_all_vertex_colors=False, export_active_vertex_color_when_no_material=True)
    print(f"traps: {name:14s} {len(o.data.polygons):6d} faces  {os.path.getsize(os.path.join(OUT, name + '.bin')) / 1e6:5.2f} MB")
    bpy.data.objects.remove(o, do_unlink=True); PARTS = []


# ------------------------------------------------------------------ the parts
def post():                                                    # every trap's switch: a pedestal, its box, a lever, the lamp's socket
    baseplate(0, 0, 0.34); cyl((0, 0.02, 0), (0, 1.02, 0), 0.045, "grey"); cyl((0, 0.02, 0), (0, 0.16, 0), 0.07, "dark", r2=0.05)
    box(0, 1.2, 0, 0.36, 0.34, 0.20, "grey", 0.016); box(0, 1.2, -0.105, 0.30, 0.28, 0.012, "dark", 0.004)                 # the door on its face (toward the middle)
    bolts([(i * 0.13, 1.2 + j * 0.12, -0.111) for i in (-1, 1) for j in (-1, 1)], r=0.01, h=0.012, axis=(0, 0, -1))
    cyl((0.06, 1.24, -0.11), (0.06, 1.24, -0.16), 0.045, "red", bevel=0.01); cyl((0.06, 1.24, -0.11), (0.06, 1.24, -0.13), 0.055, "dark")   # the mushroom button
    cyl((-0.08, 1.14, -0.11), (-0.08, 1.30, -0.21), 0.012, "bare"); dome((-0.08, 1.30, -0.21), 0.028, 1.0, "red")           # the lever
    cyl((0, 1.37, 0), (0, 1.42, 0), 0.06, "dark"); torus((0, 1.43, 0), 0.062, 0.012, (0, 1, 0), "bare", 20)                   # lamp socket
    cyl((0.12, 1.03, 0.06), (0.12, 0.03, 0.06), 0.016, "bare"); cyl((0.12, 1.03, 0.06), (0.12, 1.08, 0.02), 0.016, "bare")    # conduit down the back
    box(0, 1.05, -0.112, 0.2, 0.05, 0.006, "hazard", 0.0)
    export("post")


def steam():
    for sx in (-3.3, 3.3):
        baseplate(sx, 0); ibeam((sx, 0.02, 0), (sx, 3.5, 0), 0.2, 0.24, "grey")
        box(sx - math.copysign(0.42, sx), 3.05, 0, 0.9, 0.07, 0.07, "grey", 0.006, rot=(0, 0, math.copysign(42, sx)))        # knee brace
        box(sx, 3.52, 0, 0.3, 0.04, 0.3, "dark")
    cyl((-3.55, 3.3, 0), (3.55, 3.3, 0), 0.14, "bare", seg=32)
    for fx in (-2.6, -1.3, 0.0, 1.3, 2.6): flange((fx + 0.62, 3.3, 0), 0.21, (1, 0, 0))
    cyl((-1.9, 3.3, 0), (-0.75, 3.3, 0), 0.19, "canvas", seg=32); torus((-1.9, 3.3, 0), 0.19, 0.012, (1, 0, 0), "bare"); torus((-0.75, 3.3, 0), 0.19, 0.012, (1, 0, 0), "bare")   # a lagged length
    for j in range(-2, 3):                                                                                                # the vents
        x = j * 1.25; cyl((x, 3.18, 0), (x, 3.06, 0), 0.075, "bare"); cyl((x, 3.06, 0), (x, 2.9, 0), 0.06, "dark", r2=0.105); torus((x, 3.06, 0), 0.078, 0.014, (0, 1, 0), "bare", 20)
        box(x, 3.47, 0, 0.05, 0.07, 0.34, "dark", 0.004); cyl((x, 3.5, -0.16), (x, 3.75, -0.16), 0.008, "bare", 8); cyl((x, 3.5, 0.16), (x, 3.75, 0.16), 0.008, "bare", 8)   # hangers
    cyl((3.4, 3.2, 0), (3.4, 1.36, 0), 0.07, "bare"); cyl((3.4, 1.36, 0), (3.4, 1.04, 0), 0.11, "red", bevel=0.02)             # the drop to the valve, its body
    flange((3.4, 1.38, 0), 0.13, (0, 1, 0), 6); flange((3.4, 1.02, 0), 0.13, (0, 1, 0), 6); cyl((3.4, 1.2, 0), (3.4, 1.2, -0.2), 0.03, "bare")
    cyl((3.4, 1.0, 0), (3.4, 0.3, 0), 0.07, "bare"); cyl((3.4, 0.3, 0), (3.4, 0.0, 0), 0.07, "bare")
    cyl((3.05, 3.44, 0), (3.05, 3.6, 0), 0.012, "bare", 8); cyl((3.05, 3.66, -0.03), (3.05, 3.66, 0.03), 0.07, "dark"); cyl((3.05, 3.66, -0.035), (3.05, 3.66, -0.04), 0.06, "ceramic")   # gauge
    export("steam_frame")
    torus((0, 0, 0), 0.22, 0.022, (0, 0, 1), "red"); cyl((0, 0, 0.03), (0, 0, -0.04), 0.045, "dark")
    for k in range(5):
        a = k * 6.2832 / 5; cyl((0, 0, 0), (0.215 * math.cos(a), 0.215 * math.sin(a), 0), 0.014, "red", 10)
    export("steam_wheel")


def plate():
    box(0, 0.03, 0, 4.9, 0.045, 4.9, "plate", 0.004)
    for s in (-1, 1):                                                                                                     # its frame, hazard-painted, and what feeds it
        box(0, 0.035, s * 2.5, 5.16, 0.07, 0.16, "hazard", 0.008); box(s * 2.5, 0.035, 0, 0.16, 0.07, 5.16, "hazard", 0.008)
        box(s * 2.36, 0.085, 0, 0.05, 0.03, 4.4, "copper", 0.004); bolts([(s * 2.36, 0.1, z) for z in (-2.0, -1.0, 0.0, 1.0, 2.0)], r=0.016, h=0.02)
    for sx in (-1, 1):
        for sz in (-1, 1):
            for k in range(4): cyl((sx * 2.62, 0.02 + k * 0.045, sz * 2.62), (sx * 2.62, 0.055 + k * 0.045, sz * 2.62), 0.075 - 0.008 * (k % 2) * 3, "ceramic", bevel=0.008)
            cyl((sx * 2.62, 0.2, sz * 2.62), (sx * 2.62, 0.26, sz * 2.62), 0.03, "copper")
    pts = [(2.36, 0.1, 1.6), (2.75, 0.06, 1.75), (3.1, 0.04, 1.2), (3.3, 0.04, 0.5), (3.4, 0.05, 0.12), (3.4, 0.5, 0.1)]            # the feed cable, to the switch post
    for a, b in zip(pts, pts[1:]): cyl(a, b, 0.032, "dark", 14, bevel=0.0)
    for (x, z) in ((-1.2, -0.6), (0.9, 1.3), (1.6, -1.5)): box(x, 0.056, z, 0.5, 0.006, 0.34, "dark", 0.0, rot=(0, rnd.uniform(0, 90), 0))  # scorch plates
    export("plate")


def hose():
    for s in (-1, 1): box(-4.2 + s * 0.3, 0.42, 0.3, 0.05, 0.84, 0.05, "dark"); baseplate(-4.2 + s * 0.3, 0.3, 0.16)
    box(-4.2, 1.2, 0.3, 0.74, 0.78, 0.30, "red", 0.02); box(-4.2, 1.2, 0.142, 0.64, 0.68, 0.016, "red", 0.006)                 # cabinet, its door
    box(-4.2, 1.2, 0.13, 0.4, 0.4, 0.006, "ceramic", 0.0); cyl((-3.9, 1.2, 0.13), (-3.9, 1.2, 0.10), 0.02, "bare")              # the glass, the latch
    for k in range(7): torus((-4.2, 0.22 + 0.05 * k, -0.2), 0.3 - 0.012 * k, 0.03, (0, 1, 0), "canvas", 28)                    # the hose, flaked down beside it
    pts = [(-4.2, 0.55, -0.2), (-4.1, 0.9, -0.1), (-4.05, 1.05, -0.02), (-4.0, 1.1, 0.0)]
    for a, b in zip(pts, pts[1:]): cyl(a, b, 0.03, "canvas", 12, bevel=0.0)
    for a, b in (((-3.8, 0.0, -0.25), (-3.72, 1.05, 0.0)), ((-3.8, 0.0, 0.25), (-3.72, 1.05, 0.0)), ((-3.3, 0.0, 0.0), (-3.72, 1.05, 0.0))): cyl(a, b, 0.016, "dark", 10)   # the monitor's tripod
    cyl((-4.0, 1.1, 0), (-3.62, 1.14, 0), 0.04, "copper"); cyl((-3.62, 1.14, 0), (-3.46, 1.155, 0), 0.05, "copper", r2=0.03); torus((-3.62, 1.14, 0), 0.045, 0.01, (1, 0, 0), "copper", 20)
    cyl((-3.85, 1.12, 0), (-3.85, 1.22, 0), 0.012, "bare", 8); cyl((-3.9, 1.22, 0), (-3.8, 1.22, 0), 0.01, "red", 8)
    export("hose")


def surge():
    r2 = random.Random(5)
    for k in range(26):                                                                                                   # the floor broken up round the mouth
        a = k * 6.2832 / 26 + r2.uniform(-0.1, 0.1); d = r2.uniform(1.75, 2.35); s = r2.uniform(0.22, 0.6)
        box(math.cos(a) * d, r2.uniform(0.04, 0.2), math.sin(a) * d, s, s * r2.uniform(0.25, 0.5), s * r2.uniform(0.6, 1.3), "concrete", 0.02,
            rot=(r2.uniform(-28, 28), math.degrees(a) + r2.uniform(-30, 30), r2.uniform(-28, 28)))
    for k in range(9):                                                                                                    # slabs heaved up and tipped out
        a = k * 6.2832 / 9 + 0.3; box(math.cos(a) * 1.95, 0.16, math.sin(a) * 1.95, 0.9, 0.09, 0.7, "concrete", 0.015, rot=(r2.uniform(14, 30), -math.degrees(a) + 90, r2.uniform(-8, 8)))
    for k in range(18):
        a = r2.uniform(0, 6.2832); d0 = 1.72; d1 = r2.uniform(0.9, 1.5); cyl((math.cos(a) * d0, -0.05, math.sin(a) * d0), (math.cos(a + r2.uniform(-.3, .3)) * d1, r2.uniform(-0.3, 0.5), math.sin(a) * d1), 0.012, "bare", 6, bevel=0.0)
    torus((0, 0.0, 0), 1.72, 0.06, (0, 1, 0), "dark", 40)
    export("surge")


def crane():
    for sx in (-3.7, 3.7):
        baseplate(sx, 0, 0.6); ibeam((sx, 0.02, 0), (sx, 6.1, 0), 0.26, 0.34, "yellow")
        for k in range(12): cyl((sx, 0.5 + k * 0.45, 0.19), (sx + math.copysign(0.3, -sx), 0.5 + k * 0.45, 0.19), 0.012, "bare", 8)          # ladder
        cyl((sx + math.copysign(0.3, -sx), 0.3, 0.19), (sx + math.copysign(0.3, -sx), 5.6, 0.19), 0.014, "bare", 8)
        for sz in (-1, 1): box(sx, 0.7, sz * 0.62, 0.1, 1.7, 0.1, "yellow", 0.008, rot=(sz * 38, 0, 0)); baseplate(sx, sz * 1.15, 0.3)          # raking struts
        box(sx - math.copysign(0.55, sx), 5.75, 0, 1.2, 0.1, 0.1, "yellow", 0.008, rot=(0, 0, math.copysign(40, sx)))
    box(0, 6.3, 0, 7.9, 0.36, 0.46, "yellow", 0.02); box(0, 6.5, 0, 7.9, 0.035, 0.6, "yellow", 0.008); box(0, 6.1, 0, 7.9, 0.035, 0.6, "yellow", 0.008)   # the girder
    for x in (-3.0, -1.5, 0.0, 1.5, 3.0): box(x, 6.3, -0.235, 0.03, 0.34, 0.012, "yellow", 0.0); box(x, 6.3, 0.235, 0.03, 0.34, 0.012, "yellow", 0.0)
    box(0, 6.3, -0.24, 1.2, 0.14, 0.008, "hazard", 0.0); box(-3.9, 6.3, 0, 0.1, 0.5, 0.6, "hazard", 0.01); box(3.9, 6.3, 0, 0.1, 0.5, 0.6, "hazard", 0.01)
    box(0, 5.94, 0, 0.7, 0.26, 0.6, "dark", 0.02)                                                                           # the trolley and its hoist
    for sx in (-0.28, 0.28):
        for sz in (-0.3, 0.3): cyl((sx, 6.08, sz - 0.03), (sx, 6.08, sz + 0.03), 0.07, "bare", 20)
    cyl((-0.3, 5.78, 0), (0.3, 5.78, 0), 0.12, "bare"); cyl((0.3, 5.78, 0), (0.48, 5.78, 0), 0.09, "grey"); box(0.6, 5.8, 0, 0.22, 0.2, 0.2, "grey")
    dome((3.7, 6.62, 0), 0.07, 1.0, "red")
    export("crane_frame")
    box(0, 0, 0, 2.6, 1.5, 2.6, "grey", 0.045)                                                                               # the load: a counterweight block
    box(0, 0.5, 0, 2.66, 0.26, 2.66, "hazard", 0.02); box(0, -0.62, 0, 2.7, 0.2, 2.7, "dark", 0.03)
    for s in (-1, 1):
        for k in (-0.8, 0.0, 0.8): box(s * 1.31, -0.1, k, 0.05, 0.8, 0.12, "dark", 0.01); box(k, -0.1, s * 1.31, 0.12, 0.8, 0.05, "dark", 0.01)
    box(0, 0.86, 0, 1.6, 0.14, 0.2, "yellow", 0.02)                                                                          # spreader bar and its shackles
    for sx in (-0.12, 0.12): torus((sx, 1.02, 0), 0.07, 0.02, (0, 0, 1), "bare", 20)
    for sx in (-0.9, 0.9):
        for sz in (-0.9, 0.9): torus((sx, 0.8, sz), 0.08, 0.022, (1, 0, 0), "bare", 20)
    export("crane_load")


def door():
    for s in (-1, 1):
        box(s * 1.95, 2.0, 0, 0.7, 4.0, 0.95, "concrete", 0.03); box(s * 1.63, 2.0, 0, 0.07, 4.0, 0.62, "dark", 0.008)        # the jambs, their guide channels
        cyl((s * 2.02, 4.1, -0.52), (s * 2.02, 1.6, -0.52), 0.07, "bare"); cyl((s * 2.02, 1.6, -0.52), (s * 2.02, 0.6, -0.52), 0.045, "ceramic")   # the rams
        box(s * 2.02, 4.15, -0.5, 0.2, 0.14, 0.14, "dark"); box(s * 2.02, 0.5, -0.5, 0.2, 0.14, 0.14, "dark")
        box(s * 1.95, 0.1, 0, 0.74, 0.2, 1.0, "hazard", 0.01)
    box(0, 5.65, 0, 4.6, 3.3, 0.95, "concrete", 0.03)                                                                         # the housing it rises into
    box(0, 5.2, -0.485, 2.6, 1.3, 0.02, "grey", 0.01); bolts([(i * 1.2, 5.2 + j * 0.55, -0.5) for i in (-1, 0, 1) for j in (-1, 1)], r=0.018, h=0.02, axis=(0, 0, -1))
    box(0, 4.08, -0.49, 3.3, 0.16, 0.02, "hazard", 0.0); dome((0, 6.5, -0.5), 0.1, 1.0, "red"); cyl((0, 6.4, -0.48), (0, 6.5, -0.5), 0.08, "dark")
    box(0, 4.02, 0, 3.26, 0.06, 0.62, "dark", 0.01)
    export("door_frame")
    box(0, 1.95, 0, 3.18, 3.9, 0.46, "grey", 0.02)                                                                            # the door itself: ribbed plate
    for k in range(6): box(0, 0.75 + k * 0.58, -0.24, 3.0, 0.1, 0.06, "dark", 0.012); box(0, 0.75 + k * 0.58, 0.24, 3.0, 0.1, 0.06, "dark", 0.012)
    box(0, 0.2, -0.236, 3.18, 0.4, 0.012, "hazard", 0.0); box(0, 0.2, 0.236, 3.18, 0.4, 0.012, "hazard", 0.0); box(0, 0.03, 0, 3.18, 0.06, 0.5, "dark", 0.01)
    for s in (-1, 1): box(s * 1.5, 1.95, 0, 0.1, 3.9, 0.52, "dark", 0.012)
    bolts([(i * 0.5, 0.46 + j * 0.58, -0.232) for i in range(-2, 3) for j in range(6)], r=0.016, h=0.016, axis=(0, 0, -1))
    box(0, 3.1, -0.236, 0.9, 0.16, 0.012, "ceramic", 0.0)
    export("door_slab")


def tank():
    for a in range(4):
        x, z = 0.62 * math.cos(a * 1.5708 + 0.785), 0.62 * math.sin(a * 1.5708 + 0.785); box(x, 0.16, z, 0.12, 0.32, 0.12, "dark", 0.01); baseplate(x, z, 0.24)
    cyl((0, 0.3, 0), (0, 2.1, 0), 0.9, "teal", seg=40, bevel=0.02); dome((0, 2.1, 0), 0.9, 0.32, "teal"); dome((0, 0.3, 0), 0.9, 0.22, "teal")
    for y in (0.55, 1.2, 1.85): torus((0, y, 0), 0.905, 0.022, (0, 1, 0), "bare", 44)
    cyl((0, 2.36, 0), (0, 2.5, 0), 0.26, "dark"); flange((0, 2.5, 0), 0.3, (0, 1, 0), 10)                                      # manway
    cyl((0.85, 0.55, -0.2), (1.25, 0.55, -0.2), 0.07, "bare"); flange((0.92, 0.55, -0.2), 0.12, (1, 0, 0), 6); cyl((1.1, 0.55, -0.2), (1.1, 0.78, -0.2), 0.03, "bare")   # the outlet they burst
    torus((1.1, 0.8, -0.2), 0.11, 0.014, (0, 1, 0), "red", 20); cyl((1.25, 0.55, -0.2), (1.34, 0.5, -0.2), 0.07, "dark", r2=0.09)
    cyl((-0.2, 0.5, -0.9), (-0.2, 1.9, -0.9), 0.018, "ceramic", 10); box(-0.2, 0.48, -0.9, 0.07, 0.05, 0.07, "bare"); box(-0.2, 1.92, -0.9, 0.07, 0.05, 0.07, "bare")    # level glass
    box(0.35, 1.3, -0.9, 0.42, 0.42, 0.012, "hazard", 0.0, rot=(0, 0, 45))
    for k in range(7): cyl((-0.75, 0.4 + k * 0.28, -0.62), (-0.48, 0.4 + k * 0.28, -0.82), 0.011, "bare", 8)
    cyl((-0.75, 0.2, -0.62), (-0.75, 2.2, -0.62), 0.013, "bare", 8); cyl((-0.48, 0.2, -0.82), (-0.48, 2.2, -0.82), 0.013, "bare", 8)
    export("spill_tank", pivot=(0, 0, 0))


def turbine():
    cyl((0, 1.6, 1.0), (0, 1.6, 5.0), 1.55, "grey", seg=48, bevel=0.0, wall=0.06)                                             # casing: a tube, open at the mouth
    cyl((0, 1.6, 0.86), (0, 1.6, 1.0), 1.72, "grey", seg=48, r2=1.55, bevel=0.0, wall=0.05)                                    # bell mouth
    cyl((0, 1.6, 2.55), (0, 1.6, 2.62), 1.5, "dark", seg=48, bevel=0.0)                                                        # the bulkhead behind the stator: the dark it draws into
    torus((0, 1.6, 0.86), 1.72, 0.035, (0, 0, 1), "bare", 56)
    for z in (1.0, 2.3, 3.6, 4.96):
        cyl((0, 1.6, z - 0.035), (0, 1.6, z + 0.035), 1.66, "dark", seg=48, bevel=0.0, wall=(0.12 if z < 4.9 else 0.0))            # flange rings (the last one closes the back)
        bolts([(1.61 * math.cos(k * 0.2618), 1.6 + 1.61 * math.sin(k * 0.2618), z - 0.05) for k in range(24)], r=0.02, h=0.1, axis=(0, 0, 1))
    for k in range(10):
        a = k * 0.6283 + 0.3; box(1.57 * math.cos(a), 1.6 + 1.57 * math.sin(a), 3.0, 0.07, 0.09, 3.6, "dark", 0.01, rot=(0, 0, math.degrees(a) + 90))   # ribs
    for k in range(6):                                                                                                    # stator struts behind the rotor
        a = k * 1.0472 + 0.2; cyl((0.3 * math.cos(a), 1.6 + 0.3 * math.sin(a), 1.75), (1.5 * math.cos(a), 1.6 + 1.5 * math.sin(a), 1.75), 0.045, "dark", 10)
    cyl((0, 1.6, 1.3), (0, 1.6, 2.2), 0.34, "dark", bevel=0.02); cyl((0, 1.6, 4.98), (0, 1.6, 5.3), 1.1, "dark", r2=0.6, bevel=0.02); cyl((0, 1.6, 5.3), (0, 1.6, 5.6), 0.6, "bare")
    for sx in (-0.9, 0.9): ibeam((sx, 0.13, 1.0), (sx, 0.13, 5.2), 0.22, 0.26, "dark")                                           # the skid, the saddles
    for z in (1.6, 3.0, 4.4): box(0, 0.36, z, 2.5, 0.42, 0.16, "dark", 0.02); box(0, 0.62, z, 1.7, 0.3, 0.12, "dark", 0.02)
    box(1.72, 1.9, 2.9, 0.34, 0.6, 0.8, "grey", 0.02); cyl((1.72, 1.6, 2.9), (1.72, 0.2, 2.9), 0.04, "dark", 12)                  # terminal box, its cable
    cyl((0, 3.15, 2.0), (0, 3.4, 2.0), 0.07, "bare"); cyl((0, 3.4, 2.0), (0, 3.4, 4.2), 0.07, "bare"); cyl((0, 3.4, 4.2), (0, 3.1, 4.2), 0.07, "bare"); flange((0, 3.4, 3.1), 0.12, (0, 0, 1), 6)   # oil line
    box(1.72, 2.26, 2.9, 0.345, 0.1, 0.5, "hazard", 0.0)                                                                     # a warning band on the terminal box
    export("turbine_body")
    cyl((0, 0, 0.0), (0, 0, 0.42), 0.34, "dark", bevel=0.02); cyl((0, 0, 0.0), (0, 0, -0.36), 0.34, "bare", r2=0.04, bevel=0.01)   # hub and spinner (toward the middle = -out)
    N = 13
    for k in range(N):                                                                                                    # blades: tapered, twisted aerofoil sections
        bm = bmesh.new(); secs = []
        for i in range(9):
            t = i / 8.0; rad = 0.30 + t * 1.14; chord = 0.66 * (1.0 - 0.38 * t); tw = math.radians(62 - 30 * t); th = 0.075 * (1.0 - 0.55 * t)
            prof = [(-0.5, 0.0), (-0.25, 0.9), (0.1, 1.0), (0.5, 0.15), (0.5, -0.15), (0.1, -0.55), (-0.25, -0.5)]
            ring = []
            for (cx, cy) in prof:
                u, w = cx * chord, cy * th; y_ = u * math.cos(tw) - w * math.sin(tw); z_ = u * math.sin(tw) + w * math.cos(tw)   # y_: round the disc, z_: along the shaft
                a = k * 6.2832 / N; lx, ly = rad, y_
                ring.append(bm.verts.new(P(lx * math.cos(a) - ly * math.sin(a), lx * math.sin(a) + ly * math.cos(a), 0.32 + z_)))
            secs.append(ring)
        for i in range(8):
            for j in range(7): bm.faces.new((secs[i][j], secs[i][(j + 1) % 7], secs[i + 1][(j + 1) % 7], secs[i + 1][j]))
        bm.faces.new(secs[0][::-1]); bm.faces.new(secs[8]); bm.normal_update()
        me = bpy.data.meshes.new("blade"); bm.to_mesh(me); bm.free(); o = bpy.data.objects.new("blade", me); bpy.context.collection.objects.link(o)
        bpy.context.view_layer.objects.active = o; o.select_set(True); bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_all(action="SELECT"); bpy.ops.mesh.normals_make_consistent(inside=False); bpy.ops.object.mode_set(mode="OBJECT")
        finish(o, "bare", 0.0)
    export("turbine_rotor")


bpy.ops.wm.read_factory_settings(use_empty=True)
build_materials()
for fn in (post, steam, plate, hose, surge, crane, door, tank, turbine): fn()
print("traps: done ->", OUT)
