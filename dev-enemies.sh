#!/bin/bash
# Enemy gallery (dev): every enemy, every state, at in-game speed -- no map, no game.
#   ./dev-enemies.sh                 start on the first enemy, walking
#   ./dev-enemies.sh gunner          start on the gunner
#   ./dev-enemies.sh priest 7        start on the priest's cast (clip numbers below)
#   ./dev-enemies.sh --help          list enemies and keys
# Keys:
#   TAB / ]  next enemy     [  previous      1 idle 2 walk 3 pursue 4 attack 5 hit 6 die 7 cast (priest)
#   A auto-cycle   SPACE pause   drag/arrows turn   wheel zoom   F face close-up   ESC quit
cd "$(dirname "$0")"
ENEMIES="cook linecook butcher liquidator headchef rifleman gunner mutant priest revenant fiend"
if [[ $1 == -h || $1 == --help ]]; then
    sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'
    echo; echo "Enemies: $ENEMIES"
    exit 0
fi
if [[ -n $1 && " $ENEMIES " != *" $1 "* ]]; then
    echo "unknown enemy '$1' -- one of: $ENEMIES" >&2; exit 1
fi
if [[ -n $2 && ! $2 =~ ^[1-7]$ ]]; then
    echo "clip must be 1-7 (1 idle 2 walk 3 pursue 4 attack 5 hit 6 die 7 cast)" >&2; exit 1
fi
make -s all >/dev/null || { echo "build failed: run 'make' to see why" >&2; exit 1; }
[[ -n $1 ]] && export CHERNOBYL_GALLERY_START="$1${2:+:$2}"
exec ./build/chernobyl2 --viewer all
