# Chernobyl 2

A more-3D successor to [Chernobyl](../Chernobyl) (the sprite/billboard FPS).
Same engine family (raylib 6.0), but real polygonal 3D geometry instead of
billboards and flat grid maps.

## v0.1 - what's here

The foundation: **the player runs around a real 3D room.** No weapon yet (a new
one will be added) and no other art assets - the world is generated in code.

- FPS controls: **WASD** move, **mouse** look, **SHIFT** sprint, **SPACE** jump.
- **Procedural 3D world**: textured floor + walls + a scatter of crates
  (with collision), built from `GenImageChecked` / `GenMesh*` - zero image files.
- **Placeholder shooting**: **LMB** fires a hitscan ray (tracer + impact sparks,
  no model, no ammo) so the shooting plumbing is ready for a future weapon.

> Weapon removed in v0.1: the first attempt used a 1072-bone Uzi rig that
> raylib's skeletal skinning couldn't handle (geometry scattered; also exceeded
> the GPU's 256-bone uniform limit). A new weapon model will be added later.

## Run

```bash
./run.sh            # build + play
./run.sh --debug    # + JSON event log to /tmp/chernobyl2-debug.log
```

First run builds a vendored raylib 6.0 if the prebuilt archive is missing.

## Headless screenshot / self-test

`--shot N` grabs one screenshot at frame N to `chernobyl2-shot.png`;
`--frames N` runs N frames then quits. Useful for verifying the render without
a human at the keyboard (needs a real GL context).

```bash
./build/chernobyl2 --debug --shot 60 --frames 90
```

## Debug JSON logging

`--debug` emits one JSON object per line on stderr (same convention as
Chernobyl). Events: `boot`, `frame` (sampled), `fire`, `shot`, `shutdown`.

```bash
grep '^{' /tmp/chernobyl2-debug.log | jq -c .
```

## Play stats

`src/playstats.c` (shared with the other ximg.app games) sends anonymous play
sessions to `https://ximg.app/api/play/`: game, version, platform, a random
install id, and play time (a beat every 60 s and at exit). The server adds a
country from the IP and does not keep the IP. Dashboard (owner only):
https://stats.ximg.app/games/. Hidden test runs (`CHERNOBYL_BACKGROUND`, `--frames`, `--shot`, `--shot-every`, `--record`) and the dev tools / sandboxes / bots (`--viewer`, `--sounds`, `--traps`, `--mines`, `--bot`, `--stress`, `--cycle`) never send; `PLAYSTATS_OFF=1`
turns it off anywhere. The App Store privacy label and `ios/PrivacyInfo.xcprivacy`
declare Product Interaction + Coarse Location, not linked, not tracking — keep
them in step if what is sent ever changes. Keep `C2_VERSION` in `src/main.c` equal to `MARKETING_VERSION` in `ios/project.yml`.

## Next steps

- Add a weapon (a low-bone-count gun model works cleanly in raylib).
- Lighting shader (multi-light, like Chernobyl's) for visual depth.
- Enemies + gameplay.
