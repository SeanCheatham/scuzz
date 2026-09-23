#!/usr/bin/env bash
# Link Scuzz LLVM output and the shared runtime for the browser.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
IR="$(realpath "${1:?expected app LLVM file}")"
OUT="${2:?expected output directory}"
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
  case "$name" in net|net_request) continue ;; esac
  "${EMCC[@]}" "${FLAGS[@]}" -std=c11 "${INCLUDES[@]}" -c "$src" -o "$OBJ/rt_$name.o"
done
for name in sk_sw png_enc sk_gpu_none sk_mono sk_color; do
  "${EMCC[@]}" "${FLAGS[@]}" -std=c11 -I"$ROOT/crates/ffi-skia/include" \
    -I"$ROOT/crates/ffi-skia/src" -c "$ROOT/crates/ffi-skia/src/$name.c" -o "$OBJ/$name.o"
done
for name in web net_stub; do
  "${EMCC[@]}" "${FLAGS[@]}" "${INCLUDES[@]}" -c "$ROOT/crates/embedder-web/$name.c" -o "$OBJ/$name.o"
done
cp "$ROOT/crates/embedder-web/index.html" "$OUT/index.html"
"${EMCC[@]}" "${FLAGS[@]}" "$OBJ"/*.o -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8388608 \
  -sEXPORTED_RUNTIME_METHODS=ccall -sASYNCIFY=1 -sASYNCIFY_STACK_SIZE=1048576 -sENVIRONMENT=web \
  -o "$OUT/app.js"
# EM_ASM addresses live in the glue. A host can cache that glue longer than the module.
# A new module then calls an address the cached glue does not list. One content version
# loads the pair together.
python3 - "$OUT" <<'PY'
import hashlib
import pathlib
import sys
out = pathlib.Path(sys.argv[1])
js_path = out / "app.js"
wasm_path = out / "app.wasm"
html_path = out / "index.html"
js = js_path.read_text()
html = html_path.read_text()
wasm_load = 'locateFile("app.wasm")'
html_load = 'src="./app.js"'
if wasm_load not in js:
    raise SystemExit("web package: app.js does not load app.wasm")
if html_load not in html:
    raise SystemExit("web package: index.html does not load app.js")
version = hashlib.sha256(wasm_path.read_bytes() + js.encode()).hexdigest()[:16]
js_path.write_text(js.replace(wasm_load, f'locateFile("app.wasm?v={version}")', 1))
html_path.write_text(html.replace(html_load, f'src="./app.js?v={version}"', 1))
PY
