#!/bin/bash
# Played again after a death, the run starts fresh: as the claymores and the pickups are (b75ba60, the owner: the arena's
# respawn kept the last life's mines), so the traps -- none still armed, running or cooling from the life before. They
# were read from the map once, so a trap he armed (or one cooling after it went off) came into the next run as it was.
# Dev hooks: CHERNOBYL_TRAP_ARM=1 arms every trap; CHERNOBYL_AT stands him in breach 0, so he dies at once; and
# CHERNOBYL_DEATH_AGAIN=2 plays again 2 s after a death. The debug log says how many traps start each run not idle.
cd "$(dirname "$0")/.." || exit 1
make -s all >/dev/null || { echo "FAIL build"; exit 1; }
L=chernobyl2-debug.log; rm -f $L
# (no --frames: with it a death's "play again" ends the game; CHERNOBYL_RUN_END ends it, the watchdog behind that)
CHERNOBYL_BACKGROUND=1 CHERNOBYL_TRAP_ARM=1 CHERNOBYL_AT="-46 -33" CHERNOBYL_DEATH_AGAIN=2 CHERNOBYL_RUN_END=14 perl -e 'alarm 180; exec @ARGV' ./build/chernobyl2 --no-intro --mute --debug >/dev/null 2>&1 </dev/null
python3 - $L <<'PY'
import json, sys
ev = []
for line in open(sys.argv[1]):
    try: ev.append(json.loads(line))
    except Exception: pass
runs = [e for e in ev if e.get("ev") == "runtraps"]; deaths = [e for e in ev if e.get("ev") == "death"]; armed = [e for e in ev if e.get("ev") == "trap" and e.get("what") == "armed"]
fail = 0
def ok(t): print("ok   " + t)
def bad(t):
    global fail; print("FAIL " + t); fail = 1
(ok if len(runs) >= 2 and len(deaths) >= 1 else bad)(f"he died and played again ({len(deaths)} deaths, {len(runs)} runs started)")
(ok if runs and runs[0].get("busy") == 0 and runs[0].get("traps", 0) > 0 else bad)(f"the first run starts with every trap idle ({runs[0].get('busy') if runs else None} of {runs[0].get('traps') if runs else None} not)")
later = [r["busy"] for r in runs[1:]]
(ok if later and all(b == 0 for b in later) else bad)(f"and so does every run after a death ({', '.join(str(b) for b in later) or 'none'} not idle at its start; 0 each)")
# The hooks' own lines name each field once: a second "t" (the run clock) stood in for the log's clock, and a reader
# of the log put the end of a 10-minute run at 95.7 s.
dup = []
for line in open(sys.argv[1]):
    if '"ev":"runagain"' in line or '"ev":"runend"' in line:
        keys = [k for k, _ in json.loads(line, object_pairs_hook=lambda p: p)]
        dup += [line.strip()[:90] for k in set(keys) if keys.count(k) > 1]
hooks = sum(1 for line in open(sys.argv[1]) if '"ev":"runagain"' in line or '"ev":"runend"' in line)
(ok if hooks and not dup else bad)(f"the play-again and end lines ({hooks}) name each field once" + (f": not {dup[0]}" if dup else ""))
print("PASS" if not fail else "FAIL"); sys.exit(fail)
PY
