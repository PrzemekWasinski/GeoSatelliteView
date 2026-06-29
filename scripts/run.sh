#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
PID_FILE="$PROJECT_ROOT/geosat.pid"
LOG_FILE="$PROJECT_ROOT/geosat.log"
BINARY="$PROJECT_ROOT/build/main"

if [ ! -f "$BINARY" ]; then
    echo "Binary not found. Run ./scripts/compile.sh first."
    exit 1
fi

if [ -f "$PID_FILE" ] && kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
    echo "Already running (PID $(cat "$PID_FILE")). Stop it first with ./scripts/stop.sh"
    exit 1
fi

cd "$PROJECT_ROOT" || exit 1
nohup "$BINARY" > "$LOG_FILE" 2>&1 &
echo $! > "$PID_FILE"
echo "Started (PID $!). Logs: $LOG_FILE"
