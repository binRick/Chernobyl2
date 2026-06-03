# Chernobyl 2 — progress

Last updated: 2026-06-03 (HEAD `a34cdef`)

A raylib 6.0 first-person shooter in C. Single-file game logic in `src/main.c`,
with `src/mapload.h` (header-only Quake `.map` loader / collision / nav / lighting)
and `src/macicon.m` (macOS Dock/Cmd-Tab icon). The README is a stale v0.1 intro;
this file tracks where things actually are.

## What's implemented

- **Player / FPS movement**: WASD + mouse look, sprint, jump, gravity, capsule
  collision against the map (floor glue + stair-step camera smoothing, no
  float-through). `F` = noclip/fly on a map.
- **9 weapons** (keys `1`–`9`, slots 0–8), each with its own fire behavior + sound:
  0 M16A3 rifle (3-round burst), 1 shotgun (pump reload anim + cock sound),
  2 minigun (barrel spin while firing, 5 s overheat → spin-down + cooldown sound),
  3 LMG/biggun (sound-gated burst → 0.75 s pause), 4 AK-74M, 5 MP5, 6 Benelli M4,
  7 Flamethrower (placeholder — no flame mechanic yet), 8 Knife (placeholder — no
  melee yet).
- **Enemies**: flow-field pathfinding over the walkable grid, can't be walked
  through, headshots, finish their death animation then leave a lasting corpse;
  blood pools as flat liquid splats on the floor.
- **Map**: loads a Xonotic `.map` (`maps/afterslime.map`) — brush CSG → textured
  multi-material model, baked point lights + shadow rays, AO bake, gradient sky
  through sky surfaces. `info_player_*` spawn parsing.
- **Display / UX**: windowed 1280×720 by default. `ESC` opens a pause/options
  menu (Fullscreen toggle, Resume, Quit) that frees the cursor and freezes the
  world. Fullscreen = borderless windowed at monitor resolution; persisted in
  `options.txt` and re-applied on launch. Mouse hidden during play; macOS Dock
  icon set from `assets/icon.png`.

## Viewmodel framing system (important)

Each weapon has an `off (x,y,z) / scale / yaw / pitch / roll` transform. Sources,
in load order (later wins):

1. `LoadWeapon(...)` defaults in `src/main.c` — **the committed source of truth.**
2. `vm_tune_<name>.txt` — per-machine override, written by the in-game orient
   mode (`N`), **gitignored**. Delete it to fall back to the LoadWeapon defaults.

**Posed-vs-bind gotcha (the hard-won fix):** raylib draws the *posed* mesh
(`mesh.animVertices`), but `GetModelBoundingBox` reads the *bind* pose
(`mesh.vertices`). Some FPS rigs ship a bind pose far from where the gun is drawn
— the MP5's bind bbox is ~44k units off-origin, so recentering on it drew the gun
into empty space and it vanished. Fix: `MeasureBBox(model, useAnim, …)` plus a
per-slot `g_posedBasis[16]` opt-in. Flagged slots (currently just the MP5) frame
from the posed idle pose; everyone else keeps the bind bbox they were tuned
against (byte-identical → no regression). At pitch ±90 (the house style) yaw
rotates in the screen plane (gimbal lock), so barrel direction is very sensitive;
a +Y-up bind rig (Benelli) wants pitch +90, a −Y-down rig (MP5) wants pitch −90.

Dialed-in poses: **MP5** off (0.33, −0.49, −1.04) scale 0.000026 yaw 106 pitch −90
(posed basis); **Benelli M4** off (0.30, −0.40, −1.08) scale 0.000021 yaw 104
pitch 90 (bind basis).

## Build & run

```bash
./run.sh                 # clean build + play (loads maps/afterslime.map if present)
make all                 # build only -> build/chernobyl2
make all 2>&1 | grep -iE "error:|warning:"   # the real diagnostics
```

LSP errors about `raylib.h` / `Vector3` / `GetTime` are **false positives** (no
include path in the LSP) — trust `make`, not the editor squiggles.

### Dev flags
- `--debug` — JSON event stream to `chernobyl2-debug.log`.
- `--map <file>` — load a `.map`.
- `--shot N` — screenshot frame N to `chernobyl2-shot.png` (forces a deterministic
  1280×720, ignores the saved fullscreen pref).
- `--frames N` — run N frames then quit.
- `--no-enemies` — orient mode (empty arena + live viewmodel framing overlay; same
  as the `N` key in-game).
- `--weapon N` — start on weapon slot N.
- `--vm "x y z scale yaw pitch roll"` — override the viewmodel transform live.

### Headless weapon-framing workflow
No rebuild per try: sweep with
`./build/chernobyl2 --debug --no-enemies --weapon N --vm "…" --shot 30 --frames 45`,
then Read `chernobyl2-shot.png`. When it looks right, bake the values into that
weapon's `LoadWeapon(...)` call **and** write `vm_tune_<name>.txt`.

## Gitignored (per-machine, not in the repo)

`*.glb`, `*.mp3`, `*.map`, `maps/`, `vm_tune*.txt`, `options.txt`,
`chernobyl2-debug.log`, `chernobyl2-shot.png`, `build/`, `vendor/raylib/`. So the
game's models/sounds/maps live only on this machine; the committed framing is
whatever's in `LoadWeapon`.

## Next / deferred

- Flamethrower (slot 7) flame mechanic; Knife (slot 8) melee mechanic — both are
  framed/wired but have no special attack yet.
- Shotgun (slot 1) arm framing was flagged as imperfect.
- Keyboard navigation for the options menu (currently mouse-click only).
