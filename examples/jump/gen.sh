#!/usr/bin/env bash
# Write ~68k lines of kernel defs across many files for the compiler-scale jump.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$ROOT/src"
rm -rf "$SRC"
mkdir -p "$SRC"
# 227 files × 100 defs × 3 lines = 68100 lines. Names match `scuzz fmt`.
n=0
s=0
while [ "$s" -lt 227 ]; do
  stem=$(printf 'J%03d' "$s")
  {
    i=0
    while [ "$i" -lt 100 ]; do
      printf 'def _j%d(): Int =\n  %d\n\n' "$n" "$n"
      n=$((n + 1))
      i=$((i + 1))
    done
  } > "$SRC/${stem}.scuzz"
  s=$((s + 1))
done
echo "jump lines $(cat "$SRC"/*.scuzz | wc -l) files $(ls "$SRC" | wc -l)"
