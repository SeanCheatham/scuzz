#!/usr/bin/env bash
# Build a signed iOS simulator .app from a Scuzz package.
#
# The CLI supplies the project, build directory, name, and version.
#
# Requires Xcode (xcrun) on macOS arm64. The app .ll is target-free, so the
# same IR links against sim objects of the runtime + sk_sw. The app main is
# renamed to scuzz_app_main; the shell owns main() and UIApplicationMain.
#
# Run the result:
#   xcrun simctl install booted <out>/<name>.app
#   xcrun simctl launch booted dev.scuzz.app
set -euo pipefail

PROJ="$(cd "${1:-examples/counter}" && pwd)"
ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"

SDK="$(xcrun --sdk iphonesimulator --show-sdk-path)"
TARGET="arm64-apple-ios16.0-simulator"
CLANG=(xcrun clang)
CFLAGS=(-target "$TARGET" -isysroot "$SDK" -O2 -Wall -Wextra)
INCLUDES=(-I"$ROOT"/crates/runtime/include -I"$ROOT"/crates/ffi-skia/include
          -I"$ROOT"/crates/embedder-desktop/include -I"$ROOT"/crates/embedder-mobile/include)

NAME="${3:?missing package name}"
VERSION="${4:?missing package version}"
BUNDLE_ID="${SCUZZ_BUNDLE_ID:?missing bundle ID}"

BUILD="${2:-$PROJ/build}"
mkdir -p "$BUILD"
BUILD="$(cd "$BUILD" && pwd)"
OUT="$BUILD/ios-sim"
APP="$OUT/$NAME.app"
mkdir -p "$OUT/obj"

# SDK and compiler changes invalidate native objects.
key="$(printf '%s\n' "$TARGET" "$SDK" "$(xcrun clang --version)" "$ROOT" "$ROOT"/crates/runtime/src/*.c)"
if [ ! -f "$OUT/native-key" ] || [ "$(cat "$OUT/native-key")" != "$key" ]; then
  rm -f "$OUT"/obj/*.o
  printf '%s\n' "$key" > "$OUT/native-key"
fi
HEADERS=("$ROOT"/crates/runtime/include/*.h "$ROOT"/crates/runtime/src/*.h
         "$ROOT"/crates/ffi-skia/include/*.h "$ROOT"/crates/ffi-skia/src/*.h
         "$ROOT"/crates/embedder-desktop/include/*.h "$ROOT"/crates/embedder-mobile/include/*.h)
needs_compile() {
  local src="$1" obj="$2" header
  if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ] || [ "$0" -nt "$obj" ]; then
    return 0
  fi
  for header in "${HEADERS[@]}"; do
    if [ "$header" -nt "$obj" ]; then
      return 0
    fi
  done
  return 1
}

if [ -z "${SCUZZ:-}" ] || [ ! -x "$SCUZZ" ]; then
  SCUZZ="$ROOT/examples/cli/build/cli"
  if [ ! -x "$SCUZZ" ]; then
    "$ROOT/scripts/bootstrap.sh"
    SCUZZ="$ROOT/examples/cli/build/cli"
  fi
fi
# Emit app IR. No host binary or host renderer is required.
"$SCUZZ" build --out-dir "$BUILD" "$PROJ"

# App object. Rename main so the shell owns the process entry. The IR names
# the entry define i32 @main(...); rewrite a copy so the host .ll stays intact.
sed 's/define i32 @main(/define i32 @scuzz_app_main(/' \
  "$BUILD/$NAME.ll" > "$OUT/app.ios.ll.tmp"
if ! cmp -s "$OUT/app.ios.ll.tmp" "$OUT/app.ios.ll"; then
  mv "$OUT/app.ios.ll.tmp" "$OUT/app.ios.ll"
else
  rm "$OUT/app.ios.ll.tmp"
fi
if ! grep -q 'define i32 @scuzz_app_main(' "$OUT/app.ios.ll"; then
  echo "missing scuzz_app_main — IR main rename failed" >&2
  exit 1
fi
# net.c needs OpenSSL. This target does not ship it. Fail if the app calls Net.
if grep -E 'call [^@]*@sz_net_' "$OUT/app.ios.ll" >/dev/null; then
  echo "mobile package cannot link Net — this target has no OpenSSL" >&2
  exit 1
fi
if needs_compile "$OUT/app.ios.ll" "$OUT/obj/app.o"; then
  "${CLANG[@]}" "${CFLAGS[@]}" -c "$OUT/app.ios.ll" -o "$OUT/obj/app.o.tmp"
  mv "$OUT/obj/app.o.tmp" "$OUT/obj/app.o"
fi

# Runtime (C) for the sim SDK. Skip net.c (OpenSSL) and impurity.c (calls Net).
for src in "$ROOT"/crates/runtime/src/*.c; do
  base="$(basename "$src")"
  if [ "$base" = "net.c" ] || [ "$base" = "impurity.c" ]; then
    continue
  fi
  obj="$OUT/obj/rt_$(basename "${src%.c}").o"
  if needs_compile "$src" "$obj"; then
    "${CLANG[@]}" "${CFLAGS[@]}" -std=c11 "${INCLUDES[@]}" -c "$src" -o "$obj.tmp"
    mv "$obj.tmp" "$obj"
  fi
done

# sk_sw backend (CPU raster; the mobile renderer).
for src in "$ROOT"/crates/ffi-skia/src/sk_color.c "$ROOT"/crates/ffi-skia/src/sk_sw.c "$ROOT"/crates/ffi-skia/src/png_enc.c \
           "$ROOT"/crates/ffi-skia/src/sk_gpu_none.c "$ROOT"/crates/ffi-skia/src/sk_mono.c; do
  obj="$OUT/obj/sk_$(basename "${src%.c}").o"
  if needs_compile "$src" "$obj"; then
    "${CLANG[@]}" "${CFLAGS[@]}" -std=c11 \
      -I"$ROOT"/crates/ffi-skia/include -I"$ROOT"/crates/ffi-skia/src -c "$src" -o "$obj.tmp"
    mv "$obj.tmp" "$obj"
  fi
done

# Shell (ObjC, ARC).
for src in "$ROOT"/crates/embedder-mobile/shells/ios/main.m \
           "$ROOT"/crates/embedder-mobile/shells/ios/ScuzzShell.m; do
  obj="$OUT/obj/shell_$(basename "${src%.m}").o"
  if needs_compile "$src" "$obj"; then
    "${CLANG[@]}" "${CFLAGS[@]}" -fobjc-arc \
      -I"$ROOT"/crates/runtime/include -I"$ROOT"/crates/embedder-mobile/include \
      -c "$src" -o "$obj.tmp"
    mv "$obj.tmp" "$obj"
  fi
done

# Link. Strong sz_mobile_* defs in the shell override the weak runtime stubs.
mkdir -p "$APP"
"${CLANG[@]}" "${CFLAGS[@]}" -fobjc-arc \
  -framework UIKit -framework Foundation -framework CoreGraphics \
  "$OUT"/obj/*.o -o "$APP/$NAME"

# Bundle metadata.
cp "$ROOT/crates/embedder-mobile/shells/ios/Info.plist" "$APP/Info.plist"
plutil -replace CFBundleExecutable -string "$NAME" "$APP/Info.plist"
plutil -replace CFBundleIdentifier -string "$BUNDLE_ID" "$APP/Info.plist"
plutil -replace CFBundleDisplayName -string "$NAME" "$APP/Info.plist"
plutil -replace CFBundleName -string "$NAME" "$APP/Info.plist"
plutil -replace CFBundleShortVersionString -string "$VERSION" "$APP/Info.plist"
codesign --force --sign - --timestamp=none "$APP"

echo "built $APP"
echo "run: xcrun simctl install booted $APP"
echo "     xcrun simctl launch booted $BUNDLE_ID"
