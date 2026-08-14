#!/usr/bin/env bash
# Dual-boot gate: Stage-0 → Stage-1 → Stage-2, plus a Stage-3 fixpoint.
# Each stage must smoke examples/hello + examples/adt + examples/modules +
# examples/record + examples/trait + examples/generic, pass the Headless goldens
# (counter/todo/nav), smoke fuzz on examples/todo, smoke fuzz --exhaust --depth 1
# on examples/counter, smoke IO-only fuzz on examples/concurrency, smoke mutate
# on examples/hello (no residual oracles), examples/record --limit 1 (kill), and
# examples/counter --limit 1 --iters 0 (.require kill), smoke
# examples/resource + examples/stream + examples/server, and agree with Stage 0 on fmt --check for the compiler
# sources. Stage 2 must re-emit byte-identical compiler IR.
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
  echo "$adt_out" | grep -q "adt:box:a,b"

  echo "==> $stage runs examples/modules"
  modules_out="$("$bin" run examples/modules)"
  echo "$modules_out"
  echo "$modules_out" | grep -q "ab"

  echo "==> $stage runs examples/record"
  record_out="$("$bin" run examples/record)"
  echo "$record_out"
  echo "$record_out" | grep -q "record:8"

  echo "==> $stage runs examples/trait"
  trait_out="$("$bin" run examples/trait)"
  echo "$trait_out"
  echo "$trait_out" | grep -q "Point(3,5)"
  echo "$trait_out" | grep -q "get:3"
  echo "$trait_out" | grep -q "show:box"
  echo "$trait_out" | grep -q "show:some"
  echo "$trait_out" | grep -q "get:2"

  echo "==> $stage runs examples/generic"
  generic_out="$("$bin" run examples/generic)"
  echo "$generic_out"
  echo "$generic_out" | grep -q "7ok"

  echo "==> $stage runs examples/genum"
  genum_out="$("$bin" run examples/genum)"
  echo "$genum_out"
  echo "$genum_out" | grep -q "genum:some:3"
  echo "$genum_out" | grep -q "genum:none:9"
  echo "$genum_out" | grep -q "genum:box:4"
  echo "$genum_out" | grep -q "genum:either:ok"
  echo "$genum_out" | grep -q "genum:multi:7"

  echo "==> $stage golden tests (counter/todo/nav)"
  "$bin" test examples/counter
  "$bin" test examples/todo
  "$bin" test examples/nav

  echo "==> $stage fuzz smoke (examples/todo)"
  "$bin" fuzz --iters 4 examples/todo

  echo "==> $stage fuzz --exhaust smoke (examples/counter)"
  "$bin" fuzz --exhaust --depth 1 examples/counter

  echo "==> $stage fuzz smoke (examples/concurrency, IO-only schedules)"
  "$bin" fuzz --iters 4 examples/concurrency

  echo "==> $stage mutate smoke (examples/hello, no residual oracles)"
  mutate_out="$("$bin" mutate examples/hello)"
  echo "$mutate_out"
  echo "$mutate_out" | grep -q "no residual Law.check"

  echo "==> $stage mutate kill smoke (examples/record --limit 2 --iters 0)"
  mutate_kill="$("$bin" mutate examples/record --limit 2 --iters 0)"
  echo "$mutate_kill"
  echo "$mutate_kill" | grep -q "scuzz mutate ok"
  echo "$mutate_kill" | grep -q "arith/drop"

  echo "==> $stage mutate kill smoke (examples/counter --limit 1 --iters 0)"
  mutate_ui="$("$bin" mutate examples/counter --limit 1 --iters 0)"
  echo "$mutate_ui"
  echo "$mutate_ui" | grep -q "scuzz mutate ok"

  echo "==> $stage runs examples/resource"
  resource_out="$("$bin" run examples/resource)"
  echo "$resource_out"
  echo "$resource_out" | grep -q "use:token"
  echo "$resource_out" | grep -q "release:token"
  echo "$resource_out" | grep -q "release:token2"
  echo "$resource_out" | grep -q "recovered"
  echo "$resource_out" | grep -q "timeout-fast"
  echo "$resource_out" | grep -q "got:ok"
  echo "$resource_out" | grep -q "timed-out"
  echo "$resource_out" | grep -q "release:to-tok"

  echo "==> $stage runs examples/stream"
  stream_out="$("$bin" run examples/stream)"
  echo "$stream_out"
  echo "$stream_out" | grep -q "a!,b!,c"
  echo "$stream_out" | grep -q "drain:d"
  echo "$stream_out" | grep -q "take:x,y"
  echo "$stream_out" | grep -q "drop:y,z"
  echo "$stream_out" | grep -q "filter:a,b"
  echo "$stream_out" | grep -q "map:a!,b!"
  echo "$stream_out" | grep -q "takeWhile:a,b"
  echo "$stream_out" | grep -q "dropWhile:a,b"
  echo "$stream_out" | grep -q "find:a"
  echo "$stream_out" | grep -q "exists:1"
  echo "$stream_out" | grep -q "miss:0"

  echo "==> $stage tests examples/server"
  server_out="$("$bin" test examples/server)"
  echo "$server_out"
  echo "$server_out" | grep -q "served:/"

  echo "==> $stage lsp --help"
  "$bin" lsp --help | grep -q "scuzz check"

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

echo "==> Stage 0 check rejects unformatted sources"
if cargo run -p scuzz -- check testdata/fmt/needs_format 2>/tmp/scuzz-fmt0.err; then
  echo "expected format error from Stage 0" >&2
  exit 1
fi
grep -q "needs formatting" /tmp/scuzz-fmt0.err

echo "==> Stage 0 rejects ill-typed program"
if cargo run -p scuzz -- build testdata/typecheck/bad_main 2>/tmp/scuzz-bad0.err; then
  echo "expected type error from Stage 0" >&2
  exit 1
fi
grep -q "arithmetic needs Int" /tmp/scuzz-bad0.err

echo "==> Stage 0 rejects non-exhaustive match"
if cargo run -p scuzz -- check testdata/typecheck/nonexhaustive > /tmp/scuzz-nex0.out 2>/tmp/scuzz-nex0.err; then
  echo "expected non-exhaustive match from Stage 0" >&2
  exit 1
fi
grep -q "non-exhaustive match" /tmp/scuzz-nex0.out /tmp/scuzz-nex0.err

echo "==> Stage 0 rejects unknown scuzz.toml table"
if cargo run -p scuzz -- build testdata/manifest/unknown_table 2>/tmp/scuzz-toml-table0.err; then
  echo "expected unknown table error from Stage 0" >&2
  exit 1
fi
grep -q "unknown scuzz.toml table \[plugins\]" /tmp/scuzz-toml-table0.err

echo "==> Stage 0 rejects unknown scuzz.toml key"
if cargo run -p scuzz -- build testdata/manifest/unknown_key 2>/tmp/scuzz-toml-key0.err; then
  echo "expected unknown key error from Stage 0" >&2
  exit 1
fi
grep -q "unknown scuzz.toml key \`license\` in \[package\]" /tmp/scuzz-toml-key0.err

stage_checks "Stage 1" "$STAGE1"

echo "==> Stage 1 check rejects unformatted sources"
if "$STAGE1" check testdata/fmt/needs_format > /tmp/scuzz-fmt1.out 2>/tmp/scuzz-fmt1.err; then
  echo "expected format error from Stage 1" >&2
  exit 1
fi
grep -q "needs formatting" /tmp/scuzz-fmt1.out /tmp/scuzz-fmt1.err

echo "==> Stage 1 rejects ill-typed program"
if "$STAGE1" build testdata/typecheck/bad_main 2>/tmp/scuzz-bad1.err; then
  echo "expected type error from Stage 1" >&2
  exit 1
fi
grep -q "arithmetic needs Int" /tmp/scuzz-bad1.err

echo "==> Stage 1 rejects non-exhaustive match"
if "$STAGE1" check testdata/typecheck/nonexhaustive > /tmp/scuzz-nex1.out 2>/tmp/scuzz-nex1.err; then
  echo "expected non-exhaustive match from Stage 1" >&2
  exit 1
fi
grep -q "non-exhaustive match" /tmp/scuzz-nex1.out /tmp/scuzz-nex1.err

echo "==> Stage 1 rejects unknown scuzz.toml table"
if "$STAGE1" build testdata/manifest/unknown_table 2>/tmp/scuzz-toml-table1.err; then
  echo "expected unknown table error from Stage 1" >&2
  exit 1
fi
grep -q "unknown scuzz.toml table \[plugins\]" /tmp/scuzz-toml-table1.err

echo "==> Stage 1 rejects unknown scuzz.toml key"
if "$STAGE1" build testdata/manifest/unknown_key 2>/tmp/scuzz-toml-key1.err; then
  echo "expected unknown key error from Stage 1" >&2
  exit 1
fi
grep -q "unknown scuzz.toml key \`license\` in \[package\]" /tmp/scuzz-toml-key1.err

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
