#!/bin/bash
# Falling into a breach -- the pits they climb out of -- kills you. Always: god mode and --no-enemies too (owner,
# 2026-11-16 "if a fall into a spawn pit, i die"; 2026-11-17 "if i fall in the pit, player dies" -- the first cut still
# threw god mode back out onto the lip, as the old pit did everyone, for 25 hp). A god-mode run posts no score (scores.h).
# Drops the player into breach 0's mouth (CHERNOBYL_AT; _PLAY keeps the enemies on).
cd "$(dirname "$0")/.." || exit 1
make -s all >/dev/null || { echo "FAIL build"; exit 1; }
fail=0; ok(){ echo "ok   $1"; }; bad(){ echo "FAIL $1"; fail=1; }
L=chernobyl2-debug.log
check(){   # $1 = which run: one fall, and it killed
    local n f; n=$(grep -c '"ev":"holefall"' $L); f=$(grep -m1 '"ev":"holefall"' $L)
    case "$f" in *'"died":true'*) ;; '') bad "$1: never reached the bottom"; return;; *) bad "$1: fell in and lived: $f"; return;; esac
    [ "$n" = 1 ] || { bad "$1: $n falls logged (thrown out and back in?)"; return; }
    grep -q '"ev":"death"' $L && ok "$1: fell in, dead" || bad "$1: the fall was logged but no death"
}
rm -f $L; CHERNOBYL_BACKGROUND=1 CHERNOBYL_AT="-46 -33" CHERNOBYL_AT_PLAY=1 ./build/chernobyl2 --no-intro --mute --debug --frames 360 >/dev/null 2>&1 </dev/null
check "playing"
rm -f $L; CHERNOBYL_BACKGROUND=1 CHERNOBYL_AT="-46 -33" CHERNOBYL_AT_PLAY=1 ./build/chernobyl2 --no-intro --mute --debug --god --frames 360 >/dev/null 2>&1 </dev/null
check "god mode"
rm -f $L; CHERNOBYL_BACKGROUND=1 CHERNOBYL_AT="-46 -33" ./build/chernobyl2 --no-intro --mute --debug --no-enemies --frames 360 >/dev/null 2>&1 </dev/null
check "--no-enemies"
[ $fail = 0 ] && echo PASS || { echo FAIL; exit 1; }
