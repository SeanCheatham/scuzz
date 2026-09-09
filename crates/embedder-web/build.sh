#!/usr/bin/env bash
# Link Scuzz LLVM output and the shared runtime for the browser.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
IR="$(realpath "${1:?expected app LLVM file}")"
OUT="${2:?expected output directory}"
if grep -Eq 'call [^@]*@sz_(net_|sys_(exec|spawn|alive|kill))' "$IR"; then
  echo 'web package does not support native network or process effects' >&2
  exit 1
fi
command -v python3 >/dev/null || { echo 'missing python3: install Python 3 to build for the web' >&2; exit 1; }
SDK="$(python3 "$ROOT/crates/embedder-web/sdk.py")"
# Keep SDK configuration and compiler caches inside the managed directory.
export EM_CONFIG="$SDK/.emscripten"
export EM_CACHE="$SDK/upstream/emscripten/cache"
EMCC=(python3 "$SDK/upstream/emscripten/emcc.py")
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
OBJ="$(mktemp -d)"
trap 'rm -rf "$OBJ"' EXIT
INCLUDES=(-I"$ROOT/crates/embedder-web" -I"$ROOT/crates/runtime/include" -I"$ROOT/crates/ffi-skia/include"
  -I"$ROOT/crates/embedder-desktop/include" -I"$ROOT/crates/embedder-mobile/include")
FLAGS=(-O2 -sMEMORY64=2)
"${EMCC[@]}" "${FLAGS[@]}" -Wno-override-module -c "$IR" -o "$OBJ/app.o"
for src in "$ROOT"/crates/runtime/src/*.c; do
  name="$(basename "$src" .c)"
  case "$name" in net|impurity) continue ;; esac
  "${EMCC[@]}" "${FLAGS[@]}" -std=c11 "${INCLUDES[@]}" -c "$src" -o "$OBJ/rt_$name.o"
done
for name in sk_sw png_enc sk_gpu_none sk_mono sk_color; do
  "${EMCC[@]}" "${FLAGS[@]}" -std=c11 -I"$ROOT/crates/ffi-skia/include" \
    -I"$ROOT/crates/ffi-skia/src" -c "$ROOT/crates/ffi-skia/src/$name.c" -o "$OBJ/$name.o"
done
"${EMCC[@]}" "${FLAGS[@]}" "${INCLUDES[@]}" -c "$ROOT/crates/embedder-web/web.c" -o "$OBJ/web.o"
cp "$ROOT/crates/embedder-web/index.html" "$OUT/index.html"
"${EMCC[@]}" "${FLAGS[@]}" "$OBJ"/*.o -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8388608 \
  -sEXPORTED_RUNTIME_METHODS=ccall -sASYNCIFY=1 -sASYNCIFY_STACK_SIZE=1048576 -sENVIRONMENT=web \
  -o "$OUT/app.js"
