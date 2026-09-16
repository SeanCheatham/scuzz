#!/usr/bin/env bash
# Attach app output to the CLI session. Stdin controls restart and shutdown.
set -euo pipefail

DEVICE="$1"
OUT="$2"
PACKAGE="$OUT/package/ios"
MODE="${3:-run}"
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
    export SIMCTL_CHILD_SCUZZ_UI_DEBUG_DUMP="$OUT/debug.json"
    export SIMCTL_CHILD_SCUZZ_UI_RECORD="$OUT/record.json"
    export SIMCTL_CHILD_SCUZZ_UI_INJECT="$OUT/inject.json"
    if [ "$MODE" = "watch" ]; then
      export SIMCTL_CHILD_SCUZZ_UI_RELOAD_STAMP="$OUT/reload.stamp"
      export SIMCTL_CHILD_SCUZZ_UI_RELOAD_CODE="$OUT/reload.dylib"
    fi
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
