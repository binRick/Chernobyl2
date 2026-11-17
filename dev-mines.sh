#!/bin/bash
# Claymore sandbox (dev): the plant, an M18A1 in the hand from the start, and they never run out (you cannot be hurt).
# In the game itself the claymore is weapon 5: three carried a life, five at most, an ammo box adds one (src/mines.h).
#   LEFT CLICK  lay one: the view kneels, the hand sets it on the floor, facing where you look
#   it arms (green light, then red), its laser fan shows the arc it covers, and it fires when one of them walks into it
#   1-4 the guns, 5 the claymore again.   ./dev-mines.sh --stress   a field full of them
cd "$(dirname "$0")"
if [[ $1 == -h || $1 == --help ]]; then sed -n '2,6p' "$0" | sed 's/^# \{0,1\}//'; exit 0; fi
make -s all >/dev/null || { echo "build failed: run 'make' to see why" >&2; exit 1; }
exec tools/with-log.sh mines ./build/chernobyl2 --mines --god --no-intro "$@"
