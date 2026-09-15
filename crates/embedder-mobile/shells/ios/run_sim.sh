#!/usr/bin/env bash
# Attach app output to the CLI session. Stdin controls restart and shutdown.
set -euo pipefail

DEVICE="$1"
OUT="$2"
PACKAGE="$OUT/package/ios"
CONSOLE=""
BUNDLE=""

stop_app() {
  if [ -n "$BUNDLE" ]; then
    xcrun simctl terminate "$DEVICE" "$BUNDLE" >/dev/null 2>&1 || true
  fi
  if [ -n "$CONSOLE" ]; then
    kill "$CONSOLE" 2>/dev/null || true
    wait "$CONSOLE" 2>/dev/null || true
    CONSOLE=""
  fi
  if [ -n "$BUNDLE" ]; then
    local data trace
    data="$(xcrun simctl get_app_container "$DEVICE" "$BUNDLE" data 2>/dev/null || true)"
    for trace in debug.json record.json; do
      if [ -f "$data/Documents/$trace" ]; then
        cp "$data/Documents/$trace" "$OUT/$trace" || true
      fi
    done
  fi
}

trap stop_app EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP

launch() {
  local name next_bundle
  name="$(sed -n 's/^name = "\(.*\)"/\1/p' "$PACKAGE/package.toml")"
  next_bundle="$(/usr/libexec/PlistBuddy -c Print:CFBundleIdentifier "$PACKAGE/$name.app/Info.plist")"
  echo "Install $name on the simulator."
  if ! xcrun simctl install "$DEVICE" "$PACKAGE/$name.app"; then
    echo "Install fails. Fix the error and enter r to retry." >&2
    return 1
  fi
  stop_app
  BUNDLE="$next_bundle"
  echo "Launch $BUNDLE."
  (
    result=0
    xcrun simctl launch --console --terminate-running-process "$DEVICE" "$BUNDLE" || result=$?
    echo "App output session ends (status $result). Enter r to restart or q to stop."
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
