#!/bin/bash
# Mixamo animation fetcher for the Chernobyl2 enemy.
# Searches for named animations, exports each onto the chosen character, polls
# until ready, downloads the FBX. Requires a fresh Bearer token (expires ~1hr).
#
# Usage:
#   export MIX_TOK="eyJ..."          # Bearer token from DevTools
#   export MIX_KEY="mixamo2"         # x-api-key
#   export MIX_CHAR="cccc84b6-..."   # character_id (the Mutant)
#   ./tools/mixamo_fetch.sh idle walk run attack ...
#
# Output: FBX files in ./mixamo_dl/<name>.fbx
set -uo pipefail
OUT=mixamo_dl; mkdir -p "$OUT"
: "${MIX_TOK:?set MIX_TOK}"; : "${MIX_KEY:?set MIX_KEY}"; : "${MIX_CHAR:?set MIX_CHAR}"

api(){ curl -s -H "authorization: Bearer $MIX_TOK" -H "x-api-key: $MIX_KEY" \
            -H "accept: application/json" "$@"; }

# Find the best product id for a keyword (first Motion result).
find_anim(){
  local q="$1"
  api "https://www.mixamo.com/api/v1/products?page=1&limit=8&type=Motion%2CMotionPack&query=$(python3 -c "import urllib.parse,sys;print(urllib.parse.quote(sys.argv[1]))" "$q")" \
    | python3 -c "import sys,json
d=json.load(sys.stdin)
for r in d.get('results',[]):
    if r.get('type')=='Motion':
        print(r['id']+'\t'+r['name']); break"
}

export_anim(){  # productId outName
  local pid="$1" name="$2"
  # fetch product to get gms_hash
  local prod; prod=$(api "https://www.mixamo.com/api/v1/products/$pid?similar=0&character_id=$MIX_CHAR")
  # build export payload: set the model name on the gms hash, request fbx7_2019
  local payload; payload=$(printf '%s' "$prod" | python3 -c "
import sys,json
prod=json.load(sys.stdin)
g=prod['details']['gms_hash']; g['model']=prod['name']
print(json.dumps({
 'gms_hash':[g], 'preferences':{'format':'fbx7_2019','skin':'true','fps':'30','reducekf':'0'},
 'character_id':'$MIX_CHAR','type':'Motion','product_name':prod['name']}))")
  api -X POST -H "content-type: application/json" \
      -d "$payload" "https://www.mixamo.com/api/v1/animations/export" >/dev/null
  # poll monster status
  for i in $(seq 1 40); do
    local st; st=$(api "https://www.mixamo.com/api/v1/characters/$MIX_CHAR/monster" \
      | python3 -c "import sys,json;d=json.load(sys.stdin);print(d.get('status',''),d.get('job_result',''))" 2>/dev/null)
    local status="${st%% *}" url="${st#* }"
    if [ "$status" = "completed" ] && [ -n "$url" ] && [ "$url" != "$status" ]; then
      curl -s -L -o "$OUT/$name.fbx" "$url"
      echo "  -> $OUT/$name.fbx ($(wc -c <"$OUT/$name.fbx") bytes)"
      return 0
    fi
    sleep 2
  done
  echo "  !! $name export timed out"; return 1
}

for q in "$@"; do
  echo "[$q]"
  res=$(find_anim "$q")
  [ -z "$res" ] && { echo "  no match"; continue; }
  pid="${res%%	*}"; nm="${res#*	}"
  echo "  found: $nm ($pid)"
  export_anim "$pid" "$q"
done
