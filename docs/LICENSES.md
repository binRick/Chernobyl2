# Asset & dependency licenses

**Distribution decision (M0):** Chernobyl 2 is a **free game with published
source**. This is forced by the map content — the Xonotic maps/textures are GPL,
and everything else in the tree is compatible with free distribution. Nothing in
this repo may be sold, and the two CC-BY-**NC** models below additionally bar any
commercial distribution for as long as they remain in the tree.

Verdicts: **SHIP** (fine as-is, attribution below) · **SHIP-NC** (fine in a free
release; must be deleted before any commercial/storefront release) ·
**PLACEHOLDER** (works, but provenance unknown — replace before first public
release) · **QUARANTINE** (never ships).

## Engine & code

| Component | License | Verdict |
|---|---|---|
| raylib 6.0 (vendored, `vendor/raylib/`) | zlib/libpng | SHIP |
| `src/*` (game code) | ours | SHIP — pick a license at first public release (GPL maps do not obligate the code; MIT/GPL both fine) |
| `tools/*` (FBX2glTF, node deps) | various | dev-only, never distributed |

## Maps & textures (`maps/`, `maps/textures/`)

All 14 maps and their textures are from **Xonotic** (GPLv2+ per Xonotic's
COPYING; fetched via `tools/fetch_map_textures.sh` / `tools/stage_map.sh`).
Verdict: **SHIP** — the `.map` sources are versioned in this repo, which
satisfies GPL source availability, and the game is free.

## Weapon viewmodels (`assets/*.glb`)

License metadata is embedded in each GLB's glTF `asset.extras` block (stamped by
Sketchfab at download time) — verified 2026-07-24.

| File | In use | Title / Author | License | Verdict |
|---|---|---|---|---|
| grenadelauncher.glb | slot 0 | "Grenadelauncher animated" — DJMaesen | CC-BY-4.0 | SHIP |
| remington.glb | slot 1 | "FPS Arms remington (shotgun)" — Cransh | CC-BY-4.0 | SHIP |
| minigun.glb | slot 2 | "Minigun animated" — DJMaesen | CC-BY-4.0 | SHIP |
| lmg.glb | slot 3 | "lmg animated" — DJMaesen | CC-BY-4.0 | SHIP |
| ak74.glb | slot 4 | "FPS AK-74m animations" — Cransh | CC-BY-4.0 | SHIP |
| mp5.glb | slot 5 | "Animated MP5" — JUST | CC-BY-4.0 | SHIP |
| benelli.glb | slot 6 | "FPS Benelli M4 Animations" — Cransh | CC-BY-4.0 | SHIP |
| flamethrower.glb | slot 7 | "CSO Salamader (FlameThrower) First Person" — Amine.Elouneg | CC-BY-4.0 | SHIP |
| knife.glb | slot 8 | "knife animated" — DJMaesen | CC-BY-4.0 | SHIP |
| gun.glb | unused spare | "GUN" — NksXxX | CC-BY-4.0 | SHIP (unused) |
| sawnoff.glb | unused spare | "sawnoff animated" — DJMaesen | CC-BY-4.0 | SHIP (unused) |
| rifle.glb | unused spare (M16A3, cut) | "Animated Assault Rifle" — JUST | **CC-BY-NC-4.0** | SHIP-NC — delete before any commercial release |
| shotgun.glb | unused spare | "Shotgun Animated" — DJMaesen | **CC-BY-NC-4.0** | SHIP-NC — delete before any commercial release |

## Enemy model (`assets/enemy.glb`)

Adobe **Mixamo** character + animations (fetched via `tools/mixamo_fetch.sh`,
merged with `tools/merge_anims.mjs`). Mixamo's terms allow royalty-free use in
games, including commercial; they do not allow redistributing the content as a
standalone asset. Verdict: **SHIP** embedded in the game; do not advertise the
GLB as a reusable asset.

## Sounds (`assets/*.mp3`, `assets/*.wav`)

`knife_swing.wav` is synthesized by us (see git history) — **CC0 / SHIP**.

All 15 `.mp3` files (weapon fire, enemy attack/deaths, minigun cooldown) have
**unknown provenance**: no embedded tags, added in one bulk commit (`dce3176`).
Checksum comparison (2026-07-24) confirms **none** of them came from the old
`../Chernobyl/sounds/` C&C rip folder, but that only clears them of one bad
origin — it doesn't establish a license. Verdict: **PLACEHOLDER** — replace with
CC0 equivalents (per PLAN.md §7 sourcing) before first public release.
In use: shotgun_fire, minigun_fire, minigun_cooldown, lmg_fire,
ak74_fire_single, rifle_fire, flame_fire, enemy_attack, enemy_death1–4.
Unused: ak74_fire, sawnoff_fire, shotgun_cock.

## Other

| Item | Verdict |
|---|---|
| `assets/icon.png`, `assets/screenshot.png` | ours — SHIP |
| `../Chernobyl/sounds/` (sibling repo, 56 files, presumed C&C rips) | **QUARANTINE** — nothing ships from there, ever |
| Title intro: `assets/intro.mpg`, `intro_music.mp3`, `intro_title.png` (`tools/intro/`) | **QUARANTINE** (private build only) — archival Soviet/Chernobyl footage of mixed or unclear rights (archive.org: 1986–91 Soviet documentaries, Periscope Film uploads, Pripyat home video, plus a public-domain aerial shot and compilation), the 1977 USSR anthem recording, and *Hell March* (C&C: Red Alert). Strip the intro (or rebuild it from cleared footage and music) before any public release; the game runs without it. |
| `src/pl_mpeg.h` (phoboslab/pl_mpeg) | MIT — SHIP |

## Attribution (must ship with the game)

> Weapon models: "Grenadelauncher animated", "Minigun animated", "lmg animated",
> "knife animated", "sawnoff animated" by [DJMaesen](https://sketchfab.com/bumstrum);
> "FPS AK-74m animations", "FPS Arms remington (shotgun)", "FPS Benelli M4
> Animations" by [Cransh](https://sketchfab.com/ccransh); "Animated MP5" by
> [JUST](https://sketchfab.com/teenjust500); "CSO Salamader (FlameThrower) First
> Person" by [Amine.Elouneg](https://sketchfab.com/Amine.Elouneg); "GUN" by
> [NksXxX](https://sketchfab.com/NksXxX) — all licensed
> [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/), modified (rig/scale
> edits) for this game. Source URLs are embedded in each `.glb`.
> Maps and map textures from [Xonotic](https://xonotic.org) (GPLv2+).
> Enemy character and animations from [Adobe Mixamo](https://www.mixamo.com).
> Built with [raylib](https://www.raylib.com) (zlib).
