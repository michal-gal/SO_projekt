#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

WAIT_SEC="${WAIT_SEC:-10}"
LOG_FILE="${LOG_FILE:-/tmp/restauracja_no_orphans.log}"

rm -f "$LOG_FILE"

make clean && make

echo "[orphans] start restauracja"
if command -v setsid >/dev/null 2>&1; then
  setsid env RESTAURACJA_LOG_FILE="$LOG_FILE" RESTAURACJA_LOG_STDIO=0 RESTAURACJA_SEED=123 RESTAURACJA_CZAS_PRACY=30 ./restauracja &
else
  env RESTAURACJA_LOG_FILE="$LOG_FILE" RESTAURACJA_LOG_STDIO=0 RESTAURACJA_SEED=123 RESTAURACJA_CZAS_PRACY=30 ./restauracja &
fi
pid=$!

# Session ID used to scope orphan detection to *this* test run.
sid="$(ps -o sid= -p "$pid" 2>/dev/null | tr -d ' ' || true)"

sleep 1

echo "[orphans] SIGINT shutdown"
kill -INT "$pid"

end=$((SECONDS + WAIT_SEC))
while kill -0 "$pid" 2>/dev/null; do
  if (( SECONDS >= end )); then
    echo "[orphans] FAIL: restauracja still running after ${WAIT_SEC}s"
    kill -KILL "$pid" 2>/dev/null || true
    exit 1
  fi
  sleep 0.1
done

wait "$pid" || true

# Give children time to exit
sleep 0.5

# Check if any known processes are still alive.
# We avoid killing anything; we only detect.
left=0
for name in restauracja obsluga kucharz kierownik klient; do
  if [[ -n "${sid}" ]]; then
    if pgrep -s "$sid" -x "$name" >/dev/null 2>&1; then
      echo "[orphans] FAIL: process still running (sid=$sid): $name"
      pgrep -a -s "$sid" -x "$name" || true
      left=1
    fi
  elif pgrep -x "$name" >/dev/null 2>&1; then
    echo "[orphans] FAIL: process still running: $name"
    pgrep -a -x "$name" || true
    left=1
  fi
done

if [[ $left -ne 0 ]]; then
  exit 1
fi

echo "[orphans] OK"
