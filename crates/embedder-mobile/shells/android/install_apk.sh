#!/usr/bin/env bash
# Install a packaged APK when adb sees a device. No device is not a failure.
# A USB serial wins over an emulator so a hardware run does not hit the emulator.
#
#   shells/android/install_apk.sh APK
set -euo pipefail

APK="${1:-}"
MISS="no Android device — connect one or start an emulator, then adb install -r"
if [ -z "$APK" ] || [ ! -f "$APK" ]; then
  echo "$MISS <apk>"
  exit 0
fi

find_adb() {
  if command -v adb >/dev/null 2>&1; then
    command -v adb
    return
  fi
  local root
  for root in "${ANDROID_HOME:-}" "${ANDROID_SDK_ROOT:-}" \
              "${HOME}/Library/Android/sdk" "${HOME}/Android/Sdk"; do
    [ -n "$root" ] || continue
    if [ -x "$root/platform-tools/adb" ]; then
      echo "$root/platform-tools/adb"
      return
    fi
  done
  return 1
}

pick_serial() {
  local s
  s="$("$ADB" devices | awk 'NR>1 && $2=="device" && $1 !~ /^emulator-/ { print $1; exit }')"
  if [ -n "$s" ]; then
    echo "$s"
    return
  fi
  "$ADB" devices | awk 'NR>1 && $2=="device" { print $1; exit }'
}

ADB="$(find_adb || true)"
if [ -z "$ADB" ]; then
  echo "$MISS $APK"
  exit 0
fi

serial="$(pick_serial || true)"
if [ -z "$serial" ]; then
  echo "$MISS $APK"
  exit 0
fi

if ! "$ADB" -s "$serial" install -r "$APK" >/dev/null 2>&1; then
  echo "adb install failed: $APK" >&2
  exit 1
fi
echo "installed $APK on $serial"
