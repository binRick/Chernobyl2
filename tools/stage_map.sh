#!/bin/bash
# Stage an official Xonotic map for the in-game map picker: downloads the .map
# brush source + the levelshot thumbnail from xonotic-maps.pk3dir, then fetches
# the textures it references. Everything lands as gitignored local content under
# maps/. After this, the map shows up (with a preview) in the startup menu.
#
#   tools/stage_map.sh <mapname> [<mapname> ...]
#   e.g. tools/stage_map.sh stormkeep solarium xoylent
set -uo pipefail
cd "$(dirname "$0")/.."
BASE="https://gitlab.com/xonotic/xonotic-maps.pk3dir/-/raw/master"
mkdir -p maps maps/shots

stage_one() {
  local nm="$1" ok_map="" ok_shot=""
  echo ">> $nm"
  # 1) brush source
  if curl -sf --max-time 90 -o "maps/$nm.map.part" "$BASE/maps/$nm.map"; then
    mv "maps/$nm.map.part" "maps/$nm.map"; ok_map=1
  else rm -f "maps/$nm.map.part"; echo "   MISS .map ($nm)"; return 1; fi
  # 2) levelshot thumbnail -> png (raylib decodes png, not jpg/tga)
  if curl -sf --max-time 60 -o "maps/shots/$nm.jpg" "$BASE/maps/$nm.jpg"; then
    sips -s format png "maps/shots/$nm.jpg" --out "maps/shots/$nm.png" >/dev/null 2>&1 && rm -f "maps/shots/$nm.jpg" && ok_shot=1
  fi
  # 3) textures the map references
  local fetched
  fetched=$(tools/fetch_map_textures.sh "maps/$nm.map" maps/textures 2>&1 | grep -E '^fetched' || true)
  echo "   map=$ok_map shot=${ok_shot:-0} ; $fetched"
}

for m in "$@"; do stage_one "$m"; done
echo "done."
