"""Interpenetration check for an enemy model: hand-held weapons vs the body/clothes, and
left weapon vs right weapon, on every frame of every clip.

    blender -b -P tools/chef/check_clips.py -- assets/cook.glb [step]

Weapon = polygons rigidly skinned to one hand (all verts >= 0.99 on LeftHand/RightHand)
that aren't skin. Body = everything else, minus the hands/fingers themselves (a grip has
to sit inside the fist) and minus the other side's weapon. Prints one line per clip with
the frames that collide and the worst triangle-pair count; a final 'CLEAN' / 'HITS' line.
"""
import bpy, sys
from mathutils.bvhtree import BVHTree

path = sys.argv[sys.argv.index("--") + 1]
step = int(sys.argv[sys.argv.index("--") + 2]) if len(sys.argv) > sys.argv.index("--") + 2 else 2
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=path)
rig = [o for o in bpy.data.objects if o.type == "ARMATURE"][0]
objs = [o for o in bpy.data.objects if o.type == "MESH" and o.find_armature() == rig]

HAND = {"Left": ("LeftHand", "LeftFinger", "LeftHandFinger", "LThumb"),
        "Right": ("RightHand", "RightFinger", "RightHandFinger", "RThumb")}


def classify(o):
    me = o.data
    gname = {g.index: g.name for g in o.vertex_groups}
    vcls = []
    for v in me.vertices:
        ws = [(gname[g.group], g.weight) for g in v.groups if g.weight > 0.001]
        tot = sum(w for _, w in ws) or 1.0
        side = None
        for s in ("Left", "Right"):
            if sum(w for n, w in ws if n == f"{s}Hand") / tot >= 0.99:
                side = s
        handish = any(sum(w for n, w in ws if n.startswith(HAND[s])) / tot > 0.3 for s in HAND)
        vcls.append((side, handish))
    mats = [m.name.lower() if m else "" for m in me.materials]
    polys = {"L": [], "R": [], "body": []}
    for p in me.polygons:
        mat = mats[p.material_index] if p.material_index < len(mats) else ""
        sides = {vcls[i][0] for i in p.vertices}
        skin = "skin" in mat or "generic" in mat or "body" in mat
        if not skin and sides == {"Left"}:
            polys["L"].append(p.index)
        elif not skin and sides == {"Right"}:
            polys["R"].append(p.index)
        elif not any(vcls[i][1] for i in p.vertices) and not (sides - {None}):
            polys["body"].append(p.index)
    return polys


def dom_bone(o):
    gname = {g.index: g.name for g in o.vertex_groups}
    out = []
    for v in o.data.vertices:
        g = max(v.groups, key=lambda g: g.weight, default=None)
        out.append(gname[g.group] if g else "?")
    return out


def tree(verts, mpolys, idxs):
    return BVHTree.FromPolygons(verts, [tuple(mpolys[i].vertices) for i in idxs], all_triangles=False) if idxs else None


cls = {o.name: classify(o) for o in objs}
dom = {o.name: dom_bone(o) for o in objs}
mat_of = {o.name: [o.data.materials[p.material_index].name if o.data.materials else "" for p in o.data.polygons] for o in objs}
nw = sum(len(c["L"]) + len(c["R"]) for c in cls.values())
print(f"check: {path} meshes={len(objs)} weapon polys={nw}")
dg = bpy.context.evaluated_depsgraph_get()
o_polys = {o.name: o.data.polygons for o in objs}
from collections import defaultdict
WHO = defaultdict(dict)
total = 0
for act in sorted(bpy.data.actions, key=lambda a: a.name):
    if rig.animation_data is None:
        rig.animation_data_create()
    rig.animation_data.action = act
    f0, f1 = (int(x) for x in act.frame_range)
    bad = []
    for fr in range(f0, f1 + 1, step):
        bpy.context.scene.frame_set(fr)
        dg.update()
        parts = {"L": [], "R": [], "body": []}
        for o in objs:
            ev = o.evaluated_get(dg); me = ev.to_mesh()
            vs = [ev.matrix_world @ v.co for v in me.vertices]
            for k in parts:
                t = tree(vs, me.polygons, cls[o.name][k])
                if t: parts[k].append((o.name, t))
            ev.to_mesh_clear()
        hits = {}
        for a, b in (("L", "body"), ("R", "body"), ("L", "R")):
            n = 0
            for (oa, ta), (ob, tb) in ((x, y) for x in parts[a] for y in parts[b]):
                pr = ta.overlap(tb); n += len(pr)
                for ia, ib in pr:
                    pa = cls[oa][a][ia]; pb = cls[ob][b][ib]
                    key = (f"{a}-{b}", mat_of[oa][pa], mat_of[ob][pb] + ":" + dom[ob][o_polys[ob][pb].vertices[0]])
                    WHO[act.name][key] = WHO[act.name].get(key, 0) + 1
            if n: hits[f"{a}-{b}"] = n
        if hits:
            bad.append((fr, hits))
    total += len(bad)
    if bad:
        worst = {}
        for _, h in bad:
            for k, n in h.items(): worst[k] = max(worst.get(k, 0), n)
        frs = [f for f, _ in bad]
        print(f"  {act.name:10s} {len(bad)}/{(f1 - f0) // step + 1} frames  worst {worst}  frames {frs[:12]}{'...' if len(frs) > 12 else ''}")
    else:
        print(f"  {act.name:10s} clean")
if "-v" in sys.argv:
    for an, d in WHO.items():
        print(f"  [{an}]")
        for k, n in sorted(d.items(), key=lambda kv: -kv[1])[:10]:
            print(f"      {n:6d}  {k}")
print("check:", "CLEAN" if total == 0 else f"HITS in {total} sampled frames")
