#!/bin/bash
# A trap that crushes must kill the man under it (owner's report 2026-11-16: the crane's load came down and he was
# thrown out in front of it, alive). Holds one enemy under each crushing trap in the sandbox (CHERNOBYL_TRAP_VICTIM)
# and reads the outcome from the debug log. Hidden and muted.
cd "$(dirname "$0")/.." || exit 1
make -s all >/dev/null || { echo "FAIL build"; exit 1; }
fail=0
# trap | where the man stands, in the trap's axes: dead centre, and under the rim of what comes down
for spot in "4 0 0" "4 1.7 0" "4 0 1.7" "5 0 0" "5 1.3 0"; do   # 4 CRANE DROP (load 1.85 m round), 5 BLAST DOOR (traps.h TR_*)
  k=${spot%% *}
  for seed in 1 2 3; do
    rm -f chernobyl2-debug.log
    CHERNOBYL_BACKGROUND=1 CHERNOBYL_TRAP_VICTIM="$spot" ./build/chernobyl2 --traps --mute --debug --seed $seed --frames 900 >/dev/null 2>&1 </dev/null
    out=$(grep -o "\"k\":$k,\"victim\":[0-9]*,\"outcome\":\"[a-z]*\"" chernobyl2-debug.log | head -1)
    case "$out" in
      *dead*)  echo "ok   trap \"$spot\" seed $seed: $out";;
      *alive*) echo "FAIL trap \"$spot\" seed $seed: $out"; fail=1;;
      *)       echo "FAIL trap \"$spot\" seed $seed: no outcome logged (no enemy reached the sandbox?)"; fail=1;;
    esac
  done
done
[ $fail = 0 ] && echo PASS || { echo FAIL; exit 1; }
