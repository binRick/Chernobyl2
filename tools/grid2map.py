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
import sys

SRC = sys.argv[1] if len(sys.argv) > 1 else "../chernobyl/levels/level0.txt"
OUT = sys.argv[2] if len(sys.argv) > 2 else "maps/chernobyl.map"

U = 32.0                  # Quake units per metre
CELL = 4.0                # metres per grid cell (../chernobyl common.h)
WALL_H = 5.1              # metres (../chernobyl game.c)
COLS, ROWS = 30, 20

TEX = {   # 512px textures; scale = metres-per-repeat * U / 512
    "wall":  ("trak4x/wall-brick2a", 2.0),
    "floor": ("exx/floor-crete01b", 4.0),
    "ceil":  ("trak5x/panel-ceil1a", 4.0),
    "deck":  ("exx/floor-metal02", 2.0),
    "edge":  ("exx/floor-wrn01", 1.0),
    "lamp":  ("trak5x/light-light1a", 0.6),
    "hide":  ("common/caulk", 4.0),
}

# level-0 decks + the stairs up to the centre deck (../chernobyl src/v2/level.c
# Level0Platforms): x0 z0 x1 z1 top, metres.
LEVEL0_PLATS = [(52, 38, 55, 42, 0.45), (55, 38, 58, 42, 0.90), (58, 38, 61, 42, 1.35),
                (61, 36, 69, 44, 1.35), (69, 38, 72, 42, 0.90), (72, 38, 75, 42, 0.45),
                (20, 24, 26, 28, 1.10), (92, 60, 104, 68, 1.60)]
# the 14 scene lamps (../chernobyl game.c InitLights): cell x, cell z, rgb, radius m
LAMPS = [(5, 2, (1.0, .65, .2), 40), (14, 9, (.2, .5, 1.0), 50), (24, 5, (1.0, .15, .1), 40),
         (5, 16, (.15, 1.0, .3), 40), (24, 16, (.9, .3, 1.0), 40), (14, 17, (1.0, .8, .3), 40),
         (2, 6, (1.0, .55, .15), 35), (9, 9, (.3, .7, 1.0), 35), (19, 9, (.3, 1.0, .4), 35),
         (27, 9, (1.0, .3, .85), 35), (2, 13, (1.0, .2, .15), 35), (9, 13, (1.0, .85, .55), 35),
         (20, 13, (.2, .9, .85), 35), (27, 13, (1.0, .7, .25), 35)]


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


def face_line(p0, p1, p2, tex):
    name, metres = TEX[tex]
    sc = metres * U / 512.0
    f = lambda p: "( %g %g %g )" % p
    return f"{f(p0)} {f(p1)} {f(p2)} {name} 0 0 0 {sc:g} {sc:g}"


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
    grid, plats, spawn, face = read_level(SRC)
    if not plats and SRC.endswith("level0.txt"):
        plats = LEVEL0_PLATS
    W, D = COLS * CELL, ROWS * CELL
    out = ['{\n"classname" "worldspawn"\n"message" "Chernobyl Arena"\n'
           '"_description" "1:1 rebuild of ../chernobyl level 0 (grid2map.py)"\n'
           f'"_mpu" "{1.0 / U:g}"\n']
    # floor + ceiling slabs (the outer ring of '#' cells closes the sides)
    out.append(box(0, 0, -0.5, W, D, 0, dict({k: "hide" for k in ("-x", "+x", "-y", "+y", "-z")}, **{"+z": "floor"})))
    out.append(box(0, 0, WALL_H, W, D, WALL_H + 0.5, dict({k: "hide" for k in ("-x", "+x", "-y", "+y", "+z")}, **{"-z": "ceil"})))
    # walls: merge horizontal runs of '#' per row into one brush each
    nwall = 0
    for r in range(ROWS):
        c = 0
        while c < COLS:
            if grid[r][c] != "#":
                c += 1
                continue
            c0 = c
            while c < COLS and grid[r][c] == "#":
                c += 1
            out.append(box(c0 * CELL, r * CELL, 0, c * CELL, (r + 1) * CELL, WALL_H,
                           {"-x": "wall", "+x": "wall", "-y": "wall", "+y": "wall", "-z": "hide", "+z": "hide"}))
            nwall += 1
    # decks + stairs: metal-plate tops, hazard-striped risers
    for (x0, z0, x1, z1, top) in plats:
        out.append(box(x0, z0, 0, x1, z1, top, {"-x": "edge", "+x": "edge", "-y": "edge", "+y": "edge", "-z": "hide", "+z": "deck"}))
    # lamp fixtures: a glowing panel hung just under the ceiling
    for (cx, cz, _, _) in LAMPS:
        x, z = (cx + 0.5) * CELL, (cz + 0.5) * CELL
        out.append(box(x - 0.45, z - 0.45, WALL_H - 0.12, x + 0.45, z + 0.45, WALL_H, "lamp"))
    out.append("}\n")
    # entities: spawn + coloured lights (mapload: radius = light * mpu * 2.6)
    if spawn:
        sx, sz = (spawn[0] + 0.5) * CELL, (spawn[1] + 0.5) * CELL
        ang = math.degrees(math.atan2(math.cos(face), math.sin(face)))   # game yaw (0=+Z) -> quake angle (0=+X)
        out.append('{\n"classname" "info_player_deathmatch"\n'
                   f'"origin" "{sx * U:g} {sz * U:g} {0.1 * U:g}"\n"angle" "{ang:.0f}"\n}}\n')
    for (cx, cz, col, radius) in LAMPS:
        x, z = (cx + 0.5) * CELL, (cz + 0.5) * CELL
        out.append('{\n"classname" "light"\n'
                   f'"origin" "{x * U:g} {z * U:g} {(WALL_H - 0.35) * U:g}"\n'
                   f'"light" "{1.6 * radius / ((1.0 / U) * 2.6):.0f}"\n"_color" "{col[0]:g} {col[1]:g} {col[2]:g}"\n}}\n')
    open(OUT, "w").write("".join(out))
    print(f"{OUT}: {nwall} wall brushes, {len(plats)} platforms, {len(LAMPS)} lamps, spawn {spawn}")


if __name__ == "__main__":
    main()
