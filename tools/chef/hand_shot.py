"""Close-up render of one hand in one clip frame (dev: is the weapon really in the fist?).

    blender -b -P tools/chef/hand_shot.py -- assets/linecook.glb run 0.5 Right out.png
"""
import bpy, sys, math
from mathutils import Vector
a = sys.argv[sys.argv.index("--") + 1:]
path, clip, frac, side, out = a[0], a[1], float(a[2]), a[3], a[4]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=path)
rig = [o for o in bpy.data.objects if o.type == "ARMATURE"][0]
act = [x for x in bpy.data.actions if x.name.startswith(clip)][0]
rig.animation_data.action = act
f0, f1 = act.frame_range
bpy.context.scene.frame_set(int(f0 + (f1 - f0) * frac))
hand = rig.matrix_world @ rig.pose.bones[f"{side}Hand"].head
tip = rig.matrix_world @ rig.pose.bones[f"{side}FingerBase"].head
c = hand.lerp(tip, 0.6)
sc = bpy.context.scene
sc.render.engine = "BLENDER_WORKBENCH"
sc.display.shading.light = "STUDIO"; sc.display.shading.color_type = "MATERIAL"
sc.render.resolution_x = sc.render.resolution_y = 520
views = []
for k, (dx, dy, dz) in enumerate(((0, -1, 0.3), (1 if side == "Left" else -1, 0, 0.2), (0, 1, 0.3))):
    cam = bpy.data.objects.new(f"cam{k}", bpy.data.cameras.new(f"cam{k}"))
    sc.collection.objects.link(cam)
    cam.data.type = "ORTHO"; cam.data.ortho_scale = 0.38
    cam.location = c + Vector((dx, dy, dz)).normalized() * 1.2
    cam.rotation_euler = (c - cam.location).to_track_quat("-Z", "Y").to_euler()
    sc.camera = cam
    sc.render.filepath = out.replace(".png", f"_{k}.png")
    bpy.ops.render.render(write_still=True)
