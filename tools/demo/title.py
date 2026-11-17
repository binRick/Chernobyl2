#!/usr/bin/env python3
"""The demo's closing card, as three transparent 1920x1080 layers (ffmpeg here has no drawtext)."""
import sys
from PIL import Image, ImageDraw, ImageFont
out = sys.argv[1]
SUP = "/System/Library/Fonts/Supplemental/"
layers = [("CHERNOBYL 2", SUP + "Arial Black.ttf", 170, (200, 30, 30), -150),
          ("L I Q U I D A T O R", SUP + "Courier New Bold.ttf", 52, (216, 216, 216), 50),
          ("Coming to iPhone   ·   play now at chernobyl2.ximg.app", SUP + "Courier New Bold.ttf", 34, (154, 154, 154), 190)]
for i, (text, font, size, col, dy) in enumerate(layers):
    im = Image.new("RGBA", (1920, 1080), (0, 0, 0, 0)); d = ImageDraw.Draw(im); f = ImageFont.truetype(font, size)
    x0, y0, x1, y1 = d.textbbox((0, 0), text, font=f)
    d.text(((1920 - (x1 - x0)) / 2 - x0, 540 + dy - y0), text, font=f, fill=col + (255,))
    im.save(f"{out}/title{i}.png")
