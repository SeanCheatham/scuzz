#!/usr/bin/env bash
set -eo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
SCUZZ="${SCUZZ:-$ROOT/examples/cli/build/cli}"

fuzz() {
  local started=$SECONDS status=0
  echo "fuzz $*"
  "$SCUZZ" fuzz "$@" || status=$?
  echo "fuzz $*: $((SECONDS - started)) seconds (exit $status)"
  return "$status"
}

fuzz --iterations 16 examples/counter
fuzz --iterations 16 examples/studio
fuzz --relate examples/counter
if fuzz --relate examples/bad-sched; then
  echo "relate should have caught the schedule divergence" && exit 1
fi
if fuzz --no-fail-fast --iterations 8 examples/bad-example; then
  echo "fuzz should have found the property failure" && exit 1
fi
test -f examples/bad-example/build/fuzz/repro.toml
grep -E '^search_failures = [1-9]' examples/bad-example/build/fuzz/summary.toml
grep -E '^inert = [1-9]' examples/bad-example/build/fuzz/summary.toml
grep -E '^ran = [1-9]' examples/bad-example/build/fuzz/summary.toml
grep -E '^entries = [1-9]' examples/bad-example/build/fuzz/summary.toml
grep -E '^failures = [1-9]' examples/bad-example/build/fuzz/summary.toml
python3 - <<'PY'
import json
with open("examples/bad-example/build/fuzz/summary.json") as f:
    d = json.load(f)
assert d["v"] == 1 and d["kind"] == "fuzz"
assert d["fuzz"]["ok"] is False
assert d["fuzz"]["search_failures"] >= 1
assert d["corpus"]["failures"] >= 1
assert d["mutate"]["ran"] >= 1
PY
if fuzz --replay examples/bad-example/build/fuzz/repro.toml examples/bad-example; then
  echo "replay should have reproduced the property failure" && exit 1
fi
if fuzz --iterations 0 examples/bad-example; then
  echo "corpus-only should pin the bad-example failure" && exit 1
fi
fuzz --iterations 0 examples/kernel
grep -q 'drive addTwoThree' examples/kernel/build/seeds.txt
grep -q 'drive sumToTen' examples/kernel/build/seeds.txt
grep -q 'drive termDiff' examples/kernel/build/seeds.txt
grep -q 'termDiff e:N' examples/kernel/build/drivers.txt
fuzz --iterations 16 examples/kernel
fuzz --iterations 8 examples/scale
fuzz --iterations 8 examples/fmt
# Compiler corpus replays run in separate CI slices. Full campaigns use source copies.
# Set SCUZZ_COMPILER_FUZZ=1 for the search campaign.
compiler_campaigns() (
  campaign_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-compiler-fuzz.XXXXXX")"
  trap 'status=$?; if [ "$status" -eq 0 ]; then rm -rf "$campaign_dir"; else echo "Compiler campaign artifacts: $campaign_dir" >&2; fi' EXIT
  cp "$SCUZZ" "$campaign_dir/scuzz"
  for pkg in syntax compiler tyck codegen; do
    mkdir -p "$campaign_dir/$pkg"
    cp "examples/$pkg/scuzz.toml" "$campaign_dir/$pkg/"
    cp -R "examples/$pkg/src" "$campaign_dir/$pkg/"
    for claim in "examples/$pkg/"*.scuzz_verify; do
      if [ -f "$claim" ]; then
        cp "$claim" "$campaign_dir/$pkg/"
      fi
    done
    if [ -d "examples/$pkg/corpus" ]; then
      cp -R "examples/$pkg/corpus" "$campaign_dir/$pkg/"
    fi
  done
  "$campaign_dir/scuzz" fuzz --iterations 2 "$campaign_dir/tyck"
  "$campaign_dir/scuzz" fuzz --iterations 2 "$campaign_dir/codegen"
)
if [ "${SCUZZ_COMPILER_FUZZ:-0}" = 1 ]; then
  compiler_campaigns
fi
rm -rf /tmp/bad-seed
cp -R examples/bad-example /tmp/bad-seed
rm -rf /tmp/bad-seed/build /tmp/bad-seed/corpus
if fuzz --iterations 0 /tmp/bad-seed; then
  echo "zero-arg seed should fail bump with no stored corpus" && exit 1
fi
grep -q 'drive bump 0' /tmp/bad-seed/build/fuzz/repro.toml
if fuzz --iterations 0 examples/bad-fault; then
  echo "corpus-only should pin the bad-fault failure" && exit 1
fi
if fuzz --iterations 0 examples/bad-adt; then
  echo "corpus-only should pin the bad-adt failure" && exit 1
fi
if fuzz --iterations 0 examples/bad-sched; then
  echo "corpus-only should pin the bad-sched failure" && exit 1
fi
grep -q 'drive area Rect(' examples/bad-adt/corpus/209ce82661a8103a.toml
grep -q 'square_false' examples/bad-adt/build/fuzz/summary.toml
grep -q 'wide_false' examples/bad-adt/build/fuzz/summary.toml
if fuzz --replay examples/bad-adt/corpus/209ce82661a8103a.toml examples/bad-adt; then
  echo "replay should have reproduced the ADT property failure" && exit 1
fi
test -f examples/bad-fault/build/fuzz/repro.toml
grep -q 'fault_seed' examples/bad-fault/build/fuzz/repro.toml
grep -q 'fault_kind = "fs"' examples/bad-fault/build/fuzz/repro.toml
if fuzz --replay examples/bad-fault/corpus/f83245e1fbf633a5.toml examples/bad-fault; then
  echo "fault replay should have reproduced the failure" && exit 1
fi
grep -v -e fault_seed -e fault_kind -e fault_n -e fault_mode examples/bad-fault/corpus/f83245e1fbf633a5.toml > /tmp/bad-fault-nofault.toml
if ! fuzz --replay /tmp/bad-fault-nofault.toml examples/bad-fault; then
  echo "replay without fault_seed should pass" && exit 1
fi
grep -q 'pct_d = 2' examples/bad-sched/corpus/d037d00bc981a2fb.toml
grep -q 'pct_k = 0' examples/bad-sched/corpus/d037d00bc981a2fb.toml
grep -q 'drive checkOrder' examples/bad-sched/corpus/d037d00bc981a2fb.toml
if fuzz --replay examples/bad-sched/corpus/d037d00bc981a2fb.toml examples/bad-sched; then
  echo "schedule replay should have reproduced the failure" && exit 1
fi
grep -v -e schedule_seed -e pct_d -e pct_k examples/bad-sched/corpus/d037d00bc981a2fb.toml > /tmp/bad-sched-fifo.toml
if ! fuzz --replay /tmp/bad-sched-fifo.toml examples/bad-sched; then
  echo "FIFO replay (no schedule_seed) should pass" && exit 1
fi
if fuzz --iterations 8 examples/bad-response; then
  echo "fuzz should have found the response failure" && exit 1
fi
test -f examples/bad-response/build/fuzz/repro.toml
if fuzz --iterations 0 examples/bad-response; then
  echo "corpus-only should pin the bad-response failure" && exit 1
fi
if fuzz --iterations 0 examples/bad-split > /tmp/scuzz-bad-split.log 2>&1; then
  echo "live/verify split should fail the campaign" && exit 1
fi
cat /tmp/scuzz-bad-split.log
grep -q "fuzz live/verify split: silent observation mismatch" /tmp/scuzz-bad-split.log
rm -rf /tmp/scuzz-fuzzbug
"$SCUZZ" new --ui --path /tmp scuzz-fuzzbug
# The replacement fixture uses runtime failure checks.
rm /tmp/scuzz-fuzzbug/scuzz-fuzzbug.scuzz_verify
cat > /tmp/scuzz-fuzzbug/src/Main.scuzz <<'EOF'
@main def main: IO[Unit] =
  for {
    count = Signal.make(0)
    label = Signal.map(count, n => s"count = $n")
    _ <- Ui.run(_ => View.column(
      View.text("Bug"),
      View.bindText(label),
      View.row(View.button("boom", _ => Fs.read("/definitely/missing")))
    ))
  } yield ()
EOF
if fuzz --iterations 8 /tmp/scuzz-fuzzbug; then
  echo "fuzz should have found the failure" && exit 1
fi
test -f /tmp/scuzz-fuzzbug/build/fuzz/repro.toml
test -n "$(ls /tmp/scuzz-fuzzbug/corpus/*.toml 2>/dev/null)"
grep -E '^promoted = [1-9]' /tmp/scuzz-fuzzbug/build/fuzz/summary.toml
if fuzz --replay /tmp/scuzz-fuzzbug/build/fuzz/repro.toml /tmp/scuzz-fuzzbug; then
  echo "replay should have reproduced the failure" && exit 1
fi
cat > /tmp/scuzz-fuzzbug/src/Main.scuzz <<'EOF'
@main def main: IO[Unit] =
  for {
    count = Signal.make(0)
    label = Signal.map(count, n => s"count = $n")
    _ <- Ui.run(_ => View.column(
      View.text("Bug"),
      View.bindText(label),
      View.row(View.button("boom", _ => Signal.set(count, Signal.get(count) + 1)))
    ))
  } yield ()
EOF
if ! fuzz --iterations 0 /tmp/scuzz-fuzzbug; then
  echo "corpus-only should pass after the source fix" && exit 1
fi
grep -E '^entries = [1-9]' /tmp/scuzz-fuzzbug/build/fuzz/summary.toml
grep -E '^failures = 0' /tmp/scuzz-fuzzbug/build/fuzz/summary.toml
cat > /tmp/scuzz-fuzzbug/src/Main.scuzz <<'EOF'
@main def main: IO[Unit] =
  for {
    count = Signal.make(0)
    label = Signal.map(count, n => s"count = $n")
    _ <- Ui.run(_ => View.column(
      View.text("Bug"),
      View.bindText(label),
      View.row(View.button("boom", _ => Fs.read("/definitely/missing")))
    ))
  } yield ()
EOF
if fuzz --iterations 0 /tmp/scuzz-fuzzbug; then
  echo "corpus-only should pin the reintroduced bug" && exit 1
fi
grep -E '^entries = [1-9]' /tmp/scuzz-fuzzbug/build/fuzz/summary.toml
grep -E '^failures = [1-9]' /tmp/scuzz-fuzzbug/build/fuzz/summary.toml
grep -E '^promoted = 0' /tmp/scuzz-fuzzbug/build/fuzz/summary.toml
rm -rf /tmp/scuzz-schedbug
"$SCUZZ" new --path /tmp scuzz-schedbug
rm /tmp/scuzz-schedbug/scuzz-schedbug.scuzz_verify
cat > /tmp/scuzz-schedbug/src/Main.scuzz <<'EOF'
@main def main: IO[Unit] =
  for {
    q <- Queue.unbounded()
    _ <- IO.both(Queue.offer(q, "L"), Queue.offer(q, "R"))
    first <- Queue.take(q)
    _ <- if (Str.eq(first, "L")) IO.pure(()) else IO.fail("expected L first")
  } yield ()
EOF
"$SCUZZ" fuzz --iterations 0 /tmp/scuzz-schedbug
if fuzz --iterations 12 /tmp/scuzz-schedbug; then
  echo "schedule fuzz should have found the interleaving bug" && exit 1
fi
test -f /tmp/scuzz-schedbug/build/fuzz/repro.toml
grep -q 'schedule_seed' /tmp/scuzz-schedbug/build/fuzz/repro.toml
if fuzz --replay /tmp/scuzz-schedbug/build/fuzz/repro.toml /tmp/scuzz-schedbug; then
  echo "schedule replay should have reproduced the failure" && exit 1
fi
grep -v schedule_seed /tmp/scuzz-schedbug/build/fuzz/repro.toml > /tmp/scuzz-schedbug/build/fuzz/repro-fifo.toml
if ! fuzz --replay /tmp/scuzz-schedbug/build/fuzz/repro-fifo.toml /tmp/scuzz-schedbug; then
  echo "FIFO replay (no schedule_seed) should pass" && exit 1
fi
invalid_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-invalid-map.XXXXXX")"
mkdir -p "$invalid_dir/src"
cat > "$invalid_dir/scuzz.toml" <<'EOF'
[package]
name = "invalid-map"
EOF
cat > "$invalid_dir/src/Main.scuzz" <<'EOF'
def label(count: Signal[Int]): Signal[String] =
  Signal.map(count, n => Str.fromInt(n))

def unused(): Int =
  3

@main def main: IO[Unit] =
  IO.pure(label(Signal.make(2))).map(_ => ())
EOF
fuzz --iterations 2 "$invalid_dir"
grep -qx 'invalid = 1' "$invalid_dir/build/fuzz/summary.toml"
grep -qx 'killed = 0' "$invalid_dir/build/fuzz/summary.toml"
grep -qx 'total = 3' "$invalid_dir/build/fuzz/summary.toml"
grep -qx 'reached = 2' "$invalid_dir/build/fuzz/summary.toml"
if grep -q '^score =' "$invalid_dir/build/fuzz/summary.toml"; then
  echo "invalid mutants must not produce a score" && exit 1
fi
rm -rf "$invalid_dir"
fuzz --iterations 2 examples/io
grep -q '^\[coverage\]' examples/io/build/fuzz/summary.toml
grep -Eq '^reached = [1-9]' examples/io/build/fuzz/summary.toml
python3 - <<'PY'
import json
with open("examples/io/build/fuzz/summary.json") as f:
    d = json.load(f)
assert d["v"] == 1 and d["kind"] == "fuzz"
assert d["fuzz"]["ok"] is True
assert d["coverage"]["reached"] >= 1
assert any(r["reached"] for r in d["coverage"]["regions"])
PY
fuzz --iterations 4 examples/hello
fuzz --iterations 4 --oracles examples/counter

workload_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-workload.XXXXXX")"
mkdir -p "$workload_dir/src"
cat > "$workload_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "workload"
MANIFEST
cat > "$workload_dir/src/Main.scuzz" <<'SOURCE'
def accepts(n: Int): Bool =
  n != 3

@main def main: IO[Unit] =
  IO.pure(())
SOURCE
cat > "$workload_dir/input.scuzz_verify" <<'CLAIMS'
def input(n: Int): Bool =
  Main.accepts(n)
CLAIMS
fuzz --iterations 0 "$workload_dir"
if fuzz --seed 0 --iterations 16 "$workload_dir"; then
  echo "workload search must find an input absent from the seeds" >&2
  exit 1
fi
cp "$workload_dir/build/fuzz/repro.toml" "$workload_dir/first.toml"
grep -Fqx 'events = ["drive input 3"]' "$workload_dir/first.toml"
if fuzz --replay "$workload_dir/first.toml" "$workload_dir"; then
  echo "workload replay must preserve the failure" >&2
  exit 1
fi
rm -rf "$workload_dir/corpus"
if fuzz --seed 0 --iterations 16 "$workload_dir"; then
  echo "the same seed must find the same failure" >&2
  exit 1
fi
cmp "$workload_dir/first.toml" "$workload_dir/build/fuzz/repro.toml"
rm -rf "$workload_dir"

# A repeated campaign skips emit and link on a stamp hit.
stamp_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-stamp.XXXXXX")"
mkdir -p "$stamp_dir/src"
cat > "$stamp_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "stamp"
MANIFEST
cat > "$stamp_dir/src/Main.scuzz" <<'SOURCE'
def accepts(n: Int): Bool =
  n != 3

@main def main: IO[Unit] =
  IO.pure(())
SOURCE
cat > "$stamp_dir/input.scuzz_verify" <<'CLAIMS'
def input(n: Int): Bool =
  Main.accepts(n)
CLAIMS
fuzz --iterations 0 "$stamp_dir"
ll_before="$(stat -c %y "$stamp_dir/build/stamp.ll")"
exe_before="$(stat -c %y "$stamp_dir/build/stamp")"
live_ll_before="$(stat -c %y "$stamp_dir/build/live/stamp.ll")"
live_exe_before="$(stat -c %y "$stamp_dir/build/live/stamp")"
sleep 1
fuzz --iterations 0 "$stamp_dir"
test "$ll_before" = "$(stat -c %y "$stamp_dir/build/stamp.ll")"
test "$exe_before" = "$(stat -c %y "$stamp_dir/build/stamp")"
test "$live_ll_before" = "$(stat -c %y "$stamp_dir/build/live/stamp.ll")"
test "$live_exe_before" = "$(stat -c %y "$stamp_dir/build/live/stamp")"
rm -rf "$stamp_dir"
