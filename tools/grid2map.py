#!/usr/bin/env python3
"""Convert a ../chernobyl grid level (30x20 '#'/'.' + plat: lines) into a Quake .map
this game loads like any Xonotic arena -- same collision, nav, baked lighting, picker.

The original: CELL = 4 m grid, walls WALL_H = 5.1 m, a dark tiled ceiling, Q3-style
stair/deck platforms, 14 coloured ceiling lamps. Rebuilt 1:1 in metres (worldspawn
"_mpu" tells mapload.h the metres-per-unit, so it is NOT squashed to the usual 60 u),
dressed in the high-res Xonotic textures already in maps/textures/.

Usage: tools/grid2map.py [../chernobyl/levels/level0.txt] [maps/chernobyl.map]
"""
import math
import os
import sys

SRC = sys.argv[1] if len(sys.argv) > 1 else "../chernobyl/levels/level0.txt"
OUT = sys.argv[2] if len(sys.argv) > 2 else "maps/chernobyl.map"

U = 32.0                  # Quake units per metre
CELL = 4.0                # metres per grid cell (../chernobyl common.h)
WALL_H = 5.1              # metres (../chernobyl game.c)
COLS, ROWS = 30, 20

# The plant, in three zones across the hall (the layout itself is untouched): the turbine hall
# (machine-green dado, steel deck), the control corridor (blue dado, cable trays, floor tile) and
# the reactor side (scorched concrete, graphite dust). Textures are painted by
# tools/map/make_textures.py; (name, metres one repeat covers, its size in px).
WALL_PX = 1024
TEX = {
    "wall_t": ("chernobyl/wall_green", 5.1, 1024), "wall_c": ("chernobyl/wall_blue", 5.1, 1024), "wall_r": ("chernobyl/wall_scorched", 5.1, 1024),
    "wall_o": ("chernobyl/wall_concrete", 5.1, 1024),                 # the outer shell: bare concrete
    "floor_t": ("chernobyl/floor_plate", 4.0, 1024), "floor_c": ("chernobyl/floor_tile", 4.0, 1024), "floor_r": ("chernobyl/floor_ash", 4.0, 1024),
    "ceil": ("chernobyl/ceil_slab", 8.0, 1024), "ceil_r": ("chernobyl/ceil_scorched", 8.0, 1024),
    "deck":  ("chernobyl/floor_plate", 4.0, 1024),
    "edge":  ("chernobyl/hazard_edge", 2.0, 512),          # (broad stripes, two metres a repeat: the old fine ones crawled)
    "lamp_sodium": ("chernobyl/lamp_sodium", 0.9, 256), "lamp_tube": ("chernobyl/lamp_tube", 0.9, 256), "lamp_red": ("chernobyl/lamp_red", 0.9, 256), "lamp_fail": ("chernobyl/lamp_fail", 0.9, 256),
    "dead": ("chernobyl/fixture_dead", 0.9, 256),
    "pit": ("chernobyl/wall_scorched", 5.1, 1024), "core": ("chernobyl/glow_core", 14.0, 1024), "elena": ("chernobyl/elena", 7.0, 1024),
    "steel": ("chernobyl/steel_rust", 1.0, 256), "rubble": ("chernobyl/wall_concrete", 3.0, 1024),
    "console_body": ("chernobyl/console_body", 1.6, 512), "console_top": ("chernobyl/console_top", 1.0, 512),
    "console_top_az": ("chernobyl/console_top_az", 1.0, 512), "console_top_comm": ("chernobyl/console_top_comm", 1.0, 512),
    "valve": ("chernobyl/valve_red", 0.5, 128), "paper": ("chernobyl/paperwork", 0.42, 256),
    "hose": ("chernobyl/hose", 0.25, 128), "brass": ("chernobyl/brass", 0.25, 64), "redbox": ("chernobyl/valve_red", 0.5, 128),
    "hide":  ("common/caulk", 4.0, 512),
}
SIGN_SCALE = 1.35          # the plates are big: they have to read across a 5 m-high hall
ZONE_COLS = (10, 20)      # columns where the turbine hall ends and the reactor side begins
def zone(c): return "t" if c < ZONE_COLS[0] else ("c" if c < ZONE_COLS[1] else "r")

# level-0 decks + the stairs up to the centre deck (../chernobyl src/v2/level.c
# Level0Platforms): x0 z0 x1 z1 top, metres.
LEVEL0_PLATS = [(52, 38, 55, 42, 0.45), (55, 38, 58, 42, 0.90), (58, 38, 61, 42, 1.35),
                (61, 36, 69, 44, 1.35), (69, 38, 72, 42, 0.90), (72, 38, 75, 42, 0.45),
                (20, 24, 26, 28, 1.10), (92, 60, 104, 68, 1.60)]
# The 14 lamp positions are the original's (cell x, cell z); what hangs there is not. After the
# accident: sodium lamps still burning in the turbine hall, cold tubes in the control corridor,
# red emergency lamps on the reactor side -- and a third of them dead. (kind, rgb, radius m)
SODIUM, TUBE, RED, DEAD = ("lamp_sodium", (1.0, .70, .38), 34), ("lamp_tube", (.72, .84, 1.0), 34), ("lamp_red", (1.0, .13, .08), 30), ("dead", None, 0)
FAIL = ("lamp_fail", None, 26)      # a tube on its way out: its light is an "info_flicker" (baked apart, stuttered by the game)
LAMPS = [(5, 2, SODIUM), (14, 9, TUBE), (24, 5, RED), (5, 16, SODIUM), (24, 16, RED), (14, 17, TUBE),
         (2, 6, DEAD), (9, 9, SODIUM), (19, 9, FAIL), (27, 9, RED), (2, 13, SODIUM), (9, 13, FAIL),
         (20, 13, RED), (27, 13, DEAD), (14, 2, TUBE)]
# The breach: the east wall is GONE where the main corridor meets it (two wall cells), and beyond is
# the reactor pit -- the central hall with its roof blown off, the lid thrown on its edge across the
# shaft, the light of the core coming up from ten metres down. The stump of the wall and the rubble
# on it keep the player in the hall; everything past it is to look at. (cells; metres)
BREACH_CELLS = [(29, 9), (29, 10)]
PIT = dict(x0=120.0, x1=140.0, y0=24.0, y1=56.0, z0=-10.0, z1=15.0)
PIT_LIGHTS = [((0.32, 0.60, 1.0), 58, (129.0, 40.0, -5.0)), ((0.45, 0.70, 1.0), 26, (121.5, 40.0, 2.2))]
# Breaches in the hall's own floor: the same blue light, coming up through the plant from below. The enemies climb
# out of these (the game reads each as an "info_hole"). No two alike: (x, y, half x, half y, depth, style, seed,
# sides they can climb out over, (strength, tint, smoke, motes, shafts) of the live light, boss).
HOLES = [(14.0, 7.0, 1.5, 1.3, 3.5, "torn", 11, 3, (0.80, 0.30, 0.6, 0.5, 0.3), 0),
         (22.0, 19.5, 1.3, 1.3, 2.8, "sunk", 12, 2, (0.55, 0.10, 0.9, 0.2, 0.0), 0),
         (16.0, 51.0, 1.6, 1.6, 4.5, "punched", 13, 3, (1.00, 0.50, 0.2, 0.8, 0.7), 0),
         (30.0, 71.0, 2.4, 0.8, 3.0, "split", 14, 2, (0.70, 0.80, 0.4, 0.4, 1.0), 0),
         (10.0, 38.6, 1.3, 1.3, 3.2, "grate", 15, 2, (0.60, 0.20, 1.0, 0.3, 0.2), 0),
         (58.0, 7.0, 1.7, 1.4, 4.0, "sunk", 16, 3, (0.90, 0.60, 0.5, 0.9, 0.4), 0),
         (66.0, 26.0, 1.3, 1.5, 3.0, "grate", 17, 2, (0.50, 0.90, 0.3, 0.6, 0.8), 0),
         (60.0, 51.0, 1.5, 1.5, 5.0, "torn", 18, 3, (1.00, 0.40, 0.8, 0.7, 0.5), 0),
         (66.0, 71.5, 1.4, 1.4, 3.4, "punched", 19, 2, (0.65, 0.00, 0.5, 0.4, 0.1), 0),
         (108.6, 7.0, 1.5, 1.5, 3.6, "corner", 20, 2, (0.85, 0.70, 0.7, 0.6, 0.9), 0),
         (98.0, 20.0, 0.8, 2.0, 2.6, "split", 21, 2, (0.60, 0.35, 0.2, 1.0, 0.3), 0),
         (98.0, 51.0, 2.25, 2.25, 6.0, "torn", 22, 3, (1.00, 0.50, 1.0, 1.0, 1.0), 1),
         (106.0, 28.0, 1.6, 1.3, 3.8, "sunk", 23, 3, (0.75, 0.15, 0.9, 0.5, 0.6), 0)]
# Traps the player can set off (src/traps.h; the game reads each as an "info_trap"): (kind, x, y, turned (degrees), breach it vents).
# kind: 0 steam main, 1 live plate, 2 fire hose, 3 breach surge, 4 crane drop, 5 blast door, 6 coolant spill, 7 turbine.
TRAPS = [(0, 24.0, 40.0, 180, -1),     # steam across the main corridor, west of the stairs
         (1, 30.0, 52.0, 90, -1),      # the live plate in the turbine hall's south room
         (2, 52.0, 73.5, 90, -1),      # a fire point's hose down the south corridor
         (3, 60.0, 51.0, 90, 7),       # the control post that vents the breach in the control zone's south room
         (4, 30.0, 8.0, 180, -1),      # the crane over the turbine hall's north room
         (5, 50.0, 46.0, 90, -1),      # a blast door in the doorway south of the main corridor
         (6, 90.0, 29.0, 90, -1),      # the coolant tank on the reactor side
         (7, 10.0, 72.0, 180, -1)]     # the turbine at the west end of the south corridor, its mouth down the corridor
PANELS = []                # filled by station(): one point per control post
HOLE_SIDES = {"-x": ((-1, 0), (0, 1), 1), "+x": ((1, 0), (0, 1), 2), "-y": ((0, -1), (1, 0), 4), "+y": ((0, 1), (1, 0), 8)}
REACTOR_AT = (129.0, 40.0, -6.0)          # where the game's live glow, light and embers come from (info_reactor)
# Signs: enamelled plates on the walls. (texture, width m, height m), by zone; placed by a fixed
# seed on wall faces that look onto open floor, so a rebuilt map hangs them in the same places.
SIGNS = {"t": [("sign_turbine", 1.5, 0.5), ("sign_voltage", 0.8, 0.8), ("sign_fire", 0.8, 0.8), ("sign_exit", 1.5, 0.5), ("sign_block4", 1.0, 1.0)],
         "c": [("sign_control", 1.5, 0.5), ("sign_dosimetry", 0.9, 1.35), ("sign_reactor", 1.5, 0.5), ("sign_voltage", 0.8, 0.8), ("sign_exit", 1.5, 0.5), ("sign_block4", 1.0, 1.0)],
         "r": [("sign_radiation", 0.9, 0.9), ("sign_zone", 1.5, 0.5), ("sign_noentry", 1.5, 0.5), ("sign_radiation", 0.9, 0.9), ("sign_fire", 0.8, 0.8)]}
# Posters: paper pasted on the walls, for the Party, the plan and the peaceful atom. (width m, height m);
# on the reactor side what is left of them is burnt.
POSTER_W, N_POSTERS = 1.25, 64
# The real ones: reproductions of public-domain posters of 1919-1935 (tools/map/import_posters.py brings
# them in from Wikimedia Commons and writes their credits and sizes). Each hall gets them mixed with
# the painted ones -- the atom-age slogans the 1920s never printed.
import json as _json
_rp = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "maps", "textures", "chernobyl", "posters_real.json")
REAL_PX = {k: tuple(v) for k, v in (_json.load(open(_rp)).items() if os.path.exists(_rp) else [])}
_real = sorted(REAL_PX)
def _mix(real, painted):                 # two real posters to each painted one
    out, r, q = [], list(real), list(painted)
    while r or q:
        out += r[:2]; r = r[2:]
        if q: out.append(q.pop(0))
    return out
POSTERS = {"t": _mix(_real[0::2], ["poster_atom", "poster_labour", "poster_plan", "poster_party", "poster_may"]),
           "c": _mix(_real[1::2], ["poster_portrait", "poster_worker_atom", "poster_lenin", "poster_vigilance", "poster_science"]),
           "r": ["poster_atom_burnt", "poster_party_burnt", "poster_lenin_burnt", "poster_vigilance"]}
# Control stations: a desk against the wall with its upstand of meters, and the wall of instruments
# behind it -- the unit control room in the control corridor (a run of them, side by side: the
# turbine wall, the reactor's channel map, the alarms), and local posts out in the halls, some of
# them just a pair of relay cabinets. Placed like the signs: fixed seed, on walls that look onto floor.
MIMICS = ["mimic_turbine", "mimic_reactor", "mimic_elec"]            # the control room's three, in order
# The local posts. Each is its own kind of place, with its own wall of instruments, desk top,
# cabinets and nameplate (plate_NN: tools/map/make_textures.py NAMES) -- no two alike:
#   desk      a desk and its instrument wall            wing      the same with two angled wings
#   cabinets  three cubicles, doors as listed           skala     the unit computer: tape and lamp bays, a teleprinter desk
#   pump      a local valve station: pipes, handwheels, a gauge board
POSTS = {
    "t": [dict(kind="pump", plate=9), dict(kind="desk", mimic="mimic_drum", top="console_top", plate=7), dict(kind="cabinets", doors=("cabinet_breaker", "cabinet_breaker", "cabinet_face"), plate=6),
          dict(kind="wing", mimic="mimic_turbine", top="console_top_comm", plate=5), dict(kind="cabinets", doors=("cabinet_recorders", "cabinet_face", "cabinet_recorders"), plate=11),
          dict(kind="pump", plate=10)],
    "c": [dict(kind="skala", plate=4), dict(kind="wing", mimic="mimic_rods", top="console_top_az", plate=8), dict(kind="desk", mimic="mimic_grid", top="console_top_comm", plate=13),
          dict(kind="cabinets", doors=("cabinet_face", "cabinet_recorders", "cabinet_breaker"), plate=12), dict(kind="desk", mimic="mimic_dosimetry", top="console_top", plate=3)],
    "r": [dict(kind="desk", mimic="mimic_pumps", top="console_top_az", plate=2), dict(kind="cabinets", doors=("cabinet_breaker", "cabinet_face", "cabinet_breaker"), plate=6),
          dict(kind="desk", mimic="mimic_dosimetry", top="console_top_comm", plate=3), dict(kind="pump", plate=9), dict(kind="wing", mimic="mimic_reactor", top="console_top_az", plate=1)],
}
SIGN_PX = {"mimic_reactor": (1024, 640), "mimic_turbine": (1024, 640), "mimic_elec": (1024, 640), "console_gauges": (1024, 128),
           "mimic_pumps": (1024, 640), "mimic_rods": (1024, 640), "mimic_grid": (1024, 640), "mimic_dosimetry": (1024, 640), "mimic_drum": (1024, 640),
           "cabinet_breaker": (256, 640), "cabinet_recorders": (256, 640), "rack_tape": (256, 640), "rack_lamps": (256, 640),
           **{f"plate_{k:02d}": (512, 96) for k in range(14)}, "hose_cabinet": (256, 256), "fuse_board": (256, 340),
           "cabinet_face": (256, 640), "lamps_glow": (512, 40), "sign_radiation": (512, 512), "sign_block4": (512, 512), "sign_voltage": (512, 512), "sign_fire": (512, 512), "sign_dosimetry": (512, 768),
           "sign_exit": (768, 256), "sign_reactor": (768, 256), "sign_turbine": (768, 256), "sign_control": (768, 256), "sign_noentry": (768, 256), "sign_zone": (768, 256)}


def read_level(path):
    grid, plats, spawn, face = [], [], None, 0.0
    for line in open(path):
        line = line.rstrip("\n")
        if not line or line.startswith(";"):
            continue
        if line[0] in "#.sj":
            row = line[:COLS].ljust(COLS, ".")
            if "s" in row and spawn is None:
                spawn = (row.index("s"), len(grid))
            grid.append(["#" if ch == "#" else "." for ch in row])
        elif line.startswith("face:"):
            face = float(line[5:])
        elif line.startswith("plat:"):
            plats.append(tuple(float(v) for v in line[5:].split()[:5]))
    assert len(grid) == ROWS, f"expected {ROWS} rows, got {len(grid)}"
    return grid, plats, spawn, face


def face_line(p0, p1, p2, tex, fit=None):
    """fit = (shiftx, shifty, scalex, scaley): an exact mapping (signs); else the texture's own scale."""
    f = lambda p: "( %g %g %g )" % p
    if fit:
        return f"{f(p0)} {f(p1)} {f(p2)} chernobyl/{tex} {fit[0]:.4f} {fit[1]:.4f} 0 {fit[2]:.6f} {fit[3]:.6f}"
    name, metres, px = TEX[tex]
    sc = metres * U / px
    return f"{f(p0)} {f(p1)} {f(p2)} {name} 0 0 0 {sc:g} {sc:g}"


def _rot(yaw, pitch, roll):
    """Local axes (ex, ey, ez) after turning yaw about z, then pitch about y, then roll about x (degrees)."""
    cy, sy, cp, sp, cr, sr = (f(math.radians(a)) for a in (yaw, pitch, roll) for f in (math.cos, math.sin))
    cy, sy, cp, sp, cr, sr = math.cos(math.radians(yaw)), math.sin(math.radians(yaw)), math.cos(math.radians(pitch)), math.sin(math.radians(pitch)), math.cos(math.radians(roll)), math.sin(math.radians(roll))
    ex = (cy * cp, sy * cp, -sp)
    ey = (cy * sp * sr - sy * cr, sy * sp * sr + cy * cr, cp * sr)
    ez = (cy * sp * cr + sy * sr, sy * sp * cr - cy * sr, cp * cr)
    return ex, ey, ez


def _add(p, *terms): return tuple(p[i] + sum(k * v[i] for k, v in terms) for i in range(3))
def _q(p): return tuple(round(v * U, 3) for v in p)


def _plane(O, u, v, tex):                 # the brush plane through O spanned by u, v (outward normal = -(u x v), as box() winds them)
    return face_line(_q(O), _q(_add(O, (1.0, u))), _q(_add(O, (1.0, v))), tex)


def obox(c, half, yaw=0.0, pitch=0.0, roll=0.0, tex="rubble"):
    """A box at any angle: centre c, half-sizes, turned by yaw / pitch / roll. tex: one name, or a dict per local side."""
    ex, ey, ez = _rot(yaw, pitch, roll); hx, hy, hz = half
    t = tex if isinstance(tex, dict) else {k: tex for k in ("-x", "+x", "-y", "+y", "-z", "+z")}
    lo = _add(c, (-hx, ex), (-hy, ey), (-hz, ez)); hi = _add(c, (hx, ex), (hy, ey), (hz, ez))
    L = [_plane(lo, ey, ez, t["-x"]), _plane(lo, ez, ex, t["-y"]), _plane(lo, ex, ey, t["-z"]),
         _plane(hi, ey, ex, t["+z"]), _plane(hi, ex, ez, t["+y"]), _plane(hi, ez, ey, t["+x"])]
    return "{\n" + "\n".join(L) + "\n}\n"


def prism(c, radius, half_t, n, yaw, pitch, roll, top, side):
    """An n-sided disc (the reactor lid), axis along its local z."""
    ex, ey, ez = _rot(yaw, pitch, roll)
    L = [_plane(_add(c, (half_t, ez)), ey, ex, top), _plane(_add(c, (-half_t, ez)), ex, ey, side)]
    for k in range(n):
        a = 2 * math.pi * k / n; nrm = _add((0, 0, 0), (math.cos(a), ex), (math.sin(a), ey)); tan = _add((0, 0, 0), (-math.sin(a), ex), (math.cos(a), ey))
        L.append(_plane(_add(c, (radius * math.cos(math.pi / n), nrm)), ez, tan, side))
    return "{\n" + "\n".join(L) + "\n}\n"


def station(out, grid_face, wall, mid, spec, seed=0, width=4.0):
    """One control post against a wall. grid_face: the way it faces ('+y' = out toward +y); wall: the
    wall plane's coordinate; mid: its centre along the wall; spec: one of POSTS (or the control room's)."""
    import random
    rnd = random.Random(1000 + seed); kind = spec["kind"]
    sg = 1.0 if grid_face[0] == "+" else -1.0; alongx = grid_face[1] == "y"
    def W(u, v, z): return (mid + u, wall + sg * v, z) if alongx else (wall + sg * v, mid + u, z)          # the post's own frame -> the map
    PANELS.append(W(0.0, 0.7, 1.3))                      # where it is heard from (the game plays a control-panel sound near each: "info_panel")
    def lbox(u0, u1, v0, v1, z0, z1, tex):               # a box in the post's frame: u along the wall, v out from it
        a0, a1 = mid + u0, mid + u1; b0, b1 = sorted((wall + sg * v0, wall + sg * v1))
        return box(a0, b0, z0, a1, b1, z1, tex) if alongx else box(b0, a0, z0, b1, a1, z1, tex)
    def lobox(u, v, z, hu, hv, hz, yaw=0.0, tilt=0.0, tex="console_body"):                                 # the same, turned: yaw about z, tilt = sloping down toward the room
        half = (hu, hv, hz) if alongx else (hv, hu, hz)
        wyaw = yaw * (sg if alongx else -sg)
        return obox(W(u, v, z), half, wyaw, (sg * tilt if not alongx else 0.0), (-sg * tilt if alongx else 0.0), tex)
    plate = lambda u0, u1, z0, z1, v, tex, th: sign(grid_face, mid + u0, mid + u1, z0, z1, wall + sg * v, tex, thick=th)
    lamps = lambda u0, u1, z0, v: plate(u0, u1, z0, z0 + (u1 - u0) * 40 / 512, v, "lamps_glow", 0.03)
    top_of = lambda t: dict({k: "console_body" for k in ("-x", "+x", "-y", "+y", "-z")}, **{"+z": t})
    hw = width / 2
    def desk(u0, u1, top, yaw=0.0, v0=0.0):
        uc, hu = (u0 + u1) / 2, (u1 - u0) / 2
        if yaw == 0.0: out.append(lbox(u0, u1, v0, v0 + 0.86, 0.0, 0.74, "console_body"))
        else: out.append(lobox(uc, v0 + 0.43, 0.37, hu, 0.43, 0.37, yaw))
        out.append(lobox(uc, v0 + 0.50, 0.80, hu, 0.45, 0.035, yaw, 13.0, top_of(top)))                    # the top, sloped down toward whoever sits at it
    def chair(u, v, toppled):
        yw = rnd.uniform(-40, 40)
        if toppled:                                                                                       # on its side where it was kicked over
            out.append(lobox(u, v, 0.22, 0.21, 0.03, 0.22, yw + 90, 0.0, "console_body")); out.append(lobox(u + 0.30, v + 0.05, 0.05, 0.22, 0.21, 0.03, yw, 0.0, "console_body"))
        else:
            out.append(lobox(u, v, 0.23, 0.04, 0.04, 0.23, yw, 0.0, "steel")); out.append(lobox(u, v, 0.48, 0.22, 0.22, 0.03, yw, 0.0, "console_body"))
            out.append(lobox(u, v + 0.21, 0.74, 0.22, 0.03, 0.24, yw, 0.0, "console_body"))
    def clutter(u0, u1):                                                                                  # what was left on the desk: the log, a telephone
        if rnd.random() < 0.8: out.append(lobox(rnd.uniform(u0 + 0.4, u1 - 0.4), 0.62, 0.800, 0.21, 0.15, 0.012, rnd.uniform(-25, 25), 13.0, top_of("paper")))
        if rnd.random() < 0.7:
            u = rnd.uniform(u0 + 0.3, u1 - 0.3); out.append(lobox(u, 0.36, 0.895, 0.11, 0.08, 0.035, rnd.uniform(-20, 20), 13.0, "steel")); out.append(lobox(u, 0.36, 0.945, 0.12, 0.025, 0.022, rnd.uniform(-30, 30), 13.0, "steel"))
    nameplate = lambda z, v: out.append(plate(-0.55, 0.55, z, z + 1.1 * 96 / 512, v, f"plate_{spec['plate']:02d}", 0.03)) if "plate" in spec else None
    if kind in ("desk", "wing"):
        dw = hw if kind == "desk" else 1.25
        desk(-dw, dw, spec.get("top", "console_top"))
        if kind == "wing":                                                                                # two wings turned in toward the operator
            desk(-dw - 1.08, -dw + 0.06, spec.get("top", "console_top"), yaw=-32.0, v0=0.30); desk(dw - 0.06, dw + 1.08, spec.get("top", "console_top"), yaw=32.0, v0=0.30)
        out.append(lbox(-dw, dw, 0.0, 0.16, 0.74, 1.30, "console_body"))                                  # the upstand behind it...
        out.append(plate(-dw + 0.04, dw - 0.04, 0.86, 0.86 + (2 * dw - 0.08) * 128 / 1024 if kind == "wing" else 1.26, 0.16, "console_gauges", 0.02))   # ...and its meters
        out.append(lamps(-0.9, 0.9, 1.275, 0.16))
        mh = (width - 0.2) * 640 / 1024
        out.append(plate(-hw + 0.1, hw - 0.1, 1.52, 1.52 + mh, 0.0, spec.get("mimic", "mimic_reactor"), 0.10))   # the wall of instruments
        out.append(lamps(-1.2, 1.2, 1.44, 0.10)); nameplate(1.52 + mh + 0.06, 0.0)
        clutter(-dw, dw); chair(rnd.uniform(-0.5, 0.5), rnd.uniform(1.25, 1.55), rnd.random() < 0.45)
    elif kind == "cabinets":
        for k, door in zip((-1, 0, 1), spec.get("doors", ("cabinet_face",) * 3)):                           # three cubicles, shoulder to shoulder
            hgt = 2.05 + (0.12 if k == 0 else 0.0)
            out.append(lbox(k * 0.86 - 0.41, k * 0.86 + 0.41, 0.0, 0.56, 0.0, hgt, "console_body"))
            out.append(plate(k * 0.86 - 0.39, k * 0.86 + 0.39, 0.06, 2.01, 0.56, door, 0.02))
        out.append(lamps(-1.0, 1.0, 2.20, 0.30)); nameplate(2.42, 0.0)
        out.append(lbox(-1.6, 1.6, 0.0, 0.10, 2.9, 3.05, "steel"))                                         # the cable tray they are fed from, and its drops
        for k in (-1, 0, 1): out.append(lbox(k * 0.86 - 0.04, k * 0.86 + 0.04, 0.03, 0.10, 2.05, 2.9, "steel"))
    elif kind == "skala":
        for k, door in zip((-2, -1, 0, 1), ("rack_tape", "rack_lamps", "rack_tape", "rack_lamps")):        # the unit computer: four bays of it
            out.append(lbox(k * 0.80 - 0.02, k * 0.80 + 0.78, 0.0, 0.62, 0.0, 2.10, "console_body"))
            out.append(plate(k * 0.80, k * 0.80 + 0.76, 0.08, 2.06, 0.62, door, 0.02))
        out.append(lamps(-1.5, 1.5, 2.14, 0.40)); nameplate(2.34, 0.0)
        out.append(lobox(1.25, 1.45, 0.37, 0.55, 0.36, 0.37, -18.0))                                       # the teleprinter's table, pulled round to face the bays
        out.append(lobox(1.25, 1.45, 0.80, 0.32, 0.24, 0.07, -18.0, 0.0, "steel")); out.append(lobox(1.25, 1.55, 0.745, 0.21, 0.15, 0.008, -5.0, 0.0, top_of("paper")))
        chair(1.2, 2.05, rnd.random() < 0.5)
    elif kind == "pump":
        # a local valve station: two pipes along the wall, a branch down from each, handwheels, a board of gauges
        for z_, r_ in ((2.55, 0.16), (1.95, 0.11)):
            c_ = W(0.0, 0.34, z_); out.append(prism(c_, r_, hw, 10, 0.0 if alongx else 90.0, 90.0, 0.0, "steel", "steel"))
            for u in (-1.2, 0.0, 1.2): out.append(lbox(u - 0.05, u + 0.05, 0.0, 0.34, z_ - 0.03, z_ + 0.03, "steel"))            # brackets back to the wall
            for u in (-hw + 0.5, hw - 0.5): out.append(prism(W(u, 0.34, z_), r_ + 0.05, 0.05, 10, 0.0 if alongx else 90.0, 90.0, 0.0, "steel", "steel"))   # flanges
        for u, z0 in ((-0.9, 1.95), (0.8, 1.95)):
            out.append(prism(W(u, 0.34, (z0 + 0.0) / 2), 0.09, z0 / 2, 10, 0.0, 0.0, 0.0, "steel", "steel"))                         # the drop to the floor
            out.append(prism(W(u, 0.34, 1.05), 0.14, 0.12, 10, 0.0, 0.0, 0.0, "steel", "steel"))                                     # the valve body
            out.append(lbox(u - 0.02, u + 0.02, 0.34, 0.78, 1.03, 1.07, "steel"))                                                   # its stem...
            out.append(prism(W(u, 0.78, 1.05), 0.24, 0.022, 14, 0.0 if not alongx else 90.0, 90.0, 0.0, "valve", "valve"))          # ...and the handwheel, facing the room
        out.append(lbox(-0.55, 0.55, 0.0, 0.10, 1.25, 1.75, "console_body")); out.append(plate(-0.52, 0.52, 1.27, 1.27 + 1.04 * 128 / 1024, 0.10, "console_gauges", 0.02))
        nameplate(1.50, 0.10); out.append(lamps(-0.4, 0.4, 1.66, 0.10))


def tube(p0, p1, r, tex, n=8):
    """A round bar from p0 to p1 (pipe, hose, cable)."""
    d = tuple(b - a for a, b in zip(p0, p1)); L = math.sqrt(sum(v * v for v in d))
    if L < 1e-4: return ""
    elev = math.degrees(math.asin(max(-1.0, min(1.0, d[2] / L))))
    return prism(tuple((a + b) / 2 for a, b in zip(p0, p1)), r, L / 2, n, math.degrees(math.atan2(d[1], d[0])), 90.0 - elev, 0.0, tex, tex)


def hose_line(out, pts, r=0.038):
    """A canvas fire hose lying where it was run out: a chain of short lengths through pts (on the
    floor: it is under the height the player's body is tested at, so it is walked over), a brass
    coupling every few metres."""
    run = 0.0
    for a, b in zip(pts, pts[1:]):
        out.append(tube((a[0], a[1], r), (b[0], b[1], r), r, "hose", 7)); L = math.hypot(b[0] - a[0], b[1] - a[1])
        if int((run + L) / 6.0) != int(run / 6.0): out.append(tube((a[0], a[1], r), (a[0] + (b[0] - a[0]) * 0.22, a[1] + (b[1] - a[1]) * 0.22, r), r * 1.5, "brass", 8))
        run += L


def breach(out):
    """Everything past the east wall. Returns how many brushes it added."""
    import random
    rnd = random.Random(4); n0 = len(out); P = PIT
    ya, yb = min(r for _, r in BREACH_CELLS) * CELL, (max(r for _, r in BREACH_CELLS) + 1) * CELL        # the opening's run along the wall
    xw = BREACH_CELLS[0][0] * CELL                                                                    # the wall line (its hall face)
    inner = lambda face: dict({k: "hide" for k in ("-x", "+x", "-y", "+y", "-z", "+z")}, **{face: "pit"})
    # the shaft: floor = the core's light, four walls, and the hall's own east face above and below its floor
    out.append(box(P["x0"], P["y0"], P["z0"] - 1, P["x1"], P["y1"], P["z0"], dict(inner("+z"), **{"+z": "core"})))
    out.append(box(P["x0"], P["y0"] - 1, P["z0"], P["x1"], P["y0"], P["z1"], inner("+y")))
    out.append(box(P["x0"], P["y1"], P["z0"], P["x1"], P["y1"] + 1, P["z1"], inner("-y")))
    out.append(box(P["x1"], P["y0"] - 1, P["z0"], P["x1"] + 1, P["y1"] + 1, P["z1"], inner("-x")))
    out.append(box(P["x0"] - 1, P["y0"], P["z0"], P["x0"], P["y1"], -0.5, inner("+x")))
    out.append(box(P["x0"] - 1, P["y0"], WALL_H + 0.5, P["x0"], P["y1"], P["z1"], inner("+x")))
    # what is left of the roof: slabs hanging in over the shaft, sky between them
    for k in range(6):
        out.append(obox((rnd.uniform(P["x0"] + 2, P["x1"] - 2), P["y0"] + 3 + k * 5.2, P["z1"] - rnd.uniform(0.5, 2.5)), (rnd.uniform(4, 7), rnd.uniform(1.6, 2.6), 0.35),
                        rnd.uniform(-25, 25), rnd.uniform(-22, 22), rnd.uniform(-18, 18), "pit"))
    # the lid, on its edge across the shaft, and the fuel channels torn out with it
    lc, R, ht, (yw, pt, rl) = (129.5, 40.0, -1.2), 7.0, 0.75, (14.0, -34.0, 9.0)
    out.append(prism(lc, R, ht, 14, yw, pt, rl, "elena", "steel"))
    ex, ey, ez = _rot(yw, pt, rl)
    for k in range(46):
        a, rr = rnd.uniform(0, 6.283), R * math.sqrt(rnd.uniform(0.02, 0.8)); L = rnd.uniform(1.2, 4.2)
        base = _add(lc, (rr * math.cos(a), ex), (rr * math.sin(a), ey), (ht, ez))
        d = _add(ez, (rnd.uniform(-0.45, 0.45), ex), (rnd.uniform(-0.45, 0.45), ey)); dl = math.sqrt(sum(v * v for v in d)); d = tuple(v / dl for v in d)
        yaw_ = math.degrees(math.atan2(d[1], d[0])); pitch_ = -math.degrees(math.asin(max(-1, min(1, d[2]))))   # local x along the tube
        out.append(obox(_add(base, (L / 2, d)), (L / 2, 0.055, 0.055), yaw_, pitch_, rnd.uniform(0, 90), "steel"))
    # the stump of the wall across the opening -- too high to climb -- and the rubble heaped on and behind it
    out.append(box(xw, ya, 0, xw + 1.1, yb, 1.25, {"-x": "pit", "+x": "pit", "-y": "hide", "+y": "hide", "-z": "hide", "+z": "pit"}))
    for layer in range(2):                                                                            # broken lumps along its top: low, so the pit is seen over them
        y = ya + 0.15
        while y < yb - 0.2:
            w = rnd.uniform(0.35, 0.85); hgt = rnd.uniform(0.08, 0.26) * (1.0 if layer == 0 else 0.7)
            if rnd.random() < 0.8:
                out.append(obox((xw + rnd.uniform(0.2, 0.9), y + w / 2, 1.25 + hgt * 0.7 + layer * 0.18), (rnd.uniform(0.18, 0.42), w / 2, hgt), rnd.uniform(-30, 30), rnd.uniform(-28, 28), rnd.uniform(-28, 28), "pit"))
            y += w * rnd.uniform(0.7, 1.2)
    for k in range(34):                                                                               # slabs and blocks strewn on the ledge behind it, toward the edge
        big = k % 6 == 0
        out.append(obox((rnd.uniform(xw + 1.4, P["x0"] + 0.6), rnd.uniform(ya + 0.3, yb - 0.3), rnd.uniform(0.08, 0.35) + (0.25 if big else 0)),
                        (rnd.uniform(0.7, 1.3) if big else rnd.uniform(0.15, 0.55), rnd.uniform(0.5, 0.9) if big else rnd.uniform(0.12, 0.45), rnd.uniform(0.10, 0.22) if big else rnd.uniform(0.06, 0.25)),
                        rnd.uniform(0, 180), rnd.uniform(-35, 35), rnd.uniform(-35, 35), "pit"))
    # the torn edges of the hole: lumps left standing on the jambs and hanging from the lintel
    for side, ye in ((1, ya), (-1, yb)):
        for k in range(6):
            z = 1.0 + k * 0.72 + rnd.uniform(-0.2, 0.2); reach = rnd.uniform(0.25, 1.15) * (1.0 if k not in (2, 3) else 0.5)
            out.append(obox((xw + rnd.uniform(1.2, 3.0), ye + side * reach * 0.5, z), (rnd.uniform(0.5, 1.0), reach * 0.7, rnd.uniform(0.3, 0.55)), rnd.uniform(-20, 20), rnd.uniform(-25, 25), rnd.uniform(-25, 25), "pit"))
    for k in range(7):
        yy = ya + 0.6 + k * (yb - ya - 1.2) / 6; drop = rnd.uniform(0.2, 1.0)
        out.append(obox((xw + rnd.uniform(1.0, 3.0), yy, WALL_H - drop * 0.5), (rnd.uniform(0.5, 1.0), rnd.uniform(0.4, 0.8), drop * 0.6), rnd.uniform(-20, 20), rnd.uniform(-25, 25), rnd.uniform(-20, 20), "pit"))
    # reinforcing steel, bent out of the broken concrete
    for k in range(20):
        fromtop = k % 2 == 0; L = rnd.uniform(0.9, 2.8)
        b0 = (xw + rnd.uniform(0.6, 3.4), rnd.uniform(ya + 0.2, yb - 0.2), WALL_H - 0.1) if fromtop else (xw + rnd.uniform(0.6, 3.4), ya + 0.1 if k % 4 == 1 else yb - 0.1, rnd.uniform(1.6, 4.6))
        d = (rnd.uniform(-0.5, 0.7), rnd.uniform(-0.6, 0.6) if fromtop else (0.8 if k % 4 == 1 else -0.8), -rnd.uniform(0.5, 1.0) if fromtop else rnd.uniform(-0.6, 0.2))
        dl = math.sqrt(sum(v * v for v in d)); d = tuple(v / dl for v in d)
        out.append(obox(_add(b0, (L / 2, d)), (L / 2, 0.022, 0.022), math.degrees(math.atan2(d[1], d[0])), -math.degrees(math.asin(d[2])), 0, "steel"))
    return len(out) - n0


def cut_rects(rect, holes):
    """The floor slab `rect` (x0, y0, x1, y1) as strips round the openings in it."""
    rects = [rect]
    for (cx, cy, hx, hy, *_r) in holes:
        a0, b0, a1, b1 = cx - hx, cy - hy, cx + hx, cy + hy; nxt = []
        for (x0, y0, x1, y1) in rects:
            if a0 >= x1 or a1 <= x0 or b0 >= y1 or b1 <= y0: nxt.append((x0, y0, x1, y1)); continue
            if x0 < a0: nxt.append((x0, y0, a0, y1))
            if a1 < x1: nxt.append((a1, y0, x1, y1))
            if y0 < b0: nxt.append((max(x0, a0), y0, min(x1, a1), b0))
            if b1 < y1: nxt.append((max(x0, a0), b1, min(x1, a1), y1))
        rects = nxt
    return rects


def hole(out, h, edges, ftex):
    """One breach in the floor: the shaft, the light at the bottom of it, and its own broken rim. `edges` = the sides
    left clean for a climber (a straight lip, nothing standing on it); the others take the wreckage. Returns brushes added."""
    import random
    cx, cy, hx, hy, dp, style, seed = h[:7]; rnd = random.Random(seed); n0 = len(out)
    hide = {k: "hide" for k in ("-x", "+x", "-y", "+y", "-z", "+z")}; inner = lambda face: dict(hide, **{face: "pit"})
    out.append(box(cx - hx, cy - hy, -dp - 0.4, cx + hx, cy + hy, -dp, dict(hide, **{"+z": "core"})))
    out.append(box(cx - hx - 0.4, cy - hy, -dp, cx - hx, cy + hy, 0, inner("+x")))
    out.append(box(cx + hx, cy - hy, -dp, cx + hx + 0.4, cy + hy, 0, inner("-x")))
    out.append(box(cx - hx - 0.4, cy - hy - 0.4, -dp, cx + hx + 0.4, cy - hy, 0, inner("+y")))
    out.append(box(cx - hx - 0.4, cy + hy, -dp, cx + hx + 0.4, cy + hy + 0.4, 0, inner("-y")))
    for k in range(rnd.randint(3, 6)):                                             # what fell in: lumps on the bottom, half in the light
        out.append(obox((cx + rnd.uniform(-hx, hx) * 0.8, cy + rnd.uniform(-hy, hy) * 0.8, -dp + rnd.uniform(0.05, 0.3)),
                        (rnd.uniform(0.15, 0.45), rnd.uniform(0.15, 0.4), rnd.uniform(0.1, 0.3)), rnd.uniform(0, 180), rnd.uniform(-30, 30), rnd.uniform(-30, 30), "pit"))
    closed = [s for s in HOLE_SIDES if s not in edges]
    def geo(side): n, t, _b = HOLE_SIDES[side]; return n, t, (hx if side[1] == "x" else hy), (hy if side[1] == "x" else hx), math.degrees(math.atan2(n[1], n[0]))
    def P(side, along, outd, z): n, t, half, _l, _y = geo(side); return (cx + n[0] * (half + outd) + t[0] * along, cy + n[1] * (half + outd) + t[1] * along, z)
    def cracks(side, k):                                                           # hairlines running on out across the floor from the corners
        _n, _t, _h, L, yaw = geo(side)
        for _ in range(k):
            ln = rnd.uniform(0.5, 1.6); a = rnd.uniform(-L, L)
            out.append(obox(P(side, a, ln * 0.5, 0.003), (ln * 0.5, rnd.uniform(0.012, 0.035), 0.004), yaw + rnd.uniform(-35, 35), 0, 0, "pit"))
    def rebar(side, k, reach=(0.3, 0.9)):
        _n, _t, _h, L, _yaw = geo(side)
        for _ in range(k):
            a = rnd.uniform(-L, L) * 0.92; r = rnd.uniform(*reach)
            out.append(tube(P(side, a, 0.02, -rnd.uniform(0.08, 0.3)), P(side, a + rnd.uniform(-0.25, 0.25), -r, rnd.uniform(-0.55, 0.28)), rnd.uniform(0.011, 0.02), "steel", 5))
    def slabs_in(side, k, tex, drop=(0.18, 0.4)):                                  # floor that broke and hangs into the shaft
        _n, _t, _h, L, yaw = geo(side)
        for i in range(k):
            w = L / k; a = -L + w * (2 * i + 1) + rnd.uniform(-0.1, 0.1); ln = rnd.uniform(0.35, 0.7); tilt = rnd.uniform(18, 42)
            c = P(side, a, -ln * math.cos(math.radians(tilt)), -ln * math.sin(math.radians(tilt)) - 0.07)
            out.append(obox(c, (ln, w * rnd.uniform(0.7, 0.96), 0.07), yaw + rnd.uniform(-7, 7), -tilt, rnd.uniform(-9, 9), {"-x": "pit", "+x": "pit", "-y": "pit", "+y": "pit", "-z": "pit", "+z": tex}))
    for s_ in HOLE_SIDES:                                                          # every lip is chipped and cracked, and has grit lying off it (all of it walked over)
        _n, _t, _h, L, yaw = geo(s_); cracks(s_, rnd.randint(1, 2))
        for _ in range(rnd.randint(3, 6)):
            sz = rnd.uniform(0.025, 0.06)
            out.append(obox(P(s_, rnd.uniform(-L, L) * 1.1, rnd.uniform(0.25, 1.5), sz * 0.7), (sz * rnd.uniform(1.0, 2.2), sz * rnd.uniform(0.8, 1.6), sz), rnd.uniform(0, 180), rnd.uniform(-20, 20), rnd.uniform(-20, 20), rnd.choice(("rubble", "pit"))))
    if style == "punched":                                                         # blown through from below: the floor's own sheet peeled up and back
        for s_ in closed:
            _n, _t, _h, L, yaw = geo(s_); k = rnd.randint(2, 4)
            for i in range(k):
                w = L / k; a = -L + w * (2 * i + 1); ln = rnd.uniform(0.3, 0.55); tilt = rnd.uniform(14, 34)
                c = P(s_, a, ln * math.cos(math.radians(tilt)) - 0.04, ln * math.sin(math.radians(tilt)) + 0.012)
                out.append(obox(c, (ln, w * rnd.uniform(0.6, 0.95), 0.014), yaw + rnd.uniform(-10, 10), tilt, rnd.uniform(-12, 12), {"-x": "steel", "+x": "steel", "-y": "steel", "+y": "steel", "-z": "pit", "+z": ftex}))
            rebar(s_, rnd.randint(1, 3), (0.15, 0.4))
    elif style == "torn":                                                          # ripped: bars fanned out over the drop, slabs hanging off them
        for s_ in closed:
            rebar(s_, rnd.randint(6, 11)); slabs_in(s_, rnd.randint(1, 2), ftex); cracks(s_, rnd.randint(1, 3))
        for s_ in edges: rebar(s_, rnd.randint(0, 2), (0.05, 0.2))
    elif style == "split":                                                         # the floor parted along a line; it runs on past both ends
        for s_ in closed:
            _n, _t, _h, L, yaw = geo(s_)
            for i in range(3):
                out.append(obox(P(s_, rnd.uniform(-L, L) * 0.3, -0.25 - 0.3 * i, -0.12 - 0.3 * i), (0.22, L * rnd.uniform(0.8, 1.0), 0.09), yaw + rnd.uniform(-6, 6), rnd.uniform(-14, 4), rnd.uniform(-8, 8), {"-x": "pit", "+x": "pit", "-y": "pit", "+y": "pit", "-z": "pit", "+z": ftex}))
            ln = rnd.uniform(2.2, 3.6); x_, y_, _z = P(s_, 0, 0, 0); a_ = math.radians(yaw)
            for i in range(5):                                                     # the crack carrying on, narrowing, wandering
                a_ += rnd.uniform(-0.3, 0.3); sg = ln / 5
                out.append(obox((x_ + math.cos(a_) * sg / 2, y_ + math.sin(a_) * sg / 2, 0.003), (sg / 2 + 0.02, 0.09 * (1 - i / 5.5), 0.004), math.degrees(a_), 0, 0, "pit"))
                x_ += math.cos(a_) * sg; y_ += math.sin(a_) * sg
    elif style == "corner":                                                        # by the wall: what came down is heaped on the far sides
        for s_ in closed:
            _n, _t, _h, L, yaw = geo(s_)
            for _ in range(rnd.randint(5, 9)):
                sz = rnd.uniform(0.1, 0.24)
                out.append(obox(P(s_, rnd.uniform(-L, L), rnd.uniform(0.2, 1.0), sz * 0.6), (sz * rnd.uniform(1.0, 1.9), sz * rnd.uniform(0.8, 1.5), sz), rnd.uniform(0, 180), rnd.uniform(-25, 25), rnd.uniform(-25, 25), rnd.choice(("rubble", "pit"))))
            rebar(s_, rnd.randint(2, 4)); cracks(s_, 2)
    elif style == "sunk":                                                          # undermined: the floor round it has dropped and tipped in
        for s_ in closed:
            slabs_in(s_, rnd.randint(2, 3), ftex); cracks(s_, rnd.randint(2, 4))
        for s_ in edges: cracks(s_, 1)
    elif style == "grate":                                                         # a floor grating over a service duct, burst upward
        for s_ in HOLE_SIDES:
            _n, _t, _h, L, yaw = geo(s_)
            out.append(obox(P(s_, 0, 0.05, 0.008), (0.05, L + 0.1, 0.008), yaw, 0, 0, "steel"))
        for s_ in closed:
            _n, _t, _h, L, yaw = geo(s_); k = rnd.randint(5, 8)
            for i in range(k):
                a = -L + (2 * L) * (i + 0.5) / k; ln = rnd.uniform(0.15, 0.40); tilt = rnd.uniform(-10, 32)      # (ln = HALF its length: nothing here may stand over 0.7 m, or the player's body catches on it)
                c = P(s_, a, -ln * math.cos(math.radians(tilt)), ln * math.sin(math.radians(tilt)) + 0.02)
                out.append(obox(c, (ln, 0.022, 0.022), yaw + rnd.uniform(-9, 9), tilt, rnd.uniform(0, 45), "steel"))
    return len(out) - n0


def sign(face, a0, a1, z0, z1, wall, tex, thick=None):
    """A plate a few cm proud of a wall. face: which way it looks ('-x','+x','-y','+y'); a0..a1 its run
    along the wall (m), z0..z1 its height, wall = the wall plane's coordinate. The picture is fitted
    to the plate exactly: UV = (coord / scale + shift) / size, u along the wall, v = -z, and the two
    faces that would read mirrored get a negative u scale."""
    w_px, h_px = SIGN_PX.get(tex) or REAL_PX.get(tex) or (512, 768); t = thick if thick else (0.045 if tex.startswith("sign") else 0.025)     # (posters: 512 x 768 unless listed, and thin -- but not so thin they fight the wall for depth)
    su = (a1 - a0) * U / w_px; sv = (z1 - z0) * U / h_px
    mirror = face in ("-y", "+x")              # (the game swaps Quake's y and z, which flips handedness: the other pair than in Quake itself)
    fit = ((a1 * U / su) if mirror else (-(a0 * U) / su), (z1 * U) / sv, -su if mirror else su, sv)
    if face[1] == "x": x0, x1, y0, y1 = (wall, wall + t, a0, a1) if face == "+x" else (wall - t, wall, a0, a1)
    else:              x0, x1, y0, y1 = (a0, a1, wall, wall + t) if face == "+y" else (a0, a1, wall - t, wall)
    A = tuple(round(v * U, 3) for v in (x0, y0, z0)); B = tuple(round(v * U, 3) for v in (x1, y1, z1))
    (X0, Y0, Z0), (X1, Y1, Z1) = A, B
    ft = lambda side: fit if side == face else None
    tx = lambda side: tex if side == face else "hide"
    L = [face_line((X0, Y0, Z0), (X0, Y0 + 1, Z0), (X0, Y0, Z0 + 1), tx("-x"), ft("-x")), face_line((X0, Y0, Z0), (X0, Y0, Z0 + 1), (X0 + 1, Y0, Z0), tx("-y"), ft("-y")),
         face_line((X0, Y0, Z0), (X0 + 1, Y0, Z0), (X0, Y0 + 1, Z0), "hide"), face_line((X1, Y1, Z1), (X1, Y1 + 1, Z1), (X1 + 1, Y1, Z1), "hide"),
         face_line((X1, Y1, Z1), (X1 + 1, Y1, Z1), (X1, Y1, Z1 + 1), tx("+y"), ft("+y")), face_line((X1, Y1, Z1), (X1, Y1, Z1 + 1), (X1, Y1 + 1, Z1), tx("+x"), ft("+x"))]
    return "{\n" + "\n".join(L) + "\n}\n"


def box(x0, y0, z0, x1, y1, z1, tex):
    """Axis-aligned brush in METRES (x, y=depth, z=up). tex: one name or a dict per side
    (-x,+x,-y,+y,-z,+z). Plane order = TrenchBroom's cube (normals point out)."""
    a = tuple(round(v * U, 3) for v in (x0, y0, z0))
    b = tuple(round(v * U, 3) for v in (x1, y1, z1))
    t = tex if isinstance(tex, dict) else {k: tex for k in ("-x", "+x", "-y", "+y", "-z", "+z")}
    (X0, Y0, Z0), (X1, Y1, Z1) = a, b
    lines = [
        face_line((X0, Y0, Z0), (X0, Y0 + 1, Z0), (X0, Y0, Z0 + 1), t["-x"]),
        face_line((X0, Y0, Z0), (X0, Y0, Z0 + 1), (X0 + 1, Y0, Z0), t["-y"]),
        face_line((X0, Y0, Z0), (X0 + 1, Y0, Z0), (X0, Y0 + 1, Z0), t["-z"]),
        face_line((X1, Y1, Z1), (X1, Y1 + 1, Z1), (X1 + 1, Y1, Z1), t["+z"]),
        face_line((X1, Y1, Z1), (X1 + 1, Y1, Z1), (X1, Y1, Z1 + 1), t["+y"]),
        face_line((X1, Y1, Z1), (X1, Y1, Z1 + 1), (X1, Y1 + 1, Z1), t["+x"]),
    ]
    return "{\n" + "\n".join(lines) + "\n}\n"


def main():
    grid, plats, spawn, face_ = read_level(SRC)
    for (bc, br) in BREACH_CELLS: grid[br][bc] = "B"          # no wall here: the breach (not floor either -- nothing is hung on it or walks it)
    if not plats and SRC.endswith("level0.txt"):
        plats = LEVEL0_PLATS
    W, D = COLS * CELL, ROWS * CELL
    out = ['{\n"classname" "worldspawn"\n"message" "Chernobyl Arena"\n'
           '"_description" "../chernobyl level 0, 1:1, dressed as the plant after the accident (grid2map.py)"\n'
           f'"_mpu" "{1.0 / U:g}"\n'
           f'"_origin" "{COLS * CELL / 2 * U:g} {ROWS * CELL / 2 * U:g} {-0.5 * U:g}"\n'      # the hall keeps its place in the world whatever is built outside it
           f'"_hall" "{CELL * U:g} {CELL * U:g} {(COLS - 1) * CELL * U:g} {(ROWS - 1) * CELL * U:g}"\n']      # the playable floor: inside the outer ring of wall cells      # the hall keeps its place in the world whatever is built outside it
    # floor + ceiling slabs, one per zone (the outer ring of '#' cells closes the sides)
    xs = (0, ZONE_COLS[0] * CELL, ZONE_COLS[1] * CELL, W)
    for k, z in enumerate("tcr"):
        for (rx0, ry0, rx1, ry1) in cut_rects((xs[k], 0, xs[k + 1], D), HOLES):
            out.append(box(rx0, ry0, -0.5, rx1, ry1, 0, dict({s_: "hide" for s_ in ("-x", "+x", "-y", "+y", "-z")}, **{"+z": "floor_" + z})))
        out.append(box(xs[k], 0, WALL_H, xs[k + 1], D, WALL_H + 0.5, dict({s_: "hide" for s_ in ("-x", "+x", "-y", "+y", "+z")}, **{"-z": "ceil_r" if z == "r" else "ceil"})))
    # walls: merge horizontal runs of '#' per row into one brush each -- broken at the zone lines,
    # and the outer shell kept apart, so each takes its zone's finish
    nwall = 0
    def kind(r, c): return "o" if (r in (0, ROWS - 1) or c in (0, COLS - 1)) else zone(c)
    for r in range(ROWS):
        c = 0
        while c < COLS:
            if grid[r][c] != "#":
                c += 1
                continue
            c0 = c; k = kind(r, c)
            while c < COLS and grid[r][c] == "#" and kind(r, c) == k and (c == c0 or c not in ZONE_COLS or k == "o"):
                c += 1
            # the shell's INNER faces take the zone's paint (its bare concrete is only ever seen from inside the hall)
            wt = "wall_" + (zone(c0) if k == "o" else k)
            out.append(box(c0 * CELL, r * CELL, 0, c * CELL, (r + 1) * CELL, WALL_H,
                           {"-x": wt, "+x": wt, "-y": wt, "+y": wt, "-z": "hide", "+z": "hide"}))
            nwall += 1
    # decks + stairs: metal-plate tops, hazard-striped risers.
    # Each is cut to the floor cells it stands on. The originals run INTO the pillars beside them, so
    # a riser lay in the very plane of a wall face and the two fought for it -- crawling stripes
    # along the bottom of those walls. A side is drawn only where it is really an outside face:
    # not against a wall, not against another piece of the deck, not against a taller step.
    eps = 1e-6
    def solid_beyond(x, y, top):          # is the point just outside a side buried -- in a wall, or in a deck at least as tall?
        c, r = int(x // CELL), int(y // CELL)
        if not (0 <= c < COLS and 0 <= r < ROWS) or grid[r][c] == "#": return True
        return any(px0 - eps <= x <= px1 + eps and pz0 - eps <= y <= pz1 + eps and ptop >= top - eps for (px0, pz0, px1, pz1, ptop) in plats)
    npiece = 0
    for (x0, z0, x1, z1, top) in plats:
        for c in range(int(x0 // CELL), int(math.ceil(x1 / CELL))):
            for r in range(int(z0 // CELL), int(math.ceil(z1 / CELL))):
                if grid[r][c] == "#": continue
                ax0, ax1, ay0, ay1 = max(x0, c * CELL), min(x1, (c + 1) * CELL), max(z0, r * CELL), min(z1, (r + 1) * CELL)
                if ax1 - ax0 < 0.01 or ay1 - ay0 < 0.01: continue
                mx, my = (ax0 + ax1) / 2, (ay0 + ay1) / 2
                side = lambda bx_, by_: "hide" if solid_beyond(bx_, by_, top) else "edge"
                out.append(box(ax0, ay0, 0, ax1, ay1, top, {"-x": side(ax0 - 0.02, my), "+x": side(ax1 + 0.02, my), "-y": side(mx, ay0 - 0.02), "+y": side(mx, ay1 + 0.02), "-z": "hide", "+z": "deck"}))
                npiece += 1
    # lamp fixtures: a panel hung just under the ceiling -- lit, or dead
    for (cx, cz, (tex, _, _)) in LAMPS:
        x, z = (cx + 0.5) * CELL, (cz + 0.5) * CELL
        out.append(box(x - 0.45, z - 0.45, WALL_H - 0.12, x + 0.45, z + 0.45, WALL_H, tex))
    # the breach in the east wall, and the reactor pit beyond it
    nbreach = breach(out)
    bx = BREACH_CELLS[0][0] * CELL; by0 = min(r for _, r in BREACH_CELLS) * CELL; by1 = (max(r for _, r in BREACH_CELLS) + 1) * CELL
    # the breaches in the floor. A side is one they can climb out over only if there is open, level floor beyond it.
    import random
    def open_floor(x, y):
        c, r = int(x // CELL), int(y // CELL)
        return 0 <= c < COLS and 0 <= r < ROWS and grid[r][c] == "." and not any(px0 - 0.3 < x < px1 + 0.3 and pz0 - 0.3 < y < pz1 + 0.3 for (px0, pz0, px1, pz1, _t) in plats)
    nhole = 0; hole_ents = []
    for h in HOLES:
        cx, cy, hx, hy, dp, style, seed, nedge, fx, boss = h
        for dx in (-hx - 1.0, 0, hx + 1.0):
            for dy in (-hy - 1.0, 0, hy + 1.0): assert open_floor(cx + dx, cy + dy), f"hole at {cx},{cy} is not on open floor"
        ok = []
        for s_, (n, t, _b) in HOLE_SIDES.items():
            half, L = (hx, hy) if s_[1] == "x" else (hy, hx)
            if all(open_floor(cx + n[0] * (half + o) + t[0] * a, cy + n[1] * (half + o) + t[1] * a) for o in (0.8, 1.6, 2.2) for a in (-L, 0, L)): ok.append(s_)
        if style == "split": ok = [s_ for s_ in ok if (s_[1] == "y") == (hx > hy)]      # a crack is climbed out of over its long sides
        random.Random(seed * 7 + 1).shuffle(ok); edges = ok[:nedge]
        assert edges, f"hole at {cx},{cy} has no side to climb out over"
        nhole += hole(out, h, edges, "floor_" + zone(int(cx // CELL)))
        hole_ents.append((h, sum(HOLE_SIDES[s_][2] for s_ in edges), list(edges)))
    # signs: on wall faces that look onto open floor, a fixed-seed pick, spread out, each zone its own set
    import random
    rnd = random.Random(1986); spots = []
    for r in range(1, ROWS - 1):
        for c in range(1, COLS - 1):
            if grid[r][c] != ".": continue
            for face, (dr, dc) in (("+y", (-1, 0)), ("-y", (1, 0)), ("+x", (0, -1)), ("-x", (0, 1))):     # the wall cell this floor cell looks at, and the face of it
                if grid[r + dr][c + dc] == "#": spots.append((r, c, face))
    # control stations first: they take whole wall bays, and the signs and posters go round them
    def wall_mid(r_, c_, face):
        cx_, cy_ = (c_ + 0.5) * CELL, (r_ + 0.5) * CELL
        if face[1] == "y": return (r_ * CELL if face == "+y" else (r_ + 1) * CELL), cx_
        return (c_ * CELL if face == "+x" else (c_ + 1) * CELL), cy_
    on_deck = lambda r_, c_: any(px0 < (c_ + 1) * CELL and px1 > c_ * CELL and pz0 < (r_ + 1) * CELL and pz1 > r_ * CELL for (px0, pz0, px1, pz1, _t) in plats)
    near_breach = lambda r_, c_: abs((c_ + 0.5) * CELL - bx) < 10 and by0 - 6 < (r_ + 0.5) * CELL < by1 + 6
    free = [(r_, c_, f_) for (r_, c_, f_) in spots if not on_deck(r_, c_) and not near_breach(r_, c_) and (c_, r_) != tuple(spawn or (-1, -1))]
    srnd = random.Random(86); hung = []; nstation = 0
    best = []                                                                  # the control room: the longest straight run of wall bays in the control zone
    for (r_, c_, f_) in free:
        if zone(c_) != "c": continue
        dr, dc = (0, 1) if f_[1] == "y" else (1, 0); run = [(r_, c_, f_)]
        while (run[-1][0] + dr, run[-1][1] + dc, f_) in free and zone(run[-1][1] + dc) == "c" and len(run) < 3: run.append((run[-1][0] + dr, run[-1][1] + dc, f_))
        if len(run) > len(best): best = run
    best = best[:3]
    for k, (r_, c_, f_) in enumerate(best):
        w_, m_ = wall_mid(r_, c_, f_)
        station(out, f_, w_, m_, dict(kind="desk", mimic=MIMICS[k % 3], top=("console_top", "console_top_az", "console_top_comm")[k % 3], **({"plate": 0} if k == 1 else {})), seed=k)
        hung.append((r_, c_)); nstation += 1
        if os.environ.get("MAP_SIGNS"): print(f"  control room bay {f_} cell ({c_},{r_})")
    order = free[:]; srnd.shuffle(order); nxtp = {"t": 0, "c": 0, "r": 0}
    for gap in (4, 3):                                                         # spread them out first, then fill in closer
        for (r_, c_, f_) in order:
            z = zone(c_)
            if nxtp[z] >= len(POSTS[z]) or any(abs(r_ - r2) + abs(c_ - c2) < gap for r2, c2 in hung): continue
            spec = POSTS[z][nxtp[z]]
            if spec["kind"] in ("wing", "skala"):                              # these are wider than a bay: the bays either side must be floor against the same wall, and free
                dr, dc = (0, 1) if f_[1] == "y" else (1, 0)
                if not all((r_ + q * dr, c_ + q * dc, f_) in free and (r_ + q * dr, c_ + q * dc) not in hung for q in (-1, 1)): continue
            w_, m_ = wall_mid(r_, c_, f_); station(out, f_, w_, m_, spec, seed=10 * (r_ * COLS + c_))
            hung.append((r_, c_)); nxtp[z] += 1; nstation += 1
            if spec["kind"] in ("wing", "skala"): hung += [(r_ + q * dr, c_ + q * dc) for q in (-1, 1)]
            if os.environ.get("MAP_SIGNS"): print(f"  station {z} {spec['kind']:8s} {f_} cell ({c_},{r_})")
    rnd.shuffle(spots); nsign = 0; nxt = {"t": 0, "c": 0, "r": 0}
    for (r, c, face) in spots:
        if any(abs(r - r2) + abs(c - c2) < 4 for r2, c2 in hung): continue
        cx_, cy_ = (c + 0.5) * CELL, (r + 0.5) * CELL
        if abs(cx_ - bx) < 6 and by0 - 3 < cy_ < by1 + 3: continue                                        # not on the breach
        z = zone(c); tex, sw, sh = SIGNS[z][nxt[z] % len(SIGNS[z])]; nxt[z] += 1
        zc = 1.95 if sh < 1.2 else 1.75
        if face[1] == "y": wall = r * CELL if face == "+y" else (r + 1) * CELL; mid = cx_
        else:              wall = c * CELL if face == "+x" else (c + 1) * CELL; mid = cy_
        sw, sh = sw * SIGN_SCALE, sh * SIGN_SCALE
        out.append(sign(face, mid - sw / 2, mid + sw / 2, zc - sh / 2, zc + sh / 2, wall, tex)); hung.append((r, c)); nsign += 1
        if os.environ.get("MAP_SIGNS"): print(f"  sign {tex:16s} {face} cell ({c},{r}) at x {mid if face[1]=='y' else wall:.1f} y {wall if face[1]=='y' else mid:.1f}")
    # posters: the same way, in the gaps the signs left (two cells clear of a sign, three of each other)
    post = []; npost = 0; pn = {"t": 0, "c": 0, "r": 0}
    rnd2 = random.Random(1917); spots2 = spots[:]; rnd2.shuffle(spots2)
    for (r, c, face) in spots2:
        if npost >= N_POSTERS: break
        if (r, c) in hung or any(abs(r - r2) + abs(c - c2) < 2 for r2, c2 in post): continue
        cx_, cy_ = (c + 0.5) * CELL, (r + 0.5) * CELL
        if abs(cx_ - bx) < 6 and by0 - 3 < cy_ < by1 + 3: continue
        z = zone(c); tex = POSTERS[z][pn[z] % len(POSTERS[z])]; pn[z] += 1
        if face[1] == "y": wall = r * CELL if face == "+y" else (r + 1) * CELL; mid = cx_
        else:              wall = c * CELL if face == "+x" else (c + 1) * CELL; mid = cy_
        mid += rnd2.uniform(-0.5, 0.5); zc = 2.45 + rnd2.uniform(-0.08, 0.08)                              # pasted up by hand, not surveyed
        pw_, ph_ = REAL_PX.get(tex, (512, 768)); POSTER_H = POSTER_W * ph_ / pw_
        out.append(sign(face, mid - POSTER_W / 2, mid + POSTER_W / 2, zc - POSTER_H / 2, zc + POSTER_H / 2, wall, tex)); post.append((r, c)); npost += 1
    # ---- everything else that hangs on a wall or lies on a floor ----
    frnd = random.Random(1986 * 3); used = set(hung) | set(post); nfit = 0
    def place(r_, c_, f_):
        w_, m_ = wall_mid(r_, c_, f_); sg_ = 1.0 if f_[0] == "+" else -1.0; ax = f_[1] == "y"
        Wp = lambda u, v, z: (m_ + u, w_ + sg_ * v, z) if ax else (w_ + sg_ * v, m_ + u, z)
        def lb(u0, u1, v0, v1, z0, z1, tex):
            b0, b1 = sorted((w_ + sg_ * v0, w_ + sg_ * v1))
            return box(m_ + u0, b0, z0, m_ + u1, b1, z1, tex) if ax else box(b0, m_ + u0, z0, b1, m_ + u1, z1, tex)
        pl = lambda u0, u1, z0, z1, v, tex, th: sign(f_, m_ + u0, m_ + u1, z0, z1, w_ + sg_ * v, tex, thick=th)
        return Wp, lb, pl
    # pipes under the ceiling round the turbine hall and the reactor side: two lines, flanged at every bay
    for (r_, c_, f_) in free + [sp_ for sp_ in spots if sp_ not in free]:
        if zone(c_) == "c" or near_breach(r_, c_): continue
        Wp, lb, pl = place(r_, c_, f_)
        for z_, rr, v in ((4.55, 0.13, 0.30), (4.20, 0.08, 0.22)):
            out.append(tube(Wp(-2.0, v, z_), Wp(2.0, v, z_), rr, "steel", 9)); out.append(tube(Wp(-0.05, v, z_), Wp(0.05, v, z_), rr + 0.045, "steel", 9))
        out.append(lb(-0.04, 0.04, 0.0, 0.34, 4.05, 4.70, "steel"))                                       # a hanger
    # in the bays that are still bare: hose cabinets (with the hose run out of them), fuse boards, junction boxes
    for (r_, c_, f_) in order:
        if (r_, c_) in used or frnd.random() > 0.62: continue
        Wp, lb, pl = place(r_, c_, f_); k = frnd.random(); u = frnd.uniform(-0.9, 0.9); used.add((r_, c_)); nfit += 1
        if k < 0.34:                                                                                      # a fire point
            out.append(lb(u - 0.36, u + 0.36, 0.0, 0.20, 0.95, 1.67, "redbox")); out.append(pl(u - 0.34, u + 0.34, 0.97, 1.65, 0.20, "hose_cabinet", 0.02))
            PANELS.append(Wp(u, 0.3, 1.3))                                                                  # the red box sounds too, like a control post ("info_panel")
            pts = [Wp(u + 0.2, 0.22, 0), Wp(u + 0.3, 0.45, 0)]; x_, y_ = u + 0.3, 0.45; ang = frnd.uniform(-0.6, 0.6)
            for q in range(frnd.randint(7, 13)):                                                           # ...and its hose, pulled out across the floor
                ang += frnd.uniform(-0.55, 0.55); x_ += math.cos(ang) * 0.5 * frnd.choice((1, 1, -1)) * 0.6; y_ = min(3.2, max(0.3, y_ + abs(math.sin(ang)) * 0.45 + 0.12))
                x_ = min(1.8, max(-1.8, x_)); pts.append(Wp(x_, y_, 0))
            hose_line(out, pts)
            out.append(tube(Wp(u + 0.2, 0.22, 0.04), Wp(u + 0.2, 0.22, 0.97), 0.038, "hose", 7))               # the length hanging down to the floor
        elif k < 0.67:                                                                                    # a lighting board, its conduit up to the ceiling
            out.append(lb(u - 0.34, u + 0.34, 0.0, 0.12, 1.20, 2.10, "console_body")); out.append(pl(u - 0.32, u + 0.32, 1.22, 2.07, 0.12, "fuse_board", 0.02))
            out.append(lb(u - 0.03, u + 0.03, 0.02, 0.08, 2.10, WALL_H, "steel")); out.append(lb(u + 0.12, u + 0.17, 0.02, 0.07, 2.10, WALL_H, "steel"))
        else:                                                                                             # a junction box and a run of conduit, a switch at the door's height
            out.append(lb(u - 0.20, u + 0.20, 0.0, 0.14, 2.55, 2.95, "console_body")); out.append(lb(u - 0.025, u + 0.025, 0.02, 0.07, 1.35, 2.55, "steel"))
            out.append(lb(u - 0.09, u + 0.09, 0.0, 0.09, 1.20, 1.38, "console_body")); out.append(lb(u - 2.0, u - 0.20, 0.02, 0.07, 2.72, 2.77, "steel"))
    # the firefighters' lines: two hoses run the length of the main corridor to the breach, a third from the turbine hall
    for y0_, x0_, ph in ((38.4, 46.0, 0.0), (41.7, 30.0, 1.7), (40.3, 70.0, 3.1)):
        pts = []; x_ = x0_
        while x_ < bx - 0.9: pts.append((x_, y0_ + 0.45 * math.sin(x_ * 0.21 + ph) + 0.18 * math.sin(x_ * 0.63 + ph * 2), 0)); x_ += 0.8
        pts.append((bx + 0.2, y0_ + 0.3, 0)); hose_line(out, pts, 0.045)
        out.append(tube((bx + 0.2, y0_ + 0.3, 0.045), (bx + 0.9, y0_ + 0.3, 1.30), 0.045, "hose", 7))          # up and over the stump of the wall, into the pit
    out.append("}\n")
    # entities: spawn + lights (mapload: radius = light * mpu * 2.6)
    if spawn:
        sx, sz = (spawn[0] + 0.5) * CELL, (spawn[1] + 0.5) * CELL
        ang = math.degrees(math.atan2(math.cos(face_), math.sin(face_)))   # game yaw (0=+Z) -> quake angle (0=+X)
        out.append('{\n"classname" "info_player_deathmatch"\n'
                   f'"origin" "{sx * U:g} {sz * U:g} {0.1 * U:g}"\n"angle" "{ang:.0f}"\n}}\n')
    def light(x, y, z, col, radius):
        out.append('{\n"classname" "light"\n'
                   f'"origin" "{x * U:g} {y * U:g} {z * U:g}"\n'
                   f'"light" "{1.6 * radius / ((1.0 / U) * 2.6):.0f}"\n"_color" "{col[0]:g} {col[1]:g} {col[2]:g}"\n}}\n')
    nlit = 0
    for (cx, cz, (tex, col, radius)) in LAMPS:
        if tex == "lamp_fail":
            out.append('{\n"classname" "info_flicker"\n' f'"origin" "{(cx + 0.5) * CELL * U:g} {(cz + 0.5) * CELL * U:g} {(WALL_H - 0.35) * U:g}"\n"light" "{1.6 * radius / ((1.0 / U) * 2.6):.0f}"\n}}\n')
        if col is None: continue
        light((cx + 0.5) * CELL, (cz + 0.5) * CELL, WALL_H - 0.35, col, radius); nlit += 1
    for col, radius, (lx, ly, lz) in PIT_LIGHTS: light(lx, ly, lz, col, radius)
    for (cx, cy, hx, hy, dp, style, seed, nedge, fx, boss), bits, _edges in hole_ents:
        tint = fx[1]; col = tuple(round(a + (b - a) * tint, 3) for a, b in zip((0.22, 0.48, 1.0), (0.55, 0.86, 1.0)))
        light(cx, cy, -min(1.4, dp * 0.4), col, 7.0 + 6.0 * fx[0])
        out.append('{\n"classname" "info_hole"\n' f'"origin" "{cx * U:g} {cy * U:g} 0"\n"_size" "{hx:g} {hy:g} {dp:g}"\n"_edges" "{bits}"\n'
                   f'"_fx" "{fx[0]:g} {fx[1]:g} {fx[2]:g} {fx[3]:g} {fx[4]:g}"\n"_seed" "{seed}"\n"_boss" "{boss}"\n}}\n')
    # A trap beside every breach, laid across the way out of it (the first climbing side it fits on), so what comes up walks
    # into it. Each breach is offered a different kind first -- live plate, fire hose, coolant spill, steam main -- and takes
    # the first that has the floor it needs; where none fits (or to vent the breach itself) it gets a surge post.
    traps = list(TRAPS)
    def clear_of_holes(x, y, skip):
        return all(i == skip or abs(x - H[0]) > H[2] + 0.4 or abs(y - H[1]) > H[3] + 0.4 for i, H in enumerate(HOLES))
    def fits(cx_, cy_, a_, pts, skip):
        tv = (math.sin(a_), -math.cos(a_)); rv = (math.cos(a_), math.sin(a_))
        for (lx, lz) in pts:
            x, y = cx_ + tv[0] * lx + rv[0] * lz, cy_ + tv[1] * lx + rv[1] * lz
            if not open_floor(x, y) or not clear_of_holes(x, y, skip): return False
        return all(k_ == 3 or math.hypot(cx_ - tx_, cy_ - ty_) > 7.5 for (k_, tx_, ty_, _a, _h) in traps)
    NEED = {1: [(x, z) for x in (-2.7, 0, 2.7) for z in (-2.7, 0, 2.7)] + [(3.6, 0)],                               # plate: its 5 m square, the switch
            2: [(x, z) for x in (-4.8, -3.5, -1.0, 2.0, 5.0, 7.5) for z in (-0.5, 0.6)],                             # hose: the cabinet, the length of the jet
            6: [(-3.6, 0.9), (-4.4, 0.9), (-3.6, 1.9), (-3.6, -0.1)] + [(-1 + 3.0 * math.cos(q * 0.785), 3.0 * math.sin(q * 0.785)) for q in range(8)],   # spill: the tank, the pool
            0: [(x, z) for x in (-3.5, -1.7, 0, 1.7, 3.5) for z in (-1.0, 1.0)]}                                       # steam: both posts, the bay between
    PLACE = {1: lambda half: (half + 2.9, 0.0), 2: lambda half: (half + 1.3, -2.0), 6: lambda half: (half + 2.4, 1.0), 0: lambda half: (half + 2.0, 0.0)}   # (out from the breach's centre, along its side)
    ORDER = (1, 2, 6, 0)
    for hi, (h, bits, edges) in enumerate(hole_ents):
        cx, cy, hx, hy = h[:4]
        if any(th_ == hi for (_k, _x, _y, _a, th_) in traps): continue
        done = False
        for q in range(4):
            kind = ORDER[(hi + q) % 4]
            for s_ in edges:
                n, _t, _b = HOLE_SIDES[s_]; half = hx if s_[1] == "x" else hy; a_ = math.atan2(n[1], n[0]); tv = (math.sin(a_), -math.cos(a_))
                out_, along = PLACE[kind](half); tx_, ty_ = cx + n[0] * out_ + tv[0] * along, cy + n[1] * out_ + tv[1] * along
                if fits(tx_, ty_, a_, NEED[kind], hi): traps.append((kind, round(tx_, 2), round(ty_, 2), round(math.degrees(a_)), -1)); done = True; break
            if done: break
        if not done:                                                                # a surge post: on a side they do not climb out over, if there is one with floor for it
            sides = [s_ for s_ in HOLE_SIDES if s_ not in edges] + list(edges)
            for s_ in sides:
                n = HOLE_SIDES[s_][0]; a_ = math.atan2(-n[0], n[1])                 # turned so its switch (3.6 m along its 'across' axis) stands off that side
                tv = (math.sin(a_), -math.cos(a_))
                if open_floor(cx + tv[0] * 3.6, cy + tv[1] * 3.6) and clear_of_holes(cx + tv[0] * 3.6, cy + tv[1] * 3.6, -1): traps.append((3, cx, cy, round(math.degrees(a_)), hi)); break
    print("traps:", " ".join(f"{('steam','plate','hose','surge','crane','door','spill','turbine')[k_]}@{tx_:g},{ty_:g}" for (k_, tx_, ty_, _a, _h) in traps[len(TRAPS):]))
    for (k_, tx_, ty_, ta_, th_) in traps:
        if th_ >= 0: assert abs(HOLES[th_][0] - tx_) < 0.01 and abs(HOLES[th_][1] - ty_) < 0.01, "a surge sits on the breach it vents"
        out.append('{\n"classname" "info_trap"\n' f'"origin" "{tx_ * U:g} {ty_ * U:g} 0"\n"_kind" "{k_}"\n"_ang" "{ta_}"\n"_hole" "{th_}"\n}}\n')
    for (px_, py_, pz_) in PANELS: out.append('{\n"classname" "info_panel"\n' f'"origin" "{px_ * U:g} {py_ * U:g} {pz_ * U:g}"\n}}\n')
    out.append('{\n"classname" "info_reactor"\n' f'"origin" "{REACTOR_AT[0] * U:g} {REACTOR_AT[1] * U:g} {REACTOR_AT[2] * U:g}"\n}}\n')
    open(OUT, "w").write("".join(out))
    print(f"{OUT}: {nwall} wall brushes, {len(plats)} platforms, {nlit} lamps lit of {len(LAMPS)}, {nstation} control stations, {nsign} signs, {npost} posters, {nfit} wall fittings, {nbreach} brushes in the breach, {len(HOLES)} holes ({nhole} brushes), spawn {spawn}")


if __name__ == "__main__":
    main()
