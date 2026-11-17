#!/bin/bash
# M on the intro mutes it, as in the game, and the game starts muted (owner, 2026-11-16: "i should be able to mute on
# the intro screen"). CHERNOBYL_INTRO_MUTE=0 presses M before the intro's music starts, so the run is silent;
# CHERNOBYL_INTRO_AUTO skips the montage at 1 s and presses start at 1.5 s -- during the loading, on purpose: the hook's
# press must wait for the title (it used to be dropped, and the title waited for good once the claymore made the load a
# moment longer); _FORCE plays the intro despite --frames. A watchdog fails a hang instead of waiting on it.
# (Unmuting is the same toggle, but would play the music out loud, so it is not run here.)
cd "$(dirname "$0")/.." || exit 1
make -s all >/dev/null || { echo "FAIL build"; exit 1; }
fail=0; ok(){ echo "ok   $1"; }; bad(){ echo "FAIL $1"; fail=1; }
L=chernobyl2-debug.log; rm -f $L
CHERNOBYL_BACKGROUND=1 CHERNOBYL_INTRO_FORCE=1 CHERNOBYL_INTRO_MUTE=0 CHERNOBYL_INTRO_AUTO="1,1.5" perl -e 'alarm 90; exec @ARGV' ./build/chernobyl2 --debug --frames 120 >/dev/null 2>&1 </dev/null
grep -q '"ev":"intro","video"' $L && ok "the intro played" || bad "no intro"
grep -q '"ev":"mute","on":1,"intro":1' $L && ok "M on the intro muted it" || bad "no mute on the intro"
s=$(grep -m1 '"ev":"intro","skip"' $L | grep -o '"skip":[0-9.]*' | cut -d: -f2)
[ -n "$s" ] && python3 -c "import sys; sys.exit(0 if float('$s')>=0.9 else 1)" && ok "the montage ran on until its skip (at ${s}s), M (at 0) did not end it" || bad "skip at '${s}'"
e=$(grep -m1 '"ev":"intro","end"' $L)
case "$e" in *'"muted":1,"vol":0.00'*) ok "the game starts muted: $e";; *) bad "after the intro: '$e'";; esac
n=$(grep -c '"ev":"frame"' $L); [ "$n" -gt 0 ] && ok "and then ran ($n frame logs)" || bad "the game never ran"
grep -q '"ev":"mute","on":0' $L && bad "something unmuted it: $(grep -m1 '"ev":"mute","on":0' $L)" || ok "nothing unmuted it after"
[ $fail = 0 ] && echo PASS || { echo FAIL; exit 1; }
