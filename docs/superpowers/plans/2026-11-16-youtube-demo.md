# YouTube Gameplay Demo Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A 75–90 s 1080p60 gameplay demo MP4 with an ElevenLabs voice-over, for the owner to approve.

**Architecture:** The game records itself (`--record`): back-buffer frames piped to an ffmpeg child,
final audio mix captured by raylib's mixed-audio processor into a WAV. A shell script shoots the
shot list with existing dev hooks; another assembles takes, narration and a title card with ffmpeg.

**Tech Stack:** C / raylib 6.0, ffmpeg (Homebrew, h264_videotoolbox), ElevenLabs via the
narrating-videos-with-elevenlabs skill's `eleven.py`.

**Spec:** `docs/superpowers/specs/2026-11-16-youtube-demo-design.md`

## Global Constraints
- No intro footage, no Hell March, no music: gameplay, game sound, narration only.
- Never upload to YouTube; the deliverable is a local MP4 the owner approves.
- No synthetic keystrokes; drive the game only through flags and `CHERNOBYL_*` env hooks.
- Test runs use `CHERNOBYL_BACKGROUND=1`; `--record` is off by default and changes nothing without it.
- Output 1920x1080, 60 fps CFR, H.264 + AAC, `-movflags +faststart`.
- Working files in `build/demo/` (git-ignored); final MP4 + contact sheet copied to the scratchpad.

## Review Focus
- A hidden/background window (CHERNOBYL_BACKGROUND=1): frames must still be real, not black — Task 1 checks mean luma > 10.
- Retina: the render size is 2x the window; the recorder must use `GetRenderWidth/Height` and scale to 1080p — Task 1 asserts 1920x1080.
- Slow frames (hitches) must not desync audio from video — Task 1 checks A/V duration differ < 0.1 s.
- `--mute` / no audio device: recorder still writes video, WAV omitted, no crash — Task 1 runs once with `--mute`.
- ffmpeg missing or the pipe closes early: the game keeps running and logs one error — Task 1 runs with `PATH` lacking ffmpeg.

---

### Task 1: `--record <out.mp4>` in the game

**Files:**
- Create: `src/record.h` (header-only, like `traps.h`)
- Modify: `src/main.c` — flag parse near `"--shot"` (~6928); `RecordFrame()` just before the main `EndDrawing()` (~6153); `RecordStop()` before `CloseWindow()` (~7097); `RecordStart` after `InitAudioDevice()` (~7004)
- Test: `tools/demo/test-record.sh`

**Interfaces:**
- Produces: `int RecordStart(const char *mp4)`, `void RecordFrame(void)`, `void RecordStop(void)`; files `<out>.mp4` (video, then audio muxed in on stop) and `<out>.wav` kept beside it.

- [ ] Step 1: write `tools/demo/test-record.sh`: runs `CHERNOBYL_BACKGROUND=1 ./build/chernobyl2 --no-intro --god --autofire --frames 300 --record build/demo/t.mp4`, then asserts with ffprobe: 1920x1080, r_frame_rate 60/1, video and audio duration each 4.5–5.5 s and within 0.1 s, mean luma (`signalstats`) > 10, audio max_volume > -40 dB; then the `--mute` and no-ffmpeg runs exit 0.
- [ ] Step 2: run it; expect FAIL (unknown flag / no file).
- [ ] Step 3: implement. `popen` ffmpeg `-f rawvideo -pix_fmt rgba -s <render WxH> -use_wallclock_as_timestamps 1 -i - -vf vflip,scale=1920:1080 -r 60 -fps_mode cfr -c:v h264_videotoolbox -b:v 20M`; frames via `rlReadScreenPixels` before `EndDrawing`; audio via `AttachAudioMixedProcessor` appending float stereo to a WAV (header patched on stop); on stop `pclose`, then mux mp4+wav to final with `-c:v copy -c:a aac -shortest`. Wall-clock start shared by both.
- [ ] Step 4: run the test; expect PASS.
- [ ] Step 5: commit `src/record.h src/main.c tools/demo/test-record.sh`.

### Task 2: shot list and shooting script

**Files:**
- Create: `tools/demo/shoot.sh`, `tools/demo/shots.txt`
- Modify: `src/main.c` — dev hook `CHERNOBYL_TURN="<deg/s> [pitch]"` slow camera pan, next to `CHERNOBYL_WALK` (~4506)

**Interfaces:**
- Consumes: `--record` (Task 1).
- Produces: `build/demo/takes/<shot>-<n>.mp4`.

`shots.txt`: one line per shot: `name | frames | flags | env`. Shots: `cold` (dark zone, no enemies, slow pan), `first` (AT_PLAY near a spawn, the first enemy out of the dark), `swarm` (boss wave / stress, kite + autofire, several weapons), `trap` (CHERNOBYL_AT_TRAP + TRAP_AUTO), `gore` (shotgun/flame close range), `death` (no god, kite into a swarm until killed). 3 takes each with different `--seed`.
Owner asked (2026-11-16) to feature **reload animations** and **traps**:
- `reload-<w>`: one take per weapon (`--weapon <w> --reload-at 90`, `CHERNOBYL_RELOAD_REAL=empty`, clean HUD), cut as a fast reload montage (~1 s each) in the escalation beat.
- `trap-<n>`: each of the 8 plant traps set off with enemies in it (`CHERNOBYL_AT_TRAP=<n> CHERNOBYL_TRAP_FIRE=<n> CHERNOBYL_AT_PLAY=1`), cut as a trap sequence (~2 s each) after the reloads.

- [ ] Step 1: `shoot.sh <shot|all>` reads shots.txt, records each take, then writes a contact sheet `build/demo/takes/sheet.jpg` (ffmpeg tile, 1 frame/2 s).
- [ ] Step 2: run `shoot.sh cold`; expect a pan with no enemies; verify by viewing the sheet.
- [ ] Step 3: run `shoot.sh all`; review the sheet; drop or re-seed takes that look staged.
- [ ] Step 4: commit `tools/demo src/main.c`.

### Task 3: narration

**Files:** Create `tools/demo/narration.txt` (one line per beat, with its target start second).

- [ ] Step 1: write the script: liquidator testimony, spare; ≤ 90 words total.
- [ ] Step 2: generate the first two lines in two candidate grave voices with `eleven.py` (check credits first); send both to the owner; stop until they pick.
- [ ] Step 3: generate every line in the chosen voice into `build/demo/vo/<n>.mp3`.

### Task 4: edit

**Files:** Create `tools/demo/edit.sh`, `tools/demo/cut.txt` (`take | in | out` per segment).

- [ ] Step 1: `edit.sh` trims segments, joins with 6-frame dissolves, ducks game audio −8 dB under each VO line (sidechaincompress), places VO at narration.txt times, appends a 5 s black title card ("CHERNOBYL 2", "LIQUIDATOR", "chernobyl2.ximg.app", in the game's title red), loudness-normalises to −14 LUFS.
- [ ] Step 2: run; ffprobe: 1920x1080, 60 fps, 75–90 s; integrated loudness −14 ±1 LUFS.
- [ ] Step 3: watch it in frames (contact sheet at 1 frame/s) and listen to the VO joins; fix cut.txt until it plays clean.
- [ ] Step 4: copy MP4 + sheet to the scratchpad; give the owner the paths; commit `tools/demo`. No upload.
