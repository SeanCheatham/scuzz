#!/usr/bin/env bash
# Cross-compile a linked Android shared library from a Scuzz package.
#
#   shells/android/build_ndk.sh [project-dir]
#
# Needs the Android NDK (ANDROID_NDK_HOME, or sdk/ndk/<ver>). The app .ll is
# target-free. The same IR links against Android objects of the runtime +
# sk_sw. The app main is renamed to scuzz_app_main; JNI_OnLoad starts it.
set -euo pipefail

PROJ="$(cd "${1:-examples/counter}" && pwd)"
ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
API="${SCUZZ_ANDROID_API:-24}"
ABI="aarch64-linux-android${API}"

find_ndk() {
  if [ -n "${ANDROID_NDK_HOME:-}" ] && [ -d "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt" ]; then
    echo "$ANDROID_NDK_HOME"
    return
  fi
  local root ver
  for root in "${ANDROID_HOME:-}" "${ANDROID_SDK_ROOT:-}" \
              "${HOME}/Library/Android/sdk" "${HOME}/Android/Sdk"; do
    [ -n "$root" ] || continue
    [ -d "$root/ndk" ] || continue
    ver="$(ls -1 "$root/ndk" | sort -V | tail -1)"
    if [ -n "$ver" ] && [ -d "$root/ndk/$ver/toolchains/llvm/prebuilt" ]; then
      echo "$root/ndk/$ver"
      return
    fi
  done
  return 1
}

NDK="$(find_ndk || true)"
if [ -z "$NDK" ]; then
  echo "missing Android NDK — install the NDK, then set ANDROID_NDK_HOME" >&2
  exit 1
fi
HOST="$(ls -1 "$NDK/toolchains/llvm/prebuilt" | head -1)"
CLANG="$NDK/toolchains/llvm/prebuilt/$HOST/bin/${ABI}-clang"
if [ ! -x "$CLANG" ]; then
  echo "missing Android NDK — install the NDK, then set ANDROID_NDK_HOME" >&2
  exit 1
fi

CFLAGS=(-target "$ABI" -fPIC -O2 -Wall -Wextra)
INCLUDES=(-I"$ROOT"/crates/runtime/include -I"$ROOT"/crates/ffi-skia/include
          -I"$ROOT"/crates/embedder-desktop/include -I"$ROOT"/crates/embedder-mobile/include)

NAME="$(sed -n 's/^name = "\(.*\)"/\1/p' "$PROJ/scuzz.toml" | head -1)"
if [ -z "$NAME" ]; then
  echo "missing package name in $PROJ/scuzz.toml" >&2
  exit 1
fi

OUT="$PROJ/build/android"
SO="$OUT/lib/arm64-v8a/libscuzz.so"
rm -rf "$OUT"
mkdir -p "$OUT/obj" "$(dirname "$SO")"

if [ -z "${SCUZZ:-}" ] || [ ! -x "$SCUZZ" ]; then
  SCUZZ="$ROOT/examples/cli/build/cli"
  if [ ! -x "$SCUZZ" ]; then
    "$ROOT/scripts/bootstrap.sh"
    SCUZZ="$ROOT/examples/cli/build/cli"
  fi
fi
SCUZZ_SKIA=sk_sw "$SCUZZ" build "$PROJ"

sed 's/define i32 @main(/define i32 @scuzz_app_main(/' \
  "$PROJ/build/$NAME.ll" > "$OUT/app.android.ll"
"$CLANG" "${CFLAGS[@]}" -c "$OUT/app.android.ll" -o "$OUT/obj/app.o"

for src in "$ROOT"/crates/runtime/src/*.c; do
  obj="$OUT/obj/rt_$(basename "${src%.c}").o"
  "$CLANG" "${CFLAGS[@]}" -std=c11 "${INCLUDES[@]}" -c "$src" -o "$obj"
done

for src in "$ROOT"/crates/ffi-skia/src/sk_sw.c "$ROOT"/crates/ffi-skia/src/png_enc.c \
           "$ROOT"/crates/ffi-skia/src/sk_gpu_none.c; do
  obj="$OUT/obj/sk_$(basename "${src%.c}").o"
  "$CLANG" "${CFLAGS[@]}" -std=c11 \
    -I"$ROOT"/crates/ffi-skia/include -I"$ROOT"/crates/ffi-skia/src -c "$src" -o "$obj"
done

for src in "$ROOT"/crates/embedder-mobile/shells/android/jni/android_shell.c \
           "$ROOT"/crates/embedder-mobile/shells/android/jni/scuzz_jni.c; do
  obj="$OUT/obj/jni_$(basename "${src%.c}").o"
  "$CLANG" "${CFLAGS[@]}" -std=c11 "${INCLUDES[@]}" -c "$src" -o "$obj"
done

"$CLANG" "${CFLAGS[@]}" -shared -lm -ldl -llog -pthread \
  "$OUT"/obj/*.o -o "$SO"

echo "built $SO"
