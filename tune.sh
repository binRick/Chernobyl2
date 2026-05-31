#!/bin/bash
# Empty-arena mode for dialing in weapon viewmodel orientation (no enemies).
#   1/2/3   switch weapon (rifle / shotgun / minigun)
#   IJKL UO move    -/= scale    [ ] yaw    ; ' pitch
#   F5      save the ACTIVE weapon's framing (per-weapon tune file)
#   N       toggle enemies back on/off without restarting
exec "$(dirname "$0")/run.sh" --no-enemies "$@"
