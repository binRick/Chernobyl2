#!/bin/bash
# The guns sit where the code frames them: no saved vm_trim_*.txt can re-aim one, and the game never writes one.
# (The in-game tuning keys went in add0c6e, but a trim saved by a stray key press before then -- the grenade launcher
# tilted 22 degrees -- was still loaded at every start and written back at every quit, 2026-11-16.)
cd "$(dirname "$0")/.." || exit 1
make -s all >/dev/null || { echo "FAIL build"; exit 1; }
fail=0; f=vm_trim_minigun.txt
[ -e $f ] && mv $f $f.testbak
printf '0 0 0 1 0 40 0 0\n' > $f           # a planted 40-degree tilt on the minigun (not in SaveTune's format: a rewrite shows)
rm -f chernobyl2-debug.log
CHERNOBYL_BACKGROUND=1 ./build/chernobyl2 --no-intro --mute --debug --no-enemies --frames 60 >/dev/null 2>&1 </dev/null
t=$(grep -o '"slot":2,"label":"Minigun","trim":\[[^]]*\]' chernobyl2-debug.log | head -1)
case "$t" in
  *'[0.00,0.00,0.00,1.000,0.00,0.00,0.00]'*) echo "ok   minigun held in its coded frame: $t";;
  '') echo "FAIL no weapon line in the log"; fail=1;;
  *) echo "FAIL a saved trim re-aimed the minigun: $t"; fail=1;;
esac
[ "$(cat $f)" = "0 0 0 1 0 40 0 0" ] && echo "ok   the game did not rewrite the trim file at quit" || { echo "FAIL the game rewrote $f: $(cat $f)"; fail=1; }
rm -f $f
[ -e $f.testbak ] && mv $f.testbak $f
[ $fail = 0 ] && echo PASS || { echo FAIL; exit 1; }
