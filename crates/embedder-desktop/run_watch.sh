#!/usr/bin/env bash
# Stop the app when the CLI closes its input, sends quit, or the inject file contains a quit op.
set -euo pipefail

APP="${1:?missing app executable}"
WORKER=""
OWNER=""
INJECT_WATCH=""

stop() {
  if [ -n "$INJECT_WATCH" ]; then
    kill -KILL "$INJECT_WATCH" 2>/dev/null || true
    wait "$INJECT_WATCH" 2>/dev/null || true
    INJECT_WATCH=""
  fi
  if [ -n "$OWNER" ]; then
    kill -KILL "$OWNER" 2>/dev/null || true
    wait "$OWNER" 2>/dev/null || true
    OWNER=""
  fi
  if [ -n "$WORKER" ]; then
    kill "$WORKER" 2>/dev/null || true
    wait "$WORKER" 2>/dev/null || true
    WORKER=""
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
if [ -n "${SCUZZ_UI_INJECT:-}" ]; then
  (
    while true; do
      if [ -f "$SCUZZ_UI_INJECT" ] && grep -q '"op": *"quit"' "$SCUZZ_UI_INJECT"; then
        break
      fi
      sleep 0.2
    done
    kill -TERM "$$" 2>/dev/null || true
  ) &
  INJECT_WATCH=$!
fi
result=0
wait "$WORKER" || result=$?
WORKER=""
exit "$result"
