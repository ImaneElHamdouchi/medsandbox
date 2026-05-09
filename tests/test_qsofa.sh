#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
mkdir -p out logs

RESULT="out/test_qsofa_result.json"
REPORT="out/test_qsofa_report.txt"
ERROR="out/test_qsofa_error.txt"

./medsandbox --clinical \
  --stdout "$RESULT" \
  --stderr "$ERROR" \
  ./clinical_plugins/qsofa samples/patient_qsofa.fhir.json > "$REPORT"

grep -q '"valid": true' "$RESULT"
grep -q '"score": 3' "$RESULT"
grep -q '"positive_screen": true' "$RESULT"
grep -q 'Clinical mode: enabled' "$REPORT"
grep -q 'Status: exited normally' "$REPORT"

echo "PASS test_qsofa"
