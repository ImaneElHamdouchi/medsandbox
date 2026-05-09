#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
mkdir -p out logs

REPORT="out/test_file_limit.txt"

rm -f big_output.txt

./medsandbox -f 1 ./file_writer > "$REPORT" 2>&1 || true

grep -q 'File writer started' "$REPORT"
grep -q 'Status: terminated by signal' "$REPORT"
grep -q 'File size limit exceeded' "$REPORT"

echo "PASS test_file_limit"
