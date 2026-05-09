#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
mkdir -p out logs

REPORT="out/test_memory_limit.txt"

./medsandbox -m 50 ./memory_hog > "$REPORT" 2>&1 || true

grep -q 'Memory hog started' "$REPORT"
grep -q 'malloc failed' "$REPORT"
grep -q 'Exit code: 1' "$REPORT"

echo "PASS test_memory_limit"
