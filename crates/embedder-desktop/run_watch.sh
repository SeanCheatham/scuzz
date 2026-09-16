#!/usr/bin/env bash
# Stop the app when the CLI closes its input or sends quit.
set -euo pipefail

APP="${1:?missing app executable}"
WORKER=""
OWNER=""

stop() {
  if [ -n "$OWNER" ]; then
    kill "$OWNER" 2>/dev/null || true
    wait "$OWNER" 2>/dev/null || true
  fi
  if [ -n "$WORKER" ]; then
    kill "$WORKER" 2>/dev/null || true
    wait "$WORKER" 2>/dev/null || true
  fi
}
trap stop EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP

exec 3<&0
"$APP" 3<&- &
WORKER=$!
(
  while IFS= read -r command <&3; do
    if [ "$command" = "quit" ]; then break; fi
  done
  kill -TERM "$$" 2>/dev/null || true
) &
OWNER=$!
result=0
wait "$WORKER" || result=$?
WORKER=""
exit "$result"
