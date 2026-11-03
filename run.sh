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

# Extra args pass through; add --debug for the JSON event stream (written by the
# game to ./chernobyl2-debug.log, clean of raylib warnings). With no "--map", the
# game shows its startup map picker (scans maps/*.map); pass "--map <file>" to
# skip the picker and load that map directly.
# Filter raylib's harmless per-model warnings (texcoord-attribute limit) out of
# the console. PIPESTATUS keeps the game's own exit code: grep exiting 1 only
# means every line was filtered, not that the game failed.
MGW='No more than 2 texture coordinates'
set +e
./build/chernobyl2 "$@" 2>&1 | grep --line-buffered -v "$MGW"
status=${PIPESTATUS[0]}
set -e
if [[ " $* " == *" --debug"* ]]; then
  echo "JSON event log: $(pwd)/chernobyl2-debug.log"
fi
exit "$status"
