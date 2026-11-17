#!/bin/bash
# Weapon 5: the claymore in the game itself, not only the --mines sandbox (owner, 2026-11-17: "add mine as weapon 5").
# It loads with the guns; 5 (the phone's fifth button) takes it out, 1-4 put it away. Three carried at the start of a
# life, five at most, an ammo box adds one; laying the last one brings the gun back up, and 5 with none left only says so.
# Dev hooks, no synthetic keys: CHERNOBYL_MINE_PICK="s1,s2" presses 5 at those seconds of play; CHERNOBYL_MINE_AUTO=s
# lays one every s seconds while one is in the hand.
cd "$(dirname "$0")/.." || exit 1
make -s all >/dev/null || { echo "FAIL build"; exit 1; }
fail=0; ok(){ echo "ok   $1"; }; bad(){ echo "FAIL $1"; fail=1; }
L=chernobyl2-debug.log; rm -f $L
CHERNOBYL_BACKGROUND=1 CHERNOBYL_MINE_PICK="1.0,9.0" CHERNOBYL_MINE_AUTO=1.3 ./build/chernobyl2 --god --no-enemies --no-intro --mute --debug --frames 1200 >/dev/null 2>&1 </dev/null
ld=$(grep -n -m1 '"ev":"mines","vm":1,"world":1' $L | cut -d: -f1); f0=$(grep -n -m1 '"ev":"frame"' $L | cut -d: -f1)
[ -n "$ld" ] && [ -n "$f0" ] && [ "$ld" -lt "$f0" ] && ok "loaded with the guns, before play (log line $ld < $f0)" || bad "not loaded up front (mines at '$ld', first frame at '$f0')"
p=$(grep -m1 '"pick":' $L)
case "$p" in *'"pick":1,"left":3'*) ok "5 takes it out, three carried: $p";; *) bad "5 did not take one out: '$p'";; esac
n=$(grep -c '"ev":"mine","placed"' $L)
[ "$n" = 3 ] && ok "laid three, then no more" || bad "laid $n (want 3: the three carried)"
grep -q '"ev":"mine","empty":1' $L && ok "the last one laid: the gun back in the hand" || bad "never put away when empty"
grep -q '"pick":0,"left":0' $L && ok "5 with none left: refused" || bad "no refusal logged for 5 with none left"
# With the claymore in the hand the gun is put away: R (and an empty gun's own reload) must not work it unseen -- its
# reload sounded with no gun on screen. It reloads when it is back in the hand. Hooks: --reload-at N empties the gun
# (CHERNOBYL_RELOAD_REAL=empty) and asks for a reload at frame N; CHERNOBYL_MINE_GUN=s puts the claymore away after s.
rm -f $L
CHERNOBYL_BACKGROUND=1 CHERNOBYL_MINE_PICK="1.0" CHERNOBYL_MINE_GUN=9.0 CHERNOBYL_RELOAD_REAL=empty ./build/chernobyl2 --god --no-enemies --no-intro --mute --debug --weapon 2 --reload-at 300 --frames 1500 >/dev/null 2>&1 </dev/null
g=$(grep -n -m1 '"ev":"mine","gun":1' $L | cut -d: -f1); f=$(grep -n -m1 '"ev":"frame","n":330,' $L | cut -d: -f1); r=$(grep -n -m1 '"ev":"reload"' $L | cut -d: -f1)
[ -n "$g" ] && [ -n "$f" ] && [ "$f" -lt "$g" ] || bad "the run did not ask for a reload while the claymore was out (frame 330 at line '$f', gun back at '$g')"
[ -n "$g" ] && { [ -z "$r" ] || [ "$r" -gt "$g" ]; } && ok "claymore in the hand: the put-away gun is not reloaded" || bad "the gun was reloaded while put away (reload at line '$r', gun back at line '$g')"
[ -n "$g" ] && [ -n "$r" ] && [ "$r" -gt "$g" ] && ok "back in the hand, empty: it reloads" || bad "the empty gun never reloaded once it was back (reload at '$r', gun back at '$g')"
[ $fail = 0 ] && echo PASS || { echo FAIL; exit 1; }
