#!/bin/bash
# Cook demo: the Chernobyl arena, every spawn is the canteen cook (assets/cook.glb,
# built by tools/chef/build_cook.py). Extra args pass through, e.g. --god.
cd "$(dirname "$0")"
exec ./run.sh --map maps/chernobyl.map --enemy cook "$@"
