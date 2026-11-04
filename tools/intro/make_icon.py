#!/usr/bin/env python3
"""Chernobyl2 app icon (Dock / Cmd-Tab), in the intro's look.

A grainy black-and-white still of the wrecked unit 4 (from the 1986 aerial footage),
a worn red hammer-and-sickle stamp, and a bold "2", inside the macOS rounded square.

    build/introvenv/bin/python tools/intro/make_icon.py [variant]   -> assets/icon.png (512)
"""
import os, sys, subprocess
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageOps

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC = os.path.join(REPO, "build", "intro_src", "aerial.mp4")
S = 1024                       # work size; saved at 512
RED = (214, 18, 12)


def reactor_still():
    tmp = os.path.join(REPO, "build", "intro", "icon_still.png")
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-ss", "24.5", "-i", SRC, "-frames:v", "1", "-update", "1", tmp], check=True)
    im = Image.open(tmp).convert("L")
    w, h = im.size                                   # square crop on the ruin
    c = min(w, h)
    im = im.crop((int(w * 0.30), 0, int(w * 0.30) + c, c)).resize((S, S), Image.LANCZOS)
    im = ImageOps.autocontrast(im, cutoff=2)
    a = np.asarray(im, np.float32) / 255.0
    a = np.clip((a - 0.5) * 1.5 + 0.42, 0, 1) ** 1.25     # punchy, dark
    rng = np.random.default_rng(26)
    a = np.clip(a + rng.normal(0, 0.06, a.shape), 0, 1)    # film grain
    yy, xx = np.mgrid[0:S, 0:S] / S - 0.5
    a *= np.clip(1.15 - (xx ** 2 + yy ** 2) * 2.6, 0, 1)   # vignette
    a *= 0.55                                               # sink it under the stamp
    g = (a * 255).astype(np.uint8)
    return Image.merge("RGB", (Image.fromarray(g),) * 3)


def stamp(size):
    f = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Unicode.ttf", 1000)
    g = Image.new("L", (1200, 1200), 0)
    ImageDraw.Draw(g).text((60, -140), "☭", font=f, fill=255)
    g = g.crop(g.getbbox())
    g = g.resize((int(g.width * size / g.height), size), Image.LANCZOS)
    rng = np.random.default_rng(1917)
    a = np.asarray(g, np.float32) / 255.0
    blot = np.asarray(Image.fromarray((rng.random((a.shape[0] // 12, a.shape[1] // 12)) * 255).astype(np.uint8))
                      .resize(a.shape[::-1], Image.BICUBIC), np.float32) / 255.0
    a *= np.clip(1.0 - (rng.random(a.shape) > 0.988) - np.clip((blot - 0.80) * 4, 0, 1) * 0.8, 0, 1)
    return Image.fromarray((a * 255).astype(np.uint8))


def squircle_mask(margin=100, radius=185):
    m = Image.new("L", (S, S), 0)
    ImageDraw.Draw(m).rounded_rectangle([margin, margin, S - margin, S - margin], radius=radius, fill=255)
    return m


def build(variant="a"):
    bg = reactor_still().convert("RGBA")
    # red glow + the stamp
    hs_size = 600 if variant == "a" else 520
    a = stamp(hs_size)
    cx, cy = S // 2 - (0 if variant == "a" else 50), S // 2 + (10 if variant == "a" else -20)
    x, y = cx - a.width // 2, cy - a.height // 2
    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    glow.paste((255, 30, 10, 255), (x, y), a.point(lambda v: v * 3 // 4))
    bg.alpha_composite(glow.filter(ImageFilter.GaussianBlur(38)))
    sh = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    sh.paste((0, 0, 0, 255), (x + 14, y + 20), a.point(lambda v: v * 4 // 5))
    bg.alpha_composite(sh.filter(ImageFilter.GaussianBlur(14)))
    bg.paste(RED + (255,), (x, y), a)
    if variant == "b":                       # a bold "2" bottom-right, like the title
        f = ImageFont.truetype("/System/Library/Fonts/Supplemental/Impact.ttf", 330)
        d = ImageDraw.Draw(bg)
        tx, ty = S - 100 - 230, S - 100 - 395
        d.text((tx + 12, ty + 16), "2", font=f, fill=(0, 0, 0, 230))
        d.text((tx, ty), "2", font=f, fill=(240, 236, 228, 255))
    # rounded-square card with a thin dark rim, transparent outside (macOS icon grid)
    m = squircle_mask()
    out = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    out.paste(bg, (0, 0), m)
    rim = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(rim).rounded_rectangle([100, 100, S - 100, S - 100], radius=185, outline=(0, 0, 0, 200), width=6)
    out.alpha_composite(rim)
    dsh = Image.new("RGBA", (S, S), (0, 0, 0, 0))     # soft drop shadow under the card
    dsh.paste((0, 0, 0, 120), (0, 14), m)
    dsh = dsh.filter(ImageFilter.GaussianBlur(18))
    dsh.alpha_composite(out)
    return dsh.resize((512, 512), Image.LANCZOS)


if __name__ == "__main__":
    v = sys.argv[1] if len(sys.argv) > 1 else "b"
    dst = sys.argv[2] if len(sys.argv) > 2 else os.path.join(REPO, "assets", "icon.png")
    build(v).save(dst)
    print("wrote", dst)
