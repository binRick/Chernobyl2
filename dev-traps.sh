#!/bin/bash
# Trap sandbox (dev): the plant's eight traps in a ring round you, in the open arena, with enemies walking in through them.
#   ./dev-traps.sh            play it (you cannot be hurt)
#   ./dev-traps.sh --auto     fire each trap in turn, one every five seconds
# Keys: E at a trap's switch post (lamp green = ready) sets it off    T the next one    Y all of them    N enemies on/off
#   1 STEAM MAIN   2 LIVE PLATE   3 FIRE HOSE   4 BREACH SURGE   5 CRANE DROP   6 BLAST DOOR   7 COOLANT SPILL (E again: ignite)   8 TURBINE
cd "$(dirname "$0")"
if [[ $1 == -h || $1 == --help ]]; then sed -n '2,6p' "$0" | sed 's/^# \{0,1\}//'; exit 0; fi
make -s all >/dev/null || { echo "build failed: run 'make' to see why" >&2; exit 1; }
[[ $1 == --auto ]] && export CHERNOBYL_TRAP_AUTO=5
exec tools/with-log.sh traps ./build/chernobyl2 --traps
