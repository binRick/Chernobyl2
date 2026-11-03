#!/bin/bash
# Rebuild every enemy model (assets/<enemy>.glb). Needs Blender + the MPFB extension +
# the MakeHuman CC0 asset pack (see tools/chef/build_enemy.py). Pass names to build a subset.
set -euo pipefail
cd "$(dirname "$0")/../.."
tools/chef/fetch_mocap.sh >/dev/null
enemies=("$@"); [[ ${#enemies[@]} -gt 0 ]] || enemies=(cook linecook butcher liquidator headchef rifleman gunner mutant priest revenant fiend)
for e in "${enemies[@]}"; do
  blender -b -P tools/chef/build_enemy.py -- "$e" "assets/$e.glb" "${PREVIEW_DIR:+$PREVIEW_DIR/$e}" 2>&1 \
    | grep -E 'enemy: wrote|Error|Traceback' || true
done
