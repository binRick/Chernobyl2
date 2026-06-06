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
# With no "--map", the game shows its startup map picker (scans maps/*.map); pass
# "--map <file>" to skip the picker and load that map directly. Maps are gitignored
# local content, so a fresh clone with no maps just boots the built-in arena.
# Filter raylib's harmless per-model warnings (texcoord-attribute limit) out of the
# console so the output stays clean.
MGW='No more than 2 texture coordinates'
./build/chernobyl2 --debug "$@" 2>&1 | grep --line-buffered -v "$MGW" || true
echo "JSON event log: $(pwd)/chernobyl2-debug.log"
