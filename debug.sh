#!/bin/bash
# Build + run with the JSON event log tee'd to a file. Filter to pure JSONL:
#   grep '^{' /tmp/chernobyl2-debug.log | jq -c .
set -euo pipefail
cd "$(dirname "$0")"
make all
./build/chernobyl2 --debug 2>&1 | tee /tmp/chernobyl2-debug.log
