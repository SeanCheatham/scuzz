#!/usr/bin/env bash
# Link Scuzz IR against the runtime archive. bootstrap.sh and
# fixedpoint-ll.sh share this one link line.
# Usage: link_cli.sh <ir> <output>
set -eo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ir="$1"
output="$2"
if [ ! -f "$ir" ]; then
  echo "compiler build did not write $ir" >&2
  exit 1
fi
platform_libs=()
if [ "$(uname -s)" = Darwin ]; then
  platform_libs+=(-framework CoreFoundation)
  if command -v brew >/dev/null 2>&1 && brew --prefix openssl@3 >/dev/null 2>&1; then
    platform_libs+=("-L$(brew --prefix openssl@3)/lib")
  else
    platform_libs+=(-L/opt/homebrew/opt/openssl@3/lib -L/usr/local/opt/openssl@3/lib)
  fi
fi
echo "==> clang -O2 $ir" >&2
clang -O2 -Wno-override-module "$ir" "$ROOT/crates/runtime/build/libscuzz_rt.a" \
  "${platform_libs[@]}" -lpthread -lssl -lcrypto -o "$output"
