# PLAN — Chernobyl 2: LIQUIDATOR

**From combat sandbox to a complete, shippable game.**

This plan was produced from a full audit of the codebase (engine, gameplay, content,
structure), three competing game designs (campaign / roguelite / extraction) judged
comparatively for *interesting, challenging, complete*, and a synthesis of the winner
plus the best ideas from the losers. Line references are to the code as of 2026-07-22.

---

## 1. The verdict

**Build a run-based irradiated horde FPS** — Devil Daggers pressure wearing real FPS
gunplay, on the existing Xonotic arenas.

> You are a liquidator dropped into derelict, irradiated sectors to purge mutant
> infestations. A run is a loadout, a chain of arenas, and escalating waves of
> flow-field-driven mutants that corner you with real pathfinding. Kills drop scrap
> you draft into upgrades between waves; style (headshots, streaks, aggression) banks
> **rads**, the meta-currency that permanently unlocks the rest of the weapon roster.
> Standing still raises your **exposure** and the horde's speed with it. Death is
> final, the score is the point, and the next run starts thirty seconds later.

**Why this design won** (unanimous, 3 judges: ~33/40 vs extraction ~31, campaign ~26):

- The codebase is **80% of a horde roguelite already**. The one-goal flow-field nav
  (`MapNavBuild/MapNavUpdate/MapNavSteer`, `mapload.h:437–572`) is purpose-built for
  hordes: one BFS field feeds every enemy. `SpawnEnemyMap`'s reachability-validated
  ring placement (`main.c:871–901`) *is* a wave director's spawn function, finished.
- `HurtEnemy` (`main.c:1021`) is the single damage funnel for every weapon, so
  per-weapon damage and upgrade multipliers are a one-struct change. The per-map outer
  loop (`main.c:2771–2785`, `StartSelectedMap`/`EndCurrentMap`) *is* the run/sector
  structure. The map picker (`RunMapSelect :2387` + `maps/shots/`) is a meta screen
  for free. The ESC-menu `MenuRect` code is the draft-card UI for free.
- It **avoids everything the audit flags as structurally hard**: no doors/movers (the
  world is baked static), no second concurrent nav goal, no patchDef2 curve parsing,
  no multiplayer, no art team. DM arenas get used as what they are: arenas.
- Campaign lost on shipping probability (9 authored levels of solo pacing/narrative on
  maps designed as DM loops); extraction lost on balance-system count. Their best
  mechanics are grafted in below.

**Design pillars** — every task in this plan serves one of these:

1. **Interesting** = decisions under pressure: target priority, ammo economy, draft
   picks, push-your-luck extraction, map hazards as weapons.
2. **Challenging but fair** = every death legible and dodgeable: telegraphed attacks,
   directional damage, reachability-gated spawns, skill ceiling via headshots and
   recoil control — never stat-inflated sponges.
3. **Complete** = boot → title → run → death → score → meta-progress → go again, with
   options that persist, no silent events, no dev scaffolding in the shipped build.
   Every milestone ends in something playable; the game survives every cut line.

---

## 2. Where we actually are

The sandbox is deeper than PROGRESS.md says (player HP/death, enemy melee, headshots,
flame/knife/grenade damage all work), but it is a sandbox:

| Have (leverage it) | Missing (this plan builds it) |
|---|---|
| 9 wired weapon slots (8 guns + knife: grenade launcher, Remington pump, minigun, LMG, AK-74M, MP5, Benelli, flamethrower — the M16A3/rifle.glb was cut in the roster rework and is now an orphaned spare), per-weapon fire behavior/sounds, viewmodel rig system | Ammo (none exists — R is a cosmetic anim, `StartReload :1319`), per-weapon damage (all hitscan = flat 34, `:1096,:1111`), pellets/spread, camera recoil |
| Enemy AI: flow-field chase, stairs, ledges, occlusion cull, corpses, headshot box | Win/lose structure of any kind; waves (fixed 5 spawn, 1:1 infinite respawn `:1261`); enemy variety (one melee archetype) |
| Player HP, death overlay, hurt flash — but `g_godMode` defaults ON (`:844`) | Fair melee (currently untimed 18 hp/s touch-drain `:1242`), difficulty, economy, pickups, objectives |
| 14 Xonotic `.map` arenas, textures, levelshots, map picker, per-map loop | Entity use: 268 health + 244 armor + ~250 ammo + ~90 weapon items, 26 `trigger_hurt`, 66 jump pads are parsed and **discarded** (`mapload.h:600–616`) |
| Baked AO/shadow lighting, liquids/scroll shader, blood/scorch/smoke/flame FX | Music (zero `LoadMusicStream` calls), ambient, ~10 missing/stand-in SFX, fonts, title screen |
| JSONL `--debug` harness, `--shot/--frames/--map/--no-enemies` headless flags | Tests/CI, determinism (`--seed`), packaging (.app), Windows/Linux builds, license audit |

Known engine truths this plan respects (from the engine audit):

- **Bullets don't hit map walls.** `Fire()` raycasts only the legacy procedural arena
  (`RaycastWorld :970–984`), never `MapRayNearest`. First thing M1 fixes.
- **The world is static.** Doors/rotators are baked in place; lighting is baked into
  vertex colors; the nav grid is baked at load. Nothing in this plan moves geometry.
- **One nav goal.** The flow field seeds from the player only. Nothing in this plan
  needs a second concurrent goal (bonus objectives are *positions*, not nav goals).
- **Nav tuning is fragile and afterslime-calibrated** (`NAV_WALLR`/`ENEMY_MOVE_R`/
  `NAV_CLEAR`, PROGRESS.md warning). Arena roster is chosen by **soak test, not
  wishlist**. Difficulty *presets* never scale enemy speed or nav radii; the
  mechanics that DO scale speed (Runner variant, exposure penalty) are individually
  soak-tested on stairs/ledges before they ship, because speed interacts with the
  tuned nav following.
- **Map switching leaks** (`g_mapCol.tri`, grid arrays, `mapload.h:337–367,668–671`)
  — fixed before anything chains maps (M3, non-negotiable ordering).
- 14/15 maps use `patchDef2` curves the parser drops (visual + collision holes).
  Afterslime plays fine regardless — roster selection filters for this; parsing
  patches is explicitly **out of scope**.

---

## 3. The game

### Vocabulary (three meters, never conflated — in code and on the HUD)

| Term | What it is | Lifetime |
|---|---|---|
| **Scrap** | In-run currency dropped by kills, spent in between-wave drafts | Dies with the run |
| **Rads** | Meta-currency banked at run end from score/style; unlocks weapons | Permanent (`profile.txt`) |
| **Exposure** | Anti-camping meter (the geiger/dosimeter). High = faster horde | Per-run, moment to moment |

### The loop

- **30 seconds:** sprint and jump-kite while the pack converges from multiple
  stairwells; pick a lane, dump a mag, land a headshot (3× — already in `HurtEnemy`)
  for bonus rads, reload in the gap you made, vacuum scrap, keep moving because
  exposure climbs when you don't.
- **30 minutes:** title → loadout from unlocked weapons → Sector 1 (afterslime),
  waves with drafts between → **extract-or-push choice**: bank rads now, or continue
  to Sector 2 at a rising rad multiplier → Sector 2 (glowplant) → Sector 3 (atelier)
  → die or extract → results (waves, kills, headshots, accuracy, time, score) → rads
  bank, thresholds unlock the next weapon → next run, different loadout, higher
  per-map best.

### Systems (each maps to a milestone)

1. **Honest combat** (M1): per-weapon damage/pellets/spread/recoil, finite mag +
   reserve, fair telegraphed enemy melee, bullets stop at walls. God mode OFF.
2. **Wave director** (M2): per-wave point budget buys a composition — base Mutants,
   **Runners** (fast, fragile, red tint), **Brutes** (slow, tanky, 1.25× scale),
   later **Glowers** (leave a timed radiation pool on death via the existing
   `AddScorch` + a hurt volume — area denial that shrinks kiting lanes). Variants are
   tint/scale/stat changes to the one existing model — zero new assets.
   **Anti-kiting is an M2 acceptance criterion, not a hope**: a fraction of each wave
   spawns flank/behind-biased (invert `SpawnEnemyMap`'s facing bias), and Runner
   speed exceeds player sprint so backpedal-circling loses.
3. **Economy** (M3): scrap drops; a 3-card draft **every 2–3 waves** (skippable for
   bonus scrap so pacing stays in the player's hands); card pool capped at 6–8
   effects, all flat multipliers through the `Fire()`/`HurtEnemy` funnel (+dmg, +mag,
   +move, +maxHP, reserve refill, heal) — **no per-weapon bespoke cards in 1.0**, and
   damage stacking has diminishing returns so ammo scarcity stays the core pressure.
   Map-authored `item_health/item_*ammo` points become pickups (professional
   placement for free).
4. **Run & meta** (M4): 3-sector chain over the existing map-switch loop; extraction
   bet between sectors; `profile.txt` (banked rads, unlock bitmask, per-map bests).
   Unlock ladder: **start with Remington pump + AK-74M** (readable, skill-based,
   knife always available) → MP5 → Benelli → LMG → minigun → grenade launcher →
   flamethrower. Pacing target: full roster in ~6–8 runs (telemetry-checked, see §6).
5. **Pressure & feel** (M5): exposure meter with synthesized geiger audio;
   `trigger_hurt` lava/slime damages player *and* enemies (kiting the horde through
   hazards is mastery); far-cell bonus objectives — argmax over the existing
   `g_navDist` BFS array places an artifact at the farthest reachable point, fetch
   before the next wave for bonus rads (pins the player in space; the cheapest
   concrete anti-camping tool). Hitmarkers, directional damage, hitstop, camera
   recoil.
6. **Front door & fairness** (M6): title/options/credits, difficulty presets,
   accessibility. **Presets scale wave budget, spawn delay, and incoming damage only
   — never enemy speed or nav radii** (protects the fragile nav tuning). God mode
   survives as a menu-exposed assist toggle; one "reduced incoming damage" modifier
   guarantees 1.0 is finishable by non-experts.
7. **Ship** (M7): perf gates, leaks, bake cache, .app bundle, license compliance,
   itch.io.

**Tutorial for free:** `leave_em_behind` (a zero-combat traversal map already
committed) becomes a 90-second warmup run teaching movement, pickups, and the geiger
before the first real sector.

---

## 4. Milestones

Each milestone ends in something a stranger could play. Don't start M(n+1) until
M(n)'s "Done when" holds.

### M0 — Decisions & scaffolding (½ day, do immediately) ✅ DONE 2026-07-24

> Completed: `docs/LICENSES.md` (verdict per asset; all in-use weapon GLBs are
> CC-BY-4.0 per their embedded Sketchfab metadata; mp3s are unknown-provenance
> placeholders — NOT from the C&C rip folder, checksum-verified), `--seed` flag,
> run.sh exit-code fix + opt-in `--debug`, `tools/soak.sh` (asset-presence +
> per-map JSONL assertions), synthesized CC0 `knife_swing.wav` (was a missing
> file the code referenced). Soak passes on **all 14 maps**, not just the gate
> three.

The two things that are catastrophic to discover late, plus harness fixes everything
else leans on:

- [ ] **License decision, now not at ship time**: the game is **free, source
      published** (satisfies Xonotic GPL for maps/textures). Inventory every
      Sketchfab weapon GLB's license into `docs/LICENSES.md`; any model that forbids
      redistribution gets swapped (unwired `rifle.glb`/`shotgun.glb`/`sawnoff.glb`
      are on-disk spares). **v1 `Chernobyl/sounds/` files are presumed C&C rips until
      proven otherwise — nothing ships from there without provenance.** Music/SFX
      come from CC0/CC-BY sources (§7).
- [ ] `--seed` flag (`SetRandomSeed` next to the CLI parse at `main.c:2565–2574`) so
      wave/balance runs are reproducible.
- [ ] Fix `run.sh` swallowing exit codes (`grep || true`, run.sh:23) so scripts can
      detect crashes; make `--debug` opt-in rather than always-on.
- [ ] Headless soak script: loop all 14 maps `--map <f> --frames 300 --debug`,
      asserting the JSONL map/nav/shutdown events (`tris>0`, nav `walkable>0`, clean
      exit) **plus asset-presence checks** (audio loads currently fail silent —
      `frameCount>0` assertions catch a shipped silent game). This script is the
      regression gate for every later milestone.

**Done when:** soak passes on afterslime/glowplant/atelier; `docs/LICENSES.md` exists
with a verdict per asset.

### M1 — Honest combat (lethal, fair, differentiated) ⚙ CODE COMPLETE 2026-07-24 — playtest pending

> Implemented: map-brush hitscan (bullets stop at walls; phantom arena geometry no
> longer eats bullets on maps), MapRayNearestEx real decal normals, per-weapon
> dmg/pellets/spread/recoil + full mag/reserve ammo (slot-resident — never rides
> the Activate/Stash swap; `--cycle` soak check proves it), reload-on-anim-complete
> (benelli feeds shell-by-shell), AK 3-round burst, shotgun cock-between-shots
> (`shotgun_cock.mp3` finally wired — the pump *anim* stayed off: the Shot clips
> already animate the action and the shell-Reload clip would block firing),
> timed melee window (0.45× through the attack clip, 1.2 s cooldown, 16 hp;
> legacy drain behind `--melee-drain` for A/B), god mode OFF by default (G = cheat,
> tags the run), synthesized explosion/hurt/heartbeat WAVs wired. Adversarially
> reviewed (8 confirmed findings fixed, incl. dead-map map-switch freeze and
> map-change run-reset consistency). **Remaining for "Done": a human god-mode-off
> playtest on afterslime — deaths dodgeable, weapons distinct.**

**Goal:** afterslime with god mode off is genuinely dangerous and every weapon feels
distinct. Playable as a hard sandbox.

- [ ] Route `Fire()` (`main.c:1093–1123`) through `MapRayNearest` so hitscan stops at
      brush walls (`worldDist = min(arena, map)`) — copy the flamethrower LOS pattern
      at `:1570–1571`. Bullets currently penetrate every map wall and can hit enemies
      through them.
- [ ] Extend `MapRayNearest` (`mapload.h:370–393`) to return hit normal + triangle
      index (data already in scope at `:385`); kill the faked decal normal at
      `main.c:1584` so sparks/scorch sit on real surfaces.
- [ ] Add `dmg, pellets, spread, magSize, reserve, recoilKick` to the Weapon struct
      (`:185–222`); replace flat `ENEMY_DMG_PER_SHOT=34` at `:1111`; pellet-loop with
      cone spread for Remington/Benelli.
- [ ] **Ammo**: mag/reserve gate `Fire()`; `StartReload` (`:1319`) refills on anim
      completion; HUD counter in `DrawHUD` (`:2159`). ⚠ **Ammo state must ride BOTH
      directions of the `ActivateWeapon` (`:390`) / `StashActiveTuning` (`:287`)
      working-set swap from the first commit** — this is the most likely
      silent-corruption bug in the whole plan. Add a 9-weapon switch-cycle regression
      check to the soak script the same day.
- [ ] Wire the dead paths: AK burst=3 (`:1524–1526`), Remington/Benelli pump
      `autoReload` (`:1536–1539`), and finally load the on-disk `shotgun_cock.mp3`.
- [ ] **Melee fairness rework, behind a flag**: replace untimed 18 hp/s touch-drain
      (`:1242`) with one damage window synced to a chosen frame of the attack clip
      (cursor already tracked), ~0.35 s windup after the roar (`:1181–1183`), ~1.2 s
      cooldown. A/B against the drain before deleting it — this touches the tuned AI
      attack state, and getting it wrong either neuters the horde or reintroduces
      unreadable chip damage.
- [ ] Flip `g_godMode` default to 0 (`:844`); `G` stays as a cheat that tags the score.
- [ ] Combat audio holes: grenade explosion (`GrenadeExplode :1062` is silent), knife
      swing (loader at `:2718` already references the missing file), player-hurt +
      low-health loop — CC0 sources per §7.

**Done when:** god-mode-off session on afterslime: you can die, every death was
dodgeable, all weapons have distinct damage/ammo identity, soak + weapon-cycle checks
green.

### M2 — The wave director ⚙ CODE COMPLETE 2026-07-25 — perf-gate soak + playtest pending

> Implemented: infinite-enemy sites deleted (death frees the slot; the embed
> self-heal relocates WITHOUT healing), MAX_ENEMIES 24, enemy type table
> (walker / runner 6.2 speed 60 hp red / brute 250 hp 0.75× speed 1.25× scale)
> with per-type hitboxes+draw+melee — nav radii untouched by design; wave
> director (budget `8+5w`, alive-cap `min(7+w,16)`, trickle, ~35% flank-behind
> spawns, 6 s breathers, 3 s opening grace); death ends the run → scored results
> screen (wave/score/kills/headshots/accuracy/time) → map picker; wave banner +
> synthesized start/clear stings; `--stress`/`--god`/`--fps` + frame-time p95
> logging + soak perf gates; map picker honors `--frames`. Reviewed inline
> (agent review died on infra; no confirmed defects). **Pending:** perf-gate
> soak needs an unlocked display (watcher armed); playtest — wave 10 reachable,
> composition felt, backpedal-circling dies (runner is 6.2 vs 5.0 backpedal;
> full sprint still escapes — tune in playtest if kiting survives).

**Goal:** survive escalating waves on afterslime until death; a run has a beginning,
a middle, and a results screen. **This is the moment it becomes a game.**

- [ ] Delete the two infinite-enemy sites: 1:1 respawn-on-death (`:1261`) and the
      wedge self-heal respawn (`:1176–1177`).
- [ ] **Perf gate first** (judges: don't tune waves on unverified headroom): raise
      `MAX_ENEMIES` (`:795`) 12 → 24 and soak at 120 fps AND throttled to 30 fps
      (physics is dt-scaled, no fixed timestep — behavior differs). Watch the O(n²)
      separation pass (`:1265–1278`), 3-ray/enemy occlusion cull (`:1989–1998`), and
      `SpawnBlood`'s O(n·4096) pool scan (`:934–968` — replace with a cursor
      free-list if it shows up). Only then tune wave budgets around the real cap.
- [ ] Wave director on top of `SpawnEnemyMap`: per-wave point budget, trickle
      spawning to an alive-cap, wave-clear detection, ~6 s breather.
      **Anti-kiting acceptance criteria:** a fraction of each wave spawns
      flank/behind-biased (invert the facing bias in `SpawnEnemyMap`), and Runners
      outrun player sprint — verify by playtest that backpedal-circling dies.
- [ ] Per-enemy `speed/hp/touchDmg/scale/tint` fields; **Runner** (~1.35×, 60 hp, red
      tint) and **Brute** (250 hp, 0.75×, 1.25× scale) via `DrawModelEx` params in
      `DrawEnemies` (`:1980`). Soak Runner speed on stairs/ledges — speed multipliers
      interact with the tuned nav following.
- [ ] Results screen: extend YOU DIED (`:2183–2192`) with waves/kills/headshots/
      accuracy/time/score; ENTER sets `g_returnToMenu` (`:852`) instead of
      `RespawnPlayer` (`:1365`) — death ends the run. Accuracy needs a shots-fired
      counter in `Fire()` (per pellet-volley, not per pellet) — add it here, it
      doesn't exist yet.
- [ ] Wave HUD (wave number, remaining, breather countdown) + announcer stings (§7).

**Done when:** a run on afterslime is win-less, loss-ful, and scored; wave 10 is
reachable by a decent player and feels different from wave 2 (composition, not
sponges); 30 fps soak clean at 24 enemies.

### M3 — Scrap and the draft

**Goal:** the full single-arena economy loop. One-map horde roguelite, complete.
**This is the first cut-line-complete game** (§5).

- [ ] Scrap drops in `HurtEnemy`'s death branch: magnetized billboard sprites using
      the `g_smokeTex` procedural-texture pattern (`:2589`). Rads for
      headshots/streaks on top of existing `g_kills`/`g_headshots`.
- [ ] Draft UI on `MenuRect`/`DrawMenu` (`:1328`, `:2250–2267`): 3 cards, **every 2–3
      waves, skippable for bonus scrap**; pool of 6–8 flat effects through
      `Fire()`/`HurtEnemy`; diminishing returns on stacked damage.
- [ ] **Store entities instead of discarding them** (`mapload.h:600–616`): keep
      `{classname, transformed pos}` using the existing Quake→raylib transform
      (`:650–655`). Spawn health/ammo pickups at `item_health_*`/`item_cells`/
      `item_bullets` points; per-wave respawn timers; pickup SFX.
- [ ] **Fix the map-switch leaks NOW**, before anything chains maps: free
      `g_mapCol.tri` + grid arrays in `EndCurrentMap` (`main.c:2548`; allocs at
      `mapload.h:337–367`, `:668–671`); clear particle/blood pools (blood currently
      lives forever, `life=1e9`).

**Done when:** a full afterslime run — waves, drafts, pickups, death, score — with no
leak growth across 10 consecutive runs (measure RSS in the soak script).

### M4 — The run and the meta

**Goal:** three-sector runs with permanent weapon unlocks. Structurally complete
roguelite.

- [ ] **Run-state struct** (judges: don't use loose globals): `{hp, scrap, loadout,
      upgrades, sectorIdx, radMultiplier}` with documented reset boundaries —
      `StartSelectedMap` (`:2494–2557`) resets per-map state and must NOT touch it;
      run start/end are the only writers. Sector transitions loop
      `EndCurrentMap`/`StartSelectedMap` instead of returning to menu (the outer loop
      at `:2771–2785` already supports this).
- [ ] **Extract-or-push** between sectors: one menu card — *bank rads now at 1×* vs
      *continue, next sector at rising rad multiplier*. The bet needs a real stake:
      **extraction banks 100% of run rads; death banks 50%** — forgiving enough to
      always progress, sharp enough that pushing greedy costs something. (The single
      most interesting mechanic grafted from the extraction design; near-zero code on
      existing menus.) There is deliberately **no mid-run save** — a run is one
      sitting (~25–35 min max); quitting mid-run counts as death.
- [ ] **Sector list is data-driven** (a text table: map, wave count, budget curve) so
      a map that fails nav soak is swapped without touching code. Launch chain:
      afterslime → glowplant → atelier; both new arenas nav-soaked first (the AI was
      tuned on afterslime only — this is the plan's top playability risk).
- [ ] Store ALL `info_player*` spawns (parser keeps first-only, `mapload.h:605`; ~80
      per map discarded); pick spawn farthest from the last death per sector.
- [ ] `profile.txt` (Load/SaveOptions pattern, `:1335–1343`): banked rads, unlock
      bitmask, per-map bests. Write on run end and quit.
- [ ] Loadout select in `RunMapSelect` (`:2387–2467`): locked weapons greyed with rad
      cost; primary + sidearm + knife; gate the 1–9 keys on the bitmask.
      Start: **Remington + AK-74M**. Unlock pacing target: full roster in ~6–8 runs —
      verify from JSONL rads-per-run telemetry, don't guess.
- [ ] Music via the first `LoadMusicStream`/`UpdateMusicStream` in the codebase,
      pumped in `Frame()` (`:2312–2339`): title / combat / results states. CC0/CC-BY
      tracks per §7.

**Done when:** die in sector 2, see banked rads unlock the MP5, start a new run with
it, beat your per-map best — all across a quit/relaunch.

### M5 — Pressure and feel

**Goal:** tense, readable, loud. The difficulty-design pillar made flesh.

- [ ] **Exposure meter**: charges on low *area coverage* (distance from
      recent-positions centroid — explicitly NOT just displacement, so
      backpedal-circling charges it too; judges flagged this as the actual degenerate
      strategy); drains on real traversal. Above threshold: +20% horde speed,
      `DrawVignette` (`:2282`) grain, and a **procedurally synthesized geiger click**
      (`LoadSoundFromWave` noise bursts, same trick as the `GenImage` particle
      textures — zero asset sourcing) whose rate is the charge. HUD: dosimeter bar.
- [ ] **`trigger_hurt` hazards**: accumulate AABBs from trigger-textured faces during
      parse (identified then discarded in `MapIsFiller`, `mapload.h:69–73`); damage
      player AND enemies — lava-kiting the horde becomes a tactic (4 volumes on
      afterslime, 13 on implosion).
- [ ] **Far-cell bonus objective**: between waves, argmax over the existing
      `g_navDist` array places an artifact/scrap cache at the farthest reachable
      cell; fetch before the next wave starts for bonus rads. One array scan, no new
      nav goal.
- [ ] **Glower variant**: on death, timed radiation pool (existing `AddScorch` visual
      + a hurt volume) — third composition tool, ties the theme into combat.
- [ ] Feedback: crosshair hitmarker + kill pip on confirmed `HurtEnemy` damage;
      directional hurt arc in `DrawDamageOverlay` (`:2296–2310`); hitstop via a
      `g_timeScale` multiplied into dt at the top of `Update()` (`:1373`) — 50 ms at
      0.25× on kill, 90 ms on headshot (verify the LMG sound-gated and minigun
      spin-up state machines tolerate scaled dt); camera-pitch recoil impulse with
      spring recovery (`g_recoil :125` is currently viewmodel-only); jump-pad support
      from `trigger_push` entities (66 across the corpus, pure static physics —
      horde-escape mobility).
- [ ] **Balance soak**: seeded scripted runs per arena per loadout via
      `--seed --frames --debug` JSONL; assertions target coarse invariants (nav ok,
      no stuck enemies, wave cleared within N frames) — frame timing isn't
      deterministic, so don't assert exact outcomes. Tune: first sector-3 clear at
      ~25–35 min of play; skilled runs score-limited, not wall-limited.
- [ ] **Ranged-enemy decision gate (end of M5, explicit)**: if playtests show combat
      flatlining around wave 8–10 on melee-only composition — the design itself
      predicts this — greenlight the **Spitter**: a projectile enemy reusing the
      flame path's LOS pattern (`:1570`) and the grenade projectile code. It is real
      new AI work (~the one justified scope add); budget it here, decide on playtest
      evidence, don't let it ambush M6.

**Done when:** a blind playtester's deaths are self-explained ("I got greedy", "I
stood still"), and the same player voluntarily starts another run.

### M6 — The front door (menus, difficulty, accessibility)

**Goal:** boot → title → play → options → credits with everything persisted; fair at
four presets; a stranger on Recruit has a good first hour.

- [ ] Title state before the outer loop: name, Play/Options/Credits/Quit,
      levelshot slideshow from `maps/shots/`. One OFL display font via `LoadFontEx`
      (single cheapest production-value upgrade in the plan).
- [ ] Options page: FOV slider 60–110 (hardcoded 70 at `:2540`), mouse sensitivity
      (scales the 0.0025 at `:1435`), invert-Y, master/music/SFX volume, fullscreen;
      `options.txt` v2 as key=value with per-key defaults, backward-compatible.
- [ ] Keyboard navigation for every menu (closes PROGRESS.md's own listed gap).
- [ ] In-run ESC menu gains **Restart Run** and **Abandon Run** (abandon = death:
      banks 50%, shows the results screen) alongside the existing
      Resume/Options/Quit.
- [ ] **Difficulty presets** — `{waveBudgetScale, spawnDelay, touchDmg, healOnKill}`
      only; **never enemy speed, never nav radii**: Recruit / Liquidator (default) /
      Veteran / Chernobyl (half spawn delay, no heal). Plus run-modifier style
      hard-mode toggles ("no drafts", "2× rads") for the score chase.
- [ ] Accessibility floor: god-mode assist toggle in the menu (tags the score),
      reduced-incoming-damage modifier, shake/flash intensity scalars
      (`:1507`, `:2282–2310`), hold-vs-toggle sprint, caption feed for audio cues
      (roar direction, overheat, low health).
- [ ] Tutorial: `leave_em_behind` as the zero-combat first-boot run (movement,
      pickups, geiger); skippable, replayable from the picker.
- [ ] Credits page = the license-compliance surface: Xonotic map authors (GPL),
      model credits, sound sources, raylib.
- [ ] Gate dev persistence out of release builds: SaveTune-on-quit (`:2788–2789`) and
      H-key spawn writes become `--dev`-only so a shipped binary never writes tuning
      files beside itself.

**Done when:** a fresh `options.txt`-less boot lands on a title screen, and a
non-FPS-player finishes a Recruit run without touching a config file.

### M7 — Ship it

**Goal:** a stranger downloads a zip, double-clicks, and plays a complete free game.

- [ ] **Perf & stability pass**: 60 fps minimum at 1080p on the dev Mac across the
      shipped roster; bake cache (`maps/cache/<name>.bake` keyed on map-file hash)
      so mid-run sector transitions don't hitch on the single-threaded AO/light bake
      (`g_mapAOsec` already times it); loading card (levelshot + title) over the
      remaining bake time; if fill-bound, re-enable backface culling per-mesh where
      safe (globally disabled at `:1886` — 2× fill cost).
- [ ] Arena roster to 5–6 (candidates: darkzone, finalrage, xoylent — each must pass
      nav soak to enter; data-driven sector table makes swaps free). Per-map bests +
      locked/completed badges on the picker thumbnails.
- [ ] **Resource-path resolution first — blocks everything**: asset/map loads are
      CWD-relative (`:2594–2595`, `:2638`); resolve an asset root from
      `GetApplicationDirectory()` so the binary works outside the repo.
- [ ] macOS `.app`: Info.plist, `.icns` from `assets/icon.png` (sips + iconutil,
      replacing the runtime `macicon.m` hack), assets+maps into `Resources/`, ad-hoc
      codesign + notarization or a documented right-click-open caveat.
- [ ] Windows (MinGW-w64/MSYS2 — `dirent.h` at `main.c:27` works there; MSVC is a
      non-goal) and Linux (exercise the never-tested `Makefile:23–25` branch) builds,
      each smoke-tested with the M0 suite. Ship only what passes; mac-only 1.0 is an
      acceptable cut.
- [ ] Release hygiene: version string on title + boot event; README rewrite (stale
      v0.1); PROGRESS.md refreshed; `docs/LICENSES.md` final; itch.io page from
      `maps/shots/` + fresh `--shot` captures + a 30–60 s capture; butler push
      script; fresh-machine checklist (no `vm_tune*.txt` present — committed
      `LoadWeapon` defaults must render correctly).

**Done when:** someone who has never seen the repo plays a full run from a downloaded
zip and their scores persist. Tag v1.0.

---

## 5. Cut lines (scope insurance, outside-in)

The game stays complete at every cut. In order:

1. Windows/Linux builds → mac-only 1.0 (builds become 1.1).
2. Arena roster 6 → 3 (afterslime/glowplant/atelier). Roster is data; no code dies.
3. Second Mixamo enemy model (the pipeline is token-gated and machine-local — it was
   never on the critical path; tint/scale variants already deliver composition).
4. Spitter ranged enemy — only if the M5 gate says combat holds without it.
5. Map-authored item pickups → draft shop alone carries the economy.
6. `trigger_hurt` hazards + exposure meter → wave budget + ammo economy still
   pressure; you lose the anti-turtle spice.
7. Multi-sector runs → ONE arena, endless waves, pure score chase; picker = arena
   select with per-map bests.

**Never cut:** M1 + M2 + the M3 scrap/draft loop on afterslime, with unlocks in
`profile.txt` (M3's map-authored pickups are separately cuttable — see cut 5). A
single-arena horde roguelite with honest gunplay, permadeath, and a score is a
complete game — Devil Daggers shipped exactly one arena.

---

## 6. Risk register (top items, each owned by a milestone)

| Risk | Why it's real | Mitigation | Where |
|---|---|---|---|
| Nav breaks off-afterslime | PROGRESS.md: AI tuned on afterslime; interdependent `NAV_*`/radius constants | Roster by soak test; data-driven sector table; difficulty never touches speed/radii | M0/M4 |
| Ammo state corrupts across weapon switch | `ActivateWeapon`/`StashActiveTuning` global swap must round-trip new fields | Fields ride both directions from first commit + switch-cycle regression in soak | M1 |
| Kiting collapse | Melee-only chasers + unlimited sprint → backpedal-circling wins; naive still-meter doesn't punish it | Flank-biased spawns + Runner > sprint speed (M2 acceptance); exposure keyed to area coverage | M2/M5 |
| Combat flatlines ~wave 8–10 | One melee archetype; composition/hazards may not carry long runs | Explicit Spitter decision gate on playtest evidence, budgeted in M5 | M5 |
| Horde-size perf | O(n²) separation, 3 rays/enemy, O(n·4096) pools, dt-scaled physics | Perf gate at 24 enemies, 120 AND 30 fps, BEFORE wave tuning | M2 |
| Melee rework misfires | Touches tuned AI attack state | Behind a flag, A/B vs the drain | M1 |
| Map-switch leaks | Tens of MB per sector cycle | Fixed in M3, strictly before M4 chains maps; RSS check in soak | M3 |
| Draft inflates away ammo pressure | Multiplicative stacking trivializes the core economy | 6–8 flat effects, diminishing returns, no bespoke cards in 1.0 | M3 |
| Licensing blocks release | GPL maps + unknown-license Sketchfab GLBs + probable C&C-rip v1 sounds | Free-with-source decided in M0; GLB audit in M0; v1 sounds quarantined | M0 |
| Unlock pacing mistuned | One threshold ladder: 3-run blowthrough or 20-run grind | Rads/run telemetry via JSONL; target full roster in 6–8 runs; retune from data | M4 |
| Balance claims unverifiable | 48 `GetRandomValue` sites, no fixed timestep | `--seed` + coarse-invariant soak assertions, not exact outcomes | M0/M5 |

---

## 7. Content sourcing (no art team, no exceptions)

- **Enemy variety:** tint/scale/stat variants of `enemy.glb` (M2). Optional second
  model: any Mixamo character through the proven `tools/mixamo_fetch.sh` →
  FBX2glTF → `merge_anims.mjs` chain with the same six clip names drops into the
  name-matching loader (`main.c:2726–2733`) with zero code changes. Needs a fresh
  bearer token + an afternoon; never on the critical path.
- **Music (3–4 tracks):** Kevin MacLeod/incompetech (CC-BY, credit on the credits
  page), FreePD, OpenGameArt dark-industrial. **Not** `Chernobyl/sounds/`
  hell-march/soviet-march — presumed C&C rips; anything from v1 requires provenance
  first.
- **SFX (~10 files):** freesound.org CC0 filter / Sonniss GDC bundles — knife swing
  (loader already references it), grenade explosion, dedicated Remington/Benelli/MP5
  fire (currently share stand-ins), reload foley, hurt grunts ×2–3, footsteps,
  jump/land. UI blips generated with jsfxr (CC0 by construction). Geiger: synthesized
  in code (`LoadSoundFromWave`), no asset.
- **Pickups/scrap:** procedural billboards (`g_smokeTex` `GenImage` pattern) + tinted
  cubes. No modeling.
- **Font:** one OFL face from Google Fonts (stencil/industrial) via `LoadFontEx`.
- **Maps:** zero new. `tools/stage_map.sh` exists if the roster ever grows.
- **Icons/store:** `.icns` from existing `assets/icon.png`; screenshots from the
  `--shot` harness + `maps/shots/`.

---

## 8. Telemetry & verification (how "challenging" gets proven, not asserted)

- The JSONL `--debug` stream + `--seed` + `--frames` is the balance instrument:
  every run logs waves reached, kills, accuracy, rads earned, deaths by cause.
- Soak suite (M0) runs per-commit-ish: all roster maps boot, nav builds, enemies
  path, no stuck/embed events, assets present, clean exit, RSS stable across map
  cycles.
- Wave/unlock tuning is done against logged distributions (rads/run, wave reached at
  death), not vibes. Difficulty presets are validated by playing Recruit badly on
  purpose and Chernobyl well.
- Every milestone's "Done when" is checkable by a person in under 15 minutes.

---

*Plan reviewed against: engine/gameplay/content/structure audits (11-agent pass,
2026-07-22), 3-judge comparative verdict, PROGRESS.md, and direct code reads. Line
numbers drift — trust symbol names over offsets as the code moves.*
