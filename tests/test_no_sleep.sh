#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

LOG_FILE="${LOG_FILE:-/tmp/restauracja_no_sleep.log}"

rm -f "$LOG_FILE"

echo "[no-sleep] build with TEST_NO_SLEEP"
make clean && make EXTRA_CFLAGS='-DTEST_NO_SLEEP'

# With TEST_NO_SLEEP the program loop no longer waits, so we must force short runtime.
# RESTAURACJA_CZAS_PRACY=1 should make it exit quickly.
echo "[no-sleep] run with RESTAURACJA_CZAS_PRACY=1"
RESTAURACJA_LOG_FILE="$LOG_FILE" RESTAURACJA_LOG_STDIO=0 RESTAURACJA_SEED=123 RESTAURACJA_CZAS_PRACY=1 ./restauracja

if [[ ! -s "$LOG_FILE" ]]; then
  echo "[no-sleep] FAIL: log file not created or empty: $LOG_FILE"
  exit 1
fi

echo "[no-sleep] OK"
