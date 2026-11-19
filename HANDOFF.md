# Handoff: 14 November 2026

Written before a reboot, from the session that did the map lighting, loading and boss work.
Nothing here needs doing before the reboot; it is a record of where things stand.

## State of the repos

- **Game repo** (`main`, in sync with `origin/main`): last commit `9f8aa09`, 108 commits.
- **Site repo** (`../ximg-web`, `main`): last commit `0f627071`, working tree clean.
- **Uncommitted in the game repo, and NOT from this session:**
  - `src/main.c` (about 190 lines removed: the dev overlay, viewmodel tuning and spawn-save helpers)
  - `src/traps.h` (4 lines)
  - `playcocola-prompt.txt`, `tools/safari/` (untracked)

  Another session was part-way through these. This session did not touch them. Check with
  `git diff` before committing or discarding; a reboot does not lose them.

## What this session did (all of it is committed)

Other sessions committed after this one, and this session's work went in with theirs. Confirmed
present in `HEAD`:

| Work | Where to look |
|---|---|
| Blinking panel lamps (glow layers per surface, six waveforms) | `tools/map/make_textures.py` (`lit`, `glow_layers`), world shader in `src/main.c` |
| Lit haze: height fog, light scattering round lamps, floor sheen, dust, failing tubes, wider bloom | `GLSL_ATMOS`, `AtmosUpdate`, `DrawDust`, `FlickWave` in `src/main.c` |
| Reload only when empty on weapon switch | `g_drawTopUp` in `src/main.c` |
| Web data file under GitHub's 100 MB (map textures as JPEG) | `tools/web/pack_web.py` |
| Lighting bake cached beside the map (`maps/*.bake`, git-ignored) | `MAP_BAKE_VER` in `src/mapload.h` |
| Loading on a second thread while the intro plays, loading bar | `LoadGameAssets`, `LoadThreadStart`, `MeshVaoRebuild` in `src/main.c` |
| Intro music fed from its own thread (fixes overlap after a skip) | `IntroMusicPump` in `src/intro.h` |
| Enemies spawn only on flat floor, 9-18 m out | `SpawnFloorFlat` in `src/main.c` |
| Bosses keep coming under sustained fire | `BOSS_FLINCH_GAP`, `BOSS_HIT_SLOW` in `src/main.c` |

## Not verified by eye or ear

These passed hidden test runs but nobody has confirmed them in a real session:

- Intro music after an early skip: no overlap of the montage music and Hell March.
- Intro video stays smooth while the game loads beside it.
- The loading bar on the no-intro loading card.
- Blink speeds of the panel lamps and the stutter of the failing tubes.
- Boss difficulty now that he closes on a player holding the trigger (may be too hard).

## Useful switches

- `CHERNOBYL_BACKGROUND=1`: hidden, unfocused window for automated runs.
- `CHERNOBYL_NO_LOADTHREAD=1`: load up front under the bar instead of on the loader thread.
- `CHERNOBYL_INTRO_FORCE=1` with `CHERNOBYL_INTRO_AUTO="skipAt,startAt"`: run the intro in a
  test that also uses `--shot` / `--frames` / `--kite`.
- `--boss --kite --autofire --god`: hidden boss fight with the trigger held.
- `MAP_SIGNS=1 python3 tools/grid2map.py ...`: print where stations and signs were placed.

## Open items

- Server `/root/ximg-web/compose.yaml` has uncommitted edits by others; the next
  `docker compose up -d` there will recreate nginx (about four minutes of downtime last time).
- iPhone build: runs in the simulator only. Still needs a real-device test, bundle ID and
  App Store record, screenshots, and a decision on the intro music's rights.
- Phone touch controls for the browser version were approved but not built.
