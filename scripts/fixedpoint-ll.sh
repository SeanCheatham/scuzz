#!/usr/bin/env bash
# Build the product CLI with successive compilers. Require equal LLVM IR.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

export LIBRARY_PATH="/usr/lib/gcc/x86_64-linux-gnu/13${LIBRARY_PATH:+:$LIBRARY_PATH}"
SCUZZ="${SCUZZ:-$ROOT/examples/cli/build/cli}"
FP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-fixedpoint.XXXXXX")"
trap 'status=$?; if [ "$status" -eq 0 ]; then rm -rf "$FP_DIR"; else echo "Fixed-point evidence: $FP_DIR" >&2; fi' EXIT

build_stage() {
  local compiler="$1" stage="$2"
  "$compiler" build --full --out-dir "$stage" examples/cli
  "$ROOT/scripts/link_cli.sh" "$stage/cli.ll" "$stage/cli"
  "$stage/cli" | tee "$stage/run.log"
  grep -q "cli-ok" "$stage/run.log"
}

# A restored CLI artifact can need a fresh runtime archive.
make -C crates/runtime lib
build_stage "$SCUZZ" "$FP_DIR/first"
build_stage "$FP_DIR/first/cli" "$FP_DIR/second"

if cmp -s "$FP_DIR/first/cli.ll" "$FP_DIR/second/cli.ll"; then
  echo "ll-fixed-point-ok"
  exit 0
fi

# An emitter change can need one more compilation before IR is stable.
"$FP_DIR/second/cli" build --full --out-dir "$FP_DIR/third" examples/cli
if ! diff -q "$FP_DIR/second/cli.ll" "$FP_DIR/third/cli.ll" >"$FP_DIR/diff.log"; then
  echo "LLVM IR fixed-point failed: successive cli.ll files differ" >&2
  diff -u "$FP_DIR/second/cli.ll" "$FP_DIR/third/cli.ll" | head -80 || true
  exit 1
fi

echo "ll-fixed-point-ok"
