#!/usr/bin/env bash
# Pack a debug APK from libscuzz.so + MainActivity (no Gradle).
#
#   shells/android/build_apk.sh [project-dir]
#
# Needs the Android SDK (ANDROID_HOME or sdk/platforms + build-tools).
# Needs javac. Missing SDK fails with one install line.
set -euo pipefail

PROJ="$(cd "${1:-examples/counter}" && pwd)"
ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
SHELL_DIR="$(cd "$(dirname "$0")" && pwd)"
SDK_INSTALL="missing Android SDK — install the Android SDK, then set ANDROID_HOME"

find_sdk() {
  local root ver
  for root in "${ANDROID_HOME:-}" "${ANDROID_SDK_ROOT:-}" \
              "${HOME}/Library/Android/sdk" "${HOME}/Android/Sdk"; do
    [ -n "$root" ] || continue
    [ -d "$root/platforms" ] || continue
    [ -d "$root/build-tools" ] || continue
    ver="$(ls -1 "$root/build-tools" | sort -V | tail -1)"
    if [ -n "$ver" ] && [ -x "$root/build-tools/$ver/aapt2" ] \
         && [ -e "$root/build-tools/$ver/d8" ] \
         && [ -e "$root/build-tools/$ver/apksigner" ] \
         && [ -x "$root/build-tools/$ver/zipalign" ]; then
      echo "$root"
      return
    fi
  done
  return 1
}

SDK="$(find_sdk || true)"
if [ -z "$SDK" ]; then
  echo "$SDK_INSTALL" >&2
  exit 1
fi

BT_VER="$(ls -1 "$SDK/build-tools" | sort -V | tail -1)"
BT="$SDK/build-tools/$BT_VER"
PLAT="$(ls -1 "$SDK/platforms" | sort -V | tail -1)"
ANDROID_JAR="$SDK/platforms/$PLAT/android.jar"
if [ ! -f "$ANDROID_JAR" ]; then
  echo "$SDK_INSTALL" >&2
  exit 1
fi
if ! command -v javac >/dev/null 2>&1; then
  echo "missing JDK — install a JDK, then put javac on PATH" >&2
  exit 1
fi

NAME="$(sed -n 's/^name = "\(.*\)"/\1/p' "$PROJ/scuzz.toml" | head -1)"
if [ -z "$NAME" ]; then
  echo "missing package name in $PROJ/scuzz.toml" >&2
  exit 1
fi
BUNDLE_ID="${SCUZZ_BUNDLE_ID:-dev.scuzz.app}"

OUT="$PROJ/build/android"
SO="$OUT/lib/arm64-v8a/libscuzz.so"
APK="$OUT/$NAME.apk"
if [ ! -f "$SO" ]; then
  echo "missing $SO — run build_ndk.sh first" >&2
  exit 1
fi

STAGE="$OUT/apk"
rm -rf "$STAGE" "$APK"
mkdir -p "$STAGE/classes" "$STAGE/dex" "$STAGE/flat"

MANIFEST="$STAGE/AndroidManifest.xml"
sed "s/package=\"dev.scuzz.app\"/package=\"$BUNDLE_ID\"/" \
  "$SHELL_DIR/AndroidManifest.xml" > "$MANIFEST"

"$BT/aapt2" compile --dir "$SHELL_DIR/res" -o "$STAGE/flat/"
"$BT/aapt2" link \
  -o "$STAGE/base.apk" \
  -I "$ANDROID_JAR" \
  --manifest "$MANIFEST" \
          --min-sdk-version 24 \
          --target-sdk-version 35 \
          --version-code 1 \
          --version-name 0.1 \
  "$STAGE/flat/"*.flat

javac -source 8 -target 8 -bootclasspath "$ANDROID_JAR" \
  -classpath "$ANDROID_JAR" -d "$STAGE/classes" \
  "$SHELL_DIR/java/dev/scuzz/app/MainActivity.java"

"$BT/d8" --lib "$ANDROID_JAR" --min-api 24 --output "$STAGE/dex" \
  "$STAGE/classes/dev/scuzz/app/"*.class

# Keep aapt2's uncompressed resources.arsc. Target SDK 30+ rejects a
# compressed or unaligned resources table.
cp "$STAGE/base.apk" "$STAGE/unaligned.apk"
(
  cd "$STAGE/dex"
  zip -q -X "$STAGE/unaligned.apk" classes.dex
)
(
  cd "$OUT"
  zip -q -X -0 "$STAGE/unaligned.apk" lib/arm64-v8a/libscuzz.so
  if [ -f lib/x86_64/libscuzz.so ]; then
    zip -q -X -0 "$STAGE/unaligned.apk" lib/x86_64/libscuzz.so
  fi
)

"$BT/zipalign" -f -p 4 "$STAGE/unaligned.apk" "$STAGE/aligned.apk"

KS="$PROJ/build/android-debug.keystore"
if [ ! -f "$KS" ]; then
  keytool -genkeypair -keystore "$KS" -storepass android -alias androiddebugkey \
    -keypass android -keyalg RSA -keysize 2048 -validity 10000 \
    -dname "CN=Scuzz,O=Scuzz,C=US" -noprompt >/dev/null
fi

"$BT/apksigner" sign --ks "$KS" --ks-pass pass:android --key-pass pass:android \
  --out "$APK" "$STAGE/aligned.apk"

echo "built $APK"
bash "$(dirname "$0")/install_apk.sh" "$APK"
echo "run: adb shell am start -n $BUNDLE_ID/dev.scuzz.app.MainActivity"
