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

# pgrep reads argv. ps comm is the executable path. Match both.
app_pids() {
  [ -n "$APP_NAME" ] || return 0
  local suffix="/${APP_NAME}.app/${APP_NAME}"
  {
    pgrep -f "/${DEVICE}/.*/${APP_NAME}.app/${APP_NAME}([[:space:]]|$)" || true
    ps -axo pid=,comm= 2>/dev/null | while IFS= read -r row; do
      row="${row#"${row%%[![:space:]]*}"}"
      [ -n "$row" ] || continue
      pid="${row%% *}"
      comm="${row#"$pid"}"
      comm="${comm#"${comm%%[![:space:]]*}"}"
      case "$comm" in
        *"$suffix") printf '%s\n' "$pid" ;;
      esac
    done
  } | awk 'NF && !seen[$1]++ { print $1 }'
}

app_alive() {
  [ -n "$(app_pids)" ]
}

kill_app() {
  local pid
  for pid in $(app_pids); do
    kill -KILL "$pid" 2>/dev/null || true
  done
}

# Reap a child. Do not wait when the process does not exit.
reap_bounded() {
  local pid="$1" n=0
  while kill -0 "$pid" 2>/dev/null && [ "$n" -lt 20 ]; do
    n=$((n + 1))
    sleep 0.1 || true
  done
  if ! kill -0 "$pid" 2>/dev/null; then
    wait "$pid" 2>/dev/null || true
  fi
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
  reap_bounded "$term_pid"
}

stop_app() {
  local n
  kill_app
  if [ -n "$BUNDLE" ]; then
    terminate_bundle
  fi
  if [ -n "$CONSOLE" ]; then
    kill -KILL "$CONSOLE" 2>/dev/null || true
    reap_bounded "$CONSOLE"
    CONSOLE=""
  fi
  kill_app
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
