#!/usr/bin/env python3
"""Paint the Chernobyl arena's surfaces (maps/textures/chernobyl/*.png) -- procedurally, no
third-party art: poured concrete, the two-tone institutional paint of a Soviet plant, steel deck
plate, worn floor tile, the scorched reactor side, and the plant's signs.

Walls are painted for one repeat = the full wall height (tools/grid2map.py maps them 1:1), so the
dado line, the grime at the skirting and the soot under the ceiling sit where they should.

    python3 tools/map/make_textures.py        (needs Pillow; signs use the system's PT Sans / Arial)
"""
import math, os, random
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont, ImageOps

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(REPO, "maps", "textures", "chernobyl")
os.makedirs(OUT, exist_ok=True)
WALL_M = 5.1                       # metres one wall repeat covers (its full height)


def noise(size, cell, seed, contrast=1.0):
    """Smooth grey noise, 0..255, features about `cell` px across, tileable."""
    random.seed(seed)
    w, h = size; n = max(2, round(w / cell)), max(2, round(h / cell))
    small = Image.new("L", n); small.putdata([random.randint(0, 255) for _ in range(n[0] * n[1])])
    big = Image.new("L", (n[0] * 3, n[1] * 3))
    for i in range(3):
        for j in range(3): big.paste(small, (i * n[0], j * n[1]))             # 3x3 tiled, so resizing wraps cleanly
    big = big.resize((w * 3, h * 3), Image.BICUBIC).crop((w, h, 2 * w, 2 * h))
    return ImageOps.autocontrast(big) if contrast == 1.0 else big.point(lambda v: max(0, min(255, int((v - 128) * contrast + 128))))


def fbm(size, cell, seed, octaves=4):
    acc = None
    for o in range(octaves):
        n = noise(size, max(2, cell / (2 ** o)), seed + o * 7)
        acc = n if acc is None else Image.blend(acc, n, 1.0 / (o + 2))
    return ImageOps.autocontrast(acc)


def grain(size, seed, amount):
    random.seed(seed); g = Image.effect_noise(size, amount)
    return g


def tint(mask, col):                # L mask -> RGB of col scaled by mask
    return Image.merge("RGB", [mask.point(lambda v, c=c: int(v * c)) for c in col])


def flat(size, col): return Image.new("RGB", size, tuple(int(c * 255) for c in col))


def over(base, top, mask): return Image.composite(top, base, mask)


def shade(img, mask, lo, hi):       # multiply img by lo..hi following mask (0..255)
    m = mask.point(lambda v: int(255 * (lo + (hi - lo) * v / 255)))
    return ImageChops.multiply(img, Image.merge("RGB", (m, m, m)))


def thresh(mask, t, soft=12): return mask.point(lambda v: max(0, min(255, int((v - t) * 255 / soft))))


def vgrad(size, stops):             # vertical gradient from (y fraction, value) stops, top = 0
    w, h = size; col = Image.new("L", (1, h)); px = []
    for y in range(h):
        f = y / (h - 1)
        for (a, va), (b, vb) in zip(stops, stops[1:]):
            if a <= f <= b: px.append(int(va + (vb - va) * (f - a) / max(1e-6, b - a))); break
        else: px.append(int(stops[-1][1]))
    col.putdata(px); return col.resize((w, h))


def streaks(size, seed, n, length, width, top=True):
    """Runs of water / rust down the wall: soft vertical smears, white on black."""
    random.seed(seed); w, h = size; im = Image.new("L", size, 0); d = ImageDraw.Draw(im)
    for _ in range(n):
        x = random.randint(0, w); y0 = 0 if top else random.randint(0, h // 2); L = random.randint(length // 3, length); wd = random.randint(max(1, width // 3), width)
        for k in range(L):
            a = int(255 * (1 - k / L) ** 1.5); xx = x + int(2 * math.sin(k / 37.0 + x))
            d.line([(xx - wd, y0 + k), (xx + wd, y0 + k)], fill=a)
            if xx - wd < 0: d.line([(xx - wd + w, y0 + k), (w, y0 + k)], fill=a)
    return im.filter(ImageFilter.GaussianBlur(2.5))


def cracks(size, seed, n, steps=220, width=2):
    random.seed(seed); w, h = size; im = Image.new("L", size, 0); d = ImageDraw.Draw(im)
    for _ in range(n):
        x, y = random.uniform(0, w), random.uniform(0, h); a = random.uniform(0, 6.28)
        for s in range(steps):
            a += random.uniform(-0.5, 0.5); nx, ny = x + math.cos(a) * 5, y + math.sin(a) * 5
            d.line([(x % w, y % h), (nx % w, ny % h)] if abs(nx % w - x % w) < 20 and abs(ny % h - y % h) < 20 else [(x % w, y % h), (x % w, y % h)], fill=255, width=width if s < steps * 0.6 else 1)
            x, y = nx, ny
            if random.random() < 0.012:                                           # a branch
                bx, by, ba = x, y, a + random.choice((-1, 1)) * random.uniform(0.6, 1.2)
                for _s in range(steps // 4):
                    ba += random.uniform(-0.5, 0.5); cx, cy = bx + math.cos(ba) * 4, by + math.sin(ba) * 4
                    if abs(cx % w - bx % w) < 20 and abs(cy % h - by % h) < 20: d.line([(bx % w, by % h), (cx % w, cy % h)], fill=200, width=1)
                    bx, by = cx, cy
    return im


# ---- lit lamps: what glows on a panel, and how it blinks ----------------------------------------
# A panel's lamps are painted twice: into the panel itself (dark lenses for the ones that blink),
# and into <name>_glow.png -- GLOW_K copies of the panel stacked top to bottom, black but for the
# light of the lamps that belong to that layer. The game adds each layer times its own waveform
# (src/main.c, the world shader): 0 steady, 1-3 slow blinks out of step, 4 an alarm's fast flash,
# 5 a failing lamp's stutter. Layers are plain light, so mip-mapping them is exact.
GLOW_K = 6
_lit = []                          # (shape, box, rgb, layer) of the panel being painted; save() writes and clears it


def lit(d, shape, box, col, layer=None, **kw):
    """Paint one lit lamp ('e' ellipse / 'r' rectangle / 'p' polygon) and note it for the glow layers."""
    if layer is None:
        k = random.Random(int(box[0] * 7.31 + box[1] * 13.7) * 31 + len(_lit)).random()
        red, green = col[0] > 150 and col[1] < 100, col[1] > col[0]
        layer = (4 if k < 0.45 else (1 + int(k * 97) % 3 if k < 0.65 else 0)) if red else ((0 if k < 0.80 else 1 + int(k * 97) % 3) if green else (1 + int(k * 97) % 3 if k < 0.50 else 0))
    base = col if layer == 0 else tuple(int(c * 0.26 + 14) for c in col)          # a lamp that blinks is painted out: the glow layer lights it
    if shape == "p": d.polygon(box, fill=base, **kw)
    else: (d.ellipse if shape == "e" else d.rectangle)(box, fill=base, **kw)
    _lit.append((shape, box, col, layer))


def glow_layers(size):
    w, h = size; s = 0.5 if w >= 1024 else 1.0; gw, gh = int(w * s), int(h * s)
    out = Image.new("RGB", (gw, gh * GLOW_K), (0, 0, 0))
    for k in range(GLOW_K):
        mine = [l for l in _lit if l[3] == k]
        if not mine: continue
        img = Image.new("RGB", (gw, gh), (0, 0, 0)); d = ImageDraw.Draw(img)
        for shape, box, col, _ in mine:
            if shape == "p": d.polygon([(x * s, y * s) for x, y in box], fill=col)
            else: (d.ellipse if shape == "e" else d.rectangle)([v * s for v in box], fill=col)
        halo = img.filter(ImageFilter.GaussianBlur(max(2.0, gw / 110))).point(lambda v: int(v * 0.55))     # its light on the panel round it
        out.paste(ImageChops.add(img.filter(ImageFilter.GaussianBlur(0.7)), halo), (0, k * gh))
    return out


def save(name, img, colors=256):
    if _lit:
        g = glow_layers(img.size); p = os.path.join(OUT, name + "_glow.png"); g.save(p, optimize=True); n = len(_lit); del _lit[:]
        print(f"  {name + '_glow':18s} {g.size[0]}x{g.size[1]} {os.path.getsize(p) / 1024:.0f} KB  ({n} lamps)")
    img = img.convert("RGB").quantize(colors, method=Image.MEDIANCUT, dither=Image.FLOYDSTEINBERG) if colors else img
    p = os.path.join(OUT, name + ".png"); img.save(p, optimize=True); print(f"  {name:18s} {img.size[0]}x{img.size[1]} {os.path.getsize(p) / 1024:.0f} KB")


def concrete(size, seed, base=(0.50, 0.49, 0.46), boards=True):
    """Poured concrete: mottled grey, pin-holes, and the lines of the formwork it was cast in."""
    w, h = size
    img = shade(flat(size, base), fbm(size, w / 3, seed), 0.72, 1.12)
    img = shade(img, fbm(size, w / 14, seed + 3), 0.90, 1.06)
    g = grain(size, seed + 5, 22).point(lambda v: 128 + (v - 128) // 3)
    img = ImageChops.multiply(img, Image.merge("RGB", (g, g, g)).point(lambda v: min(255, v * 2)))
    random.seed(seed + 9); d = ImageDraw.Draw(img, "RGBA")
    for _ in range(int(w * h / 900)):                                             # bug holes
        x, y, r = random.randint(0, w), random.randint(0, h), random.choice((1, 1, 1, 2, 2, 3))
        d.ellipse([x - r, y - r, x + r, y + r], fill=(30, 28, 26, random.randint(60, 150)))
    if boards:
        ph = h / (WALL_M / 0.6)                                                   # a lift of formwork every 0.6 m
        k = 0
        while k * ph < h:
            y = int(k * ph); d.line([(0, y), (w, y)], fill=(40, 38, 36, 120), width=2); d.line([(0, y + 2), (w, y + 2)], fill=(255, 255, 255, 26), width=1)
            for x in range(int(w * 0.12), w, int(w / 4)):                         # tie holes
                yy = y + int(ph / 2); d.ellipse([x - 5, yy - 5, x + 5, yy + 5], fill=(34, 32, 30, 200)); d.ellipse([x - 8, yy - 8, x + 8, yy + 8], outline=(70, 66, 60, 120))
            k += 1
        for x in (0, w // 2):                                                     # panel joints
            d.line([(x, 0), (x, h)], fill=(40, 38, 36, 90), width=2)
    return img


def wall_bare(size):
    img = concrete(size, 11)
    img = over(img, tint(Image.new("L", size, 255), (0.30, 0.22, 0.14)), streaks(size, 12, 26, size[1] * 2 // 3, 9).point(lambda v: v * 5 // 10))   # rust runs
    img = shade(img, vgrad(size, [(0, 150), (0.12, 235), (0.85, 255), (1, 150)]), 0.0, 1.0)                                               # dirty at the ceiling and the skirting
    return img


def wall_paint(size, seed, dado, upper, tray=False):
    """Two-tone plant paint over concrete: a coloured dado to shoulder height, a dark line, cream
    above; peeling to the concrete, water runs from the ceiling, scuffs and grime at the floor."""
    w, h = size; conc = concrete(size, seed, boards=False)
    yd = int(h * (1 - 1.55 / WALL_M))                                            # top of the dado, 1.55 m up
    paint = flat(size, upper); d = ImageDraw.Draw(paint)
    d.rectangle([0, yd, w, h], fill=tuple(int(c * 255) for c in dado))
    d.rectangle([0, yd - 7, w, yd], fill=tuple(int(c * 255 * 0.45) for c in dado))                                                        # the line between them
    paint = shade(paint, fbm(size, w / 5, seed + 20), 0.80, 1.06)
    paint = shade(paint, grain(size, seed + 21, 14), 0.93, 1.05)
    peel = thresh(ImageChops.multiply(fbm(size, w / 6, seed + 30), vgrad(size, [(0, 210), (0.3, 150), (0.75, 170), (1, 255)])), 118, 6)   # where it has let go
    edge = ImageChops.subtract(peel.filter(ImageFilter.MaxFilter(7)), peel)                                                              # the lifted lip round each patch
    img = over(paint, conc, peel)
    img = over(img, flat(size, (0.92, 0.90, 0.82)), edge.point(lambda v: v * 6 // 10))
    img = over(img, flat(size, (0.20, 0.16, 0.11)), streaks(size, seed + 40, 34, h * 3 // 4, 8).point(lambda v: v * 45 // 100))           # water and rust from above
    img = over(img, flat(size, (0.10, 0.09, 0.08)), vgrad(size, [(0, 150), (0.10, 30), (0.80, 0), (0.93, 60), (1, 215)]))                 # soot high up, boot-grime low down
    d = ImageDraw.Draw(img, "RGBA"); random.seed(seed + 50)
    for _ in range(40):                                                          # scuffs along the dado
        x, y = random.randint(0, w), random.randint(yd + 20, h - 30); L = random.randint(20, 120)
        d.line([(x, y), (x + L, y + random.randint(-6, 6))], fill=(25, 22, 20, random.randint(40, 110)), width=random.choice((1, 2, 3)))
    if tray:                                                                     # a cable tray under the ceiling, a conduit dropping from it
        yt = int(h * 0.115)
        d.rectangle([0, yt, w, yt + 46], fill=(58, 60, 62, 255)); d.rectangle([0, yt, w, yt + 5], fill=(98, 100, 102, 255)); d.rectangle([0, yt + 46, w, yt + 56], fill=(0, 0, 0, 90))
        for k, c in enumerate(((22, 22, 24), (120, 28, 22), (26, 26, 28), (30, 52, 96), (24, 24, 26))):
            d.rectangle([0, yt + 9 + k * 7, w, yt + 14 + k * 7], fill=c + (255,))
        for x in range(0, w, w // 8): d.rectangle([x, yt - 3, x + 9, yt + 52], fill=(74, 76, 78, 255))                                    # the tray's hangers
        cx = int(w * 0.31); d.rectangle([cx, yt + 46, cx + 12, int(h * 0.72)], fill=(70, 72, 74, 255)); d.rectangle([cx + 9, yt + 46, cx + 12, int(h * 0.72)], fill=(30, 30, 32, 255))
        d.rectangle([cx - 26, int(h * 0.72) - 6, cx + 38, int(h * 0.72) + 70], fill=(64, 68, 66, 255), outline=(26, 26, 26, 255), width=3)  # a switch box
    return img


def wall_scorched(size):
    """The reactor side: the paint burnt off, the concrete blackened and split, the steel showing."""
    w, h = size; img = concrete(size, 61, base=(0.40, 0.38, 0.36))
    burn = ImageChops.screen(fbm(size, w / 3, 62), vgrad(size, [(0, 230), (0.45, 120), (1, 40)]))
    img = shade(img, ImageOps.invert(burn), 0.16, 1.0)
    rebar = Image.new("L", size, 0); d = ImageDraw.Draw(rebar)
    for x in range(20, w, 64): d.line([(x, 0), (x + 6, h)], fill=255, width=5)
    for y in range(30, h, 64): d.line([(0, y), (w, y + 5)], fill=255, width=5)
    spall = thresh(fbm(size, w / 5, 64), 168, 8)                                 # where the face has burst off
    hole = shade(flat(size, (0.16, 0.14, 0.13)), fbm(size, w / 20, 65), 0.6, 1.2)
    hole = over(hole, flat(size, (0.42, 0.20, 0.10)), rebar)
    img = over(img, hole, spall)
    img = over(img, flat(size, (0.02, 0.02, 0.02)), ImageChops.subtract(spall.filter(ImageFilter.MaxFilter(9)), spall).point(lambda v: v * 7 // 10))
    cr = cracks(size, 66, 7).filter(ImageFilter.GaussianBlur(0.6))
    img = over(img, flat(size, (0.01, 0.01, 0.01)), cr)
    blister = thresh(fbm(size, w / 26, 67), 170, 20)                             # what is left of the paint, bubbled
    img = over(img, shade(flat(size, (0.34, 0.36, 0.30)), fbm(size, w / 40, 68), 0.5, 1.0), ImageChops.multiply(blister, vgrad(size, [(0, 0), (0.5, 60), (1, 170)])))
    return img


def floor_plate(size):
    """Steel deck: chequer plate in bolted metre panels, worn bright where boots go, rust at the seams."""
    w, h = size; img = shade(flat(size, (0.30, 0.31, 0.32)), fbm(size, w / 3, 71), 0.7, 1.15)
    d = ImageDraw.Draw(img, "RGBA")
    for y in range(0, h, 16):                                                    # the raised tread, alternating diagonals
        for x in range(0, w, 16):
            if (x // 16 + y // 16) % 2: d.line([(x + 3, y + 12), (x + 12, y + 3)], fill=(190, 192, 196, 70), width=2)
            else: d.line([(x + 3, y + 3), (x + 12, y + 12)], fill=(12, 12, 14, 80), width=2)
    n = 4
    for k in range(n + 1):
        p = int(k * w / n) % w
        d.rectangle([p - 2, 0, p + 2, h], fill=(14, 13, 12, 230)); d.rectangle([0, p - 2, w, p + 2], fill=(14, 13, 12, 230))
    for i in range(n):
        for j in range(n):
            for dx, dy in ((14, 14), (w // n - 14, 14), (14, h // n - 14), (w // n - 14, h // n - 14)):
                x, y = i * w // n + dx, j * h // n + dy; d.ellipse([x - 5, y - 5, x + 5, y + 5], fill=(60, 62, 64, 255), outline=(10, 10, 10, 255))
    rust = thresh(ImageChops.multiply(fbm(size, w / 8, 73), ImageOps.invert(noise(size, w / n, 74))), 60, 30)
    img = over(img, shade(flat(size, (0.36, 0.18, 0.08)), fbm(size, w / 30, 75), 0.6, 1.1), rust.point(lambda v: v * 6 // 10))
    return shade(img, grain(size, 76, 16), 0.9, 1.06)


def floor_tile(size):
    """The control corridor: small ceramic tile in two tired colours, cracked, dirty down the middle."""
    w, h = size; img = Image.new("RGB", size); d = ImageDraw.Draw(img); n = 8; random.seed(81)
    for j in range(n):
        for i in range(n):
            c = (0.56, 0.52, 0.42) if (i + j) % 2 else (0.36, 0.40, 0.36); k = random.uniform(0.86, 1.06)
            d.rectangle([i * w // n, j * h // n, (i + 1) * w // n, (j + 1) * h // n], fill=tuple(int(v * 255 * k) for v in c))
    for k in range(n + 1):
        p = int(k * w / n) % w; d.rectangle([p - 2, 0, p + 1, h], fill=(38, 34, 30)); d.rectangle([0, p - 2, w, p + 1], fill=(38, 34, 30))
    img = shade(img, fbm(size, w / 3, 82), 0.62, 1.08)
    img = over(img, flat(size, (0.05, 0.05, 0.05)), cracks(size, 83, 5, steps=120, width=1).point(lambda v: v * 8 // 10))
    gone = thresh(fbm(size, w / 7, 84), 196, 6)                                  # tiles missing: the screed under them
    img = over(img, concrete(size, 85, base=(0.30, 0.29, 0.27), boards=False), gone)
    return shade(img, grain(size, 86, 14), 0.9, 1.05)


def floor_ash(size):
    """The reactor side: concrete under graphite dust and grit, scorched in patches."""
    w, h = size; img = concrete(size, 91, base=(0.27, 0.26, 0.25), boards=False)
    img = shade(img, fbm(size, w / 2, 92), 0.45, 1.1)
    d = ImageDraw.Draw(img, "RGBA"); random.seed(93)
    for _ in range(1400):                                                        # grit and chips of graphite
        x, y, r = random.randint(0, w), random.randint(0, h), random.choice((1, 1, 2, 2, 3, 5)); c = random.choice(((14, 14, 16), (22, 22, 24), (60, 58, 54), (8, 8, 9)))
        d.polygon([(x, y - r), (x + r, y), (x + r // 2, y + r), (x - r, y + r // 2)], fill=c + (random.randint(120, 240),))
    img = over(img, flat(size, (0.0, 0.0, 0.0)), cracks(size, 94, 4, steps=160, width=2).point(lambda v: v * 9 // 10))
    return img


def ceiling(size, scorched=False):
    """A slab on concrete beams; ducts' rust and damp where the roof has let water in."""
    w, h = size; img = concrete(size, 101 + scorched, base=(0.36, 0.35, 0.33) if not scorched else (0.20, 0.19, 0.18), boards=False)
    d = ImageDraw.Draw(img, "RGBA")
    for k in range(2):                                                           # beams, as bands of shadow and a lit soffit
        y = int((k + 0.5) * h / 2)
        d.rectangle([0, y - 34, w, y + 34], fill=(0, 0, 0, 70)); d.rectangle([0, y - 22, w, y + 22], fill=(120, 118, 112, 40)); d.line([(0, y - 34), (w, y - 34)], fill=(0, 0, 0, 160), width=3); d.line([(0, y + 34), (w, y + 34)], fill=(0, 0, 0, 160), width=3)
    damp = thresh(fbm(size, w / 4, 103 + scorched), 150, 40)
    img = over(img, flat(size, (0.10, 0.09, 0.07) if not scorched else (0.02, 0.02, 0.02)), damp.point(lambda v: v * (5 if not scorched else 8) // 10))
    return img


def glow_core(size):
    """What is left of the core, far down the pit: a crust of slag, split, and the light under it."""
    w, h = size
    hot = fbm(size, w / 3, 112)
    veins = ImageChops.lighter(cracks(size, 113, 16, steps=200, width=5).filter(ImageFilter.GaussianBlur(2.0)), thresh(hot, 150, 60))
    img = shade(flat(size, (0.05, 0.06, 0.08)), fbm(size, w / 12, 114), 0.4, 1.3)                      # the crust
    img = over(img, flat(size, (0.10, 0.34, 0.75)), veins.filter(ImageFilter.GaussianBlur(10)))        # glow soaking through it
    img = over(img, shade(flat(size, (0.50, 0.84, 1.0)), fbm(size, w / 10, 115), 0.6, 1.0), veins)
    img = over(img, flat(size, (0.95, 0.99, 1.0)), thresh(ImageChops.multiply(veins, hot), 120, 40))   # white where it is open
    return img


def elena(size):
    """The reactor's lid -- the upper biological shield, two thousand tonnes, thrown on its edge:
    a steel face drilled in a close grid for the fuel channels, burnt and buckled."""
    w, h = size; img = shade(flat(size, (0.30, 0.29, 0.27)), fbm(size, w / 3, 131), 0.5, 1.1)
    d = ImageDraw.Draw(img, "RGBA"); n = 22; random.seed(132)
    for j in range(n):
        for i in range(n):
            x, y = (i + 0.5) * w / n, (j + 0.5) * h / n; r = w / n * 0.30
            d.ellipse([x - r, y - r, x + r, y + r], fill=(18, 17, 16, 255), outline=(96, 92, 86, 255), width=2)          # a channel head
            if random.random() < 0.55: d.ellipse([x - r * 0.5, y - r * 0.5, x + r * 0.5, y + r * 0.5], fill=(70, 66, 60, 255))   # some still capped
    for k in range(n + 1):
        p_ = int(k * w / n) % w; d.line([(p_, 0), (p_, h)], fill=(20, 19, 18, 90), width=1); d.line([(0, p_), (w, p_)], fill=(20, 19, 18, 90), width=1)
    img = shade(img, ImageOps.invert(thresh(fbm(size, w / 4, 133), 120, 90)), 0.25, 1.0)                # burnt in sheets
    img = over(img, shade(flat(size, (0.34, 0.16, 0.07)), fbm(size, w / 30, 134), 0.6, 1.1), thresh(fbm(size, w / 7, 135), 175, 20).point(lambda v: v * 7 // 10))
    return img


def hazard(size):
    """The edge of a deck or a step: broad yellow-and-black warning stripes on steel, chipped and
    dirty. Broad on purpose -- four to the metre: the fine-striped texture this replaces broke
    up into crawling lines at any distance."""
    w, h = size; img = flat(size, (0.07, 0.07, 0.07)); d = ImageDraw.Draw(img); n = 4; step = w // n
    for k in range(-n, 2 * n):
        x = k * step; d.polygon([(x, h), (x + step // 2, h), (x + step // 2 + h, 0), (x + h, 0)], fill=(196, 150, 22))
    img = shade(img, fbm(size, w / 3, 151), 0.62, 1.05)
    chips = thresh(fbm(size, w / 9, 152), 168, 14)
    img = over(img, shade(flat(size, (0.20, 0.19, 0.18)), fbm(size, w / 25, 153), 0.6, 1.1), chips)
    img = over(img, flat(size, (0.30, 0.15, 0.07)), thresh(fbm(size, w / 6, 154), 185, 20).point(lambda v: v * 7 // 10))
    d = ImageDraw.Draw(img, "RGBA"); d.rectangle([0, 0, w, 10], fill=(20, 20, 20, 255)); d.rectangle([0, h - 8, w, h], fill=(0, 0, 0, 120))    # the deck's lip above, grime at the floor
    return shade(img, grain(size, 155, 14), 0.92, 1.05)


def steel_rust(size):
    w, h = size; img = shade(flat(size, (0.16, 0.14, 0.13)), fbm(size, w / 3, 141), 0.5, 1.2)
    return over(img, shade(flat(size, (0.36, 0.17, 0.08)), fbm(size, w / 12, 142), 0.6, 1.1), thresh(fbm(size, w / 5, 143), 120, 60).point(lambda v: v * 8 // 10))


def lamp(size, on, col=(1.0, 0.95, 0.85), fail=False):
    w, h = size; img = flat(size, (0.16, 0.16, 0.16)); d = ImageDraw.Draw(img)
    d.rectangle([8, 8, w - 8, h - 8], fill=(70, 72, 72), outline=(20, 20, 20), width=4)
    for k in range(3):                                                           # three tubes behind a grille
        x0 = 30 + k * (w - 60) // 3; x1 = x0 + (w - 60) // 3 - 14; c8 = tuple(int(255 * c) for c in col)
        d.rounded_rectangle([x0, 26, x1, h - 26], 14, fill=(c8 if not fail else tuple(v // 5 + 20 for v in c8)) if on else (46, 46, 44))
        if on and not (fail and k == 1): _lit.append(("r", [x0 + 6, 30, x1 - 6, h - 30], c8, 5 if fail else 0))     # (a failing fitting: one tube dead, two stuttering)
    for y in range(26, h - 26, 18): d.line([(20, y), (w - 20, y)], fill=(24, 24, 24), width=2)
    if not on: img = shade(img, fbm(size, w / 3, 121), 0.5, 1.0)
    return img


# ---- signs: enamelled steel plates, as they hung in the plant ---------------------------------
def font(px, bold=True, narrow=False):
    for path, idx in ((("/System/Library/Fonts/Supplemental/DIN Condensed Bold.ttf", 0),) if narrow else ()) + \
            (("/System/Library/Fonts/Supplemental/PTSans.ttc", 7 if bold else 0), ("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 0), ("/Library/Fonts/Arial Unicode.ttf", 0)):
        try: return ImageFont.truetype(path, px, index=idx)
        except Exception: continue
    return ImageFont.load_default()


def plate(size, bg, border, seed, rust=0.5):
    w, h = size; img = flat(size, bg); d = ImageDraw.Draw(img)
    d.rectangle([0, 0, w - 1, h - 1], outline=tuple(int(c * 255) for c in border), width=max(6, w // 40))
    return img, d


def weather(img, seed, rust=0.5):
    size = img.size; w, h = size
    img = shade(img, fbm(size, w / 3, seed), 0.72, 1.05)
    chips = thresh(ImageChops.lighter(fbm(size, w / 9, seed + 1), ImageOps.invert(vgrad(size, [(0, 60), (0.07, 255), (0.93, 255), (1, 40)]))), int(232 - 34 * rust), 8)   # rust mostly creeping in from the edges
    img = over(img, shade(flat(size, (0.34, 0.17, 0.08)), fbm(size, w / 20, seed + 2), 0.5, 1.1), chips)
    img = over(img, flat(size, (0.24, 0.12, 0.06)), streaks(size, seed + 3, 9, h, 5).point(lambda v: int(v * 0.5 * rust)))
    d = ImageDraw.Draw(img)
    for x, y in ((0.06, 0.08), (0.94, 0.08), (0.06, 0.92), (0.94, 0.92)):         # the screws
        cx, cy, r = int(x * w), int(y * h), max(4, w // 70); d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(52, 50, 48), outline=(14, 14, 14))
    return img


def centred(d, box, text, fnt, fill, spacing=4):
    x0, y0, x1, y1 = box; bb = d.multiline_textbbox((0, 0), text, font=fnt, spacing=spacing, align="center")
    d.multiline_text(((x0 + x1 - (bb[2] - bb[0])) / 2 - bb[0], (y0 + y1 - (bb[3] - bb[1])) / 2 - bb[1]), text, font=fnt, fill=fill, spacing=spacing, align="center")


def trefoil(d, c, r, fill):
    cx, cy = c; d.ellipse([cx - r * 0.2, cy - r * 0.2, cx + r * 0.2, cy + r * 0.2], fill=fill)
    for k in range(3):
        a0 = -90 - 30 + k * 120; d.pieslice([cx - r, cy - r, cx + r, cy + r], a0, a0 + 60, fill=fill)
    d.ellipse([cx - r * 0.3, cy - r * 0.3, cx + r * 0.3, cy + r * 0.3], outline=None)


def signs():
    Y, K, R, Wt, G, B = (0.86, 0.68, 0.10), (0.06, 0.06, 0.06), (0.70, 0.08, 0.06), (0.86, 0.85, 0.80), (0.10, 0.42, 0.22), (0.12, 0.24, 0.52)
    c = lambda v: tuple(int(x * 255) for x in v)
    S = (512, 512)
    img, d = plate(S, Y, K, 1)                                                    # radiation: the trefoil
    ring = Image.new("L", S, 0); rd = ImageDraw.Draw(ring); trefoil(rd, (256, 215), 150, 255); rd.ellipse([256 - 45, 215 - 45, 256 + 45, 215 + 45], fill=0); rd.ellipse([256 - 30, 215 - 30, 256 + 30, 215 + 30], fill=255)
    img = over(img, flat(S, K), ring); d = ImageDraw.Draw(img)
    centred(d, (20, 390, 492, 490), "РАДИАЦИОННАЯ\nОПАСНОСТЬ", font(44), c(K))
    save("sign_radiation", weather(img, 201))
    img, d = plate(S, Wt, R, 2); centred(d, (20, 40, 492, 300), "БЛОК", font(150), c(R)); centred(d, (20, 250, 492, 480), "4", font(250), c(R))
    save("sign_block4", weather(img, 202, 0.7))
    L = (768, 256)
    for name, text, bg, fg, seed in (("sign_exit", "ВЫХОД →", G, Wt, 203), ("sign_reactor", "РЕАКТОРНЫЙ ЗАЛ  →", B, Wt, 204), ("sign_turbine", "←  МАШИННЫЙ ЗАЛ", B, Wt, 205),
                                     ("sign_control", "БЩУ-4", Wt, K, 206), ("sign_noentry", "ВХОД ВОСПРЕЩЁН", R, Wt, 207), ("sign_zone", "ЗОНА СТРОГОГО РЕЖИМА", Y, K, 208)):
        img, d = plate(L, bg, fg, seed); arrow = ("→" in text) - ("←" in text); text = text.replace("→", "").replace("←", "").strip()
        f = font(96 if len(text) < 9 else (66 if len(text) < 15 else 50))
        centred(d, (24 + (110 if arrow < 0 else 0), 20, 744 - (110 if arrow > 0 else 0), 236), text, f, c(fg))
        if arrow:                                                                 # (drawn: the font has no arrows)
            x = 660 if arrow > 0 else 108; s_ = arrow
            d.polygon([(x - 46 * s_, 108), (x + 6 * s_, 108), (x + 6 * s_, 84), (x + 50 * s_, 128), (x + 6 * s_, 172), (x + 6 * s_, 148), (x - 46 * s_, 148)], fill=c(fg))
        save(name, weather(img, seed, 0.6))
    img, d = plate(S, Y, K, 9)                                                    # high voltage: the bolt in a triangle
    d.polygon([(256, 50), (450, 360), (62, 360)], outline=c(K), width=22); d.polygon([(270, 120), (215, 240), (258, 240), (232, 330), (310, 205), (262, 205), (292, 120)], fill=c(K))
    centred(d, (20, 380, 492, 492), "ВЫСОКОЕ НАПРЯЖЕНИЕ\nОПАСНО ДЛЯ ЖИЗНИ", font(34), c(K))
    save("sign_voltage", weather(img, 209))
    img, d = plate(S, R, Wt, 10); centred(d, (20, 30, 492, 300), "ПК", font(230), c(Wt)); centred(d, (20, 300, 492, 490), "ПОЖАРНЫЙ КРАН\n№ 14", font(46), c(Wt))   # the fire point
    save("sign_fire", weather(img, 210, 0.8))
    img, d = plate((512, 768), (0.80, 0.78, 0.70), K, 11)                         # the dosimetry board: a ruled table, filled in by hand until the day
    centred(d, (20, 20, 492, 110), "ДОЗИМЕТРИЧЕСКИЙ\nКОНТРОЛЬ", font(40), c(K)); random.seed(11)
    for r in range(9):
        y = 130 + r * 66; d.line([(30, y), (482, y)], fill=c(K), width=3)
        if r < 8:
            for x in (30, 200, 340, 482): d.line([(x, y), (x, y + 66)], fill=c(K), width=3)
            if r == 0:
                for x, t in ((115, "ПОСТ"), (270, "мР/ч"), (411, "ВРЕМЯ")): centred(d, (x - 80, y, x + 80, y + 66), t, font(30), c(K))
            elif r < 6:
                for x, t in ((115, f"{r:02d}"), (270, random.choice(("0,02", "0,03", "0,02", "3,6", "ЗАШКАЛ"))), (411, f"0{random.randint(0, 1)}:{random.randint(10, 59)}")):
                    centred(d, (x - 80, y, x + 80, y + 66), t, font(30, bold=False), c((0.10, 0.14, 0.40)))
    save("sign_dosimetry", weather(img, 211, 0.4))


# ---- posters: paper, printed in two or three inks, pasted up for the Party and the plan --------
# Drawn here in the manner of the period's posters (flat colour, rays, silhouettes, block capitals);
# no existing poster is copied. The slogans are the ones that hung in such places.
def pfont(px):
    for path in ("/System/Library/Fonts/Supplemental/Impact.ttf", "/System/Library/Fonts/Supplemental/Arial Narrow Bold.ttf", "/System/Library/Fonts/Supplemental/Arial Bold.ttf"):
        try: return ImageFont.truetype(path, px)
        except Exception: continue
    return ImageFont.load_default()


PR, PK, PC, PG, PB = (0.74, 0.10, 0.07), (0.07, 0.06, 0.06), (0.90, 0.85, 0.70), (0.86, 0.66, 0.16), (0.14, 0.22, 0.42)   # red, black, paper, gold, blue
P = (512, 768)
c8 = lambda v: tuple(int(x * 255) for x in v)


def rays(d, c, n, r, fill, a0=0.0):
    for k in range(n):
        a = a0 + k * 2 * math.pi / n; b = a + math.pi / n
        d.polygon([c, (c[0] + r * math.cos(a), c[1] + r * math.sin(a)), (c[0] + r * math.cos(b), c[1] + r * math.sin(b))], fill=fill)


def star5(d, c, r, fill, rot=-90):
    pts = []
    for k in range(10):
        a = math.radians(rot + k * 36); rr = r if k % 2 == 0 else r * 0.40
        pts.append((c[0] + rr * math.cos(a), c[1] + rr * math.sin(a)))
    d.polygon(pts, fill=fill)


def hammer_sickle(d, c, s, fill, bg):
    """The crossed hammer and sickle, s = half its size."""
    x, y = c
    d.pieslice([x - s * 0.80, y - s * 0.85, x + s * 0.90, y + s * 0.85], -100, 150, fill=fill)                     # the sickle's blade: a disc...
    d.ellipse([x - s * 0.72, y - s * 0.80, x + s * 0.55, y + s * 0.55], fill=bg)                                      # ...less a smaller one
    d.polygon([(x - s * 0.62, y + s * 0.40), (x - s * 0.42, y + s * 0.24), (x - s * 0.72, y + s * 0.86), (x - s * 0.92, y + s * 0.70)], fill=fill)   # its grip
    d.line([(x + s * 0.70, y + s * 0.82), (x - s * 0.35, y - s * 0.38)], fill=fill, width=max(3, int(s * 0.15)))    # the hammer's shaft
    d.polygon([(x - s * 0.62, y - s * 0.38), (x - s * 0.28, y - s * 0.74), (x - s * 0.06, y - s * 0.52), (x - s * 0.40, y - s * 0.16)], fill=fill)   # its head


def profile(d, box, fill, beard=True):
    """A head in left profile, as on a medal: high bald brow, strong nose, a pointed beard."""
    x0, y0, x1, y1 = box; w, h = x1 - x0, y1 - y0
    pts = [(0.62, 0.02), (0.40, 0.03), (0.24, 0.12), (0.17, 0.26), (0.16, 0.36), (0.10, 0.44), (0.05, 0.53), (0.12, 0.56), (0.11, 0.62), (0.15, 0.65), (0.13, 0.70),
           (0.17, 0.76), (0.10, 0.90) if beard else (0.20, 0.82), (0.22, 0.86) if beard else (0.26, 0.84), (0.36, 0.82), (0.46, 0.80), (0.50, 0.90), (0.42, 1.00), (1.00, 1.00), (0.92, 0.86),
           (0.80, 0.78), (0.82, 0.62), (0.90, 0.46), (0.90, 0.28), (0.80, 0.10)]
    d.polygon([(x0 + px * w, y0 + py * h) for px, py in pts], fill=fill)


def paper(img, seed, burnt=0.0):
    """Printed paper that has been on a wall for years: yellowed, creased where it was folded, stained, a corner gone."""
    size = img.size; w, h = size
    img = shade(img, fbm(size, w / 2.5, seed), 0.74, 1.04)
    img = shade(img, grain(size, seed + 1, 18), 0.92, 1.05)
    d = ImageDraw.Draw(img, "RGBA")
    for y in (h // 3, 2 * h // 3): d.line([(0, y), (w, y + 2)], fill=(40, 30, 20, 70), width=2); d.line([(0, y + 3), (w, y + 5)], fill=(255, 250, 235, 40), width=1)
    d.line([(w // 2, 0), (w // 2 + 2, h)], fill=(40, 30, 20, 55), width=2)
    img = over(img, flat(size, (0.30, 0.20, 0.10)), streaks(size, seed + 2, 7, h, 7).point(lambda v: v * 4 // 10))
    stain = thresh(fbm(size, w / 5, seed + 3), 190, 30)
    img = over(img, flat(size, (0.36, 0.28, 0.16)), stain.point(lambda v: v * 5 // 10))
    random.seed(seed + 4); d = ImageDraw.Draw(img)
    for cx, cy in random.sample([(0, 0), (w, 0), (0, h), (w, h)], 2):                                               # torn corners: the wall's grime shows
        r = random.randint(40, 95); pts = [(cx, cy)] + [(cx + (1 if cx == 0 else -1) * r * math.cos(a) * random.uniform(0.75, 1.1), cy + (1 if cy == 0 else -1) * r * math.sin(a) * random.uniform(0.75, 1.1)) for a in [k * math.pi / 14 for k in range(8)]]
        d.polygon(pts, fill=(52, 50, 46))
    if burnt > 0:
        b = ImageChops.screen(fbm(size, w / 3, seed + 5), vgrad(size, [(0, 255), (0.5, 120), (1, 40)]))
        img = shade(img, ImageOps.invert(b), 1.0 - 0.85 * burnt, 1.0)
    return img


def poster(bg):
    img = flat(P, bg); return img, ImageDraw.Draw(img)


def slogan(d, box, text, px, fill, spacing=6):
    f = pfont(px)
    while px > 22:                                                                # shrink until it fits the box
        bb = d.multiline_textbbox((0, 0), text, font=f, spacing=spacing, align="center")
        if bb[2] - bb[0] <= box[2] - box[0] and bb[3] - bb[1] <= box[3] - box[1]: break
        px -= 4; f = pfont(px)
    centred(d, box, text, f, fill, spacing)


def posters():
    # 1. the peaceful atom: the slogan that stood over the town
    img, d = poster(PC)
    rays(d, (256, 300), 18, 700, c8((0.96, 0.78, 0.30)))
    d.rectangle([0, 520, 512, 768], fill=c8(PR))
    for x, w_, h_ in ((70, 90, 150), (180, 150, 210), (350, 100, 130)):          # the station: blocks, and its striped stack
        d.rectangle([x, 520 - h_, x + w_, 520], fill=c8(PK))
    d.rectangle([292, 150, 318, 310], fill=c8(PK))
    for k in range(5): d.rectangle([292, 150 + k * 32, 318, 166 + k * 32], fill=c8(PR))
    for a in (0, 60, 120):                                                        # the atom over it
        e = Image.new("L", P, 0); ed = ImageDraw.Draw(e); ed.ellipse([256 - 150, 215 - 52, 256 + 150, 215 + 52], outline=255, width=9)
        img.paste(Image.new("RGB", P, c8(PB)), (0, 0), e.rotate(a, center=(256, 215)))
    d = ImageDraw.Draw(img); d.ellipse([256 - 26, 215 - 26, 256 + 26, 215 + 26], fill=c8(PR))
    slogan(d, (24, 540, 488, 748), "МИРНЫЙ АТОМ —\nВ КАЖДЫЙ ДОМ!", 84, c8(PC))
    save("poster_atom", paper(img, 301))
    # 2. the leader in profile, in the rays
    img, d = poster(PR)
    rays(d, (256, 330), 24, 800, c8((0.82, 0.16, 0.10)))
    d.ellipse([256 - 205, 300 - 205, 256 + 205, 300 + 205], fill=c8(PG)); d.ellipse([256 - 188, 300 - 188, 256 + 188, 300 + 188], fill=c8(PC))
    profile(d, (128, 118, 400, 488), c8(PK))
    d.rectangle([0, 548, 512, 768], fill=c8(PK))
    slogan(d, (22, 558, 490, 754), "ЛЕНИН ЖИЛ,\nЛЕНИН ЖИВ,\nЛЕНИН БУДЕТ ЖИТЬ!", 62, c8(PC))
    save("poster_lenin", paper(img, 302))
    # 3. glory to the Party
    img, d = poster(PR)
    rays(d, (256, 290), 20, 800, c8((0.86, 0.22, 0.10)), 0.1)
    star5(d, (256, 120), 70, c8(PG)); hammer_sickle(d, (262, 330), 150, c8(PG), c8(PR))
    d.rectangle([0, 560, 512, 768], fill=c8(PC))
    slogan(d, (24, 572, 488, 752), "СЛАВА КПСС!", 150, c8(PR))
    save("poster_party", paper(img, 303))
    # 4. glory to labour: the arm and the hammer
    img, d = poster(PC)
    d.polygon([(0, 768), (0, 420), (512, 150), (512, 768)], fill=c8(PR))
    d.polygon([(150, 768), (230, 430), (300, 300), (372, 330), (330, 470), (330, 768)], fill=c8(PK))                 # the forearm
    d.polygon([(286, 318), (300, 230), (392, 250), (392, 340), (360, 352)], fill=c8(PK))                              # the fist
    d.line([(346, 300), (346, 40)], fill=c8(PK), width=26); d.rectangle([266, 30, 440, 100], fill=c8(PK))            # the hammer it holds
    for k in range(3): d.rectangle([40 + k * 62, 300 - k * 60, 84 + k * 62, 420], fill=c8(PK))                        # works on the skyline
    slogan(d, (30, 560, 482, 750), "СЛАВА\nТРУДУ!", 130, c8(PC))
    save("poster_labour", paper(img, 304))
    # 5. the plan
    img, d = poster(PC)
    d.rectangle([0, 0, 512, 250], fill=c8(PR)); slogan(d, (20, 16, 492, 236), "ПЯТИЛЕТКУ —\nВ ЧЕТЫРЕ ГОДА!", 84, c8(PC))
    for k, hgt in enumerate((90, 150, 215, 290, 380)):
        d.rectangle([52 + k * 86, 700 - hgt, 118 + k * 86, 700], fill=c8(PK) if k < 4 else c8(PR))
    d.line([(60, 590), (200, 520), (300, 450), (430, 300)], fill=c8(PR), width=16); d.polygon([(470, 250), (400, 290), (446, 336)], fill=c8(PR))
    d.rectangle([40, 700, 480, 714], fill=c8(PK)); slogan(d, (30, 716, 482, 764), "1986 — 1990", 44, c8(PK))
    save("poster_plan", paper(img, 305))
    # 6. vigilance
    img, d = poster(PK)
    d.polygon([(256, 60), (470, 440), (42, 440)], fill=c8(PR)); d.polygon([(256, 120), (420, 410), (92, 410)], fill=c8(PK))
    d.ellipse([146, 250, 366, 370], fill=c8(PC)); d.ellipse([222, 262, 318, 358], fill=c8(PB)); d.ellipse([250, 290, 290, 330], fill=c8(PK))   # the eye in it
    slogan(d, (24, 470, 488, 620), "БУДЬ БДИТЕЛЕН!", 96, c8(PC)); slogan(d, (24, 626, 488, 750), "ВРАГ НЕ ДРЕМЛЕТ", 64, c8(PR))
    save("poster_vigilance", paper(img, 306))
    # 7. the portrait every office had: a framed head and shoulders, the title under it
    img, d = poster((0.36, 0.06, 0.05))
    for k in range(9): d.rectangle([k * 58, 0, k * 58 + 26, 768], fill=c8((0.30, 0.04, 0.04)))                        # the drapes
    d.rectangle([70, 60, 442, 560], fill=c8(PG)); d.rectangle([90, 80, 422, 540], fill=c8((0.80, 0.76, 0.66)))
    d.ellipse([196, 130, 316, 290], fill=c8(PK)); d.rectangle([236, 270, 276, 320], fill=c8(PK))                      # head and neck
    d.polygon([(90, 540), (110, 400), (200, 320), (312, 320), (402, 400), (422, 540)], fill=c8(PK))                   # the suit
    d.polygon([(236, 320), (256, 400), (276, 320)], fill=c8((0.80, 0.76, 0.66))); d.polygon([(250, 330), (256, 420), (262, 330)], fill=c8(PR))   # shirt and tie
    star5(d, (330, 420), 12, c8(PG))
    slogan(d, (30, 580, 482, 660), "ГЕНЕРАЛЬНЫЙ СЕКРЕТАРЬ", 50, c8(PG)); slogan(d, (30, 664, 482, 750), "ЦК КПСС", 80, c8(PC))
    save("poster_portrait", paper(img, 307))
    # 8. the atom is a worker
    img, d = poster(PB)
    rays(d, (256, 300), 16, 800, c8((0.18, 0.28, 0.52)))
    for a in (0, 60, 120):
        e = Image.new("L", P, 0); ed = ImageDraw.Draw(e); ed.ellipse([256 - 190, 270 - 66, 256 + 190, 270 + 66], outline=255, width=11)
        img.paste(Image.new("RGB", P, c8(PC)), (0, 0), e.rotate(a, center=(256, 270)))
    d = ImageDraw.Draw(img)
    d.line([(190, 360), (322, 180)], fill=c8(PR), width=30); d.ellipse([292, 130, 372, 210], fill=c8(PR)); d.polygon([(332, 130), (372, 130), (352, 172)], fill=c8(PB))   # a spanner through it
    d.rectangle([0, 520, 512, 768], fill=c8(PC))
    slogan(d, (22, 532, 490, 752), "АТОМ — РАБОЧИЙ,\nА НЕ СОЛДАТ", 80, c8(PR))
    save("poster_worker_atom", paper(img, 308))
    # 9. glory to Soviet science
    img, d = poster(PK)
    random.seed(309)
    for _ in range(70): x, y = random.randint(0, 512), random.randint(0, 520); d.ellipse([x - 2, y - 2, x + 2, y + 2], fill=c8(PC))
    d.polygon([(256, 60), (300, 190), (300, 400), (212, 400), (212, 190)], fill=c8(PC)); d.polygon([(212, 330), (150, 430), (212, 400)], fill=c8(PR)); d.polygon([(300, 330), (362, 430), (300, 400)], fill=c8(PR))
    d.polygon([(226, 400), (256, 540), (286, 400)], fill=c8(PG)); d.ellipse([236, 220, 276, 260], fill=c8(PR)); star5(d, (256, 150), 16, c8(PR))
    d.rectangle([0, 560, 512, 768], fill=c8(PR))
    slogan(d, (22, 572, 490, 752), "СЛАВА СОВЕТСКОЙ\nНАУКЕ!", 84, c8(PC))
    save("poster_science", paper(img, 309))
    # 10. the May holiday: peace, labour, May
    img, d = poster(PC)
    for k, col in enumerate((PR, PR, (0.80, 0.14, 0.08))):                        # banners on their poles
        x = 110 + k * 130
        d.line([(x, 80 + k * 26), (x, 520)], fill=c8(PK), width=8); d.polygon([(x, 86 + k * 26), (x + 150, 110 + k * 26), (x + 136, 170 + k * 26), (x + 150, 230 + k * 26), (x, 250 + k * 26)], fill=c8(col))
    star5(d, (165, 160), 26, c8(PG))
    d.rectangle([0, 520, 512, 768], fill=c8(PR))
    slogan(d, (22, 530, 490, 750), "МИР  ТРУД\nМАЙ", 120, c8(PC))
    save("poster_may", paper(img, 310))
    # the same, where the fire reached them
    for n, seed in (("poster_party", 303), ("poster_lenin", 302), ("poster_atom", 301)):
        src = Image.open(os.path.join(OUT, n + ".png")).convert("RGB")
        save(n + "_burnt", paper(src, seed + 50, burnt=0.8))


# ---- control rooms: the desks and the wall of instruments behind them -------------------------
# Drawn after the look of an RBMK unit control room -- cream steel panels, rows of round gauges,
# strip-chart recorders, lamp fields, the circular map of the core's channels -- not copied from
# any one photograph.
PANEL = (0.70, 0.68, 0.58)             # the cream of the panels
STEELG = (0.36, 0.40, 0.36)            # the grey-green of the desks


def gauge(d, cx, cy, r, seed, lit=None):
    """A round panel meter: black bezel, white face, scale, needle wherever it stopped."""
    rnd = random.Random(seed)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(22, 22, 22)); d.ellipse([cx - r * 0.86, cy - r * 0.86, cx + r * 0.86, cy + r * 0.86], fill=(214, 208, 190))
    for k in range(11):
        a = math.radians(210 - k * 24); x0, y0 = cx + math.cos(a) * r * 0.62, cy - math.sin(a) * r * 0.62; x1, y1 = cx + math.cos(a) * r * 0.80, cy - math.sin(a) * r * 0.80
        d.line([(x0, y0), (x1, y1)], fill=(30, 30, 30) if k < 9 else (170, 30, 20), width=max(1, int(r / 14)))
    a = math.radians(210 - rnd.uniform(0, 240) * (1.0 if rnd.random() < 0.7 else 0.02))               # most read something; some have fallen to zero
    d.line([(cx, cy), (cx + math.cos(a) * r * 0.70, cy - math.sin(a) * r * 0.70)], fill=(160, 24, 18), width=max(2, int(r / 9)))
    d.ellipse([cx - r * 0.10, cy - r * 0.10, cx + r * 0.10, cy + r * 0.10], fill=(20, 20, 20))


def lamp_field(d, x0, y0, cols, rows, step, seed, on=0.12):
    rnd = random.Random(seed); r = step * 0.30
    for j in range(rows):
        for i in range(cols):
            x, y = x0 + i * step, y0 + j * step; k = rnd.random()
            col = (200, 44, 30) if k < on * 0.4 else ((216, 170, 40) if k < on * 0.7 else ((60, 170, 70) if k < on else (58, 56, 52)))
            if k < on: lit(d, "e", [x - r, y - r, x + r, y + r], col, outline=(18, 18, 18))
            else: d.ellipse([x - r, y - r, x + r, y + r], fill=col, outline=(18, 18, 18))


def recorder(d, x, y, w, h, seed):
    """A strip-chart recorder: a window of paper with the trace it was drawing."""
    rnd = random.Random(seed)
    d.rectangle([x, y, x + w, y + h], fill=(28, 28, 28)); d.rectangle([x + w * 0.10, y + h * 0.10, x + w * 0.90, y + h * 0.78], fill=(206, 200, 176))
    for k in range(1, 6): d.line([(x + w * 0.10 + k * w * 0.8 / 6, y + h * 0.10), (x + w * 0.10 + k * w * 0.8 / 6, y + h * 0.78)], fill=(150, 150, 140), width=1)
    px = x + w * rnd.uniform(0.25, 0.75); pts = []
    for k in range(24):
        px = min(x + w * 0.86, max(x + w * 0.14, px + rnd.uniform(-w * 0.05, w * 0.05) + (w * 0.02 * (k - 16) if k > 16 else 0)))    # steady, then running away
        pts.append((px, y + h * 0.12 + k * h * 0.64 / 23))
    d.line(pts, fill=(30, 50, 130), width=2)
    for k in range(3): d.rectangle([x + w * 0.14 + k * w * 0.26, y + h * 0.84, x + w * 0.30 + k * w * 0.26, y + h * 0.94], fill=(70, 70, 66))


def label(d, x, y, text, px=18, fill=(26, 26, 26), plate=True):
    f = font(px); bb = d.textbbox((0, 0), text, font=f); w, h = bb[2] - bb[0], bb[3] - bb[1]
    if plate: d.rectangle([x - w / 2 - 6, y - 3, x + w / 2 + 6, y + h + 7], fill=(20, 20, 20)); fill = (220, 214, 196)
    d.text((x - w / 2 - bb[0], y - bb[1] + 1), text, font=f, fill=fill)


def panel_base(size, seed):
    w, h = size; img = flat(size, PANEL); d = ImageDraw.Draw(img)
    for x in range(0, w + 1, w // 4): d.line([(x, 0), (x, h)], fill=(96, 92, 80), width=3)                # the panel is bolted up in bays
    d.rectangle([0, 0, w - 1, h - 1], outline=(70, 66, 58), width=6)
    return img, d


def panel_finish(img, seed, dirt=0.5):
    size = img.size; w, h = size
    img = shade(img, fbm(size, w / 3, seed), 0.74, 1.04)
    img = over(img, flat(size, (0.20, 0.17, 0.12)), streaks(size, seed + 1, 10, h, 6).point(lambda v: int(v * 0.30 * dirt)))
    img = over(img, flat(size, (0.10, 0.09, 0.08)), thresh(fbm(size, w / 6, seed + 2), 190, 40).point(lambda v: int(v * 0.45 * dirt)))
    return shade(img, grain(size, seed + 3, 12), 0.94, 1.04)


def mimic_reactor(size):
    """The centre of the wall: the cartogram -- every channel of the core as a lamp, in its circle."""
    w, h = size; img, d = panel_base(size, 501); cx, cy, R = w * 0.50, h * 0.53, h * 0.37
    label(d, cx, h * 0.035, "РЕАКТОР РБМК-1000  ·  КАРТОГРАММА АКТИВНОЙ ЗОНЫ", 22)
    d.ellipse([cx - R - 14, cy - R - 14, cx + R + 14, cy + R + 14], fill=(26, 26, 26)); d.ellipse([cx - R, cy - R, cx + R, cy + R], fill=(52, 54, 50))
    rnd = random.Random(502); n = 44; st = 2 * R / n
    for j in range(n):
        for i in range(n):
            x, y = cx - R + (i + 0.5) * st, cy - R + (j + 0.5) * st
            if (x - cx) ** 2 + (y - cy) ** 2 > (R - st) ** 2: continue
            k = rnd.random(); hot = math.hypot(x - cx - R * 0.25, y - cy + R * 0.2) < R * 0.45                # one side of the core is all alarm
            col = (214, 48, 30) if (hot and k < 0.55) or k < 0.04 else ((220, 176, 44) if k < 0.14 else ((70, 160, 80) if k < 0.34 else (86, 86, 80)))
            if col == (86, 86, 80): d.rectangle([x - st * 0.32, y - st * 0.32, x + st * 0.32, y + st * 0.32], fill=col)
            else: lit(d, "r", [x - st * 0.32, y - st * 0.32, x + st * 0.32, y + st * 0.32], col)
    for k in range(5): gauge(d, w * 0.085, h * (0.20 + k * 0.165), h * 0.068, 510 + k); gauge(d, w * 0.915, h * (0.20 + k * 0.165), h * 0.068, 520 + k)
    for k in range(3): recorder(d, w * (0.165 + 0.0) , h * (0.16 + k * 0.27), w * 0.075, h * 0.22, 530 + k); recorder(d, w * 0.76, h * (0.16 + k * 0.27), w * 0.075, h * 0.22, 540 + k)
    label(d, cx, h * 0.935, "МОЩНОСТЬ  ·  РЕАКТИВНОСТЬ  ·  РАСХОД", 18)
    return panel_finish(img, 505)


def mimic_turbine(size):
    """The steam and feedwater side: a line diagram with a meter at every stage."""
    w, h = size; img, d = panel_base(size, 551)
    label(d, w * 0.5, h * 0.035, "ТУРБОГЕНЕРАТОР № 8  ·  ПИТАТЕЛЬНАЯ ВОДА", 22)
    ys = (h * 0.30, h * 0.55, h * 0.78)
    for k, y in enumerate(ys):
        col = ((170, 40, 30), (40, 70, 150), (40, 120, 60))[k]
        d.line([(w * 0.06, y), (w * 0.94, y)], fill=col, width=9)
        for i in range(6):
            x = w * (0.12 + i * 0.152); d.line([(x, y), (x, y - h * 0.085)], fill=col, width=5); gauge(d, x, y - h * 0.13, h * 0.058, 560 + k * 10 + i)
            d.polygon([(x - 13, y - 9), (x + 13, y + 9), (x + 13, y - 9), (x - 13, y + 9)], fill=(30, 30, 30))    # a valve
    for i in range(4): d.rectangle([w * (0.10 + i * 0.22), h * 0.865, w * (0.24 + i * 0.22), h * 0.93], fill=(52, 54, 50)); lamp_field(d, w * (0.115 + i * 0.22), h * 0.885, 7, 2, w * 0.018, 580 + i, 0.3)
    return panel_finish(img, 555)


def mimic_elec(size):
    """Alarms and electrics: fields of annunciator windows, recorders in a row, a bank of lamps."""
    w, h = size; img, d = panel_base(size, 601); rnd = random.Random(602)
    label(d, w * 0.5, h * 0.035, "СИГНАЛИЗАЦИЯ  ·  СОБСТВЕННЫЕ НУЖДЫ 6 кВ", 22)
    for j in range(5):                                                         # annunciators: most dark, some lit amber or red
        for i in range(16):
            x, y = w * (0.045 + i * 0.058), h * (0.11 + j * 0.062); k = rnd.random()
            col = (206, 60, 36) if k < 0.10 else ((222, 178, 60) if k < 0.24 else (120, 116, 100))
            if k < 0.24: lit(d, "r", [x, y, x + w * 0.050, y + h * 0.050], col, outline=(24, 24, 24), width=2)
            else: d.rectangle([x, y, x + w * 0.050, y + h * 0.050], fill=col, outline=(24, 24, 24), width=2)
            d.line([(x + 6, y + h * 0.018), (x + w * 0.044, y + h * 0.018)], fill=(40, 38, 34), width=2); d.line([(x + 6, y + h * 0.034), (x + w * 0.034, y + h * 0.034)], fill=(40, 38, 34), width=2)
    for i in range(8): recorder(d, w * (0.045 + i * 0.117), h * 0.46, w * 0.095, h * 0.24, 610 + i)
    for i in range(10): gauge(d, w * (0.075 + i * 0.094), h * 0.82, h * 0.062, 630 + i)
    return panel_finish(img, 605)


def console_gauges(size):
    """The upstand at the back of a desk: small meters, key switches, a row of lamps."""
    w, h = size; img = flat(size, PANEL); d = ImageDraw.Draw(img); d.rectangle([0, 0, w - 1, h - 1], outline=(70, 66, 58), width=4)
    for i in range(14):
        x = w * (0.04 + i * 0.07)
        if i % 4 == 3: lamp_field(d, x - w * 0.012, h * 0.26, 3, 4, h * 0.17, 700 + i, 0.35)
        else: gauge(d, x, h * 0.50, h * 0.34, 700 + i)
    return panel_finish(img, 705, 0.35)


def console_top(size):
    """A desk top, to tile: push-buttons in blocks, rotary switches, the engraved strips under them."""
    w, h = size; img = flat(size, (0.60, 0.59, 0.52)); d = ImageDraw.Draw(img); rnd = random.Random(721)
    for by in range(2):
        for bx in range(2):
            x0, y0 = bx * w // 2, by * h // 2
            d.rectangle([x0 + 8, y0 + 8, x0 + w // 2 - 8, y0 + h // 2 - 8], outline=(80, 78, 70), width=3)
            if (bx + by) % 2 == 0:
                for j in range(4):
                    for i in range(6):
                        x, y = x0 + 34 + i * 34, y0 + 40 + j * 50; k = rnd.random()
                        col = (176, 40, 30) if k < 0.12 else ((40, 44, 48) if k < 0.6 else ((214, 206, 180) if k < 0.85 else (50, 130, 70)))
                        if k < 0.12 or k >= 0.85: lit(d, "r", [x - 11, y - 11, x + 11, y + 11], col, outline=(16, 16, 16), width=2)
                        else: d.rectangle([x - 11, y - 11, x + 11, y + 11], fill=col, outline=(16, 16, 16), width=2)
                        d.rectangle([x - 12, y + 16, x + 12, y + 22], fill=(26, 26, 26))
            else:
                for j in range(2):
                    for i in range(3):
                        x, y = x0 + 50 + i * 76, y0 + 62 + j * 110
                        d.ellipse([x - 26, y - 26, x + 26, y + 26], fill=(30, 30, 30)); a = rnd.uniform(0, 6.28)
                        d.line([(x, y), (x + math.cos(a) * 22, y + math.sin(a) * 22)], fill=(220, 214, 196), width=5); d.rectangle([x - 30, y + 32, x + 30, y + 42], fill=(26, 26, 26))
    return panel_finish(img, 725, 0.5)


def console_body(size):
    """The desk's carcass and the cabinets': grey-green stove enamel, a louvre, a kick-plate."""
    w, h = size; img = flat(size, STEELG); d = ImageDraw.Draw(img)
    for x in (0, w // 2): d.line([(x, 0), (x, h)], fill=(30, 34, 30), width=4); d.line([(x + 5, 0), (x + 5, h)], fill=(120, 130, 120), width=1)
    for k in range(7): d.rectangle([w * 0.12, h * (0.16 + k * 0.035), w * 0.38, h * (0.175 + k * 0.035)], fill=(22, 24, 22))        # louvres
    d.rectangle([0, int(h * 0.90), w, h], fill=(34, 34, 34))
    for x, y in ((0.06, 0.06), (0.44, 0.06), (0.56, 0.06), (0.94, 0.06), (0.06, 0.84), (0.94, 0.84)): d.ellipse([w * x - 5, h * y - 5, w * x + 5, h * y + 5], fill=(60, 64, 60), outline=(14, 14, 14))
    img = panel_finish(img, 741, 0.7)
    return over(img, shade(flat(size, (0.34, 0.17, 0.08)), fbm(size, w / 24, 742), 0.6, 1.1), thresh(fbm(size, w / 7, 743), 186, 16).point(lambda v: v * 7 // 10))


def cabinet_face(size):
    """A relay cabinet's door: meters at eye level, switches under them, its number plate."""
    w, h = size; img = flat(size, PANEL); d = ImageDraw.Draw(img); d.rectangle([0, 0, w - 1, h - 1], outline=(60, 58, 50), width=6); d.rectangle([14, 14, w - 15, h - 15], outline=(110, 106, 94), width=2)
    label(d, w / 2, h * 0.045, "ЩИТ № 4-12", 20)
    for j in range(2):
        for i in range(2): gauge(d, w * (0.29 + i * 0.42), h * (0.17 + j * 0.13), w * 0.15, 760 + j * 2 + i)
    lamp_field(d, w * 0.16, h * 0.42, 6, 4, w * 0.135, 765, 0.3)
    for j in range(3):
        for i in range(3):
            x, y = w * (0.24 + i * 0.26), h * (0.62 + j * 0.075)
            d.ellipse([x - 16, y - 16, x + 16, y + 16], fill=(30, 30, 30)); d.line([(x, y), (x + 10, y - 12)], fill=(220, 214, 196), width=4)
    d.rectangle([w * 0.80, h * 0.50, w * 0.86, h * 0.60], fill=(40, 40, 40))                                # the handle
    return panel_finish(img, 768, 0.6)


def lamps_glow(size):
    """The lamps that are still lit: drawn full-bright in the game, so they are the only light on a dark wall."""
    w, h = size; img = flat(size, (0.02, 0.02, 0.02)); d = ImageDraw.Draw(img); rnd = random.Random(781); n = 16
    for i in range(n):
        if rnd.random() < 0.35: continue
        x = (i + 0.5) * w / n; col = rnd.choice(((255, 60, 40), (255, 60, 40), (255, 190, 50), (70, 230, 100)))
        lit(d, "e", [x - h * 0.26, h * 0.24, x + h * 0.26, h * 0.76], col)
    return img


def mimic_pumps(size):
    """Main circulation pumps: eight of them on two loops, each with its motor current and its flow."""
    w, h = size; img, d = panel_base(size, 801); label(d, w * 0.5, h * 0.035, "ГЛАВНЫЕ ЦИРКУЛЯЦИОННЫЕ НАСОСЫ  ·  ГЦН 1-8", 22)
    for row in range(2):
        y = h * (0.34 + row * 0.38); col = (40, 70, 150) if row == 0 else (170, 40, 30)
        d.line([(w * 0.05, y + h * 0.10), (w * 0.95, y + h * 0.10)], fill=col, width=10); d.line([(w * 0.05, y - h * 0.14), (w * 0.95, y - h * 0.14)], fill=col, width=6)
        for i in range(4):
            x = w * (0.14 + i * 0.24); d.line([(x, y - h * 0.14), (x, y + h * 0.10)], fill=col, width=6)
            d.ellipse([x - h * 0.075, y - h * 0.075, x + h * 0.075, y + h * 0.075], fill=(30, 30, 30)); d.ellipse([x - h * 0.060, y - h * 0.060, x + h * 0.060, y + h * 0.060], fill=(200, 196, 180))
            run = (row * 4 + i) not in (2, 5, 6)                                   # three have tripped
            lit(d, "p", [(x - h * 0.03, y + h * 0.035), (x + h * 0.04, y), (x - h * 0.03, y - h * 0.035)], (40, 150, 70) if run else (200, 44, 30), layer=0 if run else 4)
            gauge(d, x - w * 0.062, y - h * 0.02, h * 0.050, 810 + row * 4 + i); gauge(d, x + w * 0.062, y - h * 0.02, h * 0.050, 820 + row * 4 + i)
            label(d, x, y + h * 0.125, f"ГЦН-{row * 4 + i + 1}", 16)
    return panel_finish(img, 805)


def mimic_rods(size):
    """Control and protection: a wall of small round indicators, one to a rod -- how far in each one is."""
    w, h = size; img, d = panel_base(size, 841); label(d, w * 0.5, h * 0.035, "СУЗ  ·  ПОЛОЖЕНИЕ СТЕРЖНЕЙ", 22); rnd = random.Random(842)
    cols, rows = 22, 10; st = w * 0.040
    for j in range(rows):
        for i in range(cols):
            if (i in (0, cols - 1) and j in (0, rows - 1)): continue
            x, y = w * 0.08 + i * st, h * 0.13 + j * st * 1.02; r = st * 0.40
            d.ellipse([x - r, y - r, x + r, y + r], fill=(24, 24, 24)); d.ellipse([x - r * 0.82, y - r * 0.82, x + r * 0.82, y + r * 0.82], fill=(206, 200, 182))
            a = math.radians(90 + (rnd.uniform(-150, 150) if rnd.random() < 0.8 else 150))      # most withdrawn to somewhere; a fifth pegged
            d.line([(x, y), (x + math.cos(a) * r * 0.72, y - math.sin(a) * r * 0.72)], fill=(150, 26, 20), width=3)
    lit(d, "r", [w * 0.42, h * 0.845, w * 0.58, h * 0.95], (170, 30, 22), layer=2, outline=(20, 20, 20), width=4); label(d, w * 0.5, h * 0.872, "АЗ-5", 30, fill=(240, 236, 220), plate=False)
    for i in range(3): gauge(d, w * (0.12 + i * 0.10), h * 0.895, h * 0.050, 850 + i); gauge(d, w * (0.68 + i * 0.10), h * 0.895, h * 0.050, 860 + i)
    return panel_finish(img, 845)


def mimic_grid(size):
    """The electrical side as a one-line diagram: busbars, breakers shown open or closed, the generators."""
    w, h = size; img, d = panel_base(size, 881); label(d, w * 0.5, h * 0.035, "ГЛАВНАЯ ЭЛЕКТРИЧЕСКАЯ СХЕМА  ·  750 / 330 кВ", 22); rnd = random.Random(882)
    for k, y in enumerate((h * 0.20, h * 0.27)): d.line([(w * 0.05, y), (w * 0.95, y)], fill=((170, 40, 30), (200, 150, 30))[k], width=8)
    for i in range(8):
        x = w * (0.10 + i * 0.114); closed = rnd.random() < 0.55
        d.line([(x, h * 0.20), (x, h * 0.72)], fill=(40, 40, 40), width=5)
        for y in (h * 0.36, h * 0.52): lit(d, "r", [x - 13, y - 13, x + 13, y + 13], (200, 44, 30) if closed else (50, 150, 70), outline=(16, 16, 16), width=3)
        if i % 2 == 0:
            d.ellipse([x - 26, h * 0.72 - 26, x + 26, h * 0.72 + 26], outline=(30, 30, 30), width=5); d.ellipse([x - 26, h * 0.72 + 6, x + 26, h * 0.72 + 58], outline=(30, 30, 30), width=5)   # a transformer
        else:
            d.ellipse([x - 34, h * 0.72, x + 34, h * 0.72 + 68], fill=(206, 200, 182), outline=(30, 30, 30), width=5); label(d, x, h * 0.755, "Г", 26, plate=False)                 # a generator
        gauge(d, x, h * 0.915, h * 0.048, 890 + i)
    return panel_finish(img, 885)


def mimic_dosimetry(size):
    """Radiation monitoring: the unit in plan, a lamp in every room, and the big meters that went off the scale."""
    w, h = size; img, d = panel_base(size, 921); label(d, w * 0.5, h * 0.035, "ДОЗИМЕТРИЧЕСКИЙ КОНТРОЛЬ  ·  БЛОК 4", 22); rnd = random.Random(922)
    x0, y0, x1, y1 = w * 0.05, h * 0.13, w * 0.62, h * 0.93; d.rectangle([x0, y0, x1, y1], fill=(60, 64, 60), outline=(20, 20, 20), width=4)
    for i in range(1, 6): d.line([(x0 + i * (x1 - x0) / 6, y0), (x0 + i * (x1 - x0) / 6, y1)], fill=(170, 168, 150), width=3)
    for j in range(1, 4): d.line([(x0, y0 + j * (y1 - y0) / 4), (x1, y0 + j * (y1 - y0) / 4)], fill=(170, 168, 150), width=3)
    for j in range(4):
        for i in range(6):
            x, y = x0 + (i + 0.5) * (x1 - x0) / 6, y0 + (j + 0.5) * (y1 - y0) / 4; hot = i >= 3
            col = (220, 50, 32) if hot and rnd.random() < 0.8 else ((220, 176, 44) if rnd.random() < 0.4 else (70, 160, 80))
            lit(d, "e", [x - 15, y - 15, x + 15, y + 15], col, outline=(16, 16, 16), width=3)
    for k in range(3):
        cy = h * (0.25 + k * 0.25); gauge(d, w * 0.74, cy, h * 0.10, 930 + k); gauge(d, w * 0.89, cy, h * 0.10, 940 + k)
        label(d, w * 0.815, cy + h * 0.105, ("мР/ч", "Р/ч", "ЗАШКАЛ")[k], 16)
    return panel_finish(img, 925)


def mimic_drum(size):
    """Drum separators: four drums drawn end-on to their level gauges, steam pressure over each."""
    w, h = size; img, d = panel_base(size, 961); label(d, w * 0.5, h * 0.035, "БАРАБАНЫ-СЕПАРАТОРЫ  ·  УРОВЕНЬ И ДАВЛЕНИЕ", 22); rnd = random.Random(962)
    for i in range(4):
        x = w * (0.14 + i * 0.24); y = h * 0.50
        d.rounded_rectangle([x - w * 0.095, y - h * 0.11, x + w * 0.095, y + h * 0.11], int(h * 0.11), fill=(96, 100, 96), outline=(24, 24, 24), width=5)
        lv = rnd.uniform(0.15, 0.85); d.rectangle([x - w * 0.085, y + h * 0.10 - lv * h * 0.19, x + w * 0.085, y + h * 0.10], fill=(50, 90, 160))
        d.rectangle([x + w * 0.105, y - h * 0.13, x + w * 0.118, y + h * 0.13], fill=(206, 200, 182), outline=(20, 20, 20), width=2)                    # its sight glass
        d.rectangle([x + w * 0.105, y + h * 0.13 - lv * h * 0.26, x + w * 0.118, y + h * 0.13], fill=(170, 40, 30))
        gauge(d, x - w * 0.05, h * 0.22, h * 0.070, 970 + i); gauge(d, x + w * 0.05, h * 0.22, h * 0.070, 980 + i)
        d.line([(x, y + h * 0.11), (x, h * 0.80)], fill=(40, 70, 150), width=8); recorder(d, x - w * 0.045, h * 0.74, w * 0.09, h * 0.20, 990 + i)
        label(d, x, h * 0.345, f"БС-{i + 1}", 16)
    return panel_finish(img, 965)


def cabinet_breaker(size):
    """A switchgear cubicle: its place on the bus drawn on the door, the big handle, the flags."""
    w, h = size; img = flat(size, STEELG); d = ImageDraw.Draw(img); d.rectangle([0, 0, w - 1, h - 1], outline=(24, 28, 24), width=6)
    label(d, w / 2, h * 0.04, "ЯЧЕЙКА 6 кВ", 20)
    d.line([(w * 0.5, h * 0.12), (w * 0.5, h * 0.40)], fill=(200, 150, 30), width=8); d.line([(w * 0.15, h * 0.12), (w * 0.85, h * 0.12)], fill=(200, 150, 30), width=8)
    d.rectangle([w * 0.38, h * 0.22, w * 0.62, h * 0.31], fill=(200, 44, 30), outline=(16, 16, 16), width=3)
    gauge(d, w * 0.5, h * 0.50, w * 0.22, 1001)
    d.rectangle([w * 0.42, h * 0.66, w * 0.58, h * 0.86], fill=(22, 22, 22)); d.ellipse([w * 0.40, h * 0.62, w * 0.60, h * 0.70], fill=(170, 30, 22))        # the handle
    for k, col in enumerate(((200, 44, 30), (60, 160, 80), (216, 170, 40))): lit(d, "e", [w * (0.22 + k * 0.28) - 12, h * 0.93 - 12, w * (0.22 + k * 0.28) + 12, h * 0.93 + 12], col, outline=(16, 16, 16))
    img = panel_finish(img, 1003, 0.7)
    return over(img, shade(flat(size, (0.34, 0.17, 0.08)), fbm(size, w / 12, 1004), 0.6, 1.1), thresh(fbm(size, w / 5, 1005), 190, 16).point(lambda v: v * 6 // 10))


def cabinet_recorders(size):
    w, h = size; img = flat(size, PANEL); d = ImageDraw.Draw(img); d.rectangle([0, 0, w - 1, h - 1], outline=(60, 58, 50), width=6); label(d, w / 2, h * 0.04, "САМОПИСЦЫ", 20)
    for j in range(3):
        for i in range(2): recorder(d, w * (0.10 + i * 0.42), h * (0.11 + j * 0.24), w * 0.38, h * 0.21, 1010 + j * 2 + i)
    lamp_field(d, w * 0.14, h * 0.86, 6, 2, w * 0.14, 1016, 0.3)
    return panel_finish(img, 1018, 0.6)


def rack_tape(size):
    """A bay of the unit's computer: two tape reels behind a window, the transport's keys under them."""
    w, h = size; img = flat(size, (0.52, 0.56, 0.58)); d = ImageDraw.Draw(img); d.rectangle([0, 0, w - 1, h - 1], outline=(30, 32, 34), width=6); label(d, w / 2, h * 0.035, "СКАЛА  ·  НМЛ", 18)
    d.rectangle([w * 0.10, h * 0.10, w * 0.90, h * 0.56], fill=(18, 20, 22))
    for cy in (h * 0.21, h * 0.43):
        r = w * 0.30; d.ellipse([w * 0.5 - r, cy - r * 0.62, w * 0.5 + r, cy + r * 0.62], fill=(70, 50, 34)); d.ellipse([w * 0.5 - r * 0.45, cy - r * 0.28, w * 0.5 + r * 0.45, cy + r * 0.28], fill=(200, 200, 196))
        for k in range(3):
            a = k * 2.094 + cy; d.ellipse([w * 0.5 + math.cos(a) * r * 0.22 - 7, cy + math.sin(a) * r * 0.14 - 5, w * 0.5 + math.cos(a) * r * 0.22 + 7, cy + math.sin(a) * r * 0.14 + 5], fill=(30, 30, 30))
    d.line([(w * 0.26, h * 0.21), (w * 0.26, h * 0.43)], fill=(120, 90, 60), width=3)
    for i in range(5):
        c_ = ((190, 40, 30), (60, 60, 60), (60, 60, 60), (60, 150, 70), (210, 170, 40))[i]; b_ = [w * (0.14 + i * 0.15), h * 0.60, w * (0.26 + i * 0.15), h * 0.65]
        if c_[0] == c_[1]: d.rectangle(b_, fill=c_, outline=(14, 14, 14), width=2)
        else: lit(d, "r", b_, c_, outline=(14, 14, 14), width=2)
    lamp_field(d, w * 0.16, h * 0.72, 6, 5, w * 0.135, 1021, 0.45)
    return panel_finish(img, 1023, 0.5)


def rack_lamps(size):
    w, h = size; img = flat(size, (0.52, 0.56, 0.58)); d = ImageDraw.Draw(img); d.rectangle([0, 0, w - 1, h - 1], outline=(30, 32, 34), width=6); label(d, w / 2, h * 0.035, "СКАЛА  ·  АЛУ", 18)
    d.rectangle([w * 0.08, h * 0.09, w * 0.92, h * 0.62], fill=(20, 22, 24)); lamp_field(d, w * 0.14, h * 0.12, 9, 21, w * 0.090, 1031, 0.5)
    for j in range(3):
        for i in range(8): d.rectangle([w * (0.10 + i * 0.10), h * (0.67 + j * 0.05), w * (0.17 + i * 0.10), h * (0.70 + j * 0.05)], fill=(30, 30, 30))                # toggles
    for i in range(2): gauge(d, w * (0.30 + i * 0.40), h * 0.90, w * 0.14, 1035 + i)
    return panel_finish(img, 1037, 0.5)


def console_top_az(size):
    """The reactor operator's place, to tile: the guarded scram button, key switches, rod-selector buttons."""
    w, h = size; img = flat(size, (0.60, 0.59, 0.52)); d = ImageDraw.Draw(img); rnd = random.Random(1041)
    d.rectangle([8, 8, w - 8, h - 8], outline=(80, 78, 70), width=3)
    d.rectangle([w * 0.06, h * 0.08, w * 0.34, h * 0.42], fill=(30, 30, 30)); lit(d, "e", [w * 0.11, h * 0.14, w * 0.29, h * 0.36], (190, 30, 22), layer=3, outline=(240, 220, 60), width=5)
    label(d, w * 0.20, h * 0.44, "АЗ-5", 24)
    for i in range(3): x = w * (0.46 + i * 0.17); d.ellipse([x - 24, h * 0.16, x + 24, h * 0.16 + 48], fill=(30, 30, 30)); d.rectangle([x - 4, h * 0.16 + 6, x + 4, h * 0.16 + 30], fill=(200, 196, 180)); label(d, x, h * 0.30, ("БР", "АР", "ЛАР")[i], 16)
    for j in range(6):
        for i in range(12):
            x, y = w * (0.08 + i * 0.076), h * (0.56 + j * 0.068); k = rnd.random()
            if k < 0.7: d.rectangle([x - 12, y - 12, x + 12, y + 12], fill=(206, 200, 182), outline=(16, 16, 16), width=2)
            else: lit(d, "r", [x - 12, y - 12, x + 12, y + 12], (200, 44, 30) if k < 0.85 else (60, 150, 70), outline=(16, 16, 16), width=2)
    return panel_finish(img, 1045, 0.5)


def console_top_comm(size):
    """A shift supervisor's desk, to tile: a telephone concentrator, a microphone grille, sliders."""
    w, h = size; img = flat(size, (0.60, 0.59, 0.52)); d = ImageDraw.Draw(img); rnd = random.Random(1051); d.rectangle([8, 8, w - 8, h - 8], outline=(80, 78, 70), width=3)
    for j in range(5):
        for i in range(8):
            x, y = w * (0.07 + i * 0.058), h * (0.10 + j * 0.075)
            d.rectangle([x - 11, y - 9, x + 11, y + 9], fill=(40, 42, 44), outline=(14, 14, 14)); (lit(d, "e", [x - 4, y - 16, x + 4, y - 9], (216, 170, 40)) if rnd.random() < 0.2 else d.ellipse([x - 4, y - 16, x + 4, y - 9], fill=(70, 68, 60)))
    d.ellipse([w * 0.62, h * 0.08, w * 0.90, h * 0.36], fill=(30, 30, 30))
    for k in range(8): d.line([(w * 0.64, h * (0.12 + k * 0.03)), (w * 0.88, h * (0.12 + k * 0.03))], fill=(80, 80, 80), width=3)
    for i in range(7):
        x = w * (0.10 + i * 0.125); d.rectangle([x - 5, h * 0.56, x + 5, h * 0.90], fill=(24, 24, 24)); y = h * rnd.uniform(0.58, 0.86); d.rectangle([x - 18, y - 10, x + 18, y + 10], fill=(200, 196, 180), outline=(16, 16, 16), width=2)
    return panel_finish(img, 1055, 0.5)


def nameplate(text, seed):
    img = flat((512, 96), (0.10, 0.10, 0.10)); d = ImageDraw.Draw(img); d.rectangle([0, 0, 511, 95], outline=(200, 190, 150), width=4)
    f = font(46 if len(text) < 14 else (36 if len(text) < 20 else 28)); bb = d.textbbox((0, 0), text, font=f)
    d.text((256 - (bb[2] - bb[0]) / 2 - bb[0], 48 - (bb[3] - bb[1]) / 2 - bb[1]), text, font=f, fill=(226, 216, 180))
    return shade(img, fbm((512, 96), 120, seed), 0.75, 1.05)


def paperwork(size):
    """The shift log, open where the last entry stops."""
    w, h = size; img = flat(size, (0.86, 0.84, 0.74)); d = ImageDraw.Draw(img); rnd = random.Random(1071); d.line([(w // 2, 0), (w // 2, h)], fill=(120, 110, 90), width=3)
    for side in range(2):
        for j in range(13):
            y = 22 + j * 18; x0 = 14 + side * w // 2
            d.line([(x0, y), (x0 + w // 2 - 28, y)], fill=(150, 160, 190), width=1)
            if side == 0 or j < 5:
                x = x0 + 4
                while x < x0 + w // 2 - 40 - rnd.randint(0, 50): L = rnd.randint(6, 26); d.line([(x, y - 3), (x + L, y - 3 + rnd.randint(-2, 2))], fill=(30, 40, 110), width=2); x += L + 6
    return shade(img, fbm(size, w / 2, 1073), 0.8, 1.02)


NAMES = ["БЩУ-4", "РЩУ", "ЩИТ ГЦН", "ПОСТ ДОЗИМЕТРИИ", "СКАЛА", "ЩИТ ТУРБИНЫ № 8", "СОБСТВЕННЫЕ НУЖДЫ", "БАРАБАН-СЕПАРАТОР", "СУЗ", "НАСОСНАЯ", "ДЕАЭРАТОРНАЯ", "ЩИТ КИП", "РЕЛЕЙНЫЙ ЗАЛ", "ОРУ-750"]


def fittings():
    """The small things on every wall: a fire-hose cabinet, a fuse board, canvas hose, brass couplings."""
    S = (256, 256); img = flat(S, (0.60, 0.09, 0.06)); d = ImageDraw.Draw(img)
    d.rectangle([0, 0, 255, 255], outline=(40, 8, 6), width=8); d.rectangle([34, 60, 222, 226], fill=(26, 30, 32), outline=(200, 190, 160), width=4)          # the window
    for r in range(78, 10, -11): d.ellipse([128 - r, 143 - r * 0.86, 128 + r, 143 + r * 0.86], outline=(170, 160, 130), width=6)                             # the hose, coiled
    d.ellipse([112, 127, 144, 159], fill=(190, 150, 60)); label(d, 128, 14, "ПК-7", 26, fill=(240, 232, 210), plate=False)
    save("hose_cabinet", panel_finish(img, 1201, 0.8))
    B = (256, 340); img = flat(B, (0.42, 0.44, 0.42)); d = ImageDraw.Draw(img); d.rectangle([0, 0, 255, 339], outline=(26, 28, 26), width=6); rnd = random.Random(1211)
    label(d, 128, 12, "ЩО-3  ·  220 В", 18)
    for j in range(3):
        for i in range(5):
            x, y = 34 + i * 47, 70 + j * 62
            d.rectangle([x - 14, y - 22, x + 14, y + 22], fill=(206, 200, 182), outline=(20, 20, 20), width=2); d.ellipse([x - 8, y - 8, x + 8, y + 8], fill=(60, 50, 40) if rnd.random() < 0.8 else (30, 30, 30))   # a fuse; some blown
    for i in range(3):
        x = 60 + i * 68; d.rectangle([x - 20, 250, x + 20, 320], fill=(30, 30, 30)); d.line([(x, 312), (x + rnd.choice((-16, 14)), 262)], fill=(200, 150, 60), width=7)                                  # knife switches
    save("fuse_board", panel_finish(img, 1213, 0.8))
    H = (128, 128); img = flat(H, (0.50, 0.46, 0.36)); d = ImageDraw.Draw(img)
    for k in range(-128, 128, 9): d.line([(k, 0), (k + 128, 128)], fill=(118, 108, 84), width=2); d.line([(k + 128, 0), (k, 128)], fill=(140, 130, 104), width=1)     # the weave
    save("hose", shade(img, fbm(H, 40, 1221), 0.45, 1.05))
    save("brass", shade(flat((64, 64), (0.62, 0.46, 0.18)), fbm((64, 64), 20, 1231), 0.6, 1.15))


def control_room():
    M = (1024, 640)
    save("mimic_reactor", mimic_reactor(M)); save("mimic_turbine", mimic_turbine(M)); save("mimic_elec", mimic_elec(M))
    save("console_gauges", console_gauges((1024, 128))); save("console_top", console_top((512, 512))); save("console_body", console_body((512, 512)))
    save("cabinet_face", cabinet_face((256, 640))); save("lamps_glow", lamps_glow((512, 40)), colors=0)
    for n_, f_ in (("mimic_pumps", mimic_pumps), ("mimic_rods", mimic_rods), ("mimic_grid", mimic_grid), ("mimic_dosimetry", mimic_dosimetry), ("mimic_drum", mimic_drum)): save(n_, f_(M))
    save("cabinet_breaker", cabinet_breaker((256, 640))); save("cabinet_recorders", cabinet_recorders((256, 640)))
    save("rack_tape", rack_tape((256, 640))); save("rack_lamps", rack_lamps((256, 640)))
    save("console_top_az", console_top_az((512, 512))); save("console_top_comm", console_top_comm((512, 512)))
    for k_, t_ in enumerate(NAMES): save(f"plate_{k_:02d}", nameplate(t_, 1100 + k_))
    save("paperwork", paperwork((256, 256))); save("valve_red", shade(flat((128, 128), (0.55, 0.08, 0.06)), fbm((128, 128), 40, 1081), 0.5, 1.1))


def main():
    print("maps/textures/chernobyl:")
    W = (1024, 1024)
    save("wall_concrete", wall_bare(W))
    save("wall_green", wall_paint(W, 21, (0.30, 0.42, 0.33), (0.74, 0.71, 0.60)))
    save("wall_blue", wall_paint(W, 31, (0.28, 0.37, 0.46), (0.76, 0.74, 0.66), tray=True))
    save("wall_scorched", wall_scorched(W))
    F = (1024, 1024)
    save("floor_plate", floor_plate(F)); save("floor_tile", floor_tile(F)); save("floor_ash", floor_ash(F))
    save("ceil_slab", ceiling(F)); save("ceil_scorched", ceiling(F, True))
    save("glow_core", glow_core((1024, 1024))); save("elena", elena((1024, 1024))); save("steel_rust", steel_rust((256, 256))); save("hazard_edge", hazard((512, 512)))
    save("lamp_sodium", lamp((256, 256), True, (1.0, 0.78, 0.45))); save("lamp_tube", lamp((256, 256), True, (0.82, 0.92, 1.0)))
    save("lamp_red", lamp((256, 256), True, (1.0, 0.16, 0.10))); save("fixture_dead", lamp((256, 256), False))
    save("lamp_fail", lamp((256, 256), True, (0.82, 0.92, 1.0), fail=True))
    signs(); posters(); control_room(); fittings()


if __name__ == "__main__":
    main()
