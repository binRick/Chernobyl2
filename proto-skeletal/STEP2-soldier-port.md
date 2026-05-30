# Step 2 — Soldier (enemy type 7) ported to a rigged glTF model

Behind the `CHERNOBYL_GLTF_ENEMY` compile flag, the Soldier enemy renders as a
rigged, animated 3D model instead of a billboard sprite. **Off by default** —
the shipping build is byte-for-byte unchanged.

## Build / run

```bash
cd Chernobyl
make GLTF_ENEMY=1          # builds with the 3D soldier; bundles models/ into Resources
./run.sh                   # NOTE: run.sh does `make -B` (flag OFF). To test the
                           # 3D soldier: make GLTF_ENEMY=1 then open Chernobyl.app
```

Then in-game press **A** on the menu (arena picker) → select the **Soldier** to
spawn a wave of type-7s, or reach wave 2 where soldiers appear.

## What changed (all in `src/game.c`, ~75 lines, + Makefile)

1. **Globals** (after the chef-texture block): model/anim handles, per-slot
   animation cursors `g_genAnimT[MAX_ENEMIES]`, version macros so the same
   source builds on raylib 6.0 (Chernobyl's pin) and 5.5.
2. **`GenEnsureLoaded()`** — lazy-loads `models/soldier.glb` on first draw,
   applies the world multi-light `g_shader` to its materials, and maps gameplay
   states → clips by case-insensitive name (`idle`/`walk`/`punch`|`attack`/
   `death`), so a future model swap "just works".
3. **`DrawGltfEnemy(slot, e)`** — picks a clip from `e->state`/`e->dying`,
   advances that slot's cursor (death plays once and holds), faces the player
   (`atan2f` yaw), draws feet-anchored with `DrawModelEx`. Flushes the render
   batch before/after (per CLAUDE.md's billboard/model batch rule).
4. **Early-out** in `DrawEnemies`' draw loop: `if (DrawGltfEnemy(...)) continue;`
   — skips the billboard path for type 7 only.
5. **Makefile**: `GLTF_ENEMY=1` adds `-DCHERNOBYL_GLTF_ENEMY` and bundles
   `models/` into the .app Resources. `models/soldier.glb` is the CC0 robot.glb
   stand-in from proto-skeletal.

Design choice: **lazy load** (not a startup call) so the patch needs only
anchors that are stable in the 11k-line file, and adds nothing to the boot path
when no soldier is ever spawned.

## Verified

- `make` (flag OFF): exit 0, 0 errors, `models/` NOT bundled (shipping build untouched).
- `make GLTF_ENEMY=1`: exit 0, 0 errors, binary links against vendored raylib 6.0, model bundled.
- `clang -fsyntax-only` also passes against Homebrew raylib 5.5 headers (version macros work).

## NOT verified here (sandbox has no GL context)

The in-game **on-screen render** of the 3D soldier — same WindowServer/GL
limitation that blocked the proto's 6.0 render. The binary initializes raylib
6.0 (DESKTOP/GLFW backend) before the sandbox cuts the context. Run
`make GLTF_ENEMY=1 && open Chernobyl.app` on a real desktop session to see it.

## Known rough edges (intentional, for a first slice)

- Hit volumes / HP-bar still use the billboard type-7 path (unchanged) — the 3D
  model is render-only; AI, collision, and damage are untouched.
- `GLTF_ENEMY_SCALE` (1.0) and the −0.95 foot offset are tuned for robot.glb's
  ~2-unit height; a different model needs a quick re-tune.
- Uses CPU pose path (`UpdateModelAnimation`); switch to GPU skinning
  (`UpdateModelAnimationBones` + skinning shader) before scaling to many on-screen.
