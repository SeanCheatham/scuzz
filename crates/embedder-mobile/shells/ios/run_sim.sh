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

# Child processes of the console inherit the session pipe.
descendants() {
  local pid="$1" child
  for child in $(pgrep -P "$pid" 2>/dev/null || true); do
    descendants "$child"
    printf '%s\n' "$child"
  done
}

# Reap these pids together. Do not wait when a process does not exit.
reap_all() {
  local limit="$1" n=0 alive pid
  shift
  while [ "$n" -lt "$limit" ]; do
    alive=0
    for pid in "$@"; do
      if kill -0 "$pid" 2>/dev/null; then
        alive=1
      fi
    done
    [ "$alive" -eq 0 ] && break
    n=$((n + 1))
    sleep 0.1 || true
  done
  for pid in "$@"; do
    if ! kill -0 "$pid" 2>/dev/null; then
      wait "$pid" 2>/dev/null || true
    fi
  done
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
  reap_all 20 "$term_pid"
}

stop_console() {
  local pid pids
  [ -n "$CONSOLE" ] || return 0
  pids="$(descendants "$CONSOLE" || true)"
  # The console leads its process group. Kill that group before a slow wait.
  # A leftover child keeps the session pipe open.
  kill -KILL -- "-$CONSOLE" 2>/dev/null || true
  for pid in $pids "$CONSOLE"; do
    kill -KILL "$pid" 2>/dev/null || true
  done
  # Word splitting is the pid list. Two seconds is enough to reap a dead child.
  reap_all 20 $pids "$CONSOLE"
  CONSOLE=""
}

stop_app() {
  local n
  # Close the session pipe before simctl terminate. Terminate can take seconds.
  kill_app
  stop_console
  if [ -n "$BUNDLE" ]; then
    terminate_bundle
  fi
  kill_app
  n=0
  while app_alive && [ "$n" -lt 20 ]; do
    n=$((n + 1))
    sleep 0.1 || true
  done
}

exiting=0

on_exit() {
  trap : INT TERM HUP
  if [ "$exiting" -eq 1 ]; then
    return 0
  fi
  exiting=1
  stop_app
}

on_int() {
  trap : INT TERM HUP
  exit 130
}

on_term() {
  trap : INT TERM HUP
  exit 143
}

trap on_exit EXIT
trap on_int INT
trap on_term TERM HUP

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
    # Lead a new process group so stop can kill every child that inherits the pipe.
    exec perl -e 'setpgrp(0, 0); exec @ARGV or die $!' -- xcrun simctl launch --console --terminate-running-process "$DEVICE" "$BUNDLE"
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
