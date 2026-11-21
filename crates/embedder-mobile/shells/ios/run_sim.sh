#!/usr/bin/env bash
# Attach app output to the CLI session. Stdin controls restart and shutdown.
set -euo pipefail

DEVICE="$1"
OUT="$2"
PACKAGE="$OUT/package/ios"
MODE="${3:-run}"
PLISTBUDDY="${PLISTBUDDY:-/usr/libexec/PlistBuddy}"
CONSOLE=""
BUNDLE=""
APP_NAME=""

app_pids() {
  [ -n "$APP_NAME" ] && pgrep -f "/${DEVICE}/.*/${APP_NAME}.app/${APP_NAME}([[:space:]]|$)" || true
}

app_alive() {
  [ -n "$(app_pids)" ]
}

terminate_bundle() {
  xcrun simctl terminate "$DEVICE" "$BUNDLE" >/dev/null 2>&1 &
  local term_pid=$! n=0
  while kill -0 "$term_pid" 2>/dev/null && [ "$n" -lt 50 ]; do
    n=$((n + 1))
    sleep 0.1 || true
  done
  if kill -0 "$term_pid" 2>/dev/null; then
    kill -KILL "$term_pid" 2>/dev/null || true
  fi
  wait "$term_pid" 2>/dev/null || true
}

stop_app() {
  if [ -n "$BUNDLE" ]; then
    terminate_bundle
  fi
  if [ -n "$CONSOLE" ]; then
    kill -KILL "$CONSOLE" 2>/dev/null || true
    wait "$CONSOLE" 2>/dev/null || true
    CONSOLE=""
  fi
  local pid n
  for pid in $(app_pids); do
    kill "$pid" 2>/dev/null || true
  done
  n=0
  while app_alive && [ "$n" -lt 15 ]; do
    n=$((n + 1))
    sleep 0.1 || true
  done
  for pid in $(app_pids); do
    kill -KILL "$pid" 2>/dev/null || true
  done
  n=0
  while app_alive && [ "$n" -lt 20 ]; do
    n=$((n + 1))
    sleep 0.1 || true
  done
}

on_exit() {
  trap : INT TERM HUP
  stop_app
}

trap on_exit EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP

launch() {
  local name next_bundle
  name="$(sed -n 's/^name = "\(.*\)"/\1/p' "$PACKAGE/package.toml")"
  APP_NAME="$name"
  next_bundle="$("$PLISTBUDDY" -c Print:CFBundleIdentifier "$PACKAGE/$name.app/Info.plist")"
  echo "Install $name on the simulator."
  if ! xcrun simctl install "$DEVICE" "$PACKAGE/$name.app"; then
    echo "Install fails. Fix the error and enter r to retry." >&2
    return 1
  fi
  stop_app
  BUNDLE="$next_bundle"
  echo "Launch $BUNDLE."
  (
    export SIMCTL_CHILD_SCUZZ_UI_DEBUG_DUMP="$OUT/debug.json"
    export SIMCTL_CHILD_SCUZZ_UI_RECORD="$OUT/record.json"
    export SIMCTL_CHILD_SCUZZ_UI_INJECT="$OUT/inject.json"
    if [ "$MODE" = "watch" ]; then
      export SIMCTL_CHILD_SCUZZ_UI_RELOAD_STAMP="$OUT/reload.stamp"
      export SIMCTL_CHILD_SCUZZ_UI_RELOAD_CODE="$OUT/reload.dylib"
    fi
    exec xcrun simctl launch --console --terminate-running-process "$DEVICE" "$BUNDLE"
  ) &
  CONSOLE=$!
}

launch
# A blocking read detects owner EOF on the Bash version that ships with macOS.
while IFS= read -r command; do
  case "$command" in
    restart) launch || true ;;
    quit) exit 0 ;;
  esac
done
