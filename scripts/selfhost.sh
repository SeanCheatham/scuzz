#!/usr/bin/env bash
# Dual-boot gate: Stage-0 → Stage-1 → Stage-2, plus a Stage-3 fixpoint.
# Each stage must smoke examples/hello + examples/adt, pass the Headless
# goldens (counter/todo/nav), and agree with Stage 0 on fmt --check for the
# compiler sources. Stage 2 must re-emit byte-identical compiler IR.
# Fail loudly: every stage must succeed; no masked exit codes.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

stage_checks() {
  stage="$1"
  bin="$2"

  echo "==> $stage runs examples/hello"
  hello_out="$("$bin" run examples/hello)"
  echo "$hello_out"
  echo "$hello_out" | grep -q "Hello, ScalUI"
  echo "$hello_out" | grep -q "ready."

  echo "==> $stage runs examples/adt"
  adt_out="$("$bin" run examples/adt)"
  echo "$adt_out"
  echo "$adt_out" | grep -q "adt:red"

  echo "==> $stage golden tests (counter/todo/nav)"
  "$bin" test examples/counter
  "$bin" test examples/todo
  "$bin" test examples/nav

  echo "==> $stage fmt --check (compiler-scalui sources)"
  "$bin" fmt --check compiler-scalui
}

echo "==> Stage 0 builds Stage 1 (compiler-scalui)"
cargo run -p scalui -- build --full compiler-scalui
test -x compiler-scalui/build/scalui

STAGE1="/tmp/stage1-scalui"
STAGE2="/tmp/stage2-scalui"
cp -f compiler-scalui/build/scalui "$STAGE1"
chmod +x "$STAGE1"

echo "==> Stage 0 fmt --check (compiler-scalui sources)"
cargo run -p scalui -- fmt --check compiler-scalui

stage_checks "Stage 1" "$STAGE1"

echo "==> Stage 1 rebuilds compiler-scalui (Stage 2)"
"$STAGE1" build compiler-scalui
test -x compiler-scalui/build/scalui
cp -f compiler-scalui/build/scalui "$STAGE2"
chmod +x "$STAGE2"
cp -f compiler-scalui/build/scalui.ll /tmp/stage2-scalui.ll

stage_checks "Stage 2" "$STAGE2"

echo "==> Stage 3 fixpoint: Stage 2 re-emits identical compiler IR"
"$STAGE2" build compiler-scalui
cmp /tmp/stage2-scalui.ll compiler-scalui/build/scalui.ll

echo "selfhost ok"
