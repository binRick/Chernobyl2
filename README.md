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

## Multiplayer

Up to eight players in the same run, on the desktop Mac and browser builds (not the iPhone yet).
**M** on the title, or **ESC > MULTIPLAYER** in the game, opens the screen: start a new game, or join
one that is listed. A joiner drops into the run as it stands. When you die you watch through a living
player's eyes (click for the next one); when everyone is dead the run restarts from wave 1 for all.
There is no pause in a shared game, no friendly fire, and its scores do not go on the leaderboard.

How it works (`src/mp.h`, `src/net.h`, `src/net_mac.m`, `server/mp-relay.js`):

- Every game holds one WebSocket to a **relay**, which only keeps the rooms and passes messages on.
  The player who started the game is the **host**: his game runs the waves, picks each enemy's
  target, and is the only one that takes enemy health away. If he leaves, the game ends.
- Every game runs the same enemy code on its own copy of the enemies, so an enemy only hurts the
  player whose own screen he is on. The host announces arrivals and deaths as they happen and
  positions ten times a second; a client's shots are judged on his own screen and sent as hits.
- More players means more enemies: +60% wave budget per extra player.
- Not shared in this version: pickups (each player has his own), and a trap sprung on one screen
  only counts if it also springs on the host's.

The game looks for the relay at `wss://chernobyl2.ximg.app/mp`. To run one locally:

```
node server/mp-relay.js 8643
CHERNOBYL_MP_URL=ws://127.0.0.1:8643/mp ./run.sh --mp          # the multiplayer screen at once
# browser build: add ?mp=ws://127.0.0.1:8643/mp to the page's address
```

Unattended tests (no keys needed): `CHERNOBYL_MP=host` or `CHERNOBYL_MP=join` (the first game listed)
with `--kite --autofire`, `CHERNOBYL_MP_NAME=ABC`, and `CHERNOBYL_DEBUG_LOG=<file>` so two games on one
machine keep separate debug logs; in the browser, `?mpauto=host` / `?mpauto=join`.

## Next steps

- Add a weapon (a low-bone-count gun model works cleanly in raylib).
- Lighting shader (multi-light, like Chernobyl's) for visual depth.
- Enemies + gameplay.
