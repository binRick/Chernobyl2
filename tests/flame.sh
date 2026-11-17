#!/bin/bash
# The flamethrower never knocks anyone down (owner, 2026-11-17). Fire burns: it flinches a man now and then and kills
# him where he stands; it does not throw him off his feet, and it does not set off a laid claymore (whose blast throws
# everyone in its fan -- the knockdowns the owner saw while burning them in the claymore build).
cd "$(dirname "$0")/.." || exit 1
make -s all >/dev/null || { echo "FAIL build"; exit 1; }
fail=0; ok(){ echo "ok   $1"; }; bad(){ echo "FAIL $1"; fail=1; }
L=chernobyl2-debug.log
# 1. a wave, burned (--kite: face the nearest one and back away, trigger held)
rm -f $L
CHERNOBYL_BACKGROUND=1 ./build/chernobyl2 --kite --weapon 5 --autofire --god --no-intro --mute --debug --frames 3600 >/dev/null 2>&1 </dev/null
fl=$(grep -c '"ev":"flinch"' $L); de=$(grep '"ev":"edeath"' $L | grep -c '"cause":2'); kn=$(grep -c '"ev":"knock"' $L)
[ "$de" -gt 0 ] && ok "burned $de to death ($fl flinches)" || bad "the flame killed nobody ($fl flinches): the run proves nothing"
[ "$kn" = 0 ] && ok "nobody knocked down" || bad "$kn knocked down under the flame: $(grep -m2 '"ev":"knock"' $L)"
# 2. a laid claymore in the stream: lay one, take the flamethrower out four seconds on and hold it on the mine
rm -f $L
CHERNOBYL_BACKGROUND=1 CHERNOBYL_MINE_AUTO=100 CHERNOBYL_MINE_GUN=4.0 ./build/chernobyl2 --mines --weapon 5 --autofire --no-intro --mute --debug --god --no-enemies --frames 900 >/dev/null 2>&1 </dev/null
grep -q '"placed"' $L && grep -q '"gun":1' $L && grep -q '"ev":"flame","lit"' $L && ok "laid one, then burned it" || bad "never laid one / lit the flamethrower at it"
n=$(grep -cE '"setoff"|"explode"' $L)
[ "$n" = 0 ] && ok "the claymore did not go off in the fire" || bad "the fire set it off: $(grep -m2 -E '"setoff"|"explode"' $L)"
[ $fail = 0 ] && echo PASS || { echo FAIL; exit 1; }
