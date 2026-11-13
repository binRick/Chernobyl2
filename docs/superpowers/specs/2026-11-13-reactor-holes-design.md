# Reactor holes: enemies climb out of glowing breaches

Date: 2026-11-13. Status: draft for approval.

## Intent

The owner likes the reactor pit added on 13 Nov: the hole past the east wall with the blue
light, smoke, light shafts and burning motes coming out of it. He wants **many of those, each
unique, around the level**, and he wants enemies to **crawl out of them** instead of rising off
the floor on their backs, with a **highly detailed** climb-out animation.

Why (owner's words, as far as given): he likes the look of the pit. The inferred reason for the
rest, which is the builder's reading and not the owner's statement: enemies appearing out of
flat floor has no source, and the pit is the most striking thing in the map, so make it the
place the enemies come from.

## What exists today

- **The pit** is scenery. `tools/grid2map.py` `breach()` builds it beyond the east wall
  (`PIT`, 20 m x 32 m, outside the `_hall` rectangle, so it can't be walked). The map carries
  one `info_reactor` entity; `src/mapload.h` reads it into `g_mapReactor`; `src/main.c`
  `ReactorLight()` and `DrawReactorFX()` hang the live effect there (two dynamic lights, ten
  smoke rolls, a stacked blue glow about 17 m across, seven light shafts, ninety motes).
- **Spawning.** `SpawnEnemyMap()` picks a spot in a ring 9-18 m round the player, biased
  ahead of them, or behind when `behind` is set (the anti-kiting rule). The spot must be
  reachable on the nav grid, clear of walls, and flat (`SpawnFloorFlat`).
- **Entrance.** `EnemyRiseIn()` plays the get-up half of the `knockdown` clip from frame 50,
  with green vapour. `riseT` runs while he rises; he can be shot but can't act.
- **Enemy models** are not Mixamo. `tools/chef/build_enemy.py` (Blender, headless) builds
  eleven MakeHuman bodies and retargets free CMU motion-capture takes onto them
  (`tools/chef/fetch_mocap.sh` downloads the takes; no account needed). Clips are found by
  name in `LoadEnemyRig()`. The older Mixamo pipeline (`tools/mixamo_fetch.sh`) is unused.
- **The map** is generated: `tools/grid2map.py` writes `maps/chernobyl.map` from Chernobyl 1's
  level grid (4 m cells, hall 112 m x 72 m, floor at z=0 as a few very large slabs). Lighting
  is baked at first load and cached in `maps/chernobyl.map.bake`.
- **Limits.** Web data file is 95.6 MB against GitHub's 100 MiB cap. Desktop holds a
  120 fps median in the combat run. Up to 16 enemies alive, 24 hard cap.

## Goals

- **About a dozen holes** broken through the walkable floor, spread over all three zones
  (turbine hall, control corridor, reactor side) so that from anywhere in the hall at least
  one is within roughly 20 m.
- **Each one unique.** They differ in outline (round, torn, a long split, a collapsed corner),
  width (about 2.5-4 m; one larger for bosses), depth, rim (torn rebar, buckled floor plate,
  cracked tile, slumped concrete, matching the zone's floor), glow strength and tint within
  the pit's blue, and what comes out (more smoke, more motes, light shafts, steam). Nobody
  should see two alike.
- **The same family as the pit.** Each has the live blue light on the surroundings and on
  bodies in front of it, glow in the shaft, smoke and motes, scaled down to its size.
- **Enemies climb out.** Every ordinary spawn is an enemy hauling himself out of a hole:
  hands come over the rim first, he pulls up, gets an elbow and chest over, swings a leg up,
  and stands. Real motion capture, not hand-posed. Hands must land on the rim, not float or
  sink through the floor.
- **Still fair.** He can be shot while climbing and can't act until he is up (as now). Part
  of each wave still comes from behind the player.
- **No green floor pool.** The old rise-off-the-floor entrance and its green vapour go, on
  this map. Maps without holes keep the old entrance.

## Non-goals

- No change to enemy types, stats, wave budgets or bosses' behaviour.
- The player cannot enter, fall into or be hurt by a hole.
- No new enemy models. No change to the big pit itself.
- The web build and site are not refreshed as part of this; that stays a separate ask.

## Anti-goals

- Holes that are copies of one another with a different size.
- An enemy who pops up, slides, or stands up through solid floor: the climb must read as
  weight on the hands.
- Enemies stuck in or beside a hole, or the player snagging on rims in a fight.
- Frame rate falling below today's on desktop, or the web data file passing 100 MiB.

## Approaches

Where the climb animation comes from was the open choice.

1. **Motion capture (chosen by the owner).** A real climb, fitted to each hole. Looks the
   most natural.
2. **Hand-posed in code or Blender.** Needs no source material, but full-body climbs made
   this way tend to look stiff. Rejected by the owner's pick.

Source of the capture, as the owner decided: **Mixamo**. He was first offered "Mixamo, with
a login token"; the builder then proposed CMU takes instead (the enemies are MakeHuman rigs
fed by CMU takes, not Mixamo rigs); the owner answered by offering his logged-in Mixamo
session, so Mixamo it is. Nine candidate clips were downloaded on 13 Nov into
`build/mixamo/` (ignored by git; FBX, 30 fps, on Mixamo's stock skeleton): `freehang_climb`
(free hang to standing), `braced_to_crouch`, `crouch_to_stand`, `ledge_pullup`, `climb_wall`,
`hang_idle`, `climb_to_top`, `zombie_crawl`, `getup_stomach`. The Mixamo token lasts about an
hour; if more clips are wanted later the owner's Safari session is asked again. The repo's
`tools/mixamo_fetch.sh` is stale (wrong status endpoint: it is `/monitor`, and the export
body must not carry a `model` name).

## Design

### Holes in the map

- `grid2map.py` gets a table of holes: position, a seed, and a style. Each is built by one
  function driven by the seed and style, in the manner of `breach()`: the floor is opened
  (the big floor slabs are split round the opening), a shaft goes down a few metres with
  scorched walls, a glowing bottom (`glow_core`), and a broken rim.
- Placement is hand-picked in the table, not random: clear of the player spawn, stairs,
  platforms, control stations and doorways, and not blocking any corridor (leave at least
  2 m of walkable floor beside each).
- Each hole writes an entity (`info_hole`) carrying its centre, radius, depth, facing of its
  best climbing edge, and style numbers for the live effect. One baked light per hole, small.
- The rim is the barrier: a low kerb of broken floor the player and enemies collide with.
  Nav treats the opening as not walkable.
- New textures only if needed, and small; reuse `wall_scorched`, `glow_core`, `rubble`,
  `steel`.

### Live effect

- `DrawReactorFX` / `ReactorLight` are generalised to draw one emitter from parameters
  (size, strength, tint, smoke/mote/shaft amounts, a phase offset so none breathe in step).
  The big pit becomes one call with today's numbers and must look unchanged.
- Cost control: holes beyond a distance, or with no line of sight, draw nothing or a cheap
  glow only; dynamic lights go to the nearest few holes (the light budget is shared with
  muzzle flashes and fire).

### Spawning

- On a map with holes, `SpawnEnemyMap` chooses a hole instead of a ring spot: among holes
  about 9-40 m from the player, prefer ones ahead of the player, or behind when `behind` is
  set; prefer ones not currently in use by another climber; avoid a hole the player is
  standing beside (under about 6 m) unless there is no other.
- **Pace wins over tidiness** (builder's call). Each hole has two to four climbing edges
  according to its size, one climber per edge. The spawn tick is 0.9 s falling to 0.45 s and
  a climb lasts 3-4 s, so late waves need about eight free edges at once; if no preferred
  edge is free the spawn takes any free edge on the map rather than wait. Wave budgets and
  alive caps do not change. The game will play differently (enemies start further off, from
  places the player can learn); that is what was asked for, and it is judged by the owner
  in play, not tuned in advance.
- **Bosses** always use the largest hole, wherever the player is, unless the player is within
  6 m of it, in which case the next largest that fits him. The existing boss entrance sound
  announces him.
- **The priest's summons and raised corpses keep the old green rise** where they stand; that
  is his own magic and the boss fight is not to change. Only wave spawns use the holes.

### The climb

- New clip roles built into every enemy by `build_enemy.py`, retargeted from the Mixamo
  clips onto the MakeHuman rig (a new step: the script retargets CMU BVH today, so it needs a
  Mixamo-skeleton bone map). Roughly three to four seconds: reach, hang, pull, elbow and
  chest over, leg up, stand.
- **More than one climb** (builder's call), so a crowd doesn't move in unison: at least a
  clean pull-up to standing (`freehang_climb`), a braced climb to a crouch then up
  (`braced_to_crouch` + `crouch_to_stand`), and a drag over the rim onto the stomach then up
  (`ledge_pullup` or `zombie_crawl` into `getup_stomach`). Each enemy type favours one that
  suits it; speed varies a little per enemy.
- In game the enemy starts below the floor inside the shaft, facing out over the hole's
  climbing edge. The clip is root-motion-free in the file; the game moves him up and
  forward on a curve matched to the clip so his hands stay on the rim, and he ends standing
  on walkable floor just outside the kerb. Collision with the map is off for him until he is
  up; bullets, flame and blasts hit him throughout.
- Hands are pinned to the rim with a small arm correction (two-bone IK on each arm) while
  the clip says they are planted, so different body sizes and hole rims all meet.
- Detail on top of the clip: dust and grit knocked off the rim where the hands land, smoke
  dragged up with him, the blue light on him from below fading as he stands (replacing the
  green glow), and a sound for hands on concrete if a suitable one exists in `assets/`.
- **Shot while climbing** (builder's call): a climber who dies lets go and drops back down
  the shaft; no corpse is left on the floor, so the priest cannot raise him. His pickup
  appears on walkable floor at the rim. A climber cannot be knocked down or staggered; damage
  still counts.
- **Things in his hands** (builder's call): weapons and tools stay in his hands and lie over
  the rim as he grips it. Checked in stills per type; the worst clippers get a per-type
  adjustment. No slinging or hiding of props.
- Enemies without the `climb` clip (older models) fall back to the old entrance beside the
  hole.

## Left to the builder

- Exact hole positions, styles and numbers; which Mixamo clips and cut points.
- How the floor slabs are split, and whether the kerb or an invisible wall does the blocking.
- The culling distances and how many holes get a dynamic light.
- Test hooks. Follow the project rules: hidden runs with `CHERNOBYL_BACKGROUND=1`, no
  synthetic keystrokes, muted.

## How it will be checked

- Stills of every hole, to confirm they differ, and stills through one climb for each of a
  few enemy types (small, large, boss) to confirm hands on the rim and no clipping.
- A hidden combat run: no stuck enemies, frame rate at today's level.
- Web and iPhone builds still compile; web data file size reported.
- The owner watches a climb in motion; stills cannot judge that.
