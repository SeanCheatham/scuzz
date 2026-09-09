#!/usr/bin/env bash
set -eo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
SCUZZ="${SCUZZ:-$ROOT/examples/cli/build/cli}"

"$SCUZZ" run examples/kernel | tee /tmp/kernel.out
# Runner segvs here are flaky. On failure, rerun the exe under gdb so the log keeps a backtrace.
if [ "${PIPESTATUS[0]}" -ne 0 ]; then
  sudo apt-get update -qq && sudo apt-get install -y -qq gdb
  gdb -batch -ex run -ex bt ./examples/kernel/build/kernel 2>&1 | tail -30 || true
  exit 1
fi
"$SCUZZ" run examples/scale | tee /tmp/scale.out
grep -q "mapn:3048" /tmp/scale.out
grep -q "maps:2098176" /tmp/scale.out
grep -q "hit:0:49" /tmp/scale.out
SCUZZ=./examples/cli/build/cli
"$SCUZZ" run examples/io | tee /tmp/io.out
grep -q "ref-ok" /tmp/io.out
grep -q "queue-ok" /tmp/io.out
grep -q "deferred-ok" /tmp/io.out
grep -q "fork-ok" /tmp/io.out
grep -q "interrupted" /tmp/io.out
grep -q "repeat-ok" /tmp/io.out
grep -q "retry-ok" /tmp/io.out
grep -q "forever-stopped" /tmp/io.out
grep -q "foreach:a!,b!" /tmp/io.out
grep -q "foreachN:2" /tmp/io.out
grep -q "each:k" /tmp/io.out
grep -q "when:y" /tmp/io.out
grep -q "unless:y" /tmp/io.out
grep -q "refN:2" /tmp/io.out
grep -q "queueN:7" /tmp/io.out
grep -q "defN:8" /tmp/io.out
grep -q "forkN:9" /tmp/io.out
grep -q "fs:fs-note" /tmp/io.out
grep -q "rand:ok" /tmp/io.out
grep -q "use:token" /tmp/io.out
grep -q "release:token" /tmp/io.out
grep -q "release:token2" /tmp/io.out
grep -q "recovered" /tmp/io.out
grep -q "timeout-fast" /tmp/io.out
grep -q "got:ok" /tmp/io.out
grep -q "timed-out" /tmp/io.out
grep -q "release:to-tok" /tmp/io.out
grep -q "a!,b!,c" /tmp/io.out
grep -q "drain:d" /tmp/io.out
grep -q "filter:a,b" /tmp/io.out
grep -q "map:a!,b!" /tmp/io.out
grep -q "takeWhile:a,b" /tmp/io.out
grep -q "dropWhile:a,b" /tmp/io.out
grep -q "find:a" /tmp/io.out
grep -q "exists:1" /tmp/io.out
grep -q "miss:0" /tmp/io.out
grep -q "fs:fs-note" /tmp/io.out
grep -q "rand:ok" /tmp/io.out
grep -q "real:" /tmp/io.out
grep -q "mono:" /tmp/io.out
grep -q "kit:skip" /tmp/io.out
grep -q "fs:" /tmp/io.out
"$SCUZZ" test examples/io | tee /tmp/io-test.out
grep -q "served:POST:/ping:hi" /tmp/io-test.out
grep -q "impurity-ok" /tmp/io-test.out
grep -q "net:" /tmp/io-test.out
"$SCUZZ" check examples/hello
"$SCUZZ" check examples/kernel
"$SCUZZ" check examples/scale
./examples/jump/gen.sh
"$SCUZZ" check examples/jump
"$SCUZZ" check examples/fmt
"$SCUZZ" check examples/tyck
"$SCUZZ" check examples/codegen
"$SCUZZ" check examples/cli
"$SCUZZ" check examples/counter
"$SCUZZ" check examples/studio
"$SCUZZ" check examples/editor
"$SCUZZ" check examples/bad-example
"$SCUZZ" test examples/bad-example
"$SCUZZ" check examples/bad-fault
"$SCUZZ" test examples/bad-fault
"$SCUZZ" check examples/bad-adt
"$SCUZZ" test examples/bad-adt
"$SCUZZ" check examples/bad-sched
"$SCUZZ" test examples/bad-sched
"$SCUZZ" check examples/bad-response
"$SCUZZ" check examples/bad-split
if "$SCUZZ" check examples/bad-intent; then
  echo "empty verify should fail check" && exit 1
fi
"$SCUZZ" check --message-format=json examples/hello | tee /tmp/hello-check.json
grep -q '"severity":"info"' /tmp/hello-check.json
grep -q 'unclaimed def' /tmp/hello-check.json
"$SCUZZ" lsp --help | tee /tmp/lsp-help.txt
grep -q "scuzz check" /tmp/lsp-help.txt
if "$SCUZZ" check testdata/fmt/needs_format > /tmp/fmt-check.err 2>&1; then echo "expected format error" && exit 1; fi
grep -q "needs formatting" /tmp/fmt-check.err
"$SCUZZ" build examples/kernel
"$SCUZZ" build examples/kernel 2>&1 | tee /tmp/incr.out
if grep -q "^ok$" /tmp/incr.out; then echo "fingerprint hit should not rebuild" && exit 1; fi
test -x examples/kernel/build/kernel
# Path-dep invalidation: a change in a dependency source must rebuild the root.
"$SCUZZ" build --full examples/counter
"$SCUZZ" build examples/counter 2>&1 | tee /tmp/counter-incr.out
if grep -q "^ok$" /tmp/counter-incr.out; then echo "fingerprint hit should not rebuild" && exit 1; fi
cp examples/shared/src/Shared.scuzz /tmp/shared-orig.scuzz
printf 'def counterTitle(): String =\n  "Counter"\n\ndef countLabel(n: Int): String =\n  s"count = $n!"\n' > examples/shared/src/Shared.scuzz
"$SCUZZ" build examples/counter 2>&1 | tee /tmp/counter-inval.out
if ! grep -q "^ok$" /tmp/counter-inval.out; then echo "dependency edit should invalidate fingerprint" && exit 1; fi
cp /tmp/shared-orig.scuzz examples/shared/src/Shared.scuzz
"$SCUZZ" build --full examples/counter

