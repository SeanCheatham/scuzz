#!/usr/bin/env bash
# Dual-boot gate: Stage-0 → Stage-1 → Stage-2, plus a Stage-3 fixpoint.
# Each stage must smoke examples/hello + examples/adt + examples/modules, pass the Headless
# goldens (counter/todo/nav), smoke fuzz on examples/todo, smoke
# fuzz --exhaust --depth 1 on examples/counter, and agree with Stage 0 on
# fmt --check for the compiler sources. Stage 2 must re-emit byte-identical
# compiler IR.
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
  echo "$hello_out" | grep -q "Hello, Scuzz"
  echo "$hello_out" | grep -q "ready."

  echo "==> $stage runs examples/adt"
  adt_out="$("$bin" run examples/adt)"
  echo "$adt_out"
  echo "$adt_out" | grep -q "adt:red"
  echo "$adt_out" | grep -q "adt:some:42"
  echo "$adt_out" | grep -q "adt:pair:7:ok"
  echo "$adt_out" | grep -q "adt:flip:blue"
  echo "$adt_out" | grep -q "adt:describe:42"
  echo "$adt_out" | grep -q "adt:list:blue"

  echo "==> $stage runs examples/modules"
  modules_out="$("$bin" run examples/modules)"
  echo "$modules_out"
  echo "$modules_out" | grep -q "ab"

  echo "==> $stage golden tests (counter/todo/nav)"
  "$bin" test examples/counter
  "$bin" test examples/todo
  "$bin" test examples/nav

  echo "==> $stage fuzz smoke (examples/todo)"
  "$bin" fuzz --iters 4 examples/todo

  echo "==> $stage fuzz --exhaust smoke (examples/counter)"
  "$bin" fuzz --exhaust --depth 1 examples/counter

  echo "==> $stage fmt --check (compiler-scuzz sources)"
  "$bin" fmt --check compiler-scuzz
}

echo "==> Stage 0 builds Stage 1 (compiler-scuzz)"
cargo run -p scuzz -- build --full compiler-scuzz
test -x compiler-scuzz/build/scuzz

STAGE1="/tmp/stage1-scuzz"
STAGE2="/tmp/stage2-scuzz"
cp -f compiler-scuzz/build/scuzz "$STAGE1"
chmod +x "$STAGE1"

echo "==> Stage 0 fmt --check (compiler-scuzz sources)"
cargo run -p scuzz -- fmt --check compiler-scuzz

echo "==> Stage 0 rejects ill-typed program"
if cargo run -p scuzz -- build testdata/typecheck/bad_main 2>/tmp/scuzz-bad0.err; then
  echo "expected type error from Stage 0" >&2
  exit 1
fi
grep -q "arithmetic needs Int" /tmp/scuzz-bad0.err

stage_checks "Stage 1" "$STAGE1"

echo "==> Stage 1 rejects ill-typed program"
if "$STAGE1" build testdata/typecheck/bad_main 2>/tmp/scuzz-bad1.err; then
  echo "expected type error from Stage 1" >&2
  exit 1
fi
grep -q "arithmetic needs Int" /tmp/scuzz-bad1.err

echo "==> Stage 1 rebuilds compiler-scuzz (Stage 2)"
"$STAGE1" build compiler-scuzz
test -x compiler-scuzz/build/scuzz
cp -f compiler-scuzz/build/scuzz "$STAGE2"
chmod +x "$STAGE2"
cp -f compiler-scuzz/build/scuzz.ll /tmp/stage2-scuzz.ll

stage_checks "Stage 2" "$STAGE2"

echo "==> Stage 3 fixpoint: Stage 2 re-emits identical compiler IR"
"$STAGE2" build compiler-scuzz
cmp /tmp/stage2-scuzz.ll compiler-scuzz/build/scuzz.ll

echo "selfhost ok"
