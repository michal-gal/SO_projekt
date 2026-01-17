#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

TIMEOUT_SEC="${TIMEOUT_SEC:-3}"
LOG_FILE="${LOG_FILE:-/tmp/restauracja_test.log}"

rm -f "$LOG_FILE"

echo "[smoke] build"
make clean && make

echo "[smoke] run (timeout=${TIMEOUT_SEC}s) with file logging"
set +e
RESTAURACJA_LOG_FILE="$LOG_FILE" RESTAURACJA_LOG_STDIO=0 RESTAURACJA_SEED=123 RESTAURACJA_CZAS_PRACY=3 timeout "${TIMEOUT_SEC}" ./restauracja
rc=$?
set -e

# timeout returns 124 when time limit exceeded
if [[ $rc -ne 0 && $rc -ne 124 ]]; then
  echo "[smoke] FAIL: restauracja exit code=$rc"
  exit 1
fi

if [[ ! -s "$LOG_FILE" ]]; then
  echo "[smoke] FAIL: log file not created or empty: $LOG_FILE"
  exit 1
fi

echo "[smoke] OK"
