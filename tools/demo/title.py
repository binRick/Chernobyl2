#!/usr/bin/env python3
"""The demo's cards as transparent 1920x1080 layers (this ffmpeg has no drawtext).
   title.py <dir> intro  ->  intro0..2.png   (date, name, subtitle)
   title.py <dir> end    ->  title0..2.png   (name, subtitle, where to play)"""
import sys
from PIL import Image, ImageDraw, ImageFont
out, which = sys.argv[1], (sys.argv[2] if len(sys.argv) > 2 else "end")
SUP = "/System/Library/Fonts/Supplemental/"
BLACK, MONO = SUP + "Arial Black.ttf", SUP + "Courier New Bold.ttf"
RED, LIGHT, GREY = (200, 30, 30), (216, 216, 216), (154, 154, 154)
cards = {
    "intro": ("intro", [("26 APRIL 1986   ·   01:23", MONO, 40, GREY, -230),
                        ("CHERNOBYL 2", BLACK, 170, RED, -110),
                        ("L I Q U I D A T O R", MONO, 52, LIGHT, 90)]),
    "end": ("title", [("CHERNOBYL 2", BLACK, 170, RED, -150),
                      ("L I Q U I D A T O R", MONO, 52, LIGHT, 50),
                      ("Coming to iPhone   ·   play now at chernobyl2.ximg.app", MONO, 34, GREY, 190)]),
}
prefix, layers = cards[which]
for i, (text, font, size, col, dy) in enumerate(layers):
    im = Image.new("RGBA", (1920, 1080), (0, 0, 0, 0)); d = ImageDraw.Draw(im); f = ImageFont.truetype(font, size)
    x0, y0, x1, y1 = d.textbbox((0, 0), text, font=f)
    d.text(((1920 - (x1 - x0)) / 2 - x0, 540 + dy - y0), text, font=f, fill=col + (255,))
    im.save(f"{out}/{prefix}{i}.png")
