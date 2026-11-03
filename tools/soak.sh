#!/bin/bash
# Soak gate (M0..M2, PLAN.md): asserts the game actually works, headless-ish.
#   per map      map ok:true tris>0 | nav ok walkable>0 | audio ready | shutdown ok
#   asset pass   every "assets/..." literal in src/ exists on disk (raylib load
#                failures are silent -- a missing sound just never plays)
#   perf gate    --stress (MAX_ENEMIES walkers): uncapped p95 < 8.3ms; 30fps run
#                (big-dt physics) reaches a clean shutdown
#   weapon-cycle --cycle walks all 9 slots twice; pass-2 must read back pass-1's
#                spent ammo (Activate/Stash swap corruption check)
#
# Needs a live display session (InitWindow fails with no-gl-context on a locked/
# sleeping screen). We wake the display, hold a user-activity assertion, and
# retry any run that still loses the display mid-soak.
#
# Usage: tools/soak.sh [maps/foo.map ...]     (no args = every map in maps/)
# Per-run logs land in build/soak/. Exit 0 iff everything passed.
set -uo pipefail
cd "$(dirname "$0")/.."

make all >/dev/null || { echo "BUILD FAILED"; exit 1; }

caffeinate -u -t 2 2>/dev/null || true
caffeinate -dius & CAF_PID=$!
trap 'kill $CAF_PID 2>/dev/null' EXIT

LOGDIR=build/soak
mkdir -p "$LOGDIR"
fail=0

# Run the game; on a no-gl-context exit (display slept/locked mid-soak) wake the
# display and retry, up to 3 attempts. Sets $code to the final exit status.
run_game(){   # run_game <stdout-file> <game-args...>
  local out=$1; shift
  local try
  for try in 1 2 3; do
    rm -f chernobyl2-debug.log
    ./build/chernobyl2 "$@" >"$out" 2>&1
    code=$?
    grep -q '"reason":"no-gl-context"' chernobyl2-debug.log 2>/dev/null || return 0
    echo "(attempt $try lost the display -- waking + retrying)" >>"$out"
    caffeinate -u -t 3 2>/dev/null || true
    sleep 2
  done
  return 0
}

# ---- asset presence ---------------------------------------------------------
while IFS= read -r a; do
  if [[ ! -f $a ]]; then
    echo "FAIL  asset missing: $a (referenced in src/)"
    fail=1
  fi
done < <(grep -ohE '"assets/[A-Za-z0-9_.-]+\.(glb|mp3|wav|png)"' src/*.c src/*.h src/*.m 2>/dev/null | tr -d '"' | sort -u)

# ---- per-map boot checks ----------------------------------------------------
maps=("$@")
if [[ ${#maps[@]} -eq 0 ]]; then maps=(maps/*.map); fi

for m in "${maps[@]}"; do
  name=$(basename "$m" .map)
  run_game "$LOGDIR/$name.stdout" --map "$m" --frames 300 --debug --seed 1337
  [[ -f chernobyl2-debug.log ]] && cp chernobyl2-debug.log "$LOGDIR/$name.jsonl"
  if out=$(python3 - "$LOGDIR/$name.jsonl" "$code" <<'PYEOF'
import json, sys
path, code = sys.argv[1], int(sys.argv[2])
ev = {}
try:
    for line in open(path):
        line = line.strip()
        if line:
            d = json.loads(line)
            ev.setdefault(d.get("ev"), d)   # first event of each kind = the requested map
except FileNotFoundError:
    print("no debug log written"); sys.exit(1)
bad = []
if code != 0: bad.append(f"exit code {code}")
m = ev.get("map")
if not m: bad.append("no map event")
elif not (m.get("ok") and m.get("tris", 0) > 0): bad.append(f"map ok={m.get('ok')} tris={m.get('tris')}")
n = ev.get("nav")
if not n: bad.append("no nav event")
elif not (n.get("ok") and n.get("walkable", 0) > 0): bad.append(f"nav ok={n.get('ok')} walkable={n.get('walkable')}")
a = ev.get("audio")
if not a or not a.get("ready"): bad.append("audio not ready")
s = ev.get("shutdown")
if not s or s.get("ok") is not True: bad.append(f"shutdown={s}")
if bad:
    print("; ".join(bad)); sys.exit(1)
print(f"tris={m['tris']} walkable={n['walkable']} bake={m.get('bake_sec')}s")
PYEOF
  ); then
    printf 'PASS  %-18s %s\n' "$name" "$out"
  else
    printf 'FAIL  %-18s %s\n' "$name" "$out"
    fail=1
  fi
done

# ---- perf gate (M2): field saturated at MAX_ENEMIES, uncapped + 30 fps ------
for gate in "0 600 8.3" "30 240 999"; do
  set -- $gate; fps=$1; frames=$2; budget=$3
  run_game "$LOGDIR/perf$fps.stdout" --map maps/afterslime.map --frames "$frames" --debug --seed 7 --stress --god --fps "$fps"
  [[ -f chernobyl2-debug.log ]] && cp chernobyl2-debug.log "$LOGDIR/perf$fps.jsonl"
  if out=$(python3 - "$LOGDIR/perf$fps.jsonl" "$code" "$budget" <<'PYEOF'
import json, sys
path, code, budget = sys.argv[1], int(sys.argv[2]), float(sys.argv[3])
perf = shut = None
try:
    for line in open(path):
        line = line.strip()
        if line:
            d = json.loads(line)
            if d.get("ev") == "perf": perf = d
            if d.get("ev") == "shutdown": shut = d
except FileNotFoundError:
    print("no debug log written"); sys.exit(1)
bad = []
if code != 0: bad.append(f"exit code {code}")
if not shut or shut.get("ok") is not True: bad.append(f"shutdown={shut}")
if not perf: bad.append("no perf event")
elif perf["p95_ms"] > budget: bad.append(f"p95 {perf['p95_ms']}ms > budget {budget}ms")
if bad:
    print("; ".join(bad)); sys.exit(1)
print(f"avg={perf['avg_ms']}ms p95={perf['p95_ms']}ms max={perf['max_ms']}ms over {perf['frames']} frames")
PYEOF
  ); then
    printf 'PASS  %-18s %s\n' "perf-fps$fps" "$out"
  else
    printf 'FAIL  %-18s %s\n' "perf-fps$fps" "$out"
    fail=1
  fi
done

# ---- weapon-cycle ammo regression (M1) --------------------------------------
# --cycle walks all 9 slots twice (spending ammo on pass 1, reading it back on
# pass 2); any slot-state corruption around the ActivateWeapon /
# StashActiveTuning swap shows up as a pass-2 mismatch.
run_game "$LOGDIR/cycle.stdout" --map maps/afterslime.map --frames 700 --debug --seed 1 --cycle --no-enemies
[[ -f chernobyl2-debug.log ]] && cp chernobyl2-debug.log "$LOGDIR/cycle.jsonl"
if out=$(python3 - "$LOGDIR/cycle.jsonl" "$code" <<'PYEOF'
import json, sys
path, code = sys.argv[1], int(sys.argv[2])
p1, p2 = {}, {}
try:
    for line in open(path):
        line = line.strip()
        if line:
            d = json.loads(line)
            if d.get("ev") == "ammo": (p1 if d["phase"] == 1 else p2)[d["slot"]] = d
except FileNotFoundError:
    print("no debug log written"); sys.exit(1)
bad = []
if code != 0: bad.append(f"exit code {code}")
if len(p1) != 9 or len(p2) != 9: bad.append(f"saw {len(p1)}/{len(p2)} slots (want 9/9)")
for s in sorted(p1):
    a, b = p1[s], p2.get(s)
    if not b: continue
    if (a["mag"], a["reserve"]) != (b["mag"], b["reserve"]):
        bad.append(f"slot {s}: pass1 {a['mag']}/{a['reserve']} -> pass2 {b['mag']}/{b['reserve']}")
    if b["magSize"] > 0 and b["mag"] > b["magSize"]:
        bad.append(f"slot {s}: mag {b['mag']} > magSize {b['magSize']}")
if bad:
    print("; ".join(bad)); sys.exit(1)
print("9 slots, ammo state survives the swap")
PYEOF
); then
  printf 'PASS  %-18s %s\n' "weapon-cycle" "$out"
else
  printf 'FAIL  %-18s %s\n' "weapon-cycle" "$out"
  fail=1
fi

exit "$fail"
