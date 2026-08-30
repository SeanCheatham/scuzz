#!/usr/bin/env bash
# Install a packaged APK when adb sees a device. No device is not a failure.
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

ADB="$(find_adb || true)"
if [ -z "$ADB" ]; then
  echo "$MISS $APK"
  exit 0
fi

n="$("$ADB" devices | awk 'NR>1 && $2=="device" {c++} END {print c+0}')"
if [ "$n" -eq 0 ]; then
  echo "$MISS $APK"
  exit 0
fi

if ! "$ADB" install -r "$APK" >/dev/null 2>&1; then
  echo "adb install failed: $APK" >&2
  exit 1
fi
echo "installed $APK"
