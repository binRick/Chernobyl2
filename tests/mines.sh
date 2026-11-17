#!/bin/bash
# The claymore (src/mines.h): laid from the hand onto the floor -- the hand-off a short settle, upright -- armed, tripped
# by what walks into its fan, and killing. Lays one every two seconds in a full field (CHERNOBYL_MINE_AUTO). Hidden, muted.
cd "$(dirname "$0")/.." || exit 1
make -s all >/dev/null || { echo "FAIL build"; exit 1; }
rm -f chernobyl2-debug.log
CHERNOBYL_BACKGROUND=1 CHERNOBYL_MINE_AUTO=2 ./build/chernobyl2 --mines --no-intro --mute --debug --god --stress --seed 1 --frames 1500 >/dev/null 2>&1 </dev/null
L=chernobyl2-debug.log; fail=0; ok(){ echo "ok   $1"; }; bad(){ echo "FAIL $1"; fail=1; }
grep -q '"ev":"mines","vm":1,"world":1,"anims":2' $L && ok "claymore loaded (hand, world model, both clips)" || bad "claymore not loaded: $(grep '"ev":"mines"' $L)"
n=$(grep -c '"placed"' $L); [ "$n" -ge 3 ] && ok "laid $n" || bad "laid $n"
worst=$(grep -o '"drop":-*[0-9.]*' $L | cut -d: -f2 | awk '{a=$1<0?-$1:$1; if(a>m)m=a} END{print m+0}')
awk -v w=$worst 'BEGIN{exit !(w<0.10)}' && ok "hand to floor: settled at most ${worst} m" || bad "hand-off dropped ${worst} m"
lowup=$(grep -o '"upY":[0-9.]*' $L | cut -d: -f2 | sort -n | head -1)
awk -v u=$lowup 'BEGIN{exit !(u>0.95)}' && ok "let go of upright (up.y >= $lowup)" || bad "let go of tilted (up.y $lowup)"
grep -q '"armed"' $L && ok "armed" || bad "never armed"
grep -q '"tripped"' $L && ok "tripped ($(grep -c '"tripped"' $L)x)" || bad "never tripped"
k=$(grep -o '"explode".*"kills":[0-9]*' $L | grep -o '"kills":[0-9]*' | cut -d: -f2 | awk '{s+=$1} END{print s+0}')
[ "$k" -ge 1 ] && ok "killed $k" || bad "killed none"
# shot: lay one, take the Remington out four seconds on, its sights on the mine (CHERNOBYL_MINE_GUN keeps them there), and fire
rm -f chernobyl2-debug.log
CHERNOBYL_BACKGROUND=1 CHERNOBYL_MINE_AUTO=100 CHERNOBYL_MINE_GUN=4.0 ./build/chernobyl2 --mines --weapon 1 --autofire --no-intro --mute --debug --god --no-enemies --frames 600 >/dev/null 2>&1 </dev/null
grep -q '"placed"' $L && grep -q '"gun":1' $L && ok "laid one, then took the gun out" || bad "the shooting run never laid one / drew the gun"
grep -q '"setoff"' $L && grep -q '"explode"' $L && ok "a round into it set it off" || bad "shot at, it never went off: $(grep -c '"setoff"' $L) set-offs"
[ $fail = 0 ] && echo PASS || { echo FAIL; exit 1; }
