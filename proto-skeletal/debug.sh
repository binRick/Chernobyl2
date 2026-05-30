#!/bin/bash
# Quick debug build + run with the JSON event log tee'd to a file.
# Mirrors Chernobyl's debug.sh. Filter the log to pure JSONL with:
#   grep '^{' /tmp/skeletal_proto_debug.log | jq -c .
set -euo pipefail
cd "$(dirname "$0")"

make all
./build/skeletal_proto --debug 2>&1 | tee /tmp/skeletal_proto_debug.log
