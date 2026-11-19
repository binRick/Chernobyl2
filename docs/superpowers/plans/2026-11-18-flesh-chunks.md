# Flesh chunks Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** What a blast leaves of a man is torn meat and bone, lit by the room, tumbling and lying flat — not flat-coloured balls.

**Architecture:** An offline tool builds eight chunk meshes (colour in the vertices) and three small tiling pictures;
the game loads them once and draws each gib as one of them through the enemies' lit shader, with a planned tumble that
brings it down level. Without the files it draws the balls as before.

**Tech Stack:** Python 3 + numpy + Pillow (`uv run --no-project --with numpy==2.5.5 --with pillow==11.3.0 python`), C / raylib 6.0, bash tests.

**Spec:** docs/superpowers/specs/2026-11-18-flesh-chunks-design.md

## Global Constraints

- When a man is blown apart, how many pieces fly (13), their speeds, bounces and wet marks: unchanged.
- No model file names a picture (`tests/model-images.sh`). The tool writes the same bytes on every run.
- Lamps are looked up (`EnemyLightUpload`) once a burst, never once a chunk.
- No game runs side by side (one debug log); automated runs use `CHERNOBYL_BACKGROUND=1`; no synthetic keystrokes.
- Nothing is committed or deployed without the owner asking.

## Review Focus

- 96 chunks alive at once (several men to one grenade): the frame time, and how many lamp lookups a frame.
- The browser and phone builds: the lit shader's GLSL ES path with vertex colours; the wetness picture's alpha (0 =
  light bleeds through) must survive the pack (it is a PNG: a JPEG has none).
- The files missing or the lit shader unavailable: the balls, no crash, no white or black blobs.
- A man blown apart on a stair, a deck or over a breach: chunks rest at his own floor height, as the balls did.
- A new game / a restart: the pool is cleared (`memset(g_gibs…)`), the models stay loaded, nothing is loaded twice.

---

### Task 1: the chunk builder and its files

**Files:**
- Create: `tools/gibs/build_chunks.py`, `tests/gibs.sh`
- Create: `assets/gibs/chunks.gltf`, `chunks.bin`, `meat_c.png`, `meat_n.png`, `meat_orm.png` (built)
- Modify: `tests/model-images.sh` (the gibs' model is one of the files that may name no image)

**Interfaces:**
- Produces: `python tools/gibs/build_chunks.py [--out DIR]` (default `assets/gibs`). `chunks.gltf`: eight meshes named
  `lump1 lump2 lump3 flap1 flap2 bone1 bone2 rib1`, one node each in that order, one primitive each with `POSITION`,
  `NORMAL`, `TEXCOORD_0`, `COLOR_0` (VEC4, unsigned byte, normalized) and 16-bit indices; one material `meat`, no
  textures, no `images`. Each mesh: closed, lowest point y = 0, furthest point from the y axis at 1.0.
  Pictures 256 x 256: `meat_c` RGB, `meat_n` RGB, `meat_orm` RGBA with alpha 0 everywhere.

- [ ] **Step 1: Write the failing test** `tests/gibs.sh` (files part), for each of the eight meshes: found by name;
  150 to 1400 triangles; closed (welded by position, every edge used by exactly two triangles); lowest y within 1e-4
  of 0 and at least 5% of its area within 0.03 of the floor (it lies flat); furthest from the y axis 0.98 to 1.0;
  not a ball (sphericity `cbrt(36 pi V^2) / A` under 0.86; the flaps under 0.60); the bones and the rib have at least
  12% of their vertices pale (every channel over 150) and every mesh at least 25% red (r over 90 and over twice g);
  the file names no image; the three pictures are 256 x 256, the wetness picture's alpha is 0 everywhere, its mean
  roughness (G) under 110 and mean shine (B) over 180; a second run of the tool writes the same bytes.
- [ ] **Step 2: Run** `./tests/gibs.sh` — Expected: FAIL (no `assets/gibs/chunks.gltf`).
- [ ] **Step 3: Implement the tool**: an icosphere (3 subdivisions) pushed in and out by seeded lattice noise in three
  octaves, stretched per shape, ragged where torn; bones as lathed shafts with knuckle ends and a snapped end, joined
  to lumps by overlap (each part closed); everything settled onto y = 0 (what would fall below is pressed flat);
  vertex colours from a noise field (muscle, fat, clot; bone ivory, stained toward the meat) darkened in the hollows;
  texture coordinates by the face's dominant axis; smooth normals.
- [ ] **Step 4: Build and run the test** — Expected: PASS for the files part.

### Task 2: the game draws them, lit

**Files:**
- Create: `src/gibs.h` (the gib code out of `src/main.c`, with the load and the draw)
- Modify: `src/main.c` (include it where the gib code stood; the dev hook), `tests/gibs.sh` (game part)

**Interfaces:**
- Consumes: Task 1's files; `g_skinShader`, `EnemySkinInit`, `EnemyLightUpload`, `EnemyGlowDim`, `EnemyMinLit`.
- Produces: `static void SpawnGibs(Vector3 c, Vector3 away, float floorY, float scale)`, `GibsUpdate(float dt)`,
  `DrawGibs(void)` as before; `static void GibsLoad(void)` (once; debug line `"ev":"gibs","meshes":8,"tex":3,"lit":1`,
  or `"lit":0` when it falls back to the balls). Dev: `CHERNOBYL_GIBS=<frame>` throws one burst 2.2 m ahead of the
  player at that update frame (debug line `"ev":"gibburst","n":13,"kinds":<distinct shapes>,"lamps":<lookups a frame>`).

- [ ] **Step 1: Failing test** (game part): with `CHERNOBYL_GIBS=40`, the log carries the `gibs` line with 8 meshes,
  3 pictures, `lit` 1 and a `gibburst` of 13 with at least 5 kinds and 1 lamp lookup; a screenshot 300 frames on
  differs from the same run without the hook in at least 0.2% of the picture, and the pixels that differ hold at
  least 60 distinct colours (32 levels a channel) — two flat colours is what the balls were; started where
  `assets/gibs` is missing: `lit` 0, a burst of 13, a clean exit.
- [ ] **Step 2: Run** — Expected: FAIL (no `gibs` line).
- [ ] **Step 3: Implement** `src/gibs.h`: `GibsLoad` as `MinesLoadWorld` loads the mine (the three pictures on the one
  material, tangents generated, mipmaps); `DrawGibs` one `DrawMesh` a chunk with the bone attributes zeroed, lamps
  uploaded when a chunk lies more than 2.5 m from where they were last looked up; the balls when `lit` is 0.
- [ ] **Step 4: Run** — Expected: PASS.

### Task 3: tumble, lie flat, sizes, how long they stay

**Files:**
- Modify: `src/gibs.h`, `tests/gibs.sh`

**Interfaces:**
- Produces: `Gib` gains `kind`, `ax`, `ang`, `spin`, `yaw`, `life0`; debug line at rest
  `"ev":"gibrest","i":<slot>,"kind":k,"size":m,"tilt":deg,"h":m above his floor,"life":s`; and one in flight
  `"ev":"gibfly","turned":deg` (a chunk's angle a quarter of a second after it is thrown).

- [ ] **Step 1: Failing test**: all 13 `gibrest` lines arrive; each `tilt` under 0.5 degrees and `h` within 2 mm of 0;
  sizes: two at 0.105 m or over, at least five under 0.07 m, mean 0.06 to 0.09; `life` 20 to 28 s; `gibfly` turned
  more than 20 degrees.
- [ ] **Step 2: Run** — Expected: FAIL (no `gibrest` lines).
- [ ] **Step 3: Implement**: at each throw and each bounce the time to the next touch-down follows from the height
  and the upward speed (gravity 16); the spin is set so the chunk has turned a whole number of times (1 to 3) by
  then; at rest it lies as modelled, turned to a heading of its own; it shrinks to nothing over its last second.
- [ ] **Step 4: Run** — Expected: PASS.

### Task 4: the other builds, the cost, the whole suite, the look

**Files:**
- Modify: `tools/web/pack_web.py` (`assets/gibs`: the model and its three PNGs as they are)
- Create: `tools/web/gibs.mjs` (the browser build loads them lit and throws a burst without a page exception)
- Test: every script in `tests/`, `tools/demo/test-record.sh`, the web checks, the unsigned iPhone build

- [ ] **Step 1**: the cost: frame time with 96 chunks lying in view against 96 balls (the dev hook thrown eight
  times), from the debug log's `perf` lines; lamp lookups a frame.
- [ ] **Step 2**: `pack_web.py`, `make web`, `tools/web/gibs.mjs` — Expected: PASS; unsigned `xcodebuild`.
- [ ] **Step 3**: all tests one at a time — Expected: every script PASS.
- [ ] **Step 4**: pictures from the game for the owner: a burst in the air, the chunks at rest near and far, in a
  lit hall and a dark corner, beside the old balls.
