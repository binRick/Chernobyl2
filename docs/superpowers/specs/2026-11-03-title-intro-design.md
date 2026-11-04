# Chernobyl2 title intro: design

Date: 2026-11-03. Status: draft for approval.

## Intent

Chernobyl 1 (`../chernobyl`) opens on a looping black-and-white montage of real Chernobyl
footage behind a red CHERNOBYL title, with `title-music.mp3` over it. The owner loves it and
wants Chernobyl2 to have a new version: the same theme, but **more intense and exhilarating**.
Chernobyl2 has no title screen today; it boots straight into the arena.

This is a **private build, for the owner only**. Footage and music rights are not a constraint.

## Goals

- **Footage.** Real archival footage of the USSR and Chernobyl only. Never game footage.
- **Pace.** Faster and harder-hitting than Chernobyl 1:
  - beat-synced cuts to **Hell March** (`../chernobyl/sounds/hell-march.mp3`, 384 s); this replaces `title-music.mp3`
  - flashes, glitch hits, film grain
  - Geiger-counter crackle mixed under the music
- **Look.** One unified grade, gritty high-contrast black-and-white, so every source reads as one film.
- **Subtitles.** Documentary-style lines, clean and never cut off:
  - The owner liked the burned-in English subtitles in `~/Downloads/chernobyl_tier1_test.mp4`, but those are cropped (each subtitle's 2nd line is missing).
  - Mask or crop away the burned-in text and render our own complete lines.
  - Keep surviving wording where possible: "There is a hole in the platform", "So, once in your life you must work…", "…in the memory and do your business!", "Any questions?".
  - Complete the missing halves in the same grim tone. Builder's call on wording, about 4–8 lines total, punched in on beats.
- **Title.** The CHERNOBYL title slams in on a big musical hit at the end of the cut.

## Structure (about 60–75 s cut)

1. **Before.** Ominous USSR imagery: parades and propaganda, Pripyat life and the plant before the accident. Slower cuts on the build-up section of Hell March.
2. **Disaster.** When the track kicks in, fast violent cuts:
   - the wrecked reactor (the aerial shot)
   - helicopters
   - liquidators on the roof
   - gas masks and decontamination
   - Geiger counters
   - film speckled by radiation
   - the empty city
3. **Title hit.** CHERNOBYL slams in on a downbeat.
4. **Title loop.** The game draws the title over a darkened loop of footage, with "PRESS ANY KEY" pulsing. Hell March keeps playing (it may continue past the cut or loop); the music never cuts dead.

## Footage sources

Use all of them. Download into `build/intro_src/`, which is git-ignored.

- Existing Chernobyl 1 montage: `../chernobyl/sprites/intro/frame_*.jpg`, 381 frames, 320×220 at 12 fps. Low-res, so use it sparingly or in small or textured treatments.
- `~/Downloads/chernobyl_tier1_test.mp4`: 30 s, 720×484, the source of the subtitles.
- archive.org `chernobyl-aerial-shot-of-damaged-nuclear-power-plant-pripyat-ukraine-ussr`: 34 s, 1080p.
- archive.org `ChernobylDisaster`: 63 s, 400×304 `.ogv`.
- archive.org `1988_20250221`: Soviet Chernobyl documentaries 1986–91, 768×576, many files. Pick the best moments.
- archive.org `54964SovietRadiologicalDefense`: 10 min, 960×540 `.mov`.
- archive.org `chernobyl-pripyat-before-2-may-1986-avdivx-5-color-2`: Pripyat on 1 May 1986, 640×360.
- archive.org `chernobyl-pripyat-1986-afteravcolor-2`: 478×360.

Pre-accident USSR imagery (parades, propaganda) may need one or two more archive.org finds. Builder's call.

## How it gets made

- **Offline builder.**
  - A repeatable script in `tools/intro/` downloads the sources, holds an edit decision list (source, in-time, out-time, effect), and renders with ffmpeg.
  - The edit list makes re-cutting cheap.
  - Beat times come from analysing Hell March, or are hand-placed in the edit list.
- **Outputs,** committed to `assets/` and kept reasonably small:
  - the intro video
  - the title-loop video, or a section of the same file
  - the mixed audio
- **Playback.**
  - Chernobyl2 (raylib 6, C) has no video decoder. Add a single-header decoder: pl_mpeg (MPEG-1, public domain) is the default choice, and video resolution and bitrate are tuned for it.
  - Audio plays as a raylib `Music` stream, kept in sync with the video clock.
- **Game flow.** New state before the arena: INTRO, then TITLE, then game.
  - Any key or click during the intro jumps to TITLE.
  - Any key or click on TITLE starts the game.
  - The rest of startup (map load, enemies) is unchanged.
- **Automated runs skip the intro.**
  - `--frames`, `--stress`, `--shot`, `--shot-every` and the soak and perf scripts boot straight into the arena as today.
  - Add `--no-intro` to skip it explicitly.

## Non-goals

- No game footage or in-engine cinematic.
- No public release, licence vetting or attribution screen.
- No menu system beyond "press any key": no settings or map menu.
- No change to Chernobyl 1.

## Anti-goals (failure even if it "works")

- It feels like the old montage at a slightly faster pace. It must feel *intense*.
- Any subtitle is cropped, cut off or hard to read.
- The music stops dead, or the audio drifts out of sync with the cuts.
- The intro adds a noticeable stall before the arena, or slows the perf or soak gates.

## Left to the builder

- Exactly which moments to pull from each source, the cut order, and the effects vocabulary (flash frames, RGB split, shake, speed ramps).
- Output resolution, codec settings, file size, and how the title loop is produced.
- The title typography. A red CHERNOBYL in keeping with Chernobyl 1 is the starting point.
- The exact Hell March section used.
