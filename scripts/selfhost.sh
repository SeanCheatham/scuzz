#!/usr/bin/env bash
# Dual-boot: Stage-0 → Stage-1 → Stage-2, smoke-test examples/hello + examples/adt.
# Fail loudly: every stage must succeed; no masked exit codes.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "==> Stage 0 builds Stage 1 (compiler-scalui)"
cargo run -p scalui -- build --full compiler-scalui
test -x compiler-scalui/build/scalui

STAGE1="/tmp/stage1-scalui"
STAGE2="/tmp/stage2-scalui"
cp -f compiler-scalui/build/scalui "$STAGE1"
chmod +x "$STAGE1"

echo "==> Stage 1 runs examples/hello"
HELLO1="$("$STAGE1" run examples/hello)"
echo "$HELLO1"
echo "$HELLO1" | grep -q "Hello, ScalUI"
echo "$HELLO1" | grep -q "Phase 0 online"

echo "==> Stage 1 runs examples/adt"
ADT1="$("$STAGE1" run examples/adt)"
echo "$ADT1"
echo "$ADT1" | grep -q "adt:red"

echo "==> Stage 1 rebuilds compiler-scalui (Stage 2)"
"$STAGE1" build compiler-scalui
test -x compiler-scalui/build/scalui
cp -f compiler-scalui/build/scalui "$STAGE2"
chmod +x "$STAGE2"

echo "==> Stage 2 runs examples/hello"
HELLO2="$("$STAGE2" run examples/hello)"
echo "$HELLO2"
echo "$HELLO2" | grep -q "Hello, ScalUI"
echo "$HELLO2" | grep -q "Phase 0 online"

echo "==> Stage 2 runs examples/adt"
ADT2="$("$STAGE2" run examples/adt)"
echo "$ADT2"
echo "$ADT2" | grep -q "adt:red"

echo "selfhost ok"
