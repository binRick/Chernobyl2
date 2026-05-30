#!/bin/bash
# Chernobyl skeletal-anim prototype - build and run.
# Matches Chernobyl's setup: vendors raylib 6.0 (the version Chernobyl pins),
# fetches a CC0 rigged model, builds, runs.
#
#   ./run.sh                 # build + run the animated glTF robot (raylib 6.0)
#   ./run.sh --debug         # same, tee JSON log to /tmp/skeletal_proto_debug.log
#   ./run.sh --homebrew ...  # link Homebrew raylib 5.5 instead (no clone/build needed)

set -euo pipefail
cd "$(dirname "$0")"

USE_R6=1
ARGS=()
for a in "$@"; do
  case "$a" in
    --homebrew) USE_R6=0 ;;
    *) ARGS+=("$a") ;;
  esac
done

# --- ensure a raylib to link against ---
if [[ "$USE_R6" == "1" ]]; then
  # Vendor + build raylib 6.0 once (matches Chernobyl's pinned RAYLIB_TAG=6.0).
  if [[ ! -f vendor/raylib-src/src/libraylib.a ]]; then
    echo "Building vendored raylib 6.0 (one-time, ~1-2 min)..."
    make raylib6
  fi
  MAKEFLAGS_R6="RAYLIB6=1"
else
  if ! brew list raylib >/dev/null 2>&1; then
    echo "Installing raylib via Homebrew..."; brew install raylib
  fi
  MAKEFLAGS_R6=""
  echo "NOTE: linking Homebrew raylib (5.5). Animation works the same as 6.0;"
  echo "      only the struct/API layout differs (handled by macros in main.c)."
fi

# --- ensure CC0 demo models are present (animated glTF + classic IQM) ---
mkdir -p assets
fetch() { [[ -f "$2" ]] || { echo "Fetching $(basename "$2")..."; curl -fsSL -o "$2" "$1"; }; }
RAW=https://github.com/raysan5/raylib/raw/6.0/examples/models/resources/models
fetch "$RAW/gltf/robot.glb"     assets/robot.glb
fetch "$RAW/iqm/guy.iqm"        assets/guy.iqm
fetch "$RAW/iqm/guyanim.iqm"    assets/guyanim.iqm
fetch "$RAW/iqm/guytex.png"     assets/guytex.png

# --- build ---
echo "Building skeletal_proto..."
make all $MAKEFLAGS_R6

# --- launch (default model: the animated glTF robot) ---
echo "Launching..."
if [[ "${ARGS[0]:-}" == "--debug" ]]; then
  ./build/skeletal_proto --debug assets/robot.glb 2>&1 | tee /tmp/skeletal_proto_debug.log
elif [[ ${#ARGS[@]} -gt 0 ]]; then
  ./build/skeletal_proto "${ARGS[@]}"
else
  ./build/skeletal_proto assets/robot.glb
fi
