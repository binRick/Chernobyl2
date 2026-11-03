#!/bin/bash
# God-mode sandbox: the Chernobyl arena with every enemy type mixed into each wave
# (head chef included), boss interludes on. Extra args pass through (e.g. --mute).
cd "$(dirname "$0")"
exec ./run.sh --god --enemy all "$@"
