#!/usr/bin/env bash
# Cross-compile a linked Android shared library from a Scuzz package.
#
#   shells/android/build_ndk.sh [project-dir]
#
# Needs the Android NDK (ANDROID_NDK_HOME, or sdk/ndk/<ver>). The app .ll is
# target-free. The same IR links against Android objects of the runtime +
# sk_sw. The app main is renamed to scuzz_app_main; JNI_OnLoad starts it.
# arm64-v8a is for devices. x86_64 is for the host emulator when the NDK
# clang for that triple is present.
set -euo pipefail

PROJ="$(cd "${1:-examples/counter}" && pwd)"
ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
API="${SCUZZ_ANDROID_API:-24}"

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
PRE="$NDK/toolchains/llvm/prebuilt/$HOST/bin"

NAME="$(sed -n 's/^name = "\(.*\)"/\1/p' "$PROJ/scuzz.toml" | head -1)"
if [ -z "$NAME" ]; then
  echo "missing package name in $PROJ/scuzz.toml" >&2
  exit 1
fi

OUT="$PROJ/build/android"
rm -rf "$OUT"
mkdir -p "$OUT"

if [ -z "${SCUZZ:-}" ] || [ ! -x "$SCUZZ" ]; then
  SCUZZ="$ROOT/examples/cli/build/cli"
  if [ ! -x "$SCUZZ" ]; then
    "$ROOT/scripts/bootstrap.sh"
    SCUZZ="$ROOT/examples/cli/build/cli"
  fi
fi
SCUZZ_SKIA=sk_sw "$SCUZZ" build --out-dir "$PROJ/build" "$PROJ"

sed 's/define i32 @main(/define i32 @scuzz_app_main(/' \
  "$PROJ/build/$NAME.ll" > "$OUT/app.android.ll"
if ! grep -q 'define i32 @scuzz_app_main(' "$OUT/app.android.ll"; then
  echo "missing scuzz_app_main — IR main rename failed" >&2
  exit 1
fi
# net.c needs OpenSSL. This target does not ship it. Fail if the app calls Net.
if grep -E 'call [^@]*@sz_net_' "$OUT/app.android.ll" >/dev/null; then
  echo "mobile package cannot link Net — this target has no OpenSSL" >&2
  exit 1
fi

INCLUDES=(-I"$ROOT"/crates/runtime/include -I"$ROOT"/crates/ffi-skia/include
          -I"$ROOT"/crates/embedder-desktop/include -I"$ROOT"/crates/embedder-mobile/include)

build_abi() {
  local abi="$1"
  local triple="$2"
  local clang="$PRE/${triple}-clang"
  local so="$OUT/lib/${abi}/libscuzz.so"
  local obj="$OUT/obj/${abi}"
  local src base
  if [ ! -x "$clang" ]; then
    return 1
  fi
  mkdir -p "$obj" "$(dirname "$so")"
  local cflags=(-target "$triple" -fPIC -O2 -Wall -Wextra)
  "$clang" "${cflags[@]}" -c "$OUT/app.android.ll" -o "$obj/app.o"
  for src in "$ROOT"/crates/runtime/src/*.c; do
    base="$(basename "$src")"
    # net.c needs OpenSSL. impurity.c calls sz_net_http_get.
    if [ "$base" = "net.c" ] || [ "$base" = "impurity.c" ]; then
      continue
    fi
    "$clang" "${cflags[@]}" -std=c11 "${INCLUDES[@]}" -c "$src" \
      -o "$obj/rt_$(basename "${src%.c}").o"
  done
  for src in "$ROOT"/crates/ffi-skia/src/sk_sw.c "$ROOT"/crates/ffi-skia/src/png_enc.c \
             "$ROOT"/crates/ffi-skia/src/sk_gpu_none.c "$ROOT"/crates/ffi-skia/src/sk_mono.c; do
    "$clang" "${cflags[@]}" -std=c11 \
      -I"$ROOT"/crates/ffi-skia/include -I"$ROOT"/crates/ffi-skia/src -c "$src" \
      -o "$obj/sk_$(basename "${src%.c}").o"
  done
  for src in "$ROOT"/crates/embedder-mobile/shells/android/jni/android_shell.c \
             "$ROOT"/crates/embedder-mobile/shells/android/jni/scuzz_jni.c; do
    "$clang" "${cflags[@]}" -std=c11 "${INCLUDES[@]}" -c "$src" \
      -o "$obj/jni_$(basename "${src%.c}").o"
  done
  "$clang" "${cflags[@]}" -shared -lm -ldl -llog -pthread "$obj"/*.o -o "$so"
  echo "built $so"
}

if ! build_abi arm64-v8a "aarch64-linux-android${API}"; then
  echo "missing Android NDK — install the NDK, then set ANDROID_NDK_HOME" >&2
  exit 1
fi
build_abi x86_64 "x86_64-linux-android${API}" || true
