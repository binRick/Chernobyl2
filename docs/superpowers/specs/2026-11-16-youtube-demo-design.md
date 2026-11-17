# YouTube gameplay demo — design (approved 2026-11-16)

## Intent
A 75–90 s YouTube demo of Chernobyl 2 that is impressive and gut-wrenching: real gameplay only,
with an ElevenLabs voice-over. The owner approves the finished MP4 before anything is uploaded.

## Goals / non-goals / anti-goals
- Goals: 1080p60, all in-engine gameplay, the game's own sound plus narration, a closing title card.
- Non-goals: uploading (owner's call, later), thumbnails, music.
- Anti-goals: the intro montage, Hell March or any archival footage (copyright; owner said no intro);
  synthetic keystrokes; ad-copy narration.

## Approaches
- A. Owner plays and screen-records; Claude edits. Rejected: owner chose B.
- **B (picked by owner). Claude captures it** with the game's dev hooks (start points, autopilot,
  autofire, god, clean HUD) plus small scripted camera moves, many takes, best seconds kept.

## Capture
New dev flag `--record <file.mp4>`: each frame, read the back buffer and pipe raw RGBA to an
`ffmpeg` child (wall-clock timestamps, output CFR 60, VideoToolbox H.264); the final audio mix is
taken with raylib's `AttachAudioMixedProcessor` and written to a WAV beside it; muxed after.
Dev-only, off by default.

## Edit
Beats: silent cold open (dark zone, geiger) → first enemy out of the dark → escalation (swarm,
weapons, trap, gore) → last stand ending in death → black title card. ElevenLabs narrator, grave and
spare, a liquidator's testimony; two voice samples to the owner before the full read.
Output: MP4 + contact sheet in the scratchpad; owner approves.

## Left to the builder
Exact shots, scripted camera moves, script wording, mix levels.
