#!/usr/bin/env python3
"""The title screen's two panels, CONTROLS and WEAPONS, either side of the hammer and sickle (assets/intro_controls.png;
the web page shows the same image, tools/web/pack_web.py). Kept apart from build_intro.py, which beat-tracks the music
as it loads, so a change of keys needs only Pillow and numpy:

    uv run --no-project --with numpy --with pillow python tools/intro/controls.py

Keep in step with main.c: kSlotForKey maps keys 1-4 to the guns, 5 is the claymore (mines.h), M mutes (the intro too)."""
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
W, H = 1280, 720
FONT_TTY = "/System/Library/Fonts/Supplemental/Courier New Bold.ttf"
CONTROLS = [("MOVE", "W A S D"), ("LOOK", "MOUSE"), ("FIRE", "LEFT CLICK"), ("RELOAD", "R"),
            ("SPRINT", "SHIFT"), ("JUMP", "SPACE"), ("MUTE", "M"), ("MENU", "ESC")]
WEAPONS = [("1", "MINIGUN"), ("2", "FLAMETHROWER"), ("3", "GRENADE LAUNCHER"), ("4", "REMINGTON"), ("5", "CLAYMORE")]


def controls_png(path):
    """Two teletype columns flanking the hammer & sickle on the title screen."""
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    fh = ImageFont.truetype(FONT_TTY, 24)
    fr = ImageFont.truetype(FONT_TTY, 17)
    fk = ImageFont.truetype(FONT_TTY, 22)
    def column(x0, x1, ru, en, rows, key_first):
        y0, step = 300, 34
        pan = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        ImageDraw.Draw(pan).rectangle([x0 - 22, y0 - 58, x1 + 22, y0 + step * len(rows) + 8], fill=(0, 0, 0, 165))
        im.alpha_composite(pan.filter(ImageFilter.GaussianBlur(10)))
        d = ImageDraw.Draw(im)
        d.text((x0, y0 - 52), ru, font=fr, fill=(214, 40, 28, 255))
        d.text((x0, y0 - 32), en, font=fh, fill=(238, 234, 222, 255))
        d.line([x0, y0 - 4, x1, y0 - 4], fill=(214, 40, 28, 200), width=2)
        for i, (a, b) in enumerate(rows):
            y = y0 + 6 + i * step
            if key_first:      # "1   MINIGUN"
                d.text((x0, y), a, font=fk, fill=(214, 40, 28, 255))
                d.text((x0 + 44, y), b, font=fk, fill=(238, 234, 222, 255))
            else:              # "MOVE ..... W A S D"
                d.text((x0, y), a, font=fk, fill=(214, 40, 28, 255))
                w = d.textlength(b, font=fk)
                d.text((x1 - w, y), b, font=fk, fill=(238, 234, 222, 255))
    column(70, 400, "УПРАВЛЕНИЕ", "CONTROLS", CONTROLS, False)
    column(880, 1210, "ОРУЖИЕ", "WEAPONS", WEAPONS, True)
    a = np.asarray(im, np.float32)                       # grain, as on the subtitles
    rng = np.random.default_rng(42)
    a[..., :3] = np.clip(a[..., :3] + rng.normal(0, 9, a.shape[:2])[..., None], 0, 255)
    Image.fromarray(a.astype(np.uint8), "RGBA").save(path)


if __name__ == "__main__":
    out = os.path.join(REPO, "assets", "intro_controls.png")
    controls_png(out)
    print("wrote", out)
