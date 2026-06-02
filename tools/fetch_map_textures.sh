#!/bin/bash
# Fetch the diffuse textures a Xonotic .map references, from xonotic-maps.pk3dir,
# into a local dir so the in-engine loader can skin the map. Texture names look
# like "set/cat-rest"; the image lives at textures/set/cat/cat_rest.tga (verified
# against the .shader qer_editorimage). Falls back to direct paths and .jpg.
# Everything is converted to PNG via sips (the vendored raylib decodes PNG, not
# TGA/JPG) and saved under <outdir>/<map-name>.png, found by the loader by name.
#
#   tools/fetch_map_textures.sh [map] [outdir]
#   (defaults: maps/afterslime.map  maps/textures)
set -uo pipefail
MAP="${1:-maps/afterslime.map}"
OUT="${2:-maps/textures}"
BASE="https://gitlab.com/xonotic/xonotic-maps.pk3dir/-/raw/master"
mkdir -p "$OUT"
[ -f "$MAP" ] || { echo "no such map: $MAP" >&2; exit 1; }

# distinct set/name tokens (textures are the only slashed tokens in a .map),
# minus invisible compiler filler.
names=$(grep -oE '[a-zA-Z][a-zA-Z0-9_]*/[a-zA-Z0-9_/.-]+' "$MAP" \
        | grep -ivE 'common/|caulk|nodraw|skies/|/sky|hint|areaportal|trigger|/clip|botclip|/origin' \
        | grep -ivE '^models/|^sound/|^domination/|\.(md3|wav|ogg|jpg|tga|png|cfg)$' \
        | sort -u)

# try URL -> dest; keep only a real 200 image (reject html error pages)
try() {
  local url="$1" dest="$2" info code ct
  info=$(curl -s -o "$dest.part" -w '%{http_code}|%{content_type}' --max-time 40 "$url" 2>/dev/null)
  code=${info%%|*}; ct=${info#*|}
  if [ "$code" = "200" ] && [[ "$ct" != *html* ]] && [ "$(stat -f%z "$dest.part" 2>/dev/null || echo 0)" -gt 800 ]; then
    mv "$dest.part" "$dest"; return 0
  fi
  rm -f "$dest.part"; return 1
}

total=0; hit=0; misslist=""
for nm in $names; do
  total=$((total+1))
  mkdir -p "$OUT/$(dirname "$nm")"
  o="$OUT/$nm"
  if [ -f "$o.png" ]; then hit=$((hit+1)); continue; fi
  s=${nm%%/*}; tl=${nm#*/}; c=${tl%%-*}; r=${tl#*-}
  rc=${r%-cull}; rc=${rc%-nodraw}; rc=${rc%-nonsolid}; rc=${rc%-cullback}   # drop shader-variant suffixes
  got=""
  if [ "$c" != "$tl" ] && try "$BASE/textures/$s/$c/${c}_${r}.tga" "$o.tga"; then got=1
  elif [ "$rc" != "$r" ] && try "$BASE/textures/$s/$c/${c}_${rc}.tga" "$o.tga"; then got=1
  elif try "$BASE/textures/$nm.tga" "$o.tga"; then got=1
  elif { [ "$c" != "$tl" ] && try "$BASE/textures/$s/$c/${c}_${r}.jpg" "$o.jpg"; } || try "$BASE/textures/$nm.jpg" "$o.jpg"; then
    got=1
  fi
  # normalise whatever landed (.tga/.jpg) to .png, which the engine can decode
  for raw in "$o.tga" "$o.jpg"; do
    [ -f "$raw" ] && sips -s format png "$raw" --out "$o.png" >/dev/null 2>&1 && rm -f "$raw"
  done
  if [ -n "$got" ] && [ -f "$o.png" ]; then hit=$((hit+1)); printf '  ok   %s\n' "$nm"; else misslist="$misslist $nm"; printf '  MISS %s\n' "$nm"; fi
done
echo "----"
echo "fetched $hit/$total into $OUT"
[ -n "$misslist" ] && echo "missing:$misslist"
exit 0
