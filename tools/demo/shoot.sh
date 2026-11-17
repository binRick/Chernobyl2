#!/bin/bash
# Film demo shots: ./shoot.sh <name-prefix|all> [takes]   -> build/demo/takes/<name>-<seed>.mp4 + <name>-sheet.jpg
# Shots are in shots.txt; env values use _ for spaces. Runs hidden (CHERNOBYL_BACKGROUND=1).
cd "$(dirname "$0")/../.." || exit 1
make -s all >/dev/null || { echo "build failed" >&2; exit 1; }
want=${1:-all}; takes=${2:-1}; out=build/demo/takes; mkdir -p $out
while IFS='|' read -r -u 3 name frames flags env; do   # (fd 3: ffmpeg and the game must not eat the list)
  name=$(echo $name); [ -z "$name" ] && continue
  [[ $want == all || $name == $want* ]] || continue
  for seed in $(seq 1 $takes); do
    f=$out/$name-$seed.mp4
    envs=(CHERNOBYL_BACKGROUND=1 CHERNOBYL_CLEAN_HUD=1); for e in $env; do k=${e%%=*}; v=${e#*=}; envs+=("$k=${v//_/ }"); done
    env "${envs[@]}" ./build/chernobyl2 --no-intro --mute --seed $seed --frames $frames $flags --record $f >$out/$name-$seed.log 2>&1 </dev/null
    d=$(ffprobe -v error -show_entries format=duration -of csv=p=0 $f 2>/dev/null)
    ffmpeg -nostdin -v error -y -i $f -vf "fps=$(awk -v d=${d:-10} 'BEGIN{print 24/d}'),scale=384:-1,tile=6x4:padding=2" -frames:v 1 $out/$name-$seed-sheet.jpg
    echo "$name-$seed  ${d:-FAILED}s"
  done
done 3< <(grep -v '^#' tools/demo/shots.txt)
