#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

WAIT_SEC="${WAIT_SEC:-10}"
LOG_FILE="${LOG_FILE:-/tmp/restauracja_jobcontrol.log}"

rm -f "$LOG_FILE"

make clean && make

echo "[jobcontrol] start restauracja in background"
RESTAURACJA_LOG_FILE="$LOG_FILE" RESTAURACJA_LOG_STDIO=0 RESTAURACJA_SEED=123 RESTAURACJA_CZAS_PRACY=30 ./build/bin/restauracja &
pid=$!

sleep 1

echo "[jobcontrol] send SIGTSTP (stop)"
kill -TSTP "$pid"

# Give it a moment to stop
sleep 0.2

# If it didn't stop, this will fail
if ! kill -0 "$pid" 2>/dev/null; then
  echo "[jobcontrol] FAIL: restauracja is not running"
  exit 1
fi

# Resume
echo "[jobcontrol] send SIGCONT (continue)"
kill -CONT "$pid"

sleep 0.5

echo "[jobcontrol] send SIGINT (shutdown)"
kill -INT "$pid"

end=$((SECONDS + WAIT_SEC))
while kill -0 "$pid" 2>/dev/null; do
  if (( SECONDS >= end )); then
    echo "[jobcontrol] FAIL: still running after ${WAIT_SEC}s"
    kill -KILL "$pid" 2>/dev/null || true
    exit 1
  fi
  sleep 0.1
done

wait "$pid" || true

if [[ ! -s "$LOG_FILE" ]]; then
  echo "[jobcontrol] FAIL: log file not created or empty: $LOG_FILE"
  exit 1
fi

echo "[jobcontrol] OK"
