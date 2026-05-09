#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
mkdir -p out logs

REPORT="out/test_network_block.txt"

./medsandbox --profile strict ./network_test > "$REPORT" 2>&1 || true

grep -q 'Network test started' "$REPORT"
grep -q 'Status: terminated by signal' "$REPORT"
grep -q 'Reason: forbidden syscall blocked by seccomp' "$REPORT"

echo "PASS test_network_block"
