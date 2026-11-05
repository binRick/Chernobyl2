#!/bin/bash
# Enemy model viewer: the current model (AFTER) beside a baseline (BEFORE), side by side,
# in the game's own shader and arena-style lighting -- no game, no map.
#   ./view-enemy.sh cook                 # assets/cook.glb vs build/enemy_v0/cook.glb
#   ./view-enemy.sh cook path/to/new.glb # a specific candidate vs the baseline
# Keys: 1-6 clip, SPACE pause, drag/arrows turn, wheel zoom, F face close-up, ESC quit.
cd "$(dirname "$0")"
e=${1:-cook}; new=${2:-assets/$e.glb}; old=build/enemy_v0/$e.glb
make -s all >/dev/null || exit 1
if [[ -f $old ]]; then exec ./build/chernobyl2 --viewer "$new" --viewer-b "$old"
else exec ./build/chernobyl2 --viewer "$new"; fi
