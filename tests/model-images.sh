#!/bin/bash
# No trap or mine model file may name an image. The game binds those parts' textures by their materials' names -- each
# sheet read once, mipmapped, shared by every part that wears it (src/traps.h TrapTexOf, src/mines.h MineMatOf). raylib's
# own glTF loader decodes every image a file names as well: while these files named them, the 13 trap parts cost 183
# decodes for 36 sheets (most of the 2.7 s they took to load, before the first frame) and the claymore's twelve were each
# read twice; raylib's copies of the normal and ORM sheets then stayed in video memory, never drawn.
# tools/traps/build_traps.py no_images takes them out at export.      tests/model-images.sh [assets-dir]
cd "$(dirname "$0")/.." || exit 1
python3 - "${1:-assets}" <<'PY'
import glob, json, os, sys
root = sys.argv[1]; fail = 0
files = sorted(glob.glob(os.path.join(root, "traps", "*.gltf")) + glob.glob(os.path.join(root, "mines", "*.gltf")))
if len(files) < 14: print(f"FAIL only {len(files)} model files found under {root} (13 trap parts and the claymore)"); fail = 1
for g in files:
    js = json.load(open(g)); mats = js.get("materials", [])
    refs = sum(1 for m in mats for k in ("normalTexture", "occlusionTexture", "emissiveTexture") if k in m)
    refs += sum(1 for m in mats for k in ("baseColorTexture", "metallicRoughnessTexture") if k in m.get("pbrMetallicRoughness", {}))
    if js.get("images") or refs: print(f"FAIL {g}: names {len(js.get('images', []))} images ({refs} material references)"); fail = 1
    for m in mats:                                             # ...and what the game looks for by name must be there
        if not any(os.path.exists(os.path.join(os.path.dirname(g), m["name"] + "_c" + e)) for e in (".png", ".jpg")):
            print(f"FAIL {g}: no {m['name']}_c.png beside it"); fail = 1
if not fail: print(f"ok   {len(files)} trap and mine models name no image; every material's sheet is beside them")
print("PASS" if not fail else "FAIL"); sys.exit(fail)
PY
