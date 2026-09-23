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

assert_fuzz_summary() {
  python3 - "$1" "$2" <<'PY_CHECK'
import json, sys
from pathlib import Path
summary = json.loads(Path(sys.argv[1]).read_text())
lines = Path(sys.argv[2]).read_text().splitlines()
budget = summary["fuzz"]["iterations"]
search = budget * 5 // 8
assert 0 <= summary["fuzz"]["search"] <= search
assert 0 <= summary["fuzz"]["search_failures"] <= summary["fuzz"]["search"]
assert 0 <= summary["mutate"]["ran"] <= min(budget - search, summary["mutate"]["sites"])
if summary["fuzz"]["ok"]:
    assert summary["fuzz"]["search"] == search
    assert summary["mutate"]["ran"] == min(budget - search, summary["mutate"]["sites"])
coverage = summary["coverage"]
branches = coverage["branches"]
for group in (coverage, branches):
    assert group["total"] == len(group["regions"])
    assert group["reached"] == sum(row["reached"] for row in group["regions"])
expected = [f"coverage: functions {coverage['reached']}/{coverage['total']}, "
            f"branches {branches['reached']}/{branches['total']}"]
for key, action in (("sometimes", "reached"), ("triggers", "fired")):
    group = summary[key]
    expected.append(f"{key}: {len(group['reached'])}/{len(group['declared'])} {action}")
for line in expected:
    assert lines.count(line) == 1, (line, lines)
status = "ok" if summary["fuzz"]["ok"] else "fail"
assert sum(line.startswith(f"scuzz fuzz {status} (") for line in lines) == 1
assert any(line.startswith(f"scuzz fuzz {status} ({summary['fuzz']['search']} search, {summary['fuzz']['search_failures']} search failures,") for line in lines)
PY_CHECK
}

# Failed corpus entries and search iterations have separate counts.
search_counts_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-search-counts.XXXXXX")"
mkdir -p "$search_counts_dir/src" "$search_counts_dir/corpus"
cat > "$search_counts_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "search-counts"
MANIFEST
cat > "$search_counts_dir/src/Main.scuzz" <<'SOURCE'
def accepts(n: Int): Bool =
  n != 37

@main def main: IO[Unit] =
  IO.pure(())
SOURCE
cat > "$search_counts_dir/input.scuzz_verify" <<'CLAIMS'
oracle check(n: Int): Bool =
  Main.accepts(n)
CLAIMS
cat > "$search_counts_dir/corpus/rejected.toml" <<'CORPUS'
[fuzz]
events = ["drive check 37"]
CORPUS
if fuzz --iterations 8 "$search_counts_dir" > "$search_counts_dir/corpus.log" 2>&1; then
  echo "corpus failure must fail the campaign" && exit 1
fi
assert_fuzz_summary "$search_counts_dir/build/fuzz/summary.json" "$search_counts_dir/corpus.log"
cp "$search_counts_dir/build/fuzz/summary.json" "$search_counts_dir/corpus.json"
if fuzz --iterations 2 --no-fail-fast "$search_counts_dir" > "$search_counts_dir/continued.log" 2>&1; then
  echo "passing search must not hide a corpus failure" && exit 1
fi
assert_fuzz_summary "$search_counts_dir/build/fuzz/summary.json" "$search_counts_dir/continued.log"
cp "$search_counts_dir/build/fuzz/summary.json" "$search_counts_dir/continued.json"
rm "$search_counts_dir/corpus/rejected.toml"
python3 - "$search_counts_dir/src/Main.scuzz" <<'PY_CHANGE'
from pathlib import Path
import sys
p = Path(sys.argv[1])
p.write_text(p.read_text().replace("n != 37", "n == 0"))
PY_CHANGE
if fuzz --iterations 16 "$search_counts_dir" > "$search_counts_dir/search.log" 2>&1; then
  echo "search failure must fail the campaign" && exit 1
fi
assert_fuzz_summary "$search_counts_dir/build/fuzz/summary.json" "$search_counts_dir/search.log"
cp "$search_counts_dir/build/fuzz/summary.json" "$search_counts_dir/search.json"
python3 - "$search_counts_dir" <<'PY_CHECK'
import json, sys
from pathlib import Path
root = Path(sys.argv[1])
for name, ran, failures, corpus_failures in (("corpus", 0, 0, 1), ("continued", 1, 0, 1), ("search", 2, 1, 0)):
    report = json.loads((root / f"{name}.json").read_text())
    assert report["fuzz"]["ok"] is False
    assert report["fuzz"]["search"] == ran, report["fuzz"]
    assert report["fuzz"]["search_failures"] == failures
    assert report["corpus"]["failures"] == corpus_failures
PY_CHECK
rm -rf "$search_counts_dir"

fuzz --iterations 16 examples/counter | tee /tmp/scuzz-counter-summary.log
assert_fuzz_summary examples/counter/build/fuzz/summary.json /tmp/scuzz-counter-summary.log
python3 - <<'PY'
import json
with open("examples/counter/build/fuzz/summary.json") as f:
    d = json.load(f)
br = d["breadth"]
assert d["fuzz"]["ok"] is True
assert "signals" in br["varied"], br
assert "count" in br["claimed"]["signalInt"], br
assert "signals" not in br["unclaimed"], br
assert d["mutate"]["score"] >= 0.5, d["mutate"]
assert d["coverage"]["reached"] > 0
assert d["sometimes"]["reached"]
PY
fuzz --iterations 16 examples/studio | tee /tmp/scuzz-studio-summary.log
assert_fuzz_summary examples/studio/build/fuzz/summary.json /tmp/scuzz-studio-summary.log
fuzz --relate examples/counter
if fuzz --relate examples/bad-sched; then
  echo "relate should have caught the schedule divergence" && exit 1
fi
# A failing search promotes its repro into <pkg>/corpus/. Run the campaign that
# must fail on a copy so the tracked corpus stays what a human committed.
rm -rf /tmp/bad-example
cp -R examples/bad-example /tmp/bad-example
rm -rf /tmp/bad-example/build
if fuzz --no-fail-fast --iterations 8 /tmp/bad-example; then
  echo "fuzz should have found the property failure" && exit 1
fi
test -f /tmp/bad-example/build/fuzz/repro.toml
test -f /tmp/bad-example/corpus/search-42-0.toml
python3 - <<'PY'
import json
with open("/tmp/bad-example/build/fuzz/summary.json") as f:
    d = json.load(f)
assert d["v"] == 1 and d["kind"] == "fuzz"
assert d["fuzz"]["ok"] is False
assert d["fuzz"]["search_failures"] >= 1
assert d["corpus"]["entries"] >= 1
assert d["corpus"]["failures"] >= 1
assert d["mutate"]["ran"] >= 1
assert d["mutate"]["inert"] >= 1
PY
if fuzz --replay /tmp/bad-example/build/fuzz/repro.toml /tmp/bad-example; then
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
python3 - <<'PY'
import json
with open("examples/bad-adt/build/fuzz/summary.json") as f:
    d = json.load(f)
names = {c["name"]: c for c in d["classify"]}
assert names["square"]["false"] >= 1 and names["wide"]["false"] >= 1
PY
if fuzz --replay examples/bad-adt/corpus/209ce82661a8103a.toml examples/bad-adt; then
  echo "replay should have reproduced the ADT property failure" && exit 1
fi
test -f examples/bad-fault/build/fuzz/repro.toml
grep -q 'fault_seed' examples/bad-fault/build/fuzz/repro.toml
if fuzz --replay examples/bad-fault/corpus/f83245e1fbf633a5.toml examples/bad-fault; then
  echo "fault replay should have reproduced the failure" && exit 1
fi
grep -v fault_seed examples/bad-fault/corpus/f83245e1fbf633a5.toml > /tmp/bad-fault-nofault.toml
if ! fuzz --replay /tmp/bad-fault-nofault.toml examples/bad-fault; then
  echo "replay without fault_seed should pass" && exit 1
fi
grep -q 'schedule_seed = "' examples/bad-sched/corpus/d037d00bc981a2fb.toml
grep -q 'drive checkOrder' examples/bad-sched/corpus/d037d00bc981a2fb.toml
if fuzz --replay examples/bad-sched/corpus/d037d00bc981a2fb.toml examples/bad-sched; then
  echo "schedule replay should have reproduced the failure" && exit 1
fi
grep -v -e schedule_seed -e schedule_picks examples/bad-sched/corpus/d037d00bc981a2fb.toml > /tmp/bad-sched-fifo.toml
if ! fuzz --replay /tmp/bad-sched-fifo.toml examples/bad-sched; then
  echo "FIFO replay (no schedule_seed) should pass" && exit 1
fi
# A pinned schedule replays recorded picks. An unrelated edit keeps the
# failure red; a fiber-structure edit reports drift instead of going green.
rm -rf /tmp/bad-sched-picks
cp -R examples/bad-sched /tmp/bad-sched-picks
rm -rf /tmp/bad-sched-picks/build
python3 - <<'PY_EDIT'
from pathlib import Path
p = Path("/tmp/bad-sched-picks/src/Main.scuzz")
text = p.read_text()
old = 'IO.println("sched-example")'
if old not in text:
    raise SystemExit("edit fixture: println site missing")
p.write_text(text.replace(old, 'IO.println("sched example")', 1))
PY_EDIT
if fuzz --replay examples/bad-sched/corpus/d037d00bc981a2fb.toml /tmp/bad-sched-picks > /tmp/scuzz-sched-keep.log 2>&1; then
  echo "pinned schedule must stay red after an unrelated edit" && exit 1
fi
if grep -q "schedule drift" /tmp/scuzz-sched-keep.log; then
  echo "unrelated edit must not drift" && exit 1
fi
python3 - <<'PY_DRIFT'
from pathlib import Path
p = Path("/tmp/bad-sched-picks/sched.scuzz_scenario")
text = p.read_text()
old = '    _ <- IO.both(Queue.offer(q, "L"), Queue.offer(q, "R"))'
new = '    _ <- Fiber.fork(Deferred.empty().flatMap(w => Deferred.get(w))).flatMap(_ => IO.both(Queue.offer(q, "L"), Queue.offer(q, "R")))'
if old not in text:
    raise SystemExit("drift fixture: IO.both site missing")
p.write_text(text.replace(old, new, 1))
PY_DRIFT
if fuzz --replay examples/bad-sched/corpus/d037d00bc981a2fb.toml /tmp/bad-sched-picks > /tmp/scuzz-sched-drift.log 2>&1; then
  echo "drifted replay must not pass" && exit 1
fi
cat /tmp/scuzz-sched-drift.log
grep -q "schedule drift" /tmp/scuzz-sched-drift.log
rm -rf /tmp/bad-sched-picks
# A claim that guards on a renamed driver fails check. A claim antecedent
# that never fires fails a campaign with search iterations.
if "$SCUZZ" check examples/bad-driver > /tmp/scuzz-bad-driver.log 2>&1; then
  echo "renamed driveHas name should fail check" && exit 1
fi
cat /tmp/scuzz-bad-driver.log
grep -q 'Timeline.driveHas name drop is not a drive in this package' /tmp/scuzz-bad-driver.log
vacuous_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-vacuous.XXXXXX")"
mkdir -p "$vacuous_dir/src"
cat > "$vacuous_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "vacuous"
MANIFEST
cat > "$vacuous_dir/src/Main.scuzz" <<'SOURCE'
@main def main: IO[Unit] =
  IO.pure(())
SOURCE
cat > "$vacuous_dir/vac.scuzz_scenario" <<'SCENARIO'
def setup(): IO[Unit] =
  IO.pure(())

def calm(): IO[Unit] =
  IO.pure(())

def storm(): IO[Unit] =
  IO.pure(())

SCENARIO
cat > "$vacuous_dir/vac.scuzz_verify" <<'CLAIMS'
def stormWrote(t: Timeline): Verdict =
  Verdict.every(t, i => !Timeline.driveHas(t, i, "storm") || Timeline.effectHas(t, i, "Fs.write"))

CLAIMS
"$SCUZZ" check "$vacuous_dir"
if fuzz --seed 42 --iterations 2 "$vacuous_dir" > /tmp/scuzz-vacuous.log 2>&1; then
  echo "vacuous claim antecedent should fail the campaign" && exit 1
fi
cat /tmp/scuzz-vacuous.log
grep -q 'claim drive never fired: storm' /tmp/scuzz-vacuous.log
python3 - "$vacuous_dir/build/fuzz/summary.json" <<'PY_VACUOUS'
import json, sys
with open(sys.argv[1]) as f:
    d = json.load(f)
assert d["fuzz"]["ok"] is False
assert d["fuzz"]["search_failures"] == 0
assert d["claims"]["declared"] == ["storm"]
assert d["claims"]["reached"] == []
assert d["claims"]["never"] == ["storm"]
PY_VACUOUS
if ! fuzz --seed 42 --iterations 0 "$vacuous_dir" > /tmp/scuzz-vacuous-corpus.log 2>&1; then
  echo "corpus-only must report never-fired without failing" && exit 1
fi
grep -q 'claim drive never fired: storm' /tmp/scuzz-vacuous-corpus.log
rm -rf "$vacuous_dir"

# The scenario declares its fault surface. An unknown entry fails check.
fault_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-faults.XXXXXX")"
mkdir -p "$fault_dir/src"
cat > "$fault_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "faults"
MANIFEST
cat > "$fault_dir/src/Main.scuzz" <<'SOURCE'
@main def main: IO[Unit] =
  IO.pure(())
SOURCE
cat > "$fault_dir/faults.scuzz_scenario" <<'SCENARIO'
def setup(): IO[Unit] =
  IO.pure(())

def faults(): List[String] =
  ["disk"]

def calm(): IO[Unit] =
  IO.pure(())

SCENARIO
if "$SCUZZ" check "$fault_dir" > /tmp/scuzz-faults.log 2>&1; then
  echo "unknown faults entry should fail check" && exit 1
fi
cat /tmp/scuzz-faults.log
grep -q 'faults entry disk must be fs, net, or queue' /tmp/scuzz-faults.log
rm -rf "$fault_dir"

# Mutation results persist per site, keyed by compiler SHA-256. A second
# campaign reuses them. An edit re-mutates only the changed def. The default
# floor fails a surviving mutant.
gate_root="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-mutgate.XXXXXX")"
mkdir -p "$gate_root"
cp -R examples/counter "$gate_root/counter"
cp -R examples/shared "$gate_root/shared"
rm -rf "$gate_root/counter/build" "$gate_root/counter/.scuzz"
fuzz --iterations 16 "$gate_root/counter" > /tmp/scuzz-mutgate-1.log 2>&1
test -f "$gate_root/counter/.scuzz/mutate.results"
fuzz --iterations 16 "$gate_root/counter" > /tmp/scuzz-mutgate-2.log 2>&1
python3 - "$gate_root/counter/build/fuzz/summary.json" <<'PY_GATE'
import json, sys
with open(sys.argv[1]) as f:
    d = json.load(f)
assert d["fuzz"]["ok"] is True
assert d["mutate"]["ran"] >= 1
PY_GATE
GATE_DIR="$gate_root/counter" python3 - <<'PY_EDIT'
import os
from pathlib import Path
p = Path(os.environ["GATE_DIR"]) / "src" / "Main.scuzz"
text = p.read_text()
old = "Str.concat(value, env)"
assert old in text
p.write_text(text.replace(old, 'Str.concat(value, Str.concat(env, ""))', 1))
PY_EDIT
fuzz --iterations 16 "$gate_root/counter" > /tmp/scuzz-mutgate-3.log 2>&1
grep -q 'mutant 0: survived at Main.capturedLabel' /tmp/scuzz-mutgate-3.log
reused2="$(grep -c 'reused' /tmp/scuzz-mutgate-2.log)"
reused3="$(grep -c 'reused' /tmp/scuzz-mutgate-3.log)"
test "$reused2" -gt "$reused3"
rm -rf "$gate_root"
floordef_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-floordef.XXXXXX")"
mkdir -p "$floordef_dir/src"
cat > "$floordef_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "floordef"
MANIFEST
cat > "$floordef_dir/src/Main.scuzz" <<'SOURCE'
def content(n: Int): String =
  Str.fromInt(n + 1)

@main def main: IO[Unit] =
  Fs.write("out.txt", content(1))
SOURCE
if fuzz --iterations 16 "$floordef_dir" > /tmp/scuzz-floordef.log 2>&1; then
  echo "default floor must fail a surviving mutant" && exit 1
fi
cat /tmp/scuzz-floordef.log
grep -q 'mutation score 0.000 is below floor 1.000' /tmp/scuzz-floordef.log
rm -rf "$floordef_dir"

# Generation: a compound where keeps both bounds; unbounded Ints reach the
# overflow edges; strings cover the delimiter alphabet; ADT string fields
# carry delimiters through both engines.
gen_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-gen.XXXXXX")"
mkdir -p "$gen_dir/src"
cat > "$gen_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "gen"
MANIFEST
cat > "$gen_dir/src/Main.scuzz" <<'SOURCE'
def inBand(n: Int): Bool =
  n >= 10 && n <= 20

@main def main: IO[Unit] =
  IO.pure(())
SOURCE
cat > "$gen_dir/gen.scuzz_verify" <<'CLAIMS'
oracle clamped(n: Int where n >= 10 && n <= 20): Bool =
  Main.inBand(n)

CLAIMS
"$SCUZZ" check "$gen_dir"
fuzz --seed 42 --iterations 8 "$gen_dir"
grep -q 'clamped i>=10&&i<=20' "$gen_dir/build/drivers.txt"
grep -q 'drive clamped 10' "$gen_dir/build/seeds.txt"
rm -rf "$gen_dir"

edge_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-edge.XXXXXX")"
mkdir -p "$edge_dir/src"
cat > "$edge_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "edge"
MANIFEST
cat > "$edge_dir/src/Main.scuzz" <<'SOURCE'
def notMax(n: Int): Bool =
  n != 9223372036854775807

@main def main: IO[Unit] =
  IO.pure(())
SOURCE
cat > "$edge_dir/edge.scuzz_verify" <<'CLAIMS'
oracle edge(n: Int): Bool =
  Main.notMax(n)

CLAIMS
if fuzz --seed 43 --iterations 16 "$edge_dir" > /tmp/scuzz-edge.log 2>&1; then
  echo "search must reach the overflow edge" && exit 1
fi
grep -Fqx 'events = ["drive edge 9223372036854775807"]' "$edge_dir/build/fuzz/repro.toml"
if fuzz --replay "$edge_dir/build/fuzz/repro.toml" "$edge_dir"; then
  echo "overflow replay must preserve the failure" && exit 1
fi
rm -rf "$edge_dir"

str_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-genstr.XXXXXX")"
mkdir -p "$str_dir/src"
cat > "$str_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "genstr"
MANIFEST
cat > "$str_dir/src/Main.scuzz" <<'SOURCE'
enum Pair:
  case a(s: String, n: Int)

def noNewline(s: String): Bool =
  !Str.contains(s, "\n")

def firstOk(p: Pair): Bool =
  p match {
    case Pair.a(s, _n) => !Str.contains(s, ",")
  }

@main def main: IO[Unit] =
  IO.pure(())
SOURCE
cat > "$str_dir/str.scuzz_verify" <<'CLAIMS'
oracle strOk(s: String): Bool =
  Main.noNewline(s)

CLAIMS
if fuzz --seed 0 --iterations 16 "$str_dir" > /tmp/scuzz-genstr.log 2>&1; then
  echo "search must reach the newline alphabet" && exit 1
fi
grep -Fqx 'events = ["drive strOk \"\\n\""]' "$str_dir/build/fuzz/repro.toml"
if fuzz --replay "$str_dir/build/fuzz/repro.toml" "$str_dir"; then
  echo "newline replay must preserve the failure" && exit 1
fi
rm -f "$str_dir/str.scuzz_verify"
cat > "$str_dir/pair.scuzz_verify" <<'CLAIMS'
oracle pairOk(p: Pair): Bool =
  Main.firstOk(p)

CLAIMS
rm -rf "$str_dir/build" "$str_dir/corpus"
if fuzz --seed 42 --iterations 16 "$str_dir" > /tmp/scuzz-genpair.log 2>&1; then
  echo "search must reach a delimiter ADT field" && exit 1
fi
grep -Fqx 'events = ["drive pairOk a(\"a,b](x)\",9223372036854775806)"]' "$str_dir/build/fuzz/repro.toml"
if fuzz --replay "$str_dir/build/fuzz/repro.toml" "$str_dir"; then
  echo "ADT delimiter replay must preserve the failure" && exit 1
fi
rm -rf "$str_dir"
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
if ! fuzz --iterations 0 examples/bad-sometimes | tee /tmp/scuzz-sometimes-summary.log; then
  echo "corpus-only should report never-reached without failing" && exit 1
fi
assert_fuzz_summary examples/bad-sometimes/build/fuzz/summary.json /tmp/scuzz-sometimes-summary.log
python3 - <<'PY'
import json
with open("examples/bad-sometimes/build/fuzz/summary.json") as f:
    d = json.load(f)
assert d["fuzz"]["ok"] is True
assert d["fuzz"]["iterations"] == 0
assert "tappedPlus" in d["sometimes"]["declared"]
assert "tappedPlus" in d["sometimes"]["never"]
assert "button:+1" in d["triggers"]["declared"]
assert "button:+1" in d["triggers"]["never"]
assert "tappedPlus" not in d["sometimes"]["reached"]
assert "button:+1" not in d["triggers"]["reached"]
PY
if fuzz --iterations 8 examples/bad-sometimes > /tmp/scuzz-bad-sometimes.log 2>&1; then
  echo "search should fail when tappedPlus and button:+1 never fire" && exit 1
fi
cat /tmp/scuzz-bad-sometimes.log
assert_fuzz_summary examples/bad-sometimes/build/fuzz/summary.json /tmp/scuzz-bad-sometimes.log
grep -q "sometimes never reached: tappedPlus" /tmp/scuzz-bad-sometimes.log
grep -q "trigger never fired: button:+1" /tmp/scuzz-bad-sometimes.log
python3 - <<'PY'
import json
with open("examples/bad-sometimes/build/fuzz/summary.json") as f:
    d = json.load(f)
assert d["fuzz"]["ok"] is False
assert d["fuzz"]["search_failures"] == 0
assert "tappedPlus" in d["sometimes"]["never"]
assert "button:+1" in d["triggers"]["never"]
PY
chrome_root="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-chrome-breadth.XXXXXX")"
chrome_dir="$chrome_root/counter"
mkdir -p "$chrome_dir" "$chrome_root/shared"
cp -R examples/counter/. "$chrome_dir/"
cp -R examples/shared/. "$chrome_root/shared/"
rm -rf "$chrome_dir/build"
rm -f "$chrome_dir"/count.scuzz_verify
python3 - <<PY
from pathlib import Path
p = Path("$chrome_dir") / "src" / "Main.scuzz"
text = p.read_text()
old = 'View.row(View.minSize(80, 36, View.button("+1"'
new = 'View.row(View.minSize(80, 36, View.button("skip", _ => IO.pure(()))), View.minSize(80, 36, View.button("+1"'
if old not in text:
    raise SystemExit("chrome decoy: +1 button site missing")
p.write_text(text.replace(old, new, 1))
PY
cat > "$chrome_dir/chrome.scuzz_verify" <<'EOF'
def plusVisible(t: Timeline): Verdict =
  Verdict.alwaysHas(t, "button:+1")

def titleVisible(t: Timeline): Verdict =
  Verdict.alwaysHas(t, "text:Counter")
EOF
if ! fuzz --iterations 0 "$chrome_dir" > /tmp/scuzz-chrome-breadth.log 2>&1; then
  cat /tmp/scuzz-chrome-breadth.log
  echo "chrome-only corpus replay must stay green" && exit 1
fi
cat /tmp/scuzz-chrome-breadth.log
grep -Eq "varied but unclaimed:.*signals" /tmp/scuzz-chrome-breadth.log
CHROME_DIR="$chrome_dir" python3 - <<'PY'
import json, os
with open(os.environ["CHROME_DIR"] + "/build/fuzz/summary.json") as f:
    d = json.load(f)
br = d["breadth"]
assert d["fuzz"]["ok"] is True
assert "signals" in br["varied"], br
assert br["claimed"]["signalInt"] == [], br
assert "signals" in br["unclaimed"], br
PY
rm -rf "$chrome_root"
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
python3 - <<'PY'
import json
with open("/tmp/scuzz-fuzzbug/build/fuzz/summary.json") as f:
    d = json.load(f)
assert d["corpus"]["promoted"] >= 1
PY
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
python3 - <<'PY'
import json
with open("/tmp/scuzz-fuzzbug/build/fuzz/summary.json") as f:
    d = json.load(f)
assert d["corpus"]["entries"] >= 1 and d["corpus"]["failures"] == 0
PY
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
python3 - <<'PY'
import json
with open("/tmp/scuzz-fuzzbug/build/fuzz/summary.json") as f:
    d = json.load(f)
assert d["corpus"]["entries"] >= 1
assert d["corpus"]["failures"] >= 1
assert d["corpus"]["promoted"] == 0
PY
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
INVALID_DIR="$invalid_dir" python3 - <<'PY'
import json, os
with open(os.environ["INVALID_DIR"] + "/build/fuzz/summary.json") as f:
    d = json.load(f)
assert d["mutate"]["invalid"] == 1
assert d["mutate"]["killed"] == 0
assert d["coverage"]["total"] == 3
assert d["coverage"]["reached"] == 2
assert "score" not in d["mutate"], "invalid mutants must not produce a score"
PY
rm -rf "$invalid_dir"
boolean_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-boolean-claim.XXXXXX")"
mkdir -p "$boolean_dir/src"
cat > "$boolean_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "boolean-claim"
MANIFEST
cat > "$boolean_dir/src/Main.scuzz" <<'SOURCE'
@main def main: IO[Unit] =
  IO.pure(())
SOURCE
check_boolean_claim() {
  local expression="$1" expected="$2" status=0
  printf 'oracle fact(): Bool =\n  %s\n\n' "$expression" > "$boolean_dir/fact.scuzz_verify"
  rm -f "$boolean_dir/build/fuzz/summary.json"
  fuzz --iterations 0 "$boolean_dir" > /tmp/scuzz-boolean-claim.log 2>&1 || status=$?
  cat /tmp/scuzz-boolean-claim.log
  test "$status" -eq "$expected"
  assert_fuzz_summary "$boolean_dir/build/fuzz/summary.json" /tmp/scuzz-boolean-claim.log
  python3 - "$boolean_dir/build/fuzz/summary.json" "$expected" <<'PY_BOOLEAN'
import json, sys
with open(sys.argv[1]) as f:
    summary = json.load(f)
expected = int(sys.argv[2])
assert summary["fuzz"]["ok"] == (expected == 0)
assert summary["fuzz"]["search"] == 0
assert summary["corpus"]["entries"] == 1
assert summary["corpus"]["failures"] == expected
PY_BOOLEAN
}
check_boolean_claim 'false && false' 1
check_boolean_claim 'true && false' 1
check_boolean_claim 'false && true' 1
check_boolean_claim 'true && true' 0
check_boolean_claim 'false || false' 1
check_boolean_claim 'true || false' 0
check_boolean_claim 'false || true' 0
check_boolean_claim '1 != 1' 1
check_boolean_claim '1 != 2' 0
check_boolean_claim '1 > 2' 1
check_boolean_claim '1 < 2' 0
check_boolean_claim '1 == 2' 1
check_boolean_claim '1 == 1' 0
check_boolean_for() {
  local bind="$1" yield="$2" expected="$3"
  check_boolean_claim "$(printf 'for {\n    %s\n  } yield %s' "$bind" "$yield")" "$expected"
}
check_boolean_for 'x = false' 'x' 1
check_boolean_for 'x = true' 'x' 0
check_boolean_for 'pair = (7, "seven")' 'pair._1 == 7 && pair._2 == "seven"' 0
rm -rf "$boolean_dir"

match_require_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-match-require.XXXXXX")"
mkdir -p "$match_require_dir/src" "$match_require_dir/corpus"
cat > "$match_require_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "match-require"
MANIFEST
cat > "$match_require_dir/world.scuzz_scenario" <<'SCENARIO'
def setup(): IO[Unit] = IO.pure(())
def run(n: Int): IO[Int] = Main.checked(n)
SCENARIO
cat > "$match_require_dir/corpus/payload.toml" <<'CORPUS'
[fuzz]
events = ["drive run 7", "drive run -1"]
CORPUS
check_match_require() {
  local comparison="$1" expected="$2" status=0
  cat > "$match_require_dir/src/Main.scuzz" <<SOURCE
enum Packet:
  case Value(n: Int)

def checked(n: Int): IO[Int] =
  Packet.Value(n) match {
    case Packet.Value(7) => IO.pure(7).require("literal payload", result => result $comparison 7)
    case Packet.Value(value) => IO.pure(value).require("matched payload", result => result $comparison value)
  }

@main def main: IO[Unit] =
  IO.pure(())
SOURCE
  fuzz --iterations 0 "$match_require_dir" > /tmp/scuzz-match-require.log 2>&1 || status=$?
  cat /tmp/scuzz-match-require.log
  test "$status" -eq "$expected"
  assert_fuzz_summary "$match_require_dir/build/fuzz/summary.json" /tmp/scuzz-match-require.log
  python3 - "$match_require_dir/build/fuzz/summary.json" "$expected" <<'PY_MATCH'
import json, sys
with open(sys.argv[1]) as f:
    summary = json.load(f)
expected = int(sys.argv[2])
assert summary["fuzz"]["ok"] == (expected == 0)
assert summary["corpus"]["failures"] == expected
PY_MATCH
  if [ "$expected" -ne 0 ]; then
    if fuzz --replay "$match_require_dir/build/fuzz/repro.toml" "$match_require_dir" > /tmp/scuzz-match-require-replay.log 2>&1; then
      echo "false payload assertion must fail replay" && exit 1
    fi
    grep -q 'fuzz replay reproduced a failure' /tmp/scuzz-match-require-replay.log
  fi
}
check_match_require '==' 0
check_match_require '!=' 1
rm -rf "$match_require_dir"

# Both engines write the same summary. The default run is the evaluator; it
# must not fall back to compiled probes. SCUZZ_FUZZ_ENGINE=compiled is the
# control.
fuzz_both_engines() {
  local dir="$1" iterations="$2" name="$3"
  fuzz --iterations "$iterations" "$dir" | tee "/tmp/scuzz-$name-summary.log"
  if grep -q 'probes run compiled' "/tmp/scuzz-$name-summary.log"; then
    echo "$dir: the evaluator engine fell back to compiled probes" && exit 1
  fi
  cp "$dir/build/fuzz/summary.json" "/tmp/scuzz-$name-ev.json"
  SCUZZ_FUZZ_ENGINE=compiled fuzz --iterations "$iterations" "$dir" | tee "/tmp/scuzz-$name-compiled.log"
  if ! diff "/tmp/scuzz-$name-ev.json" "$dir/build/fuzz/summary.json"; then
    echo "$dir: evaluator and compiled summaries differ" && exit 1
  fi
}

fuzz_both_engines examples/webhook 160 webhook
assert_fuzz_summary examples/webhook/build/fuzz/summary.json /tmp/scuzz-webhook-summary.log
python3 - <<'PY_WEBHOOK'
import json
with open("examples/webhook/build/fuzz/summary.json") as f:
    webhook = json.load(f)
assert webhook["mutate"]["ran"] == webhook["mutate"]["sites"]
assert webhook["mutate"]["survived"] == 0
assert webhook["mutate"]["invalid"] == 0
assert "webhook.json" in webhook["breadth"]["claimed"]["fileSame"]
assert {"concurrent", "continued"} <= set(webhook["breadth"]["claimed"]["driveHas"])
assert {"alpha.status", "beta.status", "forged.status", "final.status"} <= set(webhook["breadth"]["claimed"]["fileTextIs"])
with open("examples/webhook/build/drivers.txt") as f:
    drivers = [line.split()[0] for line in f if line.strip()]
assert "faulted" not in drivers and "rejected" not in drivers
PY_WEBHOOK

fuzz_both_engines examples/api-report 320 api-report
assert_fuzz_summary examples/api-report/build/fuzz/summary.json /tmp/scuzz-api-report-summary.log
python3 - <<'PY_CHECK'
import json
with open("examples/api-report/build/fuzz/summary.json") as f:
    report = json.load(f)
assert report["breadth"]["claimed"]["fileSame"] == ["report.json"]
assert "queryPages" in report["breadth"]["claimed"]["driveHas"]
assert "files" not in report["breadth"]["unclaimed"]
assert report["mutate"]["ran"] == report["mutate"]["sites"]
assert report["mutate"]["survived"] == 0
assert report["mutate"]["invalid"] == 0
PY_CHECK

# One success drive on the host loopback TLS client. Clock and files stay simulated.
if ! fuzz --live --replay examples/api-report/live.toml examples/api-report | tee /tmp/scuzz-api-report-live.log | grep -q "fuzz replay ok"; then
  echo "examples/api-report: live loopback replay failed" && exit 1
fi

# URLSession certificate checks and TLS handshake errors on host loopback.
if ! fuzz --session --iterations 0 examples/transport | tee /tmp/scuzz-transport-session.log | grep -q "fuzz ok"; then
  echo "examples/transport: session replay failed" && exit 1
fi

# File comparisons judge recorded contents at both states.
file_compare_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-file-compare.XXXXXX")"
mkdir -p "$file_compare_dir/src" "$file_compare_dir/corpus"
cat > "$file_compare_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "file-compare"
MANIFEST
cat > "$file_compare_dir/src/Main.scuzz" <<'SOURCE'
@main def main: IO[Unit] =
  IO.pure(())
SOURCE
cat > "$file_compare_dir/files.scuzz_scenario" <<'SCENARIO'
def setup(): IO[Unit] =
  Fs.write("report.txt", "before")

def keep(): IO[Unit] =
  IO.pure(())

def change(): IO[Unit] =
  Fs.write("report.txt", "after!")
SCENARIO
cat > "$file_compare_dir/files.scuzz_verify" <<'CLAIMS'
private def unchanged(t: Timeline, a: Int, b: Int): Bool =
  Timeline.fileSame(t, a, b, "report.txt")

def preserved(t: Timeline): Verdict =
  Verdict.stepEvery(t, pair => unchanged(t, pair._1, pair._2))
CLAIMS
cat > "$file_compare_dir/corpus/files.toml" <<'CORPUS'
[fuzz]
events = ["drive keep"]
CORPUS
fuzz --iterations 0 "$file_compare_dir"
cat > "$file_compare_dir/corpus/files.toml" <<'CORPUS'
[fuzz]
events = ["drive change"]
CORPUS
if fuzz --iterations 0 "$file_compare_dir" > /tmp/scuzz-file-compare.log 2>&1; then
  echo "file comparison must reject changed contents" >&2
  exit 1
fi
FILE_COMPARE_DIR="$file_compare_dir" python3 - <<'PY_CHECK'
import json, os
with open(os.environ["FILE_COMPARE_DIR"] + "/build/fuzz/summary.json") as f:
    comparison = json.load(f)
assert comparison["fuzz"]["ok"] is False
assert comparison["corpus"]["failures"] == 1
assert comparison["breadth"]["claimed"]["fileSame"] == ["report.txt"]
PY_CHECK
rm -rf "$file_compare_dir"

# A storm faults every later call. A partial write keeps the previous file.
fault_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-fault-plan.XXXXXX")"
mkdir -p "$fault_dir/src" "$fault_dir/corpus"
cat > "$fault_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "fault-plan"
MANIFEST
cat > "$fault_dir/src/Main.scuzz" <<'SOURCE'
@main def main: IO[Unit] =
  IO.pure(())
SOURCE
cat > "$fault_dir/files.scuzz_scenario" <<'SCENARIO'
def setup(): IO[Unit] =
  Fs.write("report.txt", "before")

def faults(): List[String] =
  ["fs"]

def replaceOnce(): IO[Unit] =
  Fs.write("report.txt", "after-complete").handleErrorWith(_ => IO.pure(()))

def replaceTwice(): IO[Unit] =
  Fs.write("report.txt", "after-complete").handleErrorWith(_ => Fs.write("report.txt", "second-write").handleErrorWith(_ => IO.pure(())))
SCENARIO
cat > "$fault_dir/files.scuzz_verify" <<'CLAIMS'
private def okTwice(t: Timeline, i: Int): Bool =
  !Timeline.driveHas(t, i, "replaceTwice") || Timeline.fileTextIs(t, i, "report.txt", "before")

private def okOnce(t: Timeline, i: Int): Bool =
  !Timeline.driveHas(t, i, "replaceOnce") || (Timeline.effectHas(t, i, "Fs.partial") && Timeline.fileTextIs(t, i, "report.txt", "before"))

def whole(t: Timeline): Verdict =
  Verdict.stepEvery(t, pair => okTwice(t, pair._2) && okOnce(t, pair._2))
CLAIMS
cat > "$fault_dir/corpus/storm.toml" <<'CORPUS'
[fuzz]
fault_seed = "145"
events = ["drive replaceTwice"]
CORPUS
cat > "$fault_dir/corpus/partial.toml" <<'CORPUS'
[fuzz]
fault_seed = "289"
events = ["drive replaceOnce"]
CORPUS
fuzz --iterations 0 "$fault_dir"

# Search reaches a partial-write plan. That plan fails a claim that rejects it.
partial_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-fault-partial.XXXXXX")"
mkdir -p "$partial_dir/src" "$partial_dir/corpus"
cat > "$partial_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "fault-partial"
[fuzz]
score_floor = 0
MANIFEST
cat > "$partial_dir/src/Main.scuzz" <<'SOURCE'
@main def main: IO[Unit] =
  IO.pure(())
SOURCE
cat > "$partial_dir/files.scuzz_scenario" <<'SCENARIO'
def setup(): IO[Unit] =
  Fs.write("report.txt", "before")

def faults(): List[String] =
  ["fs"]

def replaceOnce(): IO[Unit] =
  Fs.write("report.txt", "after-complete").handleErrorWith(_ => IO.pure(()))
SCENARIO
cat > "$partial_dir/files.scuzz_verify" <<'CLAIMS'
def noPartial(t: Timeline): Verdict =
  Verdict.stepEvery(t, pair => !Timeline.effectHas(t, pair._2, "Fs.partial"))
CLAIMS
cat > "$partial_dir/corpus/one.toml" <<'CORPUS'
[fuzz]
fault_seed = "1"
events = ["drive replaceOnce"]
CORPUS
if fuzz --iterations 64 "$partial_dir" > /tmp/scuzz-fault-partial.log 2>&1; then
  echo "search must reach a partial write" >&2
  exit 1
fi
grep -q 'fault_seed = "289"' "$partial_dir/build/fuzz/repro.toml"
rm -rf "$fault_dir" "$partial_dir"
fuzz_both_engines examples/io 16 io
python3 - <<'PY'
import json
with open("examples/io/build/fuzz/summary.json") as f:
    d = json.load(f)
assert d["v"] == 1 and d["kind"] == "fuzz"
assert d["fuzz"]["ok"] is True
assert d["coverage"]["reached"] >= 1
assert any(r["reached"] for r in d["coverage"]["regions"])
PY
# Corpus replay on the compiled engine under AddressSanitizer. Leaks fail.
if ! fuzz --asan --iterations 0 examples/io | tee /tmp/scuzz-io-asan.log; then
  echo "examples/io: ASan corpus replay failed" && exit 1
fi
if grep -E 'ERROR: (Address|Leak)Sanitizer' /tmp/scuzz-io-asan.log; then
  echo "examples/io: ASan reported a defect" && exit 1
fi
fuzz --iterations 4 examples/hello
grep -q 'drive greetFact' examples/hello/build/seeds.txt
# Distance feedback: the evaluator search climbs to `code == 4242` and
# reaches the sometimes; the compiled control has no feedback and does not.
fuzz --iterations 32 examples/reach | tee /tmp/scuzz-reach-summary.log
grep -q '^sometimes: 1/1 reached' /tmp/scuzz-reach-summary.log
if grep -q 'probes run compiled' /tmp/scuzz-reach-summary.log; then
  echo "examples/reach: the evaluator engine fell back to compiled probes" && exit 1
fi
if SCUZZ_FUZZ_ENGINE=compiled fuzz --iterations 32 examples/reach | tee /tmp/scuzz-reach-compiled.log; then
  echo "examples/reach: the compiled control reached the magic value without feedback" && exit 1
fi
grep -q '^sometimes: 0/1 reached' /tmp/scuzz-reach-compiled.log
fuzz --iterations 4 --oracles examples/counter
python3 - <<'PY'
import json
with open("examples/counter/build/fuzz/summary.json") as f:
    d = json.load(f)
assert d["fuzz"]["ok"] is True
assert d["mutate"]["oracles"] is True
assert d["mutate"]["score"] >= 0.5, d["mutate"]
PY
floor_root="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-floor-fail.XXXXXX")"
floor_dir="$floor_root/counter"
mkdir -p "$floor_dir" "$floor_root/shared"
cp -R examples/counter/. "$floor_dir/"
cp -R examples/shared/. "$floor_root/shared/"
rm -rf "$floor_dir/build"
python3 - <<PY
from pathlib import Path
p = Path("$floor_dir") / "scuzz.toml"
text = p.read_text()
old = "score_floor = 0.500"
new = "score_floor = 1.001"
if old not in text:
    raise SystemExit("floor-fail: score_floor 0.500 missing")
p.write_text(text.replace(old, new, 1))
PY
if fuzz --iterations 16 "$floor_dir" > /tmp/scuzz-floor-fail.log 2>&1; then
  cat /tmp/scuzz-floor-fail.log
  echo "score_floor 1.001 must fail live-code mutation on counter" && exit 1
fi
cat /tmp/scuzz-floor-fail.log
grep -q "is below floor 1.001" /tmp/scuzz-floor-fail.log
grep -q "scuzz fuzz fail (" /tmp/scuzz-floor-fail.log
if grep -q "scuzz fuzz ok" /tmp/scuzz-floor-fail.log; then
  echo "floor-fail must not print scuzz fuzz ok" && exit 1
fi
FLOOR_DIR="$floor_dir" python3 - <<'PY'
import json, os
with open(os.environ["FLOOR_DIR"] + "/build/fuzz/summary.json") as f:
    d = json.load(f)
assert d["fuzz"]["ok"] is False, d["fuzz"]
assert d["fuzz"]["search_failures"] == 0, d["fuzz"]
PY
rm -rf "$floor_root"

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
oracle input(n: Int): Bool =
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

# A failing search stores the minimal script: shrink drops unneeded script
# lines to a fixpoint, then moves Int arguments toward the where bound or zero.
shrink_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-shrink.XXXXXX")"
mkdir -p "$shrink_dir/src" "$shrink_dir/corpus"
cat > "$shrink_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "shrink"
MANIFEST
cat > "$shrink_dir/src/Main.scuzz" <<'SOURCE'
@main def main: IO[Unit] =
  IO.pure(())
SOURCE
cat > "$shrink_dir/shrink.scuzz_scenario" <<'SCENARIO'
def setup(): IO[Unit] =
  IO.pure(())

def calm(): IO[Unit] =
  IO.pure(())

def trip(n: Int): IO[Unit] =
  if (n >= 10) IO.fail("tripped") else IO.pure(())
SCENARIO
cat > "$shrink_dir/corpus/seed.toml" <<'CORPUS'
[fuzz]
events = ["drive calm", "drive calm", "drive calm"]
CORPUS
if fuzz --seed 42 --iterations 16 "$shrink_dir"; then
  echo "shrink search must find the boundary failure" >&2
  exit 1
fi
grep -Fqx 'events = ["drive trip 10"]' "$shrink_dir/build/fuzz/repro.toml"
grep -Fqx 'events = ["drive trip 10"]' "$shrink_dir"/corpus/search-*.toml
if fuzz --replay "$shrink_dir/build/fuzz/repro.toml" "$shrink_dir"; then
  echo "shrunk replay must preserve the failure" >&2
  exit 1
fi
rm -rf "$shrink_dir"

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
oracle input(n: Int): Bool =
  Main.accepts(n)
CLAIMS
fuzz --iterations 0 "$stamp_dir"
stamp_before="$(python3 - "$stamp_dir" <<'PY_STAMP'
import json, sys
from pathlib import Path
root = Path(sys.argv[1])
paths = ("build/stamp.ll", "build/stamp", "build/live/stamp.ll", "build/live/stamp")
print(json.dumps({path: (root / path).stat().st_mtime_ns for path in paths}))
PY_STAMP
)"
sleep 1
fuzz --iterations 0 "$stamp_dir"
python3 - "$stamp_dir" "$stamp_before" <<'PY_STAMP'
import json, sys
from pathlib import Path
root = Path(sys.argv[1])
for path, before in json.loads(sys.argv[2]).items():
    assert (root / path).stat().st_mtime_ns == before, path
PY_STAMP
rm -rf "$stamp_dir"

# A compiler change invalidates live and verification artifacts.
python3 - "$SCUZZ" <<'PY_COMPILER_CACHE'
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

if sys.platform != "linux":
    sys.exit(0)
with tempfile.TemporaryDirectory(prefix="scuzz-compiler-cache-") as tmp:
    root = Path(tmp)
    compiler = root / "compiler"
    shutil.copy2(sys.argv[1], compiler)
    env = dict(os.environ, SCUZZ_EXECUTABLE_SHA256="caller-controlled")
    packages = []
    for kind in ("live", "verify"):
        pkg = root / kind
        (pkg / "src").mkdir(parents=True)
        (pkg / "scuzz.toml").write_text('[package]\nname = "cache-proof"\n')
        (pkg / "src/Main.scuzz").write_text(
            'def id(n: Int): Int = n\n@main def main: IO[Unit] = IO.pure(())\n')
        (pkg / "facts.scuzz_verify").write_text(
            'oracle identity(n: Int): Bool =\n  Main.id(n) == n\n\n')
        packages.append((kind, pkg))

    def run(kind, pkg):
        args = ["build"] if kind == "live" else ["fuzz", "--iterations", "0"]
        subprocess.run([str(compiler), *args, str(pkg)], env=env,
                       check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        paths = [pkg / "build/cache-proof.ll", pkg / ".scuzz" / (
            "fingerprint" if kind == "live" else "fingerprint.verify")]
        if kind == "verify":
            paths.extend([pkg / "build/live/cache-proof.ll", pkg / "build/live/fingerprint"])
        digest = hashlib.sha256(compiler.read_bytes()).hexdigest()
        for path in paths:
            if "fingerprint" in path.name:
                assert path.read_text().splitlines()[0] == digest
        return [p.stat().st_mtime_ns for p in paths]

    before = [run(kind, pkg) for kind, pkg in packages]
    assert before == [run(kind, pkg) for kind, pkg in packages]
    # ELF permits trailing data. The compiler behavior stays the same.
    with compiler.open("ab") as out:
        out.write(b"compiler-cache-identity-proof")
    after = [run(kind, pkg) for kind, pkg in packages]
    assert all(all(a != b for a, b in zip(old, new)) for old, new in zip(before, after))
    assert after == [run(kind, pkg) for kind, pkg in packages]
print("compiler cache identity ok")
PY_COMPILER_CACHE
