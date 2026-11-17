#!/bin/bash
# The web / iPhone asset pack must not change what a model's textures say -- only their size and encoding.
# (Pillow resizes RGBA with the colour premultiplied by alpha: the cooks' skin ORM keeps its subsurface mask in an alpha
#  of 0, so its occlusion / roughness / specular came out zero and the cooks' skin black on the web and the phone, 2026-11-16.)
cd "$(dirname "$0")/.." || exit 1
python3 - <<'PY'
import io, json, struct, sys, tempfile, os
sys.path.insert(0, "tools/web"); import pack_web
from PIL import Image, ImageStat
def glb(path):
    d=open(path,"rb").read(); jl=struct.unpack("<I",d[12:16])[0]; j=json.loads(d[20:20+jl]); bo=20+jl; return j, d[bo+8:bo+8+struct.unpack("<I",d[bo:bo+4])[0]]
def means(path):
    j,b=glb(path); out={}
    for m in j["materials"]:
        for k,t in (("base",m.get("pbrMetallicRoughness",{}).get("baseColorTexture")),("orm",m.get("occlusionTexture")),("normal",m.get("normalTexture"))):
            if not t: continue
            im=j["images"][j["textures"][t["index"]]["source"]]; bv=j["bufferViews"][im["bufferView"]]
            img=Image.open(io.BytesIO(b[bv.get("byteOffset",0):bv.get("byteOffset",0)+bv["byteLength"]])).convert("RGB")
            out[(m["name"],k)]=ImageStat.Stat(img).mean
    return out
fail=0
for model in pack_web.ENEMIES + pack_web.WEAPONS:
    src=f"assets/{model}.glb"; dst=os.path.join(tempfile.mkdtemp(), model+".glb"); pack_web.shrink_glb(src, dst)
    a, b = means(src), means(dst)
    for key in a:
        d=max(abs(x-y) for x,y in zip(a[key], b[key]))
        if d>12: print(f"FAIL {model} {key[0]} {key[1]}: colour mean {[round(x) for x in a[key]]} -> {[round(x) for x in b[key]]}"); fail=1
    if not fail: print(f"ok   {model}: {len(a)} textures keep their colour through the pack")
print("PASS" if not fail else "FAIL"); sys.exit(fail)
PY
