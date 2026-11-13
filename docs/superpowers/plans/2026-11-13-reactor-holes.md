# Reactor Holes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** About a dozen unique glowing breaches in the plant's floor, with every wave enemy climbing out of one on motion-captured climbs.

**Architecture:** `tools/grid2map.py` cuts the holes into the generated map and writes one `info_hole` entity each; `src/mapload.h` reads them; `src/main.c` draws a scaled-down version of the pit's live effect at each, picks a hole edge for every wave spawn, and plays a climb clip. `tools/chef/build_enemy.py` retargets three Mixamo climbs onto every enemy and writes where the hands land.

**Tech Stack:** C + raylib (`make`), Python map generator, Blender headless + MPFB (`tools/chef/build_all.sh`), Mixamo FBX clips in `build/mixamo/`.

**Spec:** `docs/superpowers/specs/2026-11-13-reactor-holes-design.md`

## Global Constraints

- Hidden test runs only: `CHERNOBYL_BACKGROUND=1`, `--mute`, `--no-intro`; no synthetic keystrokes, no blanket `pkill`.
- Web data file stays under 100 MiB (95.6 MB today). Desktop combat run stays at today's level (median 120 fps, low about 97).
- The big pit must look unchanged. Wave budgets, alive caps, enemy stats and the priest's green rise do not change.
- Maps without `info_hole` keep today's ring spawn and floor rise.
- Nothing is committed or pushed, and the web build is not refreshed, unless the owner asks. (This overrides the "frequent commits" default.)
- The working tree already holds yesterday's uncommitted fixes in `src/main.c`, `src/intro.h`, `src/mapload.h`; build on them, do not revert.

## One change from the spec's mechanism

The spec says the clip is root-motion-free and the game lifts the enemy on a curve. Instead the clip **keeps its own hip motion**, re-based so the last frame stands with feet at the origin. The game puts the enemy's origin at his final standing spot from the first frame and the clip itself starts him down in the shaft. Real hand contact then stays fixed in the world for free, and the arm correction (Task 6) only has to absorb body-size differences. Same result on screen, less to tune.

## Review Focus

1. Player beside a hole when an enemy tops out: he must not end inside the player or shove them into a wall.
2. All edges busy late in a wave: spawns must still happen (any free edge, then a busy one with a delay), never stall the wave.
3. Enemy killed on the first climb frame and on the last: no corpse on the floor mid-air, pickup reachable, slot freed, edge freed.
4. A map with zero holes, and an enemy model with no climb clip: old entrance, no crash.
5. Player walking or dodging backward into a hole footprint at speed, and grenades/pickups landing in one: player stays out, pickups are moved to the rim.

Each is pinned by a check in the task that owns the code (Tasks 4, 5).

---

### Task 1: Climb clips on every enemy

**Files:** Modify `tools/chef/build_enemy.py` (section 4, mocap retarget; section 6, export), `tools/chef/check_clips.py`. Produces `assets/<enemy>.glb`, `assets/<enemy>_lod.glb`, new `assets/<enemy>.climb`.

**Interfaces — produces:**
- Three new clips per model, named `climb`, `climb2`, `climb3`, 60 fps timeline at real-time density (played at 60 frames/s in game):
  - `climb` = `freehang_climb.fbx`
  - `climb2` = `braced_to_crouch.fbx` then `crouch_to_stand.fbx`, crossfaded
  - `climb3` = `ledge_pullup.fbx` (trimmed where he is chest-over) then `getup_stomach.fbx`, crossfaded; fall back to `zombie_crawl` between them only if the join needs travel
- Convention: last frame standing, feet at origin, facing glTF +Z (the direction he climbs toward). Hips translation is kept.
- `assets/<enemy>.climb`: one line per clip: `name frames handZ handY plantA plantB` — `handZ`/`handY` = mean of both hands' position (metres, model space, Z forward, Y up) while planted; `plantA`/`plantB` = first and last frame both hands are planted (hand speed under 0.15 m/s and within 0.1 m of the hold height).

- [ ] **Step 1:** Add `import_fbx(name)` beside `import_bvh`: imports `build/mixamo/<name>.fbx`, returns the armature; and `MIXAMO_MAP`, a dict from the MakeHuman rig bones the BVH retarget already drives to `mixamorig:` names. Reuse `sample()`'s world-rotation transfer; do not write a second retargeter.
- [ ] **Step 2:** Add the three clips to the build for every spec (not per-enemy opt-in). Run `fist`/`grip` as other clips do so props stay held; run `declip` and `fix_knees`.
- [ ] **Step 3:** Write the `.climb` file at export.
- [ ] **Step 4:** Extend `check_clips.py`: fail if any enemy lacks the three clips, if the last frame's lowest foot is more than 3 cm from y=0, or if `handY` at `plantA` is outside 0 ± 0.25 m (hands land at final-floor height).
- [ ] **Step 5:** Build one enemy with a preview dir (`blender -b -P tools/chef/build_enemy.py -- cook assets/cook.glb <scratch>/prev`), look at the stills for the three climbs. Fix the bone map until arms and legs read right.
- [ ] **Step 6:** `tools/chef/build_all.sh`, then `check_clips.py`: all pass. Record the size change of `assets/*.glb`.

### Task 2: Holes in the map

**Files:** Modify `tools/grid2map.py`; regenerate `maps/chernobyl.map`; `tools/map/make_textures.py` only if a new rim texture is truly needed (512 px max).

**Interfaces — produces:** per hole, in the `.map`:
```
{ "classname" "info_hole" "origin" "x y z"   (centre of the opening at floor level, Quake units)
  "_size" "hx hy depth"                      (half-extents and depth, metres)
  "_edges" "<bitmask>"                       (1=-x 2=+x 4=-y 8=+y: straight, clear rim an enemy can top out over, with >=1.6 m of open floor beyond)
  "_fx" "strength tint smoke motes shafts"   (0..1 each; tint 0=deep blue .. 1=pale cyan)
  "_seed" "n" "_boss" "0|1" }
```

- [ ] **Step 1:** `HOLES` table: 12 entries (4 per zone), hand-placed on the level grid clear of spawn, stairs, platforms (`plats`), stations, doorways and the main corridor's centre line; ≥2 m walkable beside each; one `_boss` hole of 4.5 x 4.5 m in the widest open floor. Each entry: centre, half-extents (1.25–2.0 m), depth (2.5–6 m), style name, seed, fx numbers. Styles, each a distinct builder branch: `punched` (clean square, bent plate curled up), `torn` (rebar fans, slabs tilted in), `split` (long 1.3 x 4 m crack), `corner` (against a wall, wall scorched above), `sunk` (floor slumped toward it in tilted slabs), `grate` (a burst floor grating, bars bent outward).
- [ ] **Step 2:** `floor_slabs(x0, x1, zone, holes)`: replaces the one-box-per-zone floor at `main()` line ~399 with strips round the rectangular openings. No T-junction gaps: strips share full edges.
- [ ] **Step 3:** `hole(out, h)`: shaft walls (`pit` texture inward), glowing bottom (`core`), rim dressing per style from `random.Random(h.seed)`. Dressing must stay under 0.35 m high and leave every `_edges` side clear for 1.4 m of its length. One baked `light()` per hole, radius 9–14 m, colour from tint.
- [ ] **Step 4:** Keep posters, stations, hoses, pipes and pickups off hole cells (extend the `near_breach` style filters).
- [ ] **Step 5:** Regenerate; the summary line prints the hole count. Delete `maps/chernobyl.map.bake` so lighting rebakes.

### Task 3: Read the holes, draw the effect

**Files:** Modify `src/mapload.h` (entity parse near line 655–717), `src/main.c` (`ReactorLight`, `DrawReactorFX`, lines ~5770–5815; arg parsing).

**Interfaces — produces:**
```c
typedef struct { Vector3 pos; float hx, hz, depth; int edges; float fx[5]; int seed, boss; int busy[4]; } MapHole;   // raylib space; busy[e] = enemy index+1 on that edge
#define MAX_HOLES 24
static MapHole g_mapHoles[MAX_HOLES]; static int g_nMapHoles;
static void DrawGlowEmitter(Vector3 p, float size, const float fx[5], float phase);   // size 1.0 = today's pit
```
Edge bit → raylib direction: 1=-x, 2=+x, 4 and 8 follow the same axis swap the loader applies to `info_reactor`.

- [ ] **Step 1:** Before touching the effect, take the reference still of the pit: `CHERNOBYL_BACKGROUND=1 ./run.sh --map maps/chernobyl.map --no-intro --mute --no-enemies --look <yaw> <pitch> --frames 200 --shot <scratch>/pit_before.png` (find the spawn/look that frames the breach; `CHERNOBYL_AT` positions the camera).
- [ ] **Step 2:** Parse `info_hole` in `mapload.h`.
- [ ] **Step 3:** Refactor `DrawReactorFX` into `DrawGlowEmitter`; the pit calls it with size 1 and fx all at today's values. Holes call it with size = hole width / 17 scaled so the glow sits inside the shaft mouth and smoke/shafts rise at most ~4 m; counts scale with `fx` and are capped (smoke ≤4, shafts ≤3, motes ≤14 per hole).
- [ ] **Step 4:** Culling: draw nothing beyond 45 m; beyond 22 m or with the centre behind a wall (`MapRayNearest` from the camera to 1 m above the mouth), draw the core glow only. Dynamic lights: the 3 nearest holes within 25 m get one `DynLightAdd` each (radius 7–11 m, breathing with their own phase), added in `ReactorLight`.
- [ ] **Step 5:** Add `--hole N` dev flag: no enemies, camera 6 m from hole N at eye height looking at it, for stills.
- [ ] **Step 6:** Verify: pit still matches `pit_before.png` by eye; `--hole 0..11 --shot` gives twelve stills that visibly differ; log line `"holes":12` in `--debug` output.

### Task 4: Keep the player and loose things out

**Files:** Modify `src/main.c` (player move near the `g_hasHall` edge clamp ~line 4334; `PickupSpawn`/`DropSpot`; enemy move step near `ENEMY_STEP`).

**Interfaces — produces:** `static int HoleAt(float x, float z, float pad);` → hole index or -1 (footprint grown by `pad`); `static Vector3 HolePushOut(Vector3 p, float pad);`.

- [ ] **Step 1:** Player: after movement, `HolePushOut(pos, 0.35f)`, sliding along the edge (project out along the nearest side only).
- [ ] **Step 2:** Enemies not climbing: same with pad 0.3; `EnemySpawnOK` and the nav fallback reject hole cells; nav build marks footprint cells unwalkable.
- [ ] **Step 3:** Pickups and drops landing in a footprint move to the nearest point 0.6 m outside it that `MapNavReachable` accepts.
- [ ] **Step 4:** Verify (Review Focus 5): a hidden run with `CHERNOBYL_AT` placing the player at a rim and `--kite` (backpedal) for 600 frames logs the player's min distance to every hole centre ≥ half-extent + 0.3 and y never below the floor.

### Task 5: Spawn from holes and climb

**Files:** Modify `src/main.c`: clip roles (~1460, 1805), `Enemy` struct (~1443), `SpawnEnemyMap`/`EnemyRiseIn`/`SpawnEnemyT` (~2039–2085), boss spawn (~2317), enemy update (~3285), hit/knock/death paths, `LoadEnemyRig` (read `.climb`).

**Interfaces:**
- Consumes: `g_mapHoles`, `.climb` files, clips `climb`/`climb2`/`climb3`.
- Produces: roles `CL_CLIMB, CL_CLIMB2, CL_CLIMB3` (added before `CL_NROLES`, names as the clips; no fallback: `rg->hasClimb` bitmask); `EnemyRig` gains `struct { int frames; float handZ, handY; int plantA, plantB; } climb[3]`; `Enemy` gains `int hole, edge; float climbT, climbLen;`.
```c
static int  HolePickEdge(int behind, int boss, int *hole, int *edge);   // 1 if found
static void EnemyClimbIn(int i, int hole, int edge);                    // places, picks the clip, marks busy
static int  EnemyClimbing(const Enemy *e);                              // climbT>0
```

- [ ] **Step 1:** `HolePickEdge` scoring, exactly: candidates = edges set in `_edges`; skip holes under 6 m from the player and over 40 m unless nothing else; skip busy edges; score = angle to the player's facing (or its reverse when `behind`) within ±66° preferred, then distance nearest to 16 m; small random jitter. If no free edge passes, any free edge on the map; if every edge is busy, the least-recently-used edge with the spawn's start delayed 1.2 s. Boss: the `_boss` hole's best free edge, unless the player is within 6 m, then the largest other hole.
- [ ] **Step 2:** `EnemyClimbIn`: final position = edge midpoint (random ±0.5 m along the edge, kept inside its clear length) pushed `handZ`-matched so the clip's planted hands sit on the rim line; yaw faces out over the edge (away from the hole centre). Clip choice: per-type favourite (table in `kEnemy` order: heavy/boss types → `climb2`; cooks/linecooks → `climb`; mutant, revenant, fiend → `climb3`), 30% chance of another available one. Speed factor 0.9–1.15 per enemy. `hitT`/`knockT` style "can't act" lock for the clip's length.
- [ ] **Step 3:** While `EnemyClimbing`: no map collision, no nav, no separation push, no stagger/knockdown/flinch clip change (damage, blood, burn marks still apply); LOD model allowed.
- [ ] **Step 4:** Death while climbing before the last 25% of the clip: play the death clip while his origin eases back 0.6 m toward the hole centre and falls at gravity to `depth`, then state 0 with no corpse; pickup via `PickupOnKill` at the rim point (Task 4 keeps it reachable). In the last 25% he dies on the floor as normal. Always clear `busy`.
- [ ] **Step 5:** On finishing: clear `busy`, `HolePushOut`, and if within 0.9 m of the player push him back along his facing (Review Focus 1); enable collision.
- [ ] **Step 6:** `SpawnEnemyT`: if `g_nMapHoles>0` and the rig has any climb clip → `HolePickEdge` + `EnemyClimbIn`; else today's path. The priest's summon and corpse-raise call sites keep calling `EnemyRiseIn` directly; confirm they do not go through `SpawnEnemyT`, and split a `SpawnEnemyRing` if they do.
- [ ] **Step 7:** Debug log events `climb` (i, hole, edge, clip) and `climb_done`/`climb_drop`. Verify with hidden runs:
  - `--stress --god --frames 3000 --debug`: every `spawn` has a `climb`; no `stuck`/`embed` events; count of `climb` with a busy edge reported (Review Focus 2).
  - `--enemy cook --autofire` aimed at a hole via `--hole N` variant with enemies on: `climb_drop` events appear, a pickup position outside every footprint, no enemy left in state 2 below the floor (Review Focus 3).
  - A map without holes (`--map` any other in `maps/` or the built-in arena) and a temporarily renamed `.climb`: old entrance, no crash (Review Focus 4).
  - `--boss-wave`: boss logs a climb from the `_boss` hole.

### Task 6: Hands on the rim, and the detail

**Files:** Modify `src/main.c` (pose build ~1884–1930 where twist/lean are applied; enemy draw; sounds).

- [ ] **Step 1:** Two-bone IK per arm (shoulder–elbow–hand; bone indices found by name at rig load, alongside `bHandR`): between `plantA` and `plantB` (blended in/out over 6 frames) pull each hand to its own planted world point = where the clip put it at `plantA`, snapped to the rim top (y = floor) and to the rim line. Limit correction to 0.25 m; beyond that leave the clip alone.
- [ ] **Step 2:** Effects: at `plantA` and at the leg-over (60% of the clip) spawn 4–6 grit puffs (`SpawnSmoke` cold) and `SpawnImpact`-style chips at the hand points; while climbing, drag two smoke puffs a second up from the mouth; replace the green rise tint with the hole's tint lighting him from below, fading over the last 1.2 s (reuse the `riseT` glow path with a colour parameter).
- [ ] **Step 3:** Sound: `enemy_appear.mp3` at the start if it is used today for the rise; a hand-on-concrete hit only if an existing asset fits (`bullet_wall.wav` pitched down, quiet); no new audio files.
- [ ] **Step 4:** Verify with stills: `--hole N` with one enemy type forced (`--enemy`), `--shot-every` through a climb, for cook, a heavy type, mutant and a boss, at two different holes. Hands on the rim within a few cm, no body through the floor outside the opening, props not buried. Adjust per-type clip choice or a per-type hand offset where a prop clips badly.

### Task 7: Whole-game checks

- [ ] **Step 1:** Combat run as yesterday's (hidden, `--god`, autofire, wave play, ≥ 3000 frames): median/low fps, zero stuck enemies. Compare with 120/97.
- [ ] **Step 2:** `tools/soak.sh` (perf is noisy: judge on the median).
- [ ] **Step 3:** Web build compiles and starts in headless Chrome (`tools/web`), data file size reported and under 100 MiB; iPhone build compiles. Neither is published.
- [ ] **Step 4:** Update PLAN.md's content-sourcing note only where it now misstates the pipeline (Mixamo clips are used for climbs; `mixamo_fetch.sh` endpoint fixed to `/monitor`, `model` key removed).
- [ ] **Step 5:** Report to the owner: what was checked by stills and logs, what only they can judge in motion (climb feel, pacing, difficulty).
