#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

WAIT_SEC="${WAIT_SEC:-10}"
LOG_FILE="${LOG_FILE:-/tmp/restauracja_signals.log}"

rm -f "$LOG_FILE"

make clean && make

echo "[signals] start restauracja in background"
RESTAURACJA_LOG_FILE="$LOG_FILE" RESTAURACJA_LOG_STDIO=0 RESTAURACJA_SEED=123 RESTAURACJA_CZAS_PRACY=30 ./build/bin/restauracja &
pid=$!

# Give it a moment to spawn children.
sleep 1

# Send SIGINT to parent. Parent should shutdown and forward SIGTERM to children.
echo "[signals] send SIGINT to pid=$pid"
kill -INT "$pid"

# Wait for process to exit (with a timeout loop).
end=$((SECONDS + WAIT_SEC))
while kill -0 "$pid" 2>/dev/null; do
  if (( SECONDS >= end )); then
    echo "[signals] FAIL: restauracja still running after ${WAIT_SEC}s"
    kill -KILL "$pid" 2>/dev/null || true
    exit 1
  fi
  sleep 0.1
done

# Reap it
wait "$pid" || true

if [[ ! -s "$LOG_FILE" ]]; then
  echo "[signals] FAIL: log file not created or empty: $LOG_FILE"
  exit 1
fi

echo "[signals] OK"
