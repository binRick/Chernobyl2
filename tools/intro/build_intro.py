#!/usr/bin/env python3
"""Build the Chernobyl2 title intro: a beat-cut archival montage.

The Soviet anthem plays over parades and Pripyat life, then tape-stops into static
when the reactor is revealed. Hell March builds over the liquidators and kicks in for a
fast-cut disaster section. CHERNOBYL slams in on a downbeat.

Every source is graded into one gritty black-and-white film, with flashes, shake,
RGB-split glitches and Geiger crackle. Subtitles and the title are drawn with PIL
(this ffmpeg has no drawtext).

    tools/intro/fetch_sources.sh          # once: archive.org footage + music -> build/intro_src/
    build/introvenv/bin/python tools/intro/build_intro.py [--preview]

Output: build/intro/intro.mp4 (H.264 preview, 1280x720/25).
"""
import os, sys, subprocess, random, math, shutil
import numpy as np
import librosa, soundfile as sf
from PIL import Image, ImageDraw, ImageFont, ImageFilter

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC = os.path.join(REPO, "build", "intro_src")
OUT = os.path.join(REPO, "build", "intro")
W, H, FPS = 1280, 720, 25
SR = 44100
random.seed(1986)

# ------------------------------------------------------------------ sources
# per-source crop (x, y, w, h as fractions) -- trims pillarboxes, watermarks and the
# burned-in timecodes / cut-off subtitles before the frame is scaled to fill 16:9.
CROP = {
    "mayday1958.mp4":        (0.00, 0.00, 1.00, 1.00),
    "parade1990.mp4":        (0.13, 0.04, 0.74, 0.92),
    "newsreel1961.mp4":      (0.12, 0.04, 0.76, 0.76),   # PF# timecode at the bottom
    "pripyat_may86.mp4":     (0.16, 0.12, 0.66, 0.74),   # channel bug top-right, PRIPYAT.com bottom
    "pripyat_film_0426.mp4": (0.04, 0.15, 0.80, 0.81),   # pripyat.com bug top-right
    "aerial.mp4":            (0.00, 0.00, 1.00, 1.00),
    "pd_compilation.ogv":    (0.03, 0.03, 0.94, 0.94),
    "newsreel_hd.mp4":       (0.13, 0.00, 0.72, 0.88),   # pillarbox + AM watermark bottom-right
    "radiological.mp4":      (0.14, 0.03, 0.72, 0.78),   # PF# timecode at the bottom
    "doc_subs.mp4":          (0.00, 0.00, 1.00, 0.72),   # cut off ABOVE the clipped burned-in subtitles
    "montage.mp4":           (0.00, 0.00, 1.00, 1.00),   # Chernobyl 1's montage frames
    "soldiers_dance.mp4":    (0.00, 0.20, 1.00, 0.767),  # "Soldier's dance", the Alexandrov Ensemble, 1965. 4:3 -> 16:9 from lower down: the boots stay in, spare sky goes
    "women.mp4":             (0.17, 0.08, 0.66, 0.70),   # Soviet disco home video: pillarboxed 4:3
    "disaster2.mp4":         (0.00, 0.00, 1.00, 1.00),   # "Chernobyl. Helicopter crashes.": over the vent stack, then the Mi-8 and the crane
}

# ------------------------------------------------------------------ music timing
ANTHEM_IN = 1.0          # brass intro starts
ANTHEM_LEN = 22.3        # anthem section length: the good times (the first cut was 14 s; the soldiers' dance and the disco were added, nothing taken out)
GLITCH_LEN = 3.0         # the disaster transition
STALL = 1.2              # last good shot stalls while the anthem tape-stops; then the blast
HM_KICK_SRC = 32.30      # Hell March: full band slams in here (measured)
BUILD_BEATS = 36         # Hell March build-up before the kick
KICK_BEATS = 48          # fast-cut section, then the title slam
TAIL = 6.0               # title hold after the slam
STAMP_BEATS = 4          # the hammer & sickle stamps down this many beats after the title


def hm_period():
    y, sr = librosa.load(os.path.join(SRC, "hell-march.mp3"), sr=22050, duration=90)
    _, beats = librosa.beat.beat_track(y=y, sr=sr)
    bt = librosa.frames_to_time(beats, sr=sr)
    return float(np.polyfit(np.arange(len(bt)), bt, 1)[0])


P = hm_period()
T_BUILD = ANTHEM_LEN + GLITCH_LEN
HM_START_SRC = HM_KICK_SRC - BUILD_BEATS * P
T_KICK = T_BUILD + BUILD_BEATS * P
T_TITLE = T_KICK + KICK_BEATS * P
T_STAMP = T_TITLE + STAMP_BEATS * P
DATE_HOLD = T_BUILD + 3.6          # the disaster card holds to here, then fades (2 x 0.16 s)
DATE_END = DATE_HOLD + 0.32
T_END = T_TITLE + TAIL
print(f"beat {P:.4f}s  kick T={T_KICK:.2f}  title T={T_TITLE:.2f}  end T={T_END:.2f}")

# ------------------------------------------------------------------ edit decision list
# (source, src_in seconds, length, effects). Length is seconds in the anthem section,
# BEATS in the Hell March sections. effects: flash, shake, glitch, red, zoom, slow, fast
ANTHEM = [
    ("mayday1958.mp4", 2.0, 2.2, "zoom"),           # endless columns in the rain
    ("parade1990.mp4", 183.0, 1.8, "zoom"),         # Red Square, Kremlin
    ("soldiers_dance.mp4", 26.0, 1.3, ""),          # two soldiers dance against the sky
    ("newsreel1961.mp4", 95.0, 1.6, ""),            # Khrushchev waves from the mausoleum
    ("women.mp4", 88.3, 1.3, ""),                   # the disco: girls laughing as they dance
    ("parade1990.mp4", 2930.0, 1.6, ""),            # ranks goose-stepping
    ("soldiers_dance.mp4", 184.3, 1.1, ""),         # a leap into the splits, high over the hill
    ("mayday1958.mp4", 789.0, 1.2, "zoom"),         # Marx banner
    ("women.mp4", 111.5, 1.2, ""),                  # two friends smile for the camera
    ("parade1990.mp4", 1770.0, 1.4, "zoom"),        # red flag over the Kremlin
    ("soldiers_dance.mp4", 77.0, 1.1, "zoom"),      # the whole troupe above the Moscow river
    ("women.mp4", 19.0, 1.1, ""),                   # a girl turns, smiling
    ("soldiers_dance.mp4", 250.2, 1.2, ""),         # the squat dance, his comrades clapping
    ("pripyat_may86.mp4", 30.0, 1.4, ""),           # Pripyat from the air: the model city
    ("pripyat_may86.mp4", 131.5, 1.0, ""),          # the Ferris wheel turning
    ("pripyat_may86.mp4", 236.0, 0.8, ""),          # waving girls
    ("aerial.mp4", 1.0, 1.0, "slow"),               # the plant, intact ... (pans toward unit 4)
]
GLITCH = [  # good times -> disaster: stall, blast, strobe, black + date card (sums to GLITCH_LEN)
    ("aerial.mp4", 6.0, STALL, "stall"),            # the intact plant slows, trembles, darkens
    ("WHITE", 0, 0.16, ""),                         # the blast: white-out
    ("aerial.mp4", 21.4, 0.12, "glitch red"),
    ("pd_compilation.ogv", 41.5, 0.10, "glitch"),
    ("WHITE", 0, 0.06, ""),
    ("aerial.mp4", 22.4, 0.14, "glitch shake"),
    ("pd_compilation.ogv", 22.0, 0.10, "red shake"),
    ("BLACK", 0, 0.06, ""),
    ("aerial.mp4", 23.0, 0.16, "shake glitch"),
    ("BLACK", 0, 0.90, ""),                         # silence-ish: the date and place type in
]
BUILD = [   # 32 beats; Hell March's ominous pulse
    ("aerial.mp4", 23.4, 4, "slow"),                # unit 4: the roof torn open
    ("pd_compilation.ogv", 5.0, 2, ""),             # from the helicopter, in the haze
    ("disaster2.mp4", 4.4, 2, ""),                  # down the vent stack, the ruin smoking beside it
    ("doc_subs.mp4", 0.0, 4, ""),                   # SUB 1: "There is a hole in the platform"
    ("newsreel_hd.mp4", 403.0, 2, ""),              # suiting up: lead apron
    ("newsreel_hd.mp4", 372.0, 2, ""),              # hooded, masked
    ("radiological.mp4", 79.0, 2, ""),              # the loudspeaker
    ("doc_subs.mp4", 6.5, 4, ""),                   # SUB 2: "So, once in your life..."
    ("pripyat_film_0426.mp4", 268.0, 2, ""),        # the helicopter lands
    ("newsreel_hd.mp4", 294.0, 2, ""),              # respirator close-up
    ("radiological.mp4", 169.0, 1, ""),             # dosimeter dials
    ("pripyat_film_0426.mp4", 307.0, 1, ""),        # counter in hand
    ("doc_subs.mp4", 13.0, 2, ""),                  # SUB 3: "...in the memory and do your business!"
    ("newsreel_hd.mp4", 217.0, 1, "shake"),         # face in the hood
    ("newsreel_hd.mp4", 31.0, 1, "shake"),          # the wreck
    ("disaster2.mp4", 8.3, 2, "shake"),             # the torn hall, close
    ("montage.mp4", 12.0, 2, "glitch"),             # Chernobyl 1 montage: the roof
]
KICK = [    # 40 beats from the kick; flashes on downbeats
    ("newsreel_hd.mp4", 15.5, 2, "flash shake"),    # KICK: the ruined plant
    ("newsreel_hd.mp4", 279.0, 1, ""),              # roof
    ("newsreel_hd.mp4", 511.0, 1, "shake"),         # liquidators on the roof
    ("newsreel_hd.mp4", 527.0, 1, "fast"),
    ("pd_compilation.ogv", 51.6, 1, "flash"),       # aerial: the open core
    ("newsreel_hd.mp4", 356.0, 1, "red"),           # unit 4 behind the pylons
    ("newsreel_hd.mp4", 418.0, 1, ""),              # masked men
    ("pd_compilation.ogv", 60.6, 1, "shake"),       # the vent stack from above
    ("newsreel_hd.mp4", 449.0, 1, "flash glitch"),  # twisted wreckage
    ("newsreel_hd.mp4", 434.0, 1, ""),              # ladder up the wall
    ("radiological.mp4", 319.0, 1, ""),             # hazmat patrol
    ("newsreel_hd.mp4", 465.0, 1, "fast"),          # wheelbarrow of debris
    ("pd_compilation.ogv", 17.0, 1, "flash red"),   # swooping over the ruin
    ("montage.mp4", 4.0, 1, "glitch"),
    ("newsreel_hd.mp4", 341.0, 1, "shake"),         # rubble field
    ("pripyat_film_0426.mp4", 466.0, 1, ""),        # evacuation buses
    ("pripyat_film_0426.mp4", 496.0, 1, "flash"),   # crowd leaving
    ("newsreel_hd.mp4", 589.0, 1, ""),              # smoke
    ("radiological.mp4", 498.0, 1, "shake"),        # spraying down
    ("aerial.mp4", 26.0, 1, "red"),
    ("newsreel_hd.mp4", 496.0, 1, "flash"),         # exhausted men
    ("newsreel_hd.mp4", 604.0, 1, "fast"),          # remote bulldozer
    ("mayday1958.mp4", 250.0, 1, "glitch"),         # rockets on parade -- the state's might
    ("newsreel_hd.mp4", 46.5, 1, "shake"),          # reactor hall
    ("pripyat_film_0426.mp4", 605.0, 1, "flash red"),  # the Ferris wheel, abandoned
    ("pripyat_film_0426.mp4", 575.0, 1, ""),        # broken window
    ("newsreel_hd.mp4", 310.0, 1, "glitch shake"),
    ("parade1990.mp4", 3660.0, 1, "red"),           # missiles roll by
    ("pd_compilation.ogv", 54.0, 1, "flash shake"),
    ("newsreel_hd.mp4", 558.0, 1, ""),              # debris close
    ("parade1990.mp4", 3355.0, 1, "glitch"),        # tanks on Red Square
    ("radiological.mp4", 89.0, 1, "shake"),         # the plant in fog
    ("pripyat_film_0426.mp4", 257.0, 1, "flash red"),
    ("disaster2.mp4", 13.9, 12, "flash"),           # the Mi-8: its rotor takes the crane cable, the blades fly off, it falls -- all the way
                                                    # down, as the music thins (the owner's cut: the wall going up was here; SUB 4 stays, over the fall)
    ("BLACK", 0, 2, ""),                            # breath ...
]
TITLE = [("aerial.mp4", 27.0, None, "slow title")]   # the sarcophagus-to-be, under the title

SUBS = [  # (start beat-index anchor, text) -- placed on their clips below
    ("SUB1", ["– There is a hole in the platform,", "and under it is the reactor –"],
             ["– В платформе дыра,", "а под ней – реактор –"]),
    ("SUB2", ["– So, once in your life you must work", "so that it stays in the memory..."],
             ["– Так что раз в жизни надо поработать так,", "чтобы это осталось в памяти..."]),
    ("SUB3", ["...and do your business!"], ["...и делайте своё дело!"]),
    ("SUB4", ["Any questions?"], ["Вопросы есть?"]),
]


# ------------------------------------------------------------------ video
def ff(args):
    r = subprocess.run(["ffmpeg", "-v", "error", "-y"] + args)
    if r.returncode:
        sys.exit("ffmpeg failed: " + " ".join(args))


def shot_filter(src, fx, n):
    cx, cy, cw, ch = CROP.get(src, (0, 0, 1, 1))
    f = [f"crop=iw*{cw}:ih*{ch}:iw*{cx}:ih*{cy}"]
    pad = 1.10 if ("shake" in fx or "zoom" in fx or "title" in fx or "stall" in fx) else 1.0
    f.append(f"scale={int(W*pad)}:{int(H*pad)}:force_original_aspect_ratio=increase")
    if "zoom" in fx or "title" in fx:      # slow push-in
        f.append(f"crop={W}:{H}:(iw-{W})/2*(1-n/{max(n,1)}):(ih-{H})/2*(1-n/{max(n,1)})")
    elif "stall" in fx:                    # tremor that grows to a violent shake
        f.append(f"crop={W}:{H}:'(iw-{W})/2+(random(1)-0.5)*{int(W*0.09)}*pow(n/{n},2.5)':'(ih-{H})/2+(random(2)-0.5)*{int(H*0.09)}*pow(n/{n},2.5)'")
    elif "shake" in fx:
        f.append(f"crop={W}:{H}:(iw-{W})/2+(random(1)-0.5)*{int(W*0.07)}:(ih-{H})/2+(random(2)-0.5)*{int(H*0.07)}")
    else:
        f.append(f"crop={W}:{H}")
    f.append("format=yuv420p,hue=s=0")
    con = 1.55 if ("red" in fx or "flash" in fx) else 1.38
    f.append(f"eq=contrast={con}:gamma=0.88:brightness='-0.04+0.05*(random(3)-0.5)':eval=frame")
    f.append("noise=alls=22:allf=t+u")
    f.append("vignette=PI/3.6")
    if "stall" in fx:                      # the light drains out of the last good shot
        f.append(f"eq=brightness='-0.25*pow(n/{n},2)':contrast=1.2:eval=frame")
    if "red" in fx:
        f.append("format=rgb24,colorchannelmixer=rr=1.0:rg=0.25:rb=0.1:gr=0.12:gg=0.12:gb=0.12:br=0.1:bg=0.1:bb=0.1")
    if "glitch" in fx:
        f.append("format=rgb24,rgbashift=rh=-14:bh=14:gv=4")
    if "flash" in fx:
        f.append("fade=in:st=0:d=0.12:color=white")
    f.append("format=yuv420p")
    return ",".join(f)


def render_shot(i, src, t_in, dur, fx):
    out = os.path.join(OUT, "shots", f"{i:03d}.mp4")
    n = max(1, round(dur * FPS))
    if src in ("BLACK", "WHITE"):
        ff(["-f", "lavfi", "-i", f"color={src.lower()}:s={W}x{H}:r={FPS}", "-frames:v", str(n),
            "-c:v", "libx264", "-preset", "veryfast", "-crf", "16", "-pix_fmt", "yuv420p", out])
        return out, n
    speed = 0.18 if "stall" in fx else (0.6 if "slow" in fx else (1.6 if "fast" in fx else 1.0))
    path = os.path.join(SRC, src)
    vf = f"setpts=PTS/{speed}," + f"fps={FPS}," + shot_filter(src, fx, n)
    ff(["-ss", f"{t_in:.3f}", "-i", path, "-an", "-vf", vf, "-frames:v", str(n),
        "-c:v", "libx264", "-preset", "veryfast", "-crf", "16", out])
    return out, n


def build_montage_clip():
    dst = os.path.join(SRC, "montage.mp4")
    if not os.path.exists(dst):
        ff(["-framerate", "12", "-i", os.path.join(REPO, "..", "chernobyl", "sprites", "intro", "frame_%04d.jpg"),
            "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "14", dst])


# ------------------------------------------------------------------ overlays (PIL)
FONT_SUB = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
FONT_TITLE = "/System/Library/Fonts/Supplemental/Impact.ttf"


FONT_TTY = "/System/Library/Fonts/Supplemental/Courier New Bold.ttf"


def sub_png(lines, path, ru=()):
    """Bilingual subtitle, KGB-teletype style: Courier struck unevenly on a worn ribbon --
    the Russian in red above, the English in off-white. Ink voids + grain so it sits IN the film."""
    rng = np.random.default_rng(abs(hash(path)) % (2 ** 31))
    fe = ImageFont.truetype(FONT_TTY, 38)
    fr = ImageFont.truetype(FONT_TTY, 31)
    rows = [(ln, fr, (214, 40, 28), 40) for ln in ru] + [(ln, fe, (238, 234, 222), 48) for ln in lines]
    y = H - 54 - sum(r[3] for r in rows) - (12 if ru else 0)
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    for i, (ln, f, col, step) in enumerate(rows):
        if ru and i == len(ru):
            y += 12                                  # a little air between the languages
        m = Image.new("L", (W, H), 0)
        dm = ImageDraw.Draw(m)
        x = (W - dm.textlength(ln, font=f)) / 2
        for ch in ln:                                # each key strikes a little off the line
            dm.text((x + rng.normal(0, 0.35), y + rng.normal(0, 0.9)), ch, font=f, fill=int(rng.uniform(215, 255)))
            x += dm.textlength(ch, font=f)
        a = np.asarray(m, np.float32) / 255.0        # worn ribbon: blotchy voids + pinholes
        n = np.asarray(Image.fromarray((rng.random((H // 2, W // 2)) * 255).astype(np.uint8))
                       .resize((W, H), Image.BICUBIC), np.float32) / 255.0
        a *= np.clip(1 - (n > 0.75) * 0.85, 0, 1) * np.clip(1 - (rng.random(a.shape) > 0.97) * 0.7, 0, 1)
        m = Image.fromarray((a * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.5))
        sh = m.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.GaussianBlur(2))
        layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        layer.paste((0, 0, 0, 255), (3, 4), sh.point(lambda v: v * 9 // 10))
        im.alpha_composite(layer)
        layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        layer.paste(col + (255,), (0, 0), m)
        im.alpha_composite(layer)
        y += step
    a = np.asarray(im, np.float32)                   # grain over the ink
    a[..., :3] = np.clip(a[..., :3] + rng.normal(0, 10, a.shape[:2])[..., None], 0, 255)
    Image.fromarray(a.astype(np.uint8), "RGBA").save(path)


FONT_CARD = "/System/Library/Fonts/Supplemental/DIN Condensed Bold.ttf"
CARD = ["UNION OF SOVIET SOCIALIST REPUBLICS", "SPRING  1986"]
FONT_DATE = "/System/Library/Fonts/Supplemental/DIN Condensed Bold.ttf"


def date_png(path, stage, alpha=255):
    """The disaster card, centred: date, time, then place -- typed in by stage 1..3."""
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    ru = (226, 214, 180)
    rows = [("26 АПРЕЛЯ 1986", 44, ru), ("26 APRIL 1986", 96, (232, 22, 14)),
            ("01 : 23 : 40", 52, (240, 236, 228)),
            ("ЧЕРНОБЫЛЬСКАЯ АЭС  ·  ЭНЕРГОБЛОК № 4", 28, ru),
            ("CHERNOBYL NUCLEAR POWER PLANT  ·  REACTOR No. 4", 34, (210, 206, 198)),
            ("ПРИПЯТЬ, УКРАИНСКАЯ ССР", 28, ru),
            ("PRIPYAT, UKRAINIAN SSR", 34, (210, 206, 198))]
    shown = rows[:2] if stage == 1 else (rows[:3] if stage == 2 else rows)
    y = H * 0.22
    # soft dark panel so the card reads over the busy aerial shot it holds on
    hgt = sum(sz * 1.15 + 6 for _, sz, _ in shown)
    pan = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(pan).rectangle([W * 0.17, y - 30, W * 0.83, y + hgt + 20], fill=(0, 0, 0, int(alpha * 0.62)))
    im.alpha_composite(pan.filter(ImageFilter.GaussianBlur(28)))
    d = ImageDraw.Draw(im)
    for text, size, col in shown:
        f = ImageFont.truetype(FONT_DATE, size)
        w = d.textlength(text, font=f)
        x = (W - w) / 2
        d.text((x + 3, y + 4), text, font=f, fill=(0, 0, 0, int(alpha * 0.85)))
        d.text((x, y), text, font=f, fill=col + (alpha,))
        y += size * 1.15 + 6
    im.save(path)


CARD_RU = "СОЮЗ СОВЕТСКИХ СОЦИАЛИСТИЧЕСКИХ РЕСПУБЛИК"


def card_png(path, words_ru, words1, show2, alpha=255):
    """Opening documentary card, lower left: a red rule, the country typed out word by
    word -- Russian first, the English under it -- then the date."""
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    f1 = ImageFont.truetype(FONT_CARD, 46)
    f2 = ImageFont.truetype(FONT_CARD, 34)
    fr = ImageFont.truetype(FONT_CARD, 36)
    x, y = 70, H - 190
    d.rectangle([x, y - 66, x + 90, y - 60], fill=(220, 20, 12, alpha))
    lr = " ".join(CARD_RU.split()[:words_ru])
    for ox, oy in ((3, 3), (-1, 0), (1, 0)):
        d.text((x + ox, y - 48 + oy), lr, font=fr, fill=(0, 0, 0, int(alpha * 0.8)))
    d.text((x, y - 48), lr, font=fr, fill=(226, 214, 180, alpha))
    l1 = " ".join(CARD[0].split()[:words1])
    for ox, oy in ((3, 3), (-1, 0), (1, 0)):
        d.text((x + ox, y + oy), l1, font=f1, fill=(0, 0, 0, int(alpha * 0.8)))
    d.text((x, y), l1, font=f1, fill=(240, 236, 228, alpha))
    if show2:
        d.text((x + 3, y + 62), CARD[1], font=f2, fill=(0, 0, 0, int(alpha * 0.8)))
        d.text((x, y + 59), CARD[1], font=f2, fill=(225, 30, 18, alpha))
    im.save(path)


_HS = None


def hs_base():
    """The hammer & sickle as a worn red ink stamp (glyph from Arial Unicode), cached."""
    global _HS
    if _HS is None:
        f = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Unicode.ttf", 900)
        g = Image.new("L", (1100, 1100), 0)
        ImageDraw.Draw(g).text((60, -120), "\u262d", font=f, fill=255)
        g = g.crop(g.getbbox())
        rng = np.random.default_rng(1917)
        a = np.asarray(g, dtype=np.float32) / 255.0
        # ink wear: speckle voids + a few streaks where the stamp didn't take
        sp = rng.random(a.shape)
        blot = np.asarray(Image.fromarray((rng.random((a.shape[0] // 14, a.shape[1] // 14)) * 255).astype(np.uint8))
                          .resize(a.shape[::-1], Image.BICUBIC), dtype=np.float32) / 255.0
        wear = np.clip(1.0 - (sp > 0.985) - np.clip((blot - 0.78) * 4, 0, 1) * 0.85, 0, 1)
        a = a * wear
        rgba = np.zeros(a.shape + (4,), np.uint8)
        rgba[..., 0], rgba[..., 1], rgba[..., 2] = 214, 18, 12
        rgba[..., 3] = (a * 255).astype(np.uint8)
        _HS = Image.fromarray(rgba, "RGBA")
    return _HS


def hs_png(path, scale=1.0, alpha=255, jitter=(0, 0), hot=False):
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    base = hs_base()
    hgt = int(430 * scale)
    g = base.resize((int(base.width * hgt / base.height), hgt), Image.LANCZOS)
    if alpha < 255:
        g.putalpha(g.getchannel("A").point(lambda v: v * alpha // 255))
    cx, cy = W // 2 + jitter[0], int(H * 0.655) + jitter[1]
    x, y = cx - g.width // 2, cy - g.height // 2
    sh = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    sh.paste((0, 0, 0, 255), (x + 10, y + 14), g.getchannel("A").point(lambda v: v * 3 // 4))
    im.alpha_composite(sh.filter(ImageFilter.GaussianBlur(10)))
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    glow.paste((255, 40 if not hot else 200, 20, 255), (x, y), g.getchannel("A").point(lambda v: v // (1 if hot else 2)))
    im.alpha_composite(glow.filter(ImageFilter.GaussianBlur(24 if hot else 16)))
    im.alpha_composite(g, (x, y))
    im.save(path)


def title_png(path, scale=1.0, alpha=255, jitter=(0, 0)):
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    big = ImageFont.truetype(FONT_TITLE, int(190 * scale))
    d = ImageDraw.Draw(im)
    t = "CHERNOBYL 2"
    w = d.textlength(t, font=big)
    x, y = (W - w) / 2 + jitter[0], H * 0.20 - 95 * scale + jitter[1]
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(glow).text((x, y), t, font=big, fill=(255, 30, 10, int(alpha * 0.8)))
    glow = glow.filter(ImageFilter.GaussianBlur(18))
    im.alpha_composite(glow)
    d.text((x + 6, y + 8), t, font=big, fill=(60, 0, 0, alpha))
    d.text((x, y), t, font=big, fill=(225, 18, 12, alpha))
    im.save(path)


# ------------------------------------------------------------------ audio
def load(name, offset=0.0, dur=None):
    y, _ = librosa.load(os.path.join(SRC, name), sr=SR, mono=False, offset=offset, duration=dur)
    if y.ndim == 1:
        y = np.stack([y, y])
    return y


def place(mix, clip, t, gain=1.0):
    a = int(t * SR)
    b = min(mix.shape[1], a + clip.shape[1])
    if b > a:
        mix[:, a:b] += gain * clip[:, : b - a]


def tape_stop(y, length):
    """Slow the clip to a halt (pitch falls with it), like a reel losing power."""
    n = int(length * SR)
    rate = np.linspace(1.0, 0.05, n) ** 1.6
    pos = np.cumsum(rate)
    pos = pos[pos < y.shape[1] - 1]
    out = np.stack([np.interp(pos, np.arange(y.shape[1]), ch) for ch in y])
    env = np.linspace(1.0, 0.0, out.shape[1]) ** 0.7
    return out * env


def geiger(length, rate_fn, seed=0):
    rng = np.random.default_rng(seed)
    n = int(length * SR)
    out = np.zeros(n)
    click = rng.standard_normal(int(0.004 * SR)) * np.exp(-np.linspace(0, 9, int(0.004 * SR)))
    t = 0.0
    while t < length:
        r = max(0.5, rate_fn(t))
        t += rng.exponential(1.0 / r)
        a = int(t * SR)
        if a + len(click) < n:
            out[a:a + len(click)] += click * rng.uniform(0.5, 1.0)
    return np.stack([out, out])


def boom(length=2.2):
    t = np.linspace(0, length, int(length * SR), endpoint=False)
    sub = np.sin(2 * np.pi * (52 - 18 * t / length) * t) * np.exp(-t * 2.2)
    hit = np.random.default_rng(3).standard_normal(len(t)) * np.exp(-t * 18)
    y = 0.9 * sub + 0.5 * hit
    return np.stack([y, y])


def build_audio(path, game=False):
    """game=True: no fade at the end -- Hell March plays on to the end of the track, so the
    title screen keeps its music (the game loops this stream)."""
    hm_len = (librosa.get_duration(path=os.path.join(SRC, "hell-march.mp3")) - HM_START_SRC) if game else (T_END - T_BUILD + 0.5)
    mix = np.zeros((2, int((T_BUILD + hm_len + 0.5) * SR)))
    an = load("anthem.mp3", ANTHEM_IN, ANTHEM_LEN + 1.2)
    fade = np.minimum(1, np.arange(an.shape[1]) / (0.3 * SR))
    an *= fade
    main = an[:, : int(ANTHEM_LEN * SR)]
    place(mix, main, 0.0, 0.9)
    place(mix, tape_stop(an[:, int(ANTHEM_LEN * SR):], STALL), ANTHEM_LEN, 0.95)
    T_BLAST = ANTHEM_LEN + STALL
    rng = np.random.default_rng(7)
    # a low rumble swelling under the dying anthem
    n = int(STALL * SR); rum = rng.standard_normal(n)
    rum = np.convolve(rum, np.ones(400) / 400, mode="same") * np.linspace(0, 1, n) ** 2 * 6.0
    place(mix, np.stack([rum, rum]), ANTHEM_LEN, 0.5)
    # the blast: a deep distorted boom + a crack of noise
    bl = boom(3.0) * 1.6
    bl = np.tanh(bl * 2.2) * 0.8
    place(mix, bl, T_BLAST, 1.0)
    cr = rng.standard_normal(int(0.5 * SR)) * np.exp(-np.linspace(0, 10, int(0.5 * SR)))
    place(mix, np.stack([cr, cr]), T_BLAST, 0.7)
    # shell-shock ringing, then an air-raid siren wailing up into Hell March
    t = np.arange(int(2.6 * SR)) / SR
    ring = np.sin(2 * np.pi * 3150 * t) * np.exp(-t * 1.1) * np.minimum(1, t / 0.05)
    place(mix, np.stack([ring, ring]), T_BLAST + 0.05, 0.10)
    t = np.arange(int(3.6 * SR)) / SR
    fr = 330 + 380 * np.clip(t / 1.6, 0, 1) ** 0.6 - 120 * np.clip((t - 2.6) / 1.0, 0, 1)
    ph = 2 * np.pi * np.cumsum(fr) / SR
    sir = (np.sin(ph) + 0.35 * np.sin(2 * ph) + 0.15 * np.sin(3 * ph)) * np.minimum(1, t / 0.6) * np.clip((3.6 - t) / 1.0, 0, 1)
    place(mix, np.stack([sir, sir * 0.9]), T_BLAST + 0.6, 0.16)
    # Hell March from the build-up through the end
    hm = load("hell-march.mp3", HM_START_SRC, hm_len)
    if not game:
        hm[:, -int(2.5 * SR):] *= np.linspace(1, 0, int(2.5 * SR))
    place(mix, hm, T_BUILD, 1.0)
    # Geiger: a frenzy at the blast, rising with the build-up,
    # a roar into the title, silence on the slam
    # (no Geiger in the good times: it starts with the blast)
    place(mix, geiger(T_BUILD - T_BLAST, lambda t: 160 * np.exp(-t * 0.9) + 8, 2), T_BLAST, 0.45)
    place(mix, geiger(T_KICK - T_BUILD, lambda t: 4 + 26 * (t / (T_KICK - T_BUILD)) ** 2, 3), T_BUILD, 0.30)
    place(mix, geiger(T_TITLE - T_KICK, lambda t: 18 + 200 * max(0, (t - (T_TITLE - T_KICK - 3 * P)) / (3 * P)), 4), T_KICK, 0.22)
    place(mix, boom(), T_TITLE, 0.9)
    # the stamp: a heavy, short, distorted thud + a crack
    th = np.tanh(boom(1.6) * 3.0) * 0.9
    place(mix, th, T_STAMP, 1.0)
    ck = np.random.default_rng(9).standard_normal(int(0.18 * SR)) * np.exp(-np.linspace(0, 14, int(0.18 * SR)))
    place(mix, np.stack([ck, ck]), T_STAMP, 0.8)
    pk = np.max(np.abs(mix))
    mix = mix / pk * 0.95 if pk > 0.95 else mix
    sf.write(path, mix.T, SR)


# ------------------------------------------------------------------ assemble
def main():
    os.makedirs(os.path.join(OUT, "shots"), exist_ok=True)
    os.makedirs(os.path.join(OUT, "ovl"), exist_ok=True)
    build_montage_clip()
    plan = []          # (T_start, src, t_in, dur, fx)
    T = 0.0
    for src, t_in, d, fx in ANTHEM + GLITCH:
        plan.append((T, src, t_in, d, fx)); T += d
    for sect, start in ((BUILD, T_BUILD), (KICK, T_KICK)):
        b = 0
        for src, t_in, nb, fx in sect:
            t0 = start + b * P
            plan.append((t0, src, t_in, nb * P, fx)); b += nb
    src, t_in, _, fx = TITLE[0]
    plan.append((T_TITLE, src, t_in, T_END - T_TITLE, fx))
    # render shots with exact frame counts (cumulative rounding keeps cuts on the beat)
    files, frame0 = [], []
    for i, (t0, src, t_in, d, fx) in enumerate(plan):
        f_start = round(t0 * FPS)
        f_end = round((t0 + d) * FPS)
        out, _ = render_shot(i, src, t_in, (f_end - f_start) / FPS, fx)
        files.append(out); frame0.append((f_start, src, fx, t0))
        print(f"  {i:02d} T={t0:6.2f} {d:5.2f}s {src:22s} {fx}")
    with open(os.path.join(OUT, "shots.txt"), "w") as fh:
        for f in files:
            fh.write(f"file '{f}'\n")
    # re-encoded at constant 25 fps (a stream-copy concat left irregular timestamps at a few
    # joins; the text overlay pairs frames by timestamp, so subtitles blinked out there)
    ff(["-f", "concat", "-safe", "0", "-i", os.path.join(OUT, "shots.txt"), "-vf", f"fps={FPS}", "-fps_mode", "cfr",
        "-c:v", "libx264", "-preset", "veryfast", "-crf", "14", "-pix_fmt", "yuv420p", os.path.join(OUT, "video.mp4")])

    # overlay track: subtitles on their doc_subs clips, the title slam
    ovl = os.path.join(OUT, "ovl")
    events = []   # (t_start, t_end, png)
    subs = iter(SUBS)
    starts = []
    for (t0, src, t_in, d, fx) in plan:
        if src == "doc_subs.mp4":
            tag, lines, ru = next(subs)
            p = os.path.join(ovl, f"{tag}.png"); sub_png(lines, p, ru)
            starts.append([t0 + 0.08, p])
    tag, lines, ru = next(subs)                  # SUB 4, "Any questions?": its own shot is gone; it lands where it always did,
    p = os.path.join(ovl, f"{tag}.png"); sub_png(lines, p, ru)          # eight beats before the title, over the falling helicopter
    starts.append([T_TITLE - 8 * P + 0.08, p])
    for st in starts:                            # never on screen with the date card
        st[0] = max(st[0], DATE_END + 0.25)
    for k, (a, p) in enumerate(starts):          # hold ~5 s: readable, carried over the next shots
        nxt = starts[k + 1][0] - 0.12 if k + 1 < len(starts) else T_TITLE - 0.04
        events.append((a, min(a + 5.0, nxt), p))
    # opening card: the country typed out word by word on the anthem's opening brass,
    # then the place and date, held, then faded out before the Pripyat shots
    t = 0.5
    steps = [(r, 0) for r in range(1, 5)] + [(4, e) for e in range(1, 6)]
    for k, (r, e) in enumerate(steps):
        p = os.path.join(ovl, f"card{k}.png"); card_png(p, r, e, False)
        events.append((t, t + 0.27, p)); t += 0.27
    p = os.path.join(ovl, "card_hold.png"); card_png(p, 4, 5, True)
    events.append((t, 7.4, p))
    for k, a in enumerate((170, 90)):
        p = os.path.join(ovl, f"card_fade{k}.png"); card_png(p, 4, 5, True, a)
        events.append((7.4 + k * 0.16, 7.4 + (k + 1) * 0.16, p))
    # disaster card: types in on the black after the blast, holds over the reveal
    tb = ANTHEM_LEN + GLITCH_LEN - 0.90
    for k, (a0, a1) in enumerate(((tb + 0.05, tb + 0.35), (tb + 0.35, tb + 0.65)), 1):
        p = os.path.join(ovl, f"date{k}.png"); date_png(p, k); events.append((a0, a1, p))
    p = os.path.join(ovl, "date3.png"); date_png(p, 3); events.append((tb + 0.65, DATE_HOLD, p))
    for k, a in enumerate((160, 70)):
        p = os.path.join(ovl, f"date_fade{k}.png"); date_png(p, 3, a)
        events.append((DATE_HOLD + k * 0.16, DATE_HOLD + (k + 1) * 0.16, p))
    # title: 5-frame slam (big -> settle, jitter), then hold
    for k in range(6):
        p = os.path.join(ovl, f"title{k}.png")
        sc = 1.0 + 0.45 * (1 - k / 5) ** 2
        title_png(p, sc, 255, (random.randint(-14, 14) * (5 - k) // 5, random.randint(-10, 10) * (5 - k) // 5))
        events.append((T_TITLE + k / FPS, T_TITLE + (k + 1) / FPS, p))
    p = os.path.join(ovl, "title.png"); title_png(p)
    events.append((T_TITLE + 6 / FPS, T_END, p))
    # hammer & sickle: drops in from 3x (4 frames), hits white-hot, judders, settles
    fr = 1.0 / FPS
    seq = [(3.0, 90, (0, 0), False), (2.2, 150, (0, 0), False), (1.6, 210, (0, 0), False), (1.2, 245, (0, 0), False),
           (0.97, 255, (0, 0), True), (1.03, 255, (9, -6), True), (1.0, 255, (-7, 5), False),
           (1.01, 255, (5, 3), False), (1.0, 255, (-3, -2), False), (1.0, 255, (1, 1), False)]
    t0 = T_STAMP - 4 * fr                        # impact lands exactly on the beat
    for k, (sc, al, jt, hot) in enumerate(seq):
        p = os.path.join(ovl, f"hs{k}.png"); hs_png(p, sc, al, jt, hot)
        events.append((t0 + k * fr, t0 + (k + 1) * fr, p))
    p = os.path.join(ovl, "hs.png"); hs_png(p)
    events.append((t0 + len(seq) * fr, T_END, p))
    events.sort()
    if os.environ.get("INTRO_EVENTS"): [print(f"EV {a:7.3f} {b:7.3f} {os.path.basename(p)}") for a, b, p in events]
    build_audio(os.path.join(OUT, "audio.wav"))
    # the text layer: composited here frame by frame (stacked in event order) and piped to
    # ffmpeg as ONE transparent rawvideo stream -- dozens of looped image inputs with
    # enable= windows dropped frames, and itsoffset inputs stalled the frame sync
    nfr = int(math.ceil(T_END * FPS)) + 1
    imgs, combos = {}, {}
    blank = bytes(W * H * 4)
    def frame_bytes(active):
        key = tuple(active)
        if key not in combos:
            if not active:
                combos[key] = blank
            else:
                im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
                for p in active:
                    if p not in imgs:
                        imgs[p] = Image.open(p).convert("RGBA")
                    im.alpha_composite(imgs[p])
                combos[key] = im.tobytes()
        return combos[key]
    flash = f"between(t,{T_TITLE:.3f},{T_TITLE + 0.10:.3f})"
    st = f"between(t,{T_STAMP:.3f},{T_STAMP + 0.28:.3f})"
    fc = (f"[0:v]eq=brightness='if(gte(t,{T_TITLE + 0.2:.3f}),-0.18*min(1,(t-{T_TITLE + 0.2:.3f})/0.6),0)':eval=frame[bg];"
          f"[bg][1:v]overlay=0:0:format=auto[v0];"
          f"[v0]eq=brightness='if({flash},0.6,0)':eval=frame,"
          f"colorchannelmixer=rr=1.0:gg=0.3:bb=0.3:enable='between(t,{T_STAMP:.3f},{T_STAMP + 0.10:.3f})',"
          f"scale={W + 40}:{H + 22},crop={W}:{H}:'20+if({st},(random(1)-0.5)*44,0)':'11+if({st},(random(2)-0.5)*26,0)'[v]")
    cmd = ["ffmpeg", "-v", "error", "-y", "-i", os.path.join(OUT, "video.mp4"),
           "-f", "rawvideo", "-pix_fmt", "rgba", "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-",
           "-i", os.path.join(OUT, "audio.wav"), "-filter_complex", fc,
           "-map", "[v]", "-map", "2:a", "-t", f"{T_END:.3f}",
           "-c:v", "libx264", "-preset", "slow", "-crf", "18", "-pix_fmt", "yuv420p",
           "-c:a", "aac", "-b:a", "192k", os.path.join(OUT, "intro.mp4")]
    pr = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    for f in range(nfr):
        t = f / FPS
        pr.stdin.write(frame_bytes([p for a, b, p in events if a <= t < b]))
    pr.stdin.close()
    if pr.wait():
        sys.exit("final composite failed")
    print("wrote", os.path.join(OUT, "intro.mp4"))
    export_game_assets()


def press_png(path):
    """'PRESS ANY KEY' for the title screen, in the subtitles' teletype style."""
    f = ImageFont.truetype(FONT_TTY, 34)
    fr = ImageFont.truetype(FONT_TTY, 24)
    im = Image.new("RGBA", (760, 110), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    for txt, font, y, col in (("НАЖМИТЕ ЛЮБУЮ КЛАВИШУ", fr, 6, (214, 40, 28)), ("[ PRESS ANY KEY ]", f, 48, (238, 234, 222))):
        w = d.textlength(txt, font=font)
        d.text(((760 - w) / 2 + 3, y + 4), txt, font=font, fill=(0, 0, 0, 220))
        d.text(((760 - w) / 2, y), txt, font=font, fill=col + (255,))
    im.save(path)


# Title-screen controls (keep in step with main.c: kSlotForKey maps keys 1-4 to slots).
CONTROLS = [("MOVE", "W A S D"), ("LOOK", "MOUSE"), ("FIRE", "LEFT CLICK"), ("RELOAD", "R"),
            ("SPRINT", "SHIFT"), ("JUMP", "SPACE"), ("MENU", "ESC")]
WEAPONS = [("1", "MINIGUN"), ("2", "FLAMETHROWER"), ("3", "GRENADE LAUNCHER"), ("4", "REMINGTON")]


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


def export_game_assets():
    """What the game plays: MPEG-1 video (pl_mpeg), the looping music, the title still."""
    A = os.path.join(REPO, "assets")
    ff(["-i", os.path.join(OUT, "intro.mp4"), "-an", "-c:v", "mpeg1video", "-q:v", "7", "-bf", "0",
        "-r", str(FPS), "-f", "mpeg", os.path.join(A, "intro.mpg")])
    build_audio(os.path.join(OUT, "audio_game.wav"), game=True)
    ff(["-i", os.path.join(OUT, "audio_game.wav"), "-c:a", "libmp3lame", "-b:a", "192k", os.path.join(A, "intro_music.mp3")])
    ff(["-sseof", "-0.08", "-i", os.path.join(OUT, "intro.mp4"), "-frames:v", "1", "-update", "1",
        os.path.join(A, "intro_title.png")])
    press_png(os.path.join(A, "intro_press.png"))
    controls_png(os.path.join(A, "intro_controls.png"))
    with open(os.path.join(A, "intro_times.txt"), "w") as fh:     # the game reads these
        fh.write(f"title {T_TITLE:.3f}\nstamp {T_STAMP:.3f}\nend {T_END:.3f}\n")
    for n in ("intro.mpg", "intro_music.mp3", "intro_title.png", "intro_press.png", "intro_controls.png"):
        print(f"  assets/{n}: {os.path.getsize(os.path.join(A, n)) // 1024} KB")


if __name__ == "__main__":
    n = sum(x[2] for x in KICK); assert n == KICK_BEATS, f"KICK is {n} beats, want {KICK_BEATS}"
    n = sum(x[2] for x in BUILD); assert n == BUILD_BEATS, f"BUILD is {n} beats, want {BUILD_BEATS}"
    main()
