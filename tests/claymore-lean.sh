#!/bin/bash
# The claymore kept cheap (owner, 2026-11-17: "check yesterday's changes. specifically the mine. optimize.").
# As first built it cost 0.39 s of the load and some 60 MB of video memory -- seventeen 1024 px textures, five of them
# the same pictures twice -- and four draws for every one laid, seen or not. Now:
#   - the mine is ONE mesh on ONE sheet (tools/mines/build_claymore.py one_sheet): colour, normal, ORM -- three files;
#   - the one in the hand carries no textures of its own: it is given the same colour sheet, and the grenade launcher's
#     sleeve and glove (it is that rig's arm), so nothing is decoded or held twice;
#   - a laid one that cannot be on screen is not drawn, nor its lasers; one that can be, is.
# Dev hooks, no synthetic keys: --mines (the sandbox: never runs out), CHERNOBYL_MINE_AUTO=s lays one every s seconds,
# CHERNOBYL_WALK="dx dz" walks that way. The game logs what it drew once a second ("minedraw").
cd "$(dirname "$0")/.." || exit 1
make -s all >/dev/null || { echo "FAIL build"; exit 1; }
fail=0; ok(){ echo "ok   $1"; }; bad(){ echo "FAIL $1"; fail=1; }
L=chernobyl2-debug.log
num(){ echo "$1" | sed -n "s/.*\"$2\":\([0-9-]*\).*/\1/p"; }

# ---- the files
vm=$(stat -f%z assets/claymore_vm.glb)
[ "$vm" -lt 1000000 ] && ok "the hand model carries no textures ($vm bytes)" || bad "assets/claymore_vm.glb is $vm bytes (textures inside it)"
n=$(ls assets/mines/*.png 2>/dev/null | wc -l | tr -d ' '); tot=$(cat assets/mines/*.png 2>/dev/null | wc -c | tr -d ' ')
[ "$n" = 3 ] && ok "one sheet: 3 texture files" || bad "$n texture files in assets/mines (want 3: colour, normal, ORM)"
[ "$tot" -lt 3000000 ] && ok "its textures: $tot bytes" || bad "its textures: $tot bytes (want under 3 MB)"

# ---- loaded once; looked down at, at his feet, they are all drawn (--look: the spawn's own heading, 38 degrees down --
#      looking level they are under the bottom of the screen, rightly not drawn, and only just: no test to hang on that)
rm -f $L
CHERNOBYL_BACKGROUND=1 CHERNOBYL_MINE_AUTO=1.2 ./build/chernobyl2 --mines --god --no-enemies --no-intro --mute --debug --look -94.7 -38 --frames 900 >/dev/null 2>&1 </dev/null
m=$(grep -m1 '"ev":"mines"' $L)
case "$m" in *'"vm":1,"world":1'*) ok "loads: $m";; *) bad "did not load: '$m'";; esac
[ "$(num "$m" meshes)" = 1 ] && ok "one mesh: one draw a mine" || bad "meshes '$(num "$m" meshes)' (want 1)"
[ "$(num "$m" tex)" = 3 ] && ok "three textures read from disk" || bad "textures read '$(num "$m" tex)' (want 3)"
[ "$(num "$m" vmTex)" = 2 ] && ok "the hand model shares them: the mine's sheet, the launcher's sleeve" || bad "hand model shared '$(num "$m" vmTex)' (want 2)"
echo "     (load: $(num "$m" ms) ms)"
d=$(grep '"ev":"minedraw"' $L | tail -1); laid=$(num "$d" laid); models=$(num "$d" models); lasers=$(num "$d" lasers); armed=$(num "$d" armed)
[ -n "$laid" ] && [ "$laid" -ge 3 ] && [ "$models" = "$laid" ] && ok "at his feet: all $laid drawn" || bad "at his feet: laid '$laid', drawn '$models' ($d)"
[ -n "$armed" ] && [ "$armed" -ge 2 ] && [ "$lasers" = "$armed" ] && ok "and the lasers of the $armed armed" || bad "armed '$armed', lasers drawn '$lasers' ($d)"

# ---- walked away from: the ones left far behind are not drawn
rm -f $L
CHERNOBYL_BACKGROUND=1 CHERNOBYL_MINE_AUTO=1.2 CHERNOBYL_WALK="-1 0" ./build/chernobyl2 --mines --god --no-enemies --no-intro --mute --debug --frames 1500 >/dev/null 2>&1 </dev/null
d=$(grep '"ev":"minedraw"' $L | tail -1); laid=$(num "$d" laid); models=$(num "$d" models); lasers=$(num "$d" lasers); armed=$(num "$d" armed)
[ -n "$laid" ] && [ "$laid" -ge 4 ] && [ "$models" -lt "$laid" ] && ok "walked on: $models of $laid drawn" || bad "walked on: laid '$laid', drawn '$models' -- none culled ($d)"
[ -n "$armed" ] && [ "$lasers" -lt "$armed" ] && ok "and $lasers of $armed fans of laser" || bad "armed '$armed', lasers drawn '$lasers' -- none culled ($d)"
[ $fail = 0 ] && echo PASS || { echo FAIL; exit 1; }
