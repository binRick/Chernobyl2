#!/usr/bin/env python3
"""Bring the real Soviet posters into the arena's textures (maps/textures/chernobyl/real_NN.png).

tools/map/posters.json lists them: each is a file on Wikimedia Commons that Commons marks as in
the PUBLIC DOMAIN (works of 1919-1935), with its page there. This fetches each one (or takes a
copy already in build/tmp/commons), sizes it for a wall, ages it a little -- it has hung in a
power station for decades -- and writes maps/textures/chernobyl/POSTERS.md, the credits.

    python3 tools/map/import_posters.py
"""
import json, os, sys, time, urllib.request
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_textures as mt

REPO = mt.REPO; OUT = mt.OUT; CACHE = os.path.join(REPO, "build", "tmp", "commons")
os.makedirs(CACHE, exist_ok=True)
man = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "posters.json")))
lines = ["# Posters on the walls", "",
         "The `real_NN.png` textures are reproductions of Soviet posters of 1919-1935. Each is a file that",
         "Wikimedia Commons marks as being in the public domain; they were fetched from there, resized and",
         "weathered by `tools/map/import_posters.py`. The `poster_*.png` textures are not reproductions:",
         "they are painted by `tools/map/make_textures.py`.", "",
         "| Texture | Poster | Artist | Year | Licence | Source |", "|---|---|---|---|---|---|"]
sizes = {}
for p in man:
    src = os.path.join(CACHE, p.get("src") or p["name"] + ".jpg")
    if not os.path.exists(src):
        time.sleep(4)
        req = urllib.request.Request(p["image"], headers={"User-Agent": "poster-import/1.0 (generic client)"})
        open(src, "wb").write(urllib.request.urlopen(req, timeout=60).read())
    img = Image.open(src).convert("RGB")
    w = 512; h = min(832, round(img.size[1] * w / img.size[0])); img = img.resize((w, h), Image.LANCZOS)
    seed = 400 + int(p["name"][-2:])
    img = mt.shade(img, mt.fbm((w, h), w / 2.5, seed), 0.72, 1.0)                                   # dulled and unevenly dirty
    img = mt.over(img, mt.flat((w, h), (0.26, 0.18, 0.10)), mt.streaks((w, h), seed + 1, 6, h, 6).point(lambda v: v * 35 // 100))
    img = mt.over(img, mt.flat((w, h), (0.30, 0.24, 0.14)), mt.thresh(mt.fbm((w, h), w / 5, seed + 2), 200, 30).point(lambda v: v * 4 // 10))
    mt.save(p["name"], img); sizes[p["name"]] = (w, h)
    lines.append(f"| `{p['name']}.png` | {p['title']} | {p['artist']} | {p['date']} | {p['licence']} | [Commons]({p['page']}) |")
open(os.path.join(OUT, "POSTERS.md"), "w").write("\n".join(lines) + "\n")
json.dump(sizes, open(os.path.join(OUT, "posters_real.json"), "w"), indent=1)
print(f"{len(man)} posters; credits in maps/textures/chernobyl/POSTERS.md")
