#!/usr/bin/env bash
# Build a signed iOS simulator .app from a Scuzz package.
#
#   shells/ios/build_sim.sh [project-dir]
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

NAME="$(sed -n 's/^name = "\(.*\)"/\1/p' "$PROJ/scuzz.toml" | head -1)"
if [ -z "$NAME" ]; then
  echo "missing package name in $PROJ/scuzz.toml" >&2
  exit 1
fi
BUNDLE_ID="${SCUZZ_BUNDLE_ID:-dev.scuzz.app}"

OUT="$PROJ/build/ios-sim"
APP="$OUT/$NAME.app"
rm -rf "$OUT"
mkdir -p "$OUT/obj"

# App IR (host binary is a byproduct; the .ll is what this script consumes).
SCUZZ="$ROOT/target/release/scuzz"
if [ ! -x "$SCUZZ" ]; then
  SCUZZ="$ROOT/target/debug/scuzz"
fi
if [ ! -x "$SCUZZ" ]; then
  cargo build --release -p scuzz
  SCUZZ="$ROOT/target/release/scuzz"
fi
# App IR (host binary is a byproduct; the .ll is what this script consumes).
# sk_sw keeps the host link free of the Skia prebuilt's brotli dependency.
SCUZZ_SKIA=sk_sw "$SCUZZ" build "$PROJ"

# App object. Rename main so the shell owns the process entry. The IR names
# the entry define i32 @main(...); rewrite a copy so the host .ll stays intact.
sed 's/define i32 @main(/define i32 @scuzz_app_main(/' \
  "$PROJ/build/$NAME.ll" > "$OUT/app.ios.ll"
"${CLANG[@]}" "${CFLAGS[@]}" -c "$OUT/app.ios.ll" -o "$OUT/obj/app.o"

# Runtime (C) for the sim SDK.
for src in "$ROOT"/crates/runtime/src/*.c; do
  obj="$OUT/obj/rt_$(basename "${src%.c}").o"
  "${CLANG[@]}" "${CFLAGS[@]}" -std=c11 "${INCLUDES[@]}" -c "$src" -o "$obj"
done

# sk_sw backend (CPU raster; the mobile renderer).
for src in "$ROOT"/crates/ffi-skia/src/sk_sw.c "$ROOT"/crates/ffi-skia/src/png_enc.c; do
  obj="$OUT/obj/sk_$(basename "${src%.c}").o"
  "${CLANG[@]}" "${CFLAGS[@]}" -std=c11 \
    -I"$ROOT"/crates/ffi-skia/include -I"$ROOT"/crates/ffi-skia/src -c "$src" -o "$obj"
done

# Shell (ObjC, ARC).
for src in "$ROOT"/crates/embedder-mobile/shells/ios/main.m \
           "$ROOT"/crates/embedder-mobile/shells/ios/ScuzzShell.m; do
  obj="$OUT/obj/shell_$(basename "${src%.m}").o"
  "${CLANG[@]}" "${CFLAGS[@]}" -fobjc-arc \
    -I"$ROOT"/crates/runtime/include -I"$ROOT"/crates/embedder-mobile/include \
    -c "$src" -o "$obj"
done

# Link. Strong sz_mobile_* defs in the shell override the weak runtime stubs.
mkdir -p "$APP"
"${CLANG[@]}" "${CFLAGS[@]}" -fobjc-arc \
  -framework UIKit -framework Foundation -framework CoreGraphics \
  "$OUT"/obj/*.o -o "$APP/$NAME"

# Bundle metadata.
sed -e "s/\$(EXECUTABLE_NAME)/$NAME/" -e "s/dev\.scuzz\.app/$BUNDLE_ID/" \
  "$ROOT"/crates/embedder-mobile/shells/ios/Info.plist > "$APP/Info.plist"
codesign --force --sign - --timestamp=none "$APP"

echo "built $APP"
echo "run: xcrun simctl install booted $APP"
echo "     xcrun simctl launch booted $BUNDLE_ID"
