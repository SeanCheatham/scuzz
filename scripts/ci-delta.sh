#!/usr/bin/env bash
# Prove `scuzz diff` on a copy of examples/counter in a temp git repo.
set -eo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
SCUZZ="${SCUZZ:-$ROOT/examples/cli/build/cli}"

repo="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-delta.XXXXXX")"
pkg="$repo/examples/counter"
main="$pkg/src/Main.scuzz"
report="$pkg/build/delta/report.json"
mkdir -p "$repo/examples"
cp -R examples/counter examples/shared "$repo/examples/"
rm -rf "$repo/examples/counter/build" "$repo/examples/shared/build"
printf 'build/\n' > "$repo/.gitignore"
git -C "$repo" init -q
git -C "$repo" add -A
git -C "$repo" -c user.name=ci -c user.email=ci@localhost commit -q -m base

# Run `scuzz diff`. Check the exit code and the report counts.
# Usage: expect_diff <exit> <regressed> <fixed> <diverged> <unrunnable> [diff args]
expect_diff() {
  local want="$1" status=0
  local counts="$2 $3 $4 $5"
  shift 5
  echo "diff $* $pkg"
  "$SCUZZ" diff "$@" "$pkg" > "$repo/diff.log" 2>&1 || status=$?
  cat "$repo/diff.log"
  if [ "$status" != "$want" ]; then
    echo "scuzz diff: want exit $want, got $status" >&2
    exit 1
  fi
  python3 - "$report" "$counts" <<'PY_CHECK'
import json, sys
report = json.load(open(sys.argv[1]))
want = [int(n) for n in sys.argv[2].split()]
counts = report["counts"]
got = [counts[k] for k in ("regressed", "fixed", "diverged", "unrunnable")]
assert report["v"] == 1 and report["kind"] == "diff", report
assert got == want, (got, want)
assert counts["same"] == len(report["workloads"]) - sum(got), counts
assert report["ok"] == (counts["regressed"] == 0), report
PY_CHECK
}

reset_tree() {
  git -C "$repo" checkout -q -- .
}

# Self-diff: no change gives all same.
expect_diff 0 0 0 0 0

# A whitespace-only edit gives all same.
python3 - "$main" <<'PY_EDIT'
import sys
p = sys.argv[1]
s = open(p).read()
open(p, "w").write(s.replace("@main def main", "\n\n@main def main", 1))
PY_EDIT
expect_diff 0 0 0 0 0
reset_tree

# A label change diverges on the a11y section.
sed -i.bak 's/"Counter"/"Counter app"/' "$repo/examples/shared/src/Shared.scuzz"
rm -f "$repo/examples/shared/src/Shared.scuzz.bak"
expect_diff 0 0 0 4 0
python3 - "$report" <<'PY_CHECK'
import json, sys
report = json.load(open(sys.argv[1]))
for row in report["workloads"]:
    sections = {c["section"] for c in row["delta"]["changes"]}
    assert "a11y" in sections, row
PY_CHECK
reset_tree

# A broken +1 handler fails a claim on the working tree only.
sed -i.bak 's/Signal.get(count) + 1/Signal.get(count) + 2/' "$main"
rm -f "$main.bak"
expect_diff 1 3 0 0 0
python3 - "$report" <<'PY_CHECK'
import json, sys
report = json.load(open(sys.argv[1]))
changed = report["claims"]["changed"]
assert {"name": "afterPlusShowsOne", "a": "pass", "b": "fail"} in changed, changed
PY_CHECK

# Divergence search writes witnesses that `scuzz fuzz --replay` reproduces.
expect_diff 1 3 0 0 0 --iterations 16 --seed 7
python3 - "$report" <<'PY_CHECK'
import json, sys
report = json.load(open(sys.argv[1]))
assert report["search"]["ran"] == 16, report["search"]
assert report["witnesses"], report
PY_CHECK
side_a="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["sides"]["a"])' "$report")"
set -- "$pkg"/build/delta/witness-*.toml
[ -f "$1" ]
for witness in "$@"; do
  if "$SCUZZ" fuzz --replay "$witness" "$pkg"; then
    echo "witness must fail on the working tree: $witness" >&2
    exit 1
  fi
  "$SCUZZ" fuzz --replay "$witness" "$side_a"
done
if [ -n "$(git -C "$repo" status --porcelain -- examples/counter/corpus)" ]; then
  echo 'scuzz diff must not write to corpus/' >&2
  exit 1
fi
reset_tree

# A missing revision fails before any build.
if "$SCUZZ" diff no-such-rev "$pkg" > "$repo/rev.log" 2>&1; then
  echo 'scuzz diff must reject an unknown revision' >&2
  exit 1
fi
grep -q 'unknown revision no-such-rev' "$repo/rev.log"

echo "delta ok"
