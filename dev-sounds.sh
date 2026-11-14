#!/bin/bash
# Sound board (dev): every sound in the game, grouped (weapons, player, enemies, events,
# feedback, ambience), each with what triggers it and where it comes from. No map, no game.
#   ./dev-sounds.sh
# Keys:
#   click a row  play it          SPACE  stop          wheel / arrows  scroll
#   TAB  next group               M  show only the MISSING and BORROWED rows          ESC  quit
# Rows marked MISSING are cues with no sound yet; BORROWED rows use another sound as a stand-in.
cd "$(dirname "$0")"
if [[ $1 == -h || $1 == --help ]]; then sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'; exit 0; fi
make -s all && exec tools/with-log.sh sounds ./build/chernobyl2 --no-intro --sounds "$@"
