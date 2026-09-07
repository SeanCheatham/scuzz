#!/usr/bin/env bash
# Prove LLVM IR fixed-point for the product CLI (`examples/cli`).
#
# Stage 2: the bootstrap compiles the current CLI. That binary is the new compiler.
# Stage 3: the new compiler emits `cli.ll` and a binary.
# Stage 4: that binary emits `cli.ll` again.
# Stage-3 and stage-4 `.ll` must match.
# Bootstrap IR may differ from stage-3 when emit changes.
set -eo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

export LIBRARY_PATH="/usr/lib/gcc/x86_64-linux-gnu/13${LIBRARY_PATH:+:$LIBRARY_PATH}"

SCUZZ="${SCUZZ:-$ROOT/examples/cli/build/cli}"
STAGE2="${SCUZZ_FP_STAGE2:-/tmp/scuzz-fp-stage2}"
STAGE3="${SCUZZ_FP_STAGE3:-/tmp/scuzz-fp-stage3}"
STAGE4="${SCUZZ_FP_STAGE4:-/tmp/scuzz-fp-stage4}"

rm -rf "$STAGE2" "$STAGE3" "$STAGE4"
mkdir -p "$STAGE2" "$STAGE3" "$STAGE4"

"$SCUZZ" run --out-dir "$STAGE2" examples/cli | tee /tmp/scuzz-fp-cli.out
grep -q "cli-ok" /tmp/scuzz-fp-cli.out
test -f "$STAGE2/cli.ll"
test -x "$STAGE2/cli"

"$STAGE2/cli" run --out-dir "$STAGE3" examples/cli | tee /tmp/scuzz-fp-cli3.out
grep -q "cli-ok" /tmp/scuzz-fp-cli3.out
test -f "$STAGE3/cli.ll"
test -x "$STAGE3/cli"

"$STAGE3/cli" build --full --out-dir "$STAGE4" examples/cli
test -f "$STAGE4/cli.ll"

if ! diff -q "$STAGE3/cli.ll" "$STAGE4/cli.ll" >/tmp/scuzz-fp-ll.diff; then
  echo "LLVM IR fixed-point failed: stage-3 and stage-4 cli.ll differ"
  diff -u "$STAGE3/cli.ll" "$STAGE4/cli.ll" | head -80 || true
  exit 1
fi

echo "ll-fixed-point-ok"
