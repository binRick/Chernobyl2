#!/usr/bin/env python3
"""Build the web asset set (build/web/fs): what the browser build preloads.

The desktop assets are ~600 MB, 95% of it 2048-px PNG textures inside the .glb models. For the
web each model's textures are re-encoded -- base colour capped at 1024 px, normal / ORM /
emission at 512 px, JPEG wherever the image has no alpha -- and the .glb is rebuilt round them.
Only what the game loads goes in: the 11 enemies (+ LODs, gait files), the 4 weapons, the
sounds, and the default map with the textures it names.

    python3 tools/web/pack_web.py [--albedo 1024] [--detail 512]
"""
import io, json, os, re, shutil, struct, sys
from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(REPO, "build", "web", "fs")
ALBEDO = int(sys.argv[sys.argv.index("--albedo") + 1]) if "--albedo" in sys.argv else 1024
DETAIL = int(sys.argv[sys.argv.index("--detail") + 1]) if "--detail" in sys.argv else 512
ENEMIES = "cook linecook butcher liquidator headchef rifleman gunner mutant priest revenant fiend".split()
WEAPONS = "minigun flamethrower grenadelauncher remington".split()


def shrink_glb(src, dst):
    d = open(src, "rb").read()
    assert d[:4] == b"glTF", src
    jl = struct.unpack("<I", d[12:16])[0]
    j = json.loads(d[20:20 + jl])
    bo = 20 + jl
    bl = struct.unpack("<I", d[bo:bo + 4])[0]
    binc = d[bo + 8:bo + 8 + bl]
    role = {}                                    # image index -> "albedo" | "detail"
    tex = j.get("textures", [])
    def mark(t, r):
        if t is None: return
        s = tex[t["index"]].get("source")
        if s is not None and role.get(s) != "albedo": role[s] = r
    for m in j.get("materials", []):
        pbr = m.get("pbrMetallicRoughness", {})
        mark(pbr.get("baseColorTexture"), "albedo")
        for k in ("normalTexture", "occlusionTexture", "emissiveTexture"): mark(m.get(k), "detail")
        mark(pbr.get("metallicRoughnessTexture"), "detail")
    new = {}                                     # bufferView index -> replacement bytes
    for i, im in enumerate(j.get("images", [])):
        bv = j["bufferViews"][im["bufferView"]]
        raw = binc[bv.get("byteOffset", 0):bv.get("byteOffset", 0) + bv["byteLength"]]
        img = Image.open(io.BytesIO(raw)); img.load()
        cap = ALBEDO if role.get(i, "albedo") == "albedo" else DETAIL
        if max(img.size) > cap:
            k = cap / max(img.size)
            img = img.resize((max(1, round(img.size[0] * k)), max(1, round(img.size[1] * k))), Image.LANCZOS)
        alpha = img.mode in ("RGBA", "LA", "P") and img.convert("RGBA").getextrema()[3][0] < 255
        buf = io.BytesIO()
        if alpha:
            img.convert("RGBA").save(buf, "PNG", optimize=True); im["mimeType"] = "image/png"
        else:
            img.convert("RGB").save(buf, "JPEG", quality=86, optimize=True); im["mimeType"] = "image/jpeg"
        if len(buf.getvalue()) < len(raw): new[im["bufferView"]] = buf.getvalue()
        else: im["mimeType"] = im.get("mimeType", "image/png")
    out = bytearray()
    for i, bv in enumerate(j["bufferViews"]):    # re-lay every view, 4-byte aligned
        data = new.get(i, binc[bv.get("byteOffset", 0):bv.get("byteOffset", 0) + bv["byteLength"]])
        while len(out) % 4: out.append(0)
        bv["byteOffset"] = len(out); bv["byteLength"] = len(data); out += data
    while len(out) % 4: out.append(0)
    j["buffers"][0]["byteLength"] = len(out)
    js = json.dumps(j, separators=(",", ":")).encode()
    while len(js) % 4: js += b" "
    total = 12 + 8 + len(js) + 8 + len(out)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "wb") as f:
        f.write(b"glTF" + struct.pack("<II", 2, total) + struct.pack("<I", len(js)) + b"JSON" + js
                + struct.pack("<I", len(out)) + b"BIN\x00" + bytes(out))
    return len(d), total


def copy(rel):
    s, t = os.path.join(REPO, rel), os.path.join(OUT, rel)
    os.makedirs(os.path.dirname(t), exist_ok=True); shutil.copy2(s, t); return os.path.getsize(t)


def main():
    shutil.rmtree(OUT, ignore_errors=True); os.makedirs(OUT)
    tot_in = tot_out = 0
    for n in ENEMIES + WEAPONS:
        a, b = shrink_glb(os.path.join(REPO, "assets", n + ".glb"), os.path.join(OUT, "assets", n + ".glb"))
        tot_in += a; tot_out += b; print(f"  {n:16s} {a/1e6:6.1f} -> {b/1e6:5.1f} MB")
    other = 0
    for n in ENEMIES:
        for suf in ("_lod.glb", ".gait"):
            if os.path.exists(os.path.join(REPO, "assets", n + suf)): other += copy(f"assets/{n}{suf}")
    for n in WEAPONS:                               # true bind poses (ApplyIbmBindPose): without them the arms skin against the rest pose
        if os.path.exists(os.path.join(REPO, "assets", n + ".glb.ibm")): other += copy(f"assets/{n}.glb.ibm")
        if os.path.exists(os.path.join(REPO, "assets", n + ".glb.parts")): other += copy(f"assets/{n}.glb.parts")   # authored gun-piece animation (PartsLoad)
    for f in sorted(os.listdir(os.path.join(REPO, "assets"))):
        if f.startswith("intro"): continue          # the title montage (33 MB of video + music) stays desktop-only for now
        if f.endswith((".mp3", ".wav", ".txt")) or f == "icon.png":
            other += copy("assets/" + f)
    for f in sorted(os.listdir(os.path.join(REPO, "assets", "voice"))): other += copy("assets/voice/" + f)
    # the default map + only the textures it names (any extension), capped at 512 px
    other += copy("maps/chernobyl.map")
    names = set(re.findall(r"\)\s+([A-Za-z0-9_\-/\.]+)\s+[-\d\.]+\s+[-\d\.]+", open(os.path.join(REPO, "maps", "chernobyl.map")).read()))
    mt = 0
    for dp, _, fs in os.walk(os.path.join(REPO, "maps", "textures")):
        for f in fs:
            rel = os.path.relpath(os.path.join(dp, f), os.path.join(REPO, "maps", "textures"))
            stem = os.path.splitext(rel)[0].replace(os.sep, "/")
            if not any(stem == n or stem.endswith("/" + n) or n.endswith("/" + stem) or stem == n.split("/")[-1] for n in names): continue
            dst = os.path.join(OUT, "maps", "textures", rel); os.makedirs(os.path.dirname(dst), exist_ok=True)
            try:
                img = Image.open(os.path.join(dp, f)); img.load()
                if max(img.size) > 512: k = 512 / max(img.size); img = img.resize((round(img.size[0]*k), round(img.size[1]*k)), Image.LANCZOS)
                img.save(dst); mt += os.path.getsize(dst)
            except Exception:
                shutil.copy2(os.path.join(dp, f), dst); mt += os.path.getsize(dst)
    print(f"models {tot_in/1e6:.0f} -> {tot_out/1e6:.1f} MB; sounds/intro/misc {other/1e6:.1f} MB; map textures {mt/1e6:.1f} MB ({len(names)} names)")
    size = sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(OUT) for f in fs)
    print(f"web asset set: {size/1e6:.1f} MB in {OUT}")


if __name__ == "__main__":
    main()
