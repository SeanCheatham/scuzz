#!/usr/bin/env bash
set -eo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
SCUZZ="${SCUZZ:-$ROOT/examples/cli/build/cli}"

"$SCUZZ" fuzz --iterations 16 examples/counter
"$SCUZZ" fuzz --iterations 16 examples/studio
"$SCUZZ" fuzz --relate examples/counter
if "$SCUZZ" fuzz --relate examples/bad-sched; then
  echo "relate should have caught the schedule divergence" && exit 1
fi
if "$SCUZZ" fuzz --no-fail-fast --iterations 8 examples/bad-example; then
  echo "fuzz should have found the property failure" && exit 1
fi
test -f examples/bad-example/build/fuzz/repro.toml
grep -E '^search_failures = [1-9]' examples/bad-example/build/fuzz/summary.toml
grep -E '^inert = [1-9]' examples/bad-example/build/fuzz/summary.toml
grep -E '^ran = [1-9]' examples/bad-example/build/fuzz/summary.toml
grep -E '^entries = [1-9]' examples/bad-example/build/fuzz/summary.toml
grep -E '^failures = [1-9]' examples/bad-example/build/fuzz/summary.toml
if "$SCUZZ" fuzz --replay examples/bad-example/build/fuzz/repro.toml examples/bad-example; then
  echo "replay should have reproduced the property failure" && exit 1
fi
if "$SCUZZ" fuzz --iterations 0 examples/bad-example; then
  echo "corpus-only should pin the bad-example failure" && exit 1
fi
"$SCUZZ" fuzz --iterations 0 examples/kernel
grep -q 'drive addTwoThree' examples/kernel/build/seeds.txt
grep -q 'drive sumToTen' examples/kernel/build/seeds.txt
grep -q 'drive termDiff' examples/kernel/build/seeds.txt
grep -q 'termDiff e:N' examples/kernel/build/drivers.txt
"$SCUZZ" fuzz --iterations 16 examples/kernel
"$SCUZZ" fuzz --iterations 8 examples/scale
"$SCUZZ" fuzz --iterations 8 examples/fmt
"$SCUZZ" fuzz --iterations 8 examples/tyck
"$SCUZZ" fuzz --iterations 8 examples/codegen
rm -rf /tmp/bad-seed
cp -R examples/bad-example /tmp/bad-seed
rm -rf /tmp/bad-seed/build /tmp/bad-seed/corpus
if "$SCUZZ" fuzz --iterations 0 /tmp/bad-seed; then
  echo "zero-arg seed should fail bump with no stored corpus" && exit 1
fi
grep -q 'drive bump 0' /tmp/bad-seed/build/fuzz/repro.toml
if "$SCUZZ" fuzz --iterations 0 examples/bad-fault; then
  echo "corpus-only should pin the bad-fault failure" && exit 1
fi
if "$SCUZZ" fuzz --iterations 0 examples/bad-adt; then
  echo "corpus-only should pin the bad-adt failure" && exit 1
fi
if "$SCUZZ" fuzz --iterations 0 examples/bad-sched; then
  echo "corpus-only should pin the bad-sched failure" && exit 1
fi
grep -q 'drive area Rect(' examples/bad-adt/corpus/209ce82661a8103a.toml
grep -q 'square_false' examples/bad-adt/build/fuzz/summary.toml
grep -q 'wide_false' examples/bad-adt/build/fuzz/summary.toml
if "$SCUZZ" fuzz --replay examples/bad-adt/corpus/209ce82661a8103a.toml examples/bad-adt; then
  echo "replay should have reproduced the ADT property failure" && exit 1
fi
test -f examples/bad-fault/build/fuzz/repro.toml
grep -q 'fault_seed' examples/bad-fault/build/fuzz/repro.toml
grep -q 'fault_kind = "fs"' examples/bad-fault/build/fuzz/repro.toml
if "$SCUZZ" fuzz --replay examples/bad-fault/corpus/f83245e1fbf633a5.toml examples/bad-fault; then
  echo "fault replay should have reproduced the failure" && exit 1
fi
grep -v -e fault_seed -e fault_kind -e fault_n -e fault_mode examples/bad-fault/corpus/f83245e1fbf633a5.toml > /tmp/bad-fault-nofault.toml
if ! "$SCUZZ" fuzz --replay /tmp/bad-fault-nofault.toml examples/bad-fault; then
  echo "replay without fault_seed should pass" && exit 1
fi
grep -q 'pct_d = 2' examples/bad-sched/corpus/d037d00bc981a2fb.toml
grep -q 'pct_k = 0' examples/bad-sched/corpus/d037d00bc981a2fb.toml
grep -q 'drive checkOrder' examples/bad-sched/corpus/d037d00bc981a2fb.toml
if "$SCUZZ" fuzz --replay examples/bad-sched/corpus/d037d00bc981a2fb.toml examples/bad-sched; then
  echo "schedule replay should have reproduced the failure" && exit 1
fi
grep -v -e schedule_seed -e pct_d -e pct_k examples/bad-sched/corpus/d037d00bc981a2fb.toml > /tmp/bad-sched-fifo.toml
if ! "$SCUZZ" fuzz --replay /tmp/bad-sched-fifo.toml examples/bad-sched; then
  echo "FIFO replay (no schedule_seed) should pass" && exit 1
fi
if "$SCUZZ" fuzz --iterations 8 examples/bad-response; then
  echo "fuzz should have found the response failure" && exit 1
fi
test -f examples/bad-response/build/fuzz/repro.toml
if "$SCUZZ" fuzz --iterations 0 examples/bad-response; then
  echo "corpus-only should pin the bad-response failure" && exit 1
fi
if "$SCUZZ" fuzz --iterations 0 examples/bad-split; then
  echo "live/verify split should fail the campaign" && exit 1
fi
rm -rf /tmp/scuzz-fuzzbug
"$SCUZZ" new --ui --path /tmp scuzz-fuzzbug
cat > /tmp/scuzz-fuzzbug/src/Main.scuzz <<'EOF'
@main def main: IO[Unit] =
  for {
    count = Signal.int(0)
    label = Signal.map(count, n => s"count = $n")
    _ <- Ui.run(_ => View.column(
      View.text("Bug"),
      View.bindText(label),
      View.row(View.button("boom", _ => Fs.read("/definitely/missing")))
    ))
  } yield ()
EOF
if "$SCUZZ" fuzz --iterations 8 /tmp/scuzz-fuzzbug; then
  echo "fuzz should have found the failure" && exit 1
fi
test -f /tmp/scuzz-fuzzbug/build/fuzz/repro.toml
test -n "$(ls /tmp/scuzz-fuzzbug/corpus/*.toml 2>/dev/null)"
grep -E '^promoted = [1-9]' /tmp/scuzz-fuzzbug/build/fuzz/summary.toml
if "$SCUZZ" fuzz --replay /tmp/scuzz-fuzzbug/build/fuzz/repro.toml /tmp/scuzz-fuzzbug; then
  echo "replay should have reproduced the failure" && exit 1
fi
cat > /tmp/scuzz-fuzzbug/src/Main.scuzz <<'EOF'
@main def main: IO[Unit] =
  for {
    count = Signal.int(0)
    label = Signal.map(count, n => s"count = $n")
    _ <- Ui.run(_ => View.column(
      View.text("Bug"),
      View.bindText(label),
      View.row(View.button("boom", _ => Signal.set(count, Signal.get(count) + 1)))
    ))
  } yield ()
EOF
if ! "$SCUZZ" fuzz --iterations 0 /tmp/scuzz-fuzzbug; then
  echo "corpus-only should pass after the source fix" && exit 1
fi
grep -E '^entries = [1-9]' /tmp/scuzz-fuzzbug/build/fuzz/summary.toml
grep -E '^failures = 0' /tmp/scuzz-fuzzbug/build/fuzz/summary.toml
cat > /tmp/scuzz-fuzzbug/src/Main.scuzz <<'EOF'
@main def main: IO[Unit] =
  for {
    count = Signal.int(0)
    label = Signal.map(count, n => s"count = $n")
    _ <- Ui.run(_ => View.column(
      View.text("Bug"),
      View.bindText(label),
      View.row(View.button("boom", _ => Fs.read("/definitely/missing")))
    ))
  } yield ()
EOF
if "$SCUZZ" fuzz --iterations 0 /tmp/scuzz-fuzzbug; then
  echo "corpus-only should pin the reintroduced bug" && exit 1
fi
grep -E '^entries = [1-9]' /tmp/scuzz-fuzzbug/build/fuzz/summary.toml
grep -E '^failures = [1-9]' /tmp/scuzz-fuzzbug/build/fuzz/summary.toml
grep -E '^promoted = 0' /tmp/scuzz-fuzzbug/build/fuzz/summary.toml
rm -rf /tmp/scuzz-schedbug
"$SCUZZ" new --path /tmp scuzz-schedbug
cat > /tmp/scuzz-schedbug/src/Main.scuzz <<'EOF'
@main def main: IO[Unit] =
  for {
    q <- Queue.unbounded()
    _ <- IO.both(Queue.offer(q, "L"), Queue.offer(q, "R"))
    first <- Queue.take(q)
    _ <- if (Str.eq(first, "L")) IO.pure(()) else IO.fail("expected L first")
  } yield ()
EOF
"$SCUZZ" test /tmp/scuzz-schedbug
if "$SCUZZ" fuzz --iterations 12 /tmp/scuzz-schedbug; then
  echo "schedule fuzz should have found the interleaving bug" && exit 1
fi
test -f /tmp/scuzz-schedbug/build/fuzz/repro.toml
grep -q 'schedule_seed' /tmp/scuzz-schedbug/build/fuzz/repro.toml
if "$SCUZZ" fuzz --replay /tmp/scuzz-schedbug/build/fuzz/repro.toml /tmp/scuzz-schedbug; then
  echo "schedule replay should have reproduced the failure" && exit 1
fi
grep -v schedule_seed /tmp/scuzz-schedbug/build/fuzz/repro.toml > /tmp/scuzz-schedbug/build/fuzz/repro-fifo.toml
if ! "$SCUZZ" fuzz --replay /tmp/scuzz-schedbug/build/fuzz/repro-fifo.toml /tmp/scuzz-schedbug; then
  echo "FIFO replay (no schedule_seed) should pass" && exit 1
fi
"$SCUZZ" fuzz --iterations 1 examples/io
"$SCUZZ" fuzz --iterations 4 examples/hello
"$SCUZZ" fuzz --iterations 4 --oracles examples/counter

