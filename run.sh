#!/bin/bash
# Chernobyl 3D - build and run. Full rebuild each time (so you know you're
# testing the latest code), mirroring Chernobyl's run.sh.
set -euo pipefail
cd "$(dirname "$0")"

if [[ ! -f vendor/raylib/lib/libraylib.a ]]; then
  echo "Building vendored raylib 6.0 (one-time)..."
  make raylib6
fi

make clean >/dev/null 2>&1 || true
make all

# Always run with --debug. The JSON event stream is written by the game to
# ./chernobyl2-debug.log (clean JSON, no raylib warnings). Extra args pass through.
# Default to flying the bundled Xonotic map if it's present; override with your
# own "--map <file>" (last one wins) or any other flags - they're appended after.
# The map is gitignored local content, so a fresh clone just boots the arena.
if [[ -f maps/afterslime.map ]]; then
  ./build/chernobyl2 --debug --map maps/afterslime.map "$@"
else
  ./build/chernobyl2 --debug "$@"
fi
echo "JSON event log: $(pwd)/chernobyl2-debug.log"
