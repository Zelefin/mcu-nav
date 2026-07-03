#!/usr/bin/env sh
set -eu

cd "$(dirname "$0")"
PORT="${NAV_CONTROL_APP_PORT:-8765}"
URL="http://127.0.0.1:${PORT}/index.html"

python3 serve.py --port "$PORT" --host 127.0.0.1 >/tmp/nav-mcu-control-app.log 2>&1 &
SERVER_PID=$!

sleep 1
if command -v xdg-open >/dev/null 2>&1; then
  xdg-open "$URL" >/dev/null 2>&1 || true
else
  printf 'Open %s in Chrome or Edge.\n' "$URL"
fi

printf 'nav-mcu control app running at %s\n' "$URL"
printf 'Press Ctrl+C to stop.\n'
trap 'kill "$SERVER_PID" 2>/dev/null || true' INT TERM EXIT
wait "$SERVER_PID"
