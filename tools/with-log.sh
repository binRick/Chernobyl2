#!/bin/bash
# Run a launch with everything it prints kept: tools/with-log.sh <name> <command...>
# The whole of stdout + stderr goes to logs/<name>.log (overwritten each run; the previous one is kept as <name>.prev.log)
# and to the console -- less raylib's per-model "No more than 2 texture coordinates" lines, which are harmless (a gun
# model carries more UV sets than raylib reads) and are only counted. Ends with a one-line tally and the log's path, and
# exits with the game's own status.
name="$1"; shift
cd "$(dirname "$0")/.." || exit 1
mkdir -p logs; log="logs/$name.log"
[[ -f $log ]] && mv -f "$log" "logs/$name.prev.log"
echo "=== $(date '+%Y-%m-%d %H:%M:%S')  $*" > "$log"
HARMLESS='No more than 2 texture coordinates'
"$@" 2>&1 | tee -a "$log" | grep --line-buffered -v "$HARMLESS"
status=${PIPESTATUS[0]}
all=$(grep -c -E 'WARNING|ERROR|[Ee]rror|Failed|failed' "$log"); hid=$(grep -c "$HARMLESS" "$log")
echo "[$name] exit $status   $((all - hid)) warning/error lines   ($hid harmless model warnings hidden)   log: $(pwd)/$log"
exit "$status"
