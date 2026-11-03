#!/bin/bash
# Fetch the CMU mocap takes the cook uses (cgspeed BVH conversion; CMU + the converter
# allow any use -- see READMEFIRST.txt in the mirror) into build/cmu/.
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/cmu
BASE=https://raw.githubusercontent.com/una-dinosauria/cmu-mocap/master/data
for c in 120/120_22 104/104_41 009/09_01 079/79_01 090/90_18; do
  f=build/cmu/$(basename $c).bvh
  [[ -s $f ]] || curl -sSL --fail -o "$f" "$BASE/$c.bvh"
  echo "$f"
done
