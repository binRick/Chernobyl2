#!/bin/bash
# Enemy gallery (dev): every enemy, every state, at in-game speed -- no map, no game.
#   TAB / ]  next enemy     [  previous      1 idle 2 walk 3 pursue 4 attack 5 hit 6 die 7 cast (priest)
#   A auto-cycle   SPACE pause   drag/arrows turn   wheel zoom   F face close-up   ESC quit
cd "$(dirname "$0")"
make -s all >/dev/null || exit 1
exec ./build/chernobyl2 --viewer all
