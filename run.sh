#!/bin/bash
# Chernobyl 3D - build and run. Incremental build: `make` only recompiles when
# src/main.c, the headers, or the raylib archive are newer than the binary (per
# the Makefile's dependency rule), so an unchanged tree launches instantly.
set -euo pipefail
cd "$(dirname "$0")"

if [[ ! -f vendor/raylib/lib/libraylib.a ]]; then
  echo "Building vendored raylib 6.0 (one-time)..."
  make raylib6
fi

make all

# Always run with --debug. The JSON event stream is written by the game to
# ./chernobyl2-debug.log (clean JSON, no raylib warnings). Extra args pass through.
# Default to flying the bundled Xonotic map if it's present; override with your
# own "--map <file>" (last one wins) or any other flags - they're appended after.
# The map is gitignored local content, so a fresh clone just boots the arena.
# Filter raylib's harmless per-model warnings (texcoord-attribute limit) out of the
# console so the output stays clean.
MGW='No more than 2 texture coordinates'
if [[ -f maps/afterslime.map ]]; then
  ./build/chernobyl2 --debug --map maps/afterslime.map "$@" 2>&1 | grep --line-buffered -v "$MGW" || true
else
  ./build/chernobyl2 --debug "$@" 2>&1 | grep --line-buffered -v "$MGW" || true
fi
echo "JSON event log: $(pwd)/chernobyl2-debug.log"
