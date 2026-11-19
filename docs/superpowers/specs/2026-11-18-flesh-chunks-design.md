# Flesh chunks — design (2026-11-18)

## Intent
The owner: "i blew people up with the grenade launcher and i saw little flesh pieces on the floor. they are simple
object. can you increase their quality to be real chunks". What a blast leaves is thirteen small flat-coloured balls
(`DrawSphereEx`, no light, no texture): they read as placeholders, most of all lying on the floor. Offered three ways
(chunks of meat and bone; pieces of the man himself; chunks plus a few generic parts) he chose the first: "go with 1".

## Goals, non-goals, anti-goals
- Goals: what is thrown out and what is left on the floor reads as torn meat and bone; it is lit by the room's lamps
  like everything else; it tumbles in the air and comes to rest lying flat.
- Non-goals: pieces of the particular man (his clothes), recognisable limbs or heads (a possible later step: several
  times the work, and a matter for the App Store age-rating answers, which are the owner's); new sounds; any change
  to WHEN a man is blown apart, how many pieces fly or how far.
- Anti-goals: a cost in frame time anyone can feel with the floor full of chunks; chunks hanging above the floor or
  sunk in it; chunks that glow in a dark corner or stay black under a lamp; a crash or nothing at all when the
  files are missing.

## Constraints
- raylib 6, one translation unit. The lit shader is the enemies' (`g_skinShader`): colour picture x vertex colour, a
  bump (normal) picture, and one that packs occlusion, roughness, shine, and in its alpha whether light bleeds red
  through the surface (alpha 0 = it does). Its lamps are looked up by `EnemyLightUpload`, a ray per lamp: not
  something to do per chunk.
- The browser and phone builds ship what `tools/web/pack_web.py` packs. No model file names a picture
  (`tests/model-images.sh`): raylib would decode it a second time.
- Random numbers in the game come from `GetRandomValue`; test runs are not frame-locked.

## Approaches
1. **Chunks of meat and bone, the same set for every enemy (picked by the owner).** Cheap, one set of files, no
   per-enemy work; says "flesh", not "this cook".
2. Pieces of the man himself, cut from his model: most convincing, but eleven enemy types, cut ends to close, and
   severed limbs raise the age-rating question.
3. Chunks plus a few generic parts (boot, hand, helmet): a middle road; the parts would not match who died.

## Decided (the builder's calls unless marked)
- **Owner:** approach 1.
- Eight shapes, built by a tool in the repo (`tools/gibs/build_chunks.py`, numpy and Pillow, seeded: the same bytes
  on every run): three lumps of muscle, two torn flaps, two bones with meat on them (a long bone snapped at one end;
  a joint end), one piece of rib. Each 1,280 to 1,960 triangles (a lump is 1,280; a bone is its shaft and the meat left
  on it), closed, flat underneath so it lies on a floor, and sized to reach 1 from its own middle so the game sets
  the size.
- One model file, `assets/gibs/chunks.gltf` + `.bin` (eight meshes, no pictures named), and three small tiling
  pictures, `assets/gibs/meat_c.png`, `meat_n.png`, `meat_orm.png` (256 px): grain, bumps, and wet shine with the red
  bleed on. The colour itself (muscle, fat, clot, bone) is in the vertices, so one material serves all eight.
- Drawn with the enemies' lit shader. Lamps are looked up once for each burst of chunks, where the burst began (where
  the man stood, on his floor); a chunk thrown more than 5 m clear of that gets a lookup of its own, and chunks
  outside the view are neither drawn nor lit.
- Thrown as now: thirteen a man, the same speeds and bounces. The wet mark a landing leaves is in proportion to the
  chunk (a pool 1.5 times its reach, 6 to 20 cm across the radius; a ball left a 20 cm pool and ten flying drops at
  every bounce, and still does when the files are missing). Sizes: two large, five medium, six small, 0.084 m on
  average (the balls: 0.075). Each tumbles one to three turns a hop about an axis of its own (none in a hop shorter
  than 0.22 s) and is level again each time it touches down, so it ends lying as modelled, at a heading of its own.
- **After the review (2026-11-18).** A chunk lies ON the blood: its foot is drawn 15 mm above the floor (a pool is a
  film 7 to 14 mm up, and on the floor itself the flat pieces were buried under their own wet marks). And at each
  touch-down the floor under the chunk is looked up: over a breach it goes down the shaft and lies at the bottom; past
  the edge of a deck it falls to the floor below; inside a raised deck's footprint it ends on top of the deck. (The
  balls rested at the dead man's floor height wherever they landed, and still do.)
- It stays 20 to 28 seconds (the balls: 7 to 10), then shrinks away over its last second (the lit shader does not
  fade by transparency). The pool of 96 is as it was: the oldest is reused.
- Without the model, the pictures or the lit shader: the balls, as before.
- Dev hooks for the tests (`src/gibs.h`): `CHERNOBYL_GIBS=<frame>[*n]` throws bursts in front of the player;
  `CHERNOBYL_GIB_ROW=<frame>[*size]` lets the eight shapes fall in a row; `CHERNOBYL_GIB_DRY` / `_KEY` / `_DARK=<frame>`
  (no blood / each chunk flat green / the room's lamps left out, from that frame); `CHERNOBYL_GIB_LIFE=<seconds>`.

## Left to the builder
The shapes themselves, the colours, how wet, test thresholds.
