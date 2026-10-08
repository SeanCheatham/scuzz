#!/usr/bin/env bash
# Same slices as `.github/workflows/ci.yml`. Run one slice locally.
# Selected slices use clean example build directories.
# Keep examples/cli/build because it contains the compiler.
set -eo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
SCUZZ="${SCUZZ:-$ROOT/examples/cli/build/cli}"
case "$SCUZZ" in
  /*) ;;
  *) SCUZZ="$ROOT/$SCUZZ" ;;
esac
export SCUZZ
JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
# A prior `scuzz run` in this shell can leak session env. GitHub starts clean.
unset SCUZZ_SNAPSHOT_PATH SCUZZ_FUZZ_DUMP SCUZZ_UI_RUNTIME SCUZZ_UI_WIDTH \
  SCUZZ_UI_HEIGHT SCUZZ_UI_SCALE SCUZZ_LIVE_FRAMES SCUZZ_MOBILE_SHELL \
  SCUZZ_UI_RECORD SCUZZ_UI_DEBUG_DUMP SCUZZ_UI_SCRIPT SCUZZ_UI_INJECT \
  SCUZZ_UI_TAP SCUZZ_UI_TEXT SCUZZ_SKIA || true

need_scuzz() {
  if [ ! -x "$SCUZZ" ]; then
    echo "missing $SCUZZ"
    echo "./scripts/bootstrap.sh"
    exit 1
  fi
}

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "missing $1"
    echo "$2"
    exit 1
  fi
}

# Keep examples/cli/build: that tree holds the product CLI.
wipe_example_builds() {
  local d
  for d in examples/*/build; do
    [ -d "$d" ] || continue
    case "$d" in
      examples/cli/build) continue ;;
    esac
    rm -rf "$d"
  done
}

# GitHub sets CI=true and starts with clean build directories.
# Local runs use the same starting state for these slices.
maybe_wipe() {
  if [ "${CI:-}" = "" ]; then
    wipe_example_builds
  fi
}

slice_help() {
  cat <<'EOF'
Usage:
  ./scripts/ci.sh <slice>

Slices (same names as ci.yml where one step maps to one slice):
  pr              macos-smoke + oracles + kernel + ui + fuzz + delta
  linux-headless  full Linux job minus apt install and artifact upload
  macos-smoke     required Darwin PR job (runtime + hello smoke)
  macos-hello     hello and counter fuzz, kernel check, bad-intent
  macos-app       relocated macOS UI bundle and Finder launch
  oracles         hello, tyck, kits, codegen, hello-outdir, fixedpoint
  install-dry     installer and bump_version dry-run
  fetch-skia      fetch_skia.sh retry proof (no GitHub Releases)
  runtime         make -C crates/runtime test
  asan            make -C crates/runtime test-asan
  skia            ffi-skia tests
  embedders       ffi-skia lib + desktop and mobile embedders
  hello           hello, fmt
  tyck            typecheck oracle
  kits            check and corpus-only fuzz for examples/manual
  codegen         codegen emit + oracle
  tyck-replay     typechecker corpus replay
  codegen-replay  code-generation corpus replay
  compiler-cases  bounded generated compiler checks
  hello-outdir    hello via --out-dir
  fixedpoint      LLVM IR fixed-point
  kernel          ./scripts/ci-kernel.sh
  package         release tarball + install smoke
  ui              headless counter/studio/editor + fuzz --iterations 0
  ui-test         corpus-only fuzz for counter/studio/editor
  gpu             GPU presenter (needs xvfb)
  differential    skia vs sk_sw vs gpu dumps (needs xvfb)
  fuzz            ./scripts/ci-fuzz.sh
  delta           ./scripts/ci-delta.sh (scuzz diff on a temp git repo)
  new-ui          scuzz new --ui path + cheap search
  desktop         Desktop peer + X11 (needs xvfb)
  mobile          mobile shell + package targets
  ios             local iOS simulator loop (Apple Silicon + Xcode)
  web             Docs package + browser input + Headless claims

Examples:
  ./scripts/bootstrap.sh
  ./scripts/ci.sh pr
  ./scripts/ci.sh ui
  ./scripts/ci.sh fuzz
  SCUZZ=./examples/cli/build/cli ./scripts/ci.sh macos-smoke
EOF
}

# Local HTTP proof for fetch_skia.sh retries. Does not call GitHub Releases.
prove_fetch_skia_retry() (
  need_cmd python3 "sudo apt-get install -y python3"
  work="$(mktemp -d)"
  triple=fetch-retry-proof
  dest="$ROOT/third_party/skia/prebuilt/${triple}"
  mkdir -p "$work/pkg"
  : >"$work/pkg/libsk_capi.a"
  tar -czf "$work/skia.tgz" -C "$work/pkg" libsk_capi.a
  cat >"$work/server.py" <<'PY'
from http.server import BaseHTTPRequestHandler, HTTPServer
import sys

tarball = open(sys.argv[1], "rb").read()
truncated = tarball[:24]
state = {"n": 0}
portfile = sys.argv[2]


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith("/missing"):
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b"not found")
            return
        state["n"] += 1
        n = state["n"]
        if n == 1:
            self.send_response(502)
            self.end_headers()
            self.wfile.write(b"bad gateway")
        elif n == 2:
            self.send_response(503)
            self.end_headers()
            self.wfile.write(b"unavailable")
        elif n == 3:
            self.send_response(504)
            self.end_headers()
        elif n == 4:
            body = truncated
            self.send_response(200)
            self.send_header("Content-Type", "application/gzip")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_response(200)
            self.send_header("Content-Type", "application/gzip")
            self.send_header("Content-Length", str(len(tarball)))
            self.end_headers()
            self.wfile.write(tarball)

    def log_message(self, *_args):
        return


httpd = HTTPServer(("127.0.0.1", 0), Handler)
with open(portfile, "w", encoding="utf-8") as f:
    f.write(str(httpd.server_address[1]))
httpd.serve_forever()
PY
  python3 "$work/server.py" "$work/skia.tgz" "$work/port" &
  srv_pid=$!
  # shellcheck disable=SC2064
  trap "kill $srv_pid 2>/dev/null || true; rm -rf '$work' '$dest'" EXIT
  i=0
  while [ ! -s "$work/port" ]; do
    i=$((i + 1))
    if [ "$i" -gt 50 ]; then
      echo "fetch_skia retry proof: server did not bind" >&2
      return 1
    fi
    sleep 0.1
  done
  port="$(cat "$work/port")"
  url="http://127.0.0.1:${port}/skia-cpu.tar.gz"
  rm -rf "$dest"
  SCUZZ_SKIA_URL="$url" SCUZZ_SKIA_TRIPLE="$triple" SCUZZ_SKIA_FORCE=1 \
    SCUZZ_SKIA_FETCH_ATTEMPTS=5 SCUZZ_SKIA_FETCH_RETRY_DELAY=0 \
    ./scripts/fetch_skia.sh >"$work/retry.out" 2>&1
  cat "$work/retry.out"
  grep -q "HTTP 502" "$work/retry.out"
  grep -q "HTTP 503" "$work/retry.out"
  grep -q "HTTP 504" "$work/retry.out"
  grep -q "truncated or corrupt gzip" "$work/retry.out"
  grep -q "installed under ${dest}" "$work/retry.out"
  test -f "$dest/libsk_capi.a"
  SCUZZ_SKIA_FETCH_ATTEMPTS=5 SCUZZ_SKIA_FETCH_RETRY_DELAY=0 \
    ./scripts/fetch_skia.sh --download "$url" "$work/include.tar.gz"
  tar -tzf "$work/include.tar.gz" >/dev/null
  test -f "$work/include.tar.gz"
  if SCUZZ_SKIA_URL="http://127.0.0.1:${port}/missing.tar.gz" \
      SCUZZ_SKIA_TRIPLE="$triple" SCUZZ_SKIA_FORCE=1 \
      SCUZZ_SKIA_FETCH_ATTEMPTS=5 SCUZZ_SKIA_FETCH_RETRY_DELAY=0 \
      ./scripts/fetch_skia.sh >"$work/missing.out" 2>&1; then
    echo "fetch_skia retry proof: 404 must fail closed" >&2
    cat "$work/missing.out" >&2
    return 1
  fi
  grep -q "HTTP 404" "$work/missing.out"
  if grep -q "retry in" "$work/missing.out"; then
    echo "fetch_skia retry proof: 404 must not retry" >&2
    cat "$work/missing.out" >&2
    return 1
  fi
  echo "fetch_skia retry proof: 404 fail-closed"
  cat "$work/missing.out"
  rm -rf "$dest"
  SCUZZ_SKIA_URL="file://${work}/skia.tgz" SCUZZ_SKIA_TRIPLE="$triple" \
    SCUZZ_SKIA_FORCE=1 ./scripts/fetch_skia.sh >"$work/file.out" 2>&1
  cat "$work/file.out"
  grep -q "copying file://" "$work/file.out"
  test -f "$dest/libsk_capi.a"
  rm -rf "$dest"
)

slice_install_dry() {
  ./scripts/install.sh --help
  SCUZZ_INSTALL_SOURCE=github SCUZZ_INSTALL_DRY_RUN=1 ./scripts/install.sh | tee /tmp/install-dry.out
  grep -q 'source=github SeanCheatham/scuzz latest' /tmp/install-dry.out
  grep -q 'api.github.com/repos/SeanCheatham/scuzz/releases' /tmp/install-dry.out
  grep -q 'asset=scuzz-' /tmp/install-dry.out
  SCUZZ_INSTALL_SOURCE=github SCUZZ_VERSION=v0.1.0 SCUZZ_INSTALL_DRY_RUN=1 ./scripts/install.sh | tee /tmp/install-pin.out
  grep -q 'releases/download/v0.1.0/scuzz-' /tmp/install-pin.out
  ./scripts/install.sh --dry-run | tee /tmp/install-checkout-dry.out
  grep -q 'source=checkout' /tmp/install-checkout-dry.out
  cat scripts/install.sh | sh -s -- --dry-run | tee /tmp/install-pipe.out
  grep -q 'source=github SeanCheatham/scuzz latest' /tmp/install-pipe.out
  grep -q 'api.github.com/repos/SeanCheatham/scuzz/releases' /tmp/install-pipe.out
  ./scripts/bump_version.sh --help
  ./scripts/bump_version.sh --dry-run patch | tee /tmp/bump.out
  grep -Eq '^tag=v[0-9]+\.[0-9]+\.[0-9]+$' /tmp/bump.out
  grep -q 'dry-run=1' /tmp/bump.out
  git diff --exit-code -- VERSION examples/cli/src/Version.scuzz
  printf '%s\n' '"tag_name": "skia-cpu-v0.1"' '"tag_name": "v0.1.0-rc.1"' '"tag_name": "v9.9.9"' > /tmp/releases.json
  SCUZZ_RELEASES_JSON=/tmp/releases.json SCUZZ_INSTALL_SOURCE=github SCUZZ_INSTALL_DRY_RUN=1 ./scripts/install.sh | tee /tmp/install-tag.out
  grep -q 'tag=v9.9.9' /tmp/install-tag.out
  printf '%s' '[{"tag_name":"skia-cpu-v0.1"},{"tag_name":"v0.1.0-rc.1"},{"tag_name":"v9.9.9"}]' > /tmp/releases-compact.json
  SCUZZ_RELEASES_JSON=/tmp/releases-compact.json SCUZZ_INSTALL_SOURCE=github SCUZZ_INSTALL_DRY_RUN=1 ./scripts/install.sh | tee /tmp/install-compact.out
  grep -q 'tag=v9.9.9' /tmp/install-compact.out
  prove_fetch_skia_retry
}

slice_runtime() {
  make -C crates/runtime test -j"$JOBS" CC=clang
}

slice_asan() {
  make -C crates/runtime test-asan -j"$JOBS" CC=clang
}

slice_skia() {
  make -C crates/ffi-skia test -j"$JOBS" CC=clang
  grep -qx skia crates/ffi-skia/build/sk_capi_backend
}

slice_embedders() {
  make -C crates/ffi-skia lib -j"$JOBS" CC=clang
  grep -qx skia crates/ffi-skia/build/sk_capi_backend
  make -C crates/embedder-desktop lib -j"$JOBS" CC=clang
  make -C crates/embedder-mobile lib -j"$JOBS" CC=clang
}

slice_hello() {
  need_scuzz
  maybe_wipe
  "$SCUZZ" run examples/hello | tee /tmp/hello.out
  grep -q "Hello, Scuzz!" /tmp/hello.out
  grep -q "ready." /tmp/hello.out
  # Evaluator parity: eval stdout must equal the compiled program stdout.
  # The driver prints one "ok" line when it emits fresh IR; that line is not program output.
  "$SCUZZ" run examples/hello | grep -v '^ok$' > /tmp/hello.run.out
  "$SCUZZ" eval examples/hello | tee /tmp/hello.eval.out
  diff /tmp/hello.run.out /tmp/hello.eval.out
  "$SCUZZ" run examples/fmt | tee /tmp/fmt.out
  grep -q "fmt-ok" /tmp/fmt.out
}

slice_tyck() {
  need_scuzz
  "$SCUZZ" run examples/tyck | tee /tmp/tyck.out
  grep -q "tyck-ok" /tmp/tyck.out
}

slice_kits() {
  need_scuzz
  "$SCUZZ" check examples/manual
  "$SCUZZ" fuzz --iterations 0 examples/manual
}

slice_codegen() {
  need_scuzz
  echo "codegen emit start"
  "$SCUZZ" build examples/codegen
  echo "codegen emit done"
  # The probe oracle runs last under SCUZZ_EV_*: two evAdd drive lines, one
  # claim, one coverage key.
  printf '{"v":1,"kind":"inject","events":[{"op":"drive","name":"evAdd","args":[3]},{"op":"drive","name":"evAdd","args":[4]}]}' > /tmp/codegen-probe.json
  rm -f /tmp/codegen-probe.cov
  SCUZZ_EV_TESTRT=1 SCUZZ_EV_DRIVE_SCRIPT=/tmp/codegen-probe.json SCUZZ_EV_COVERAGE_DUMP=/tmp/codegen-probe.cov \
    "$SCUZZ" run examples/codegen | tee /tmp/codegen.out
  grep -q "ir-ok" /tmp/codegen.out
  grep -q "eval-ok" /tmp/codegen.out
  grep -q "eval-ui-ok" /tmp/codegen.out
  grep -q "eval-trace-ok" /tmp/codegen.out
  grep -q "eval-sched-ok" /tmp/codegen.out
  grep -q "eval-camp-ok" /tmp/codegen.out
  grep -q "probe-ok" /tmp/codegen.out
  grep -qx "codegen:probe" /tmp/codegen-probe.cov
  local memory_dir
  memory_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-match-memory.XXXXXX")"
  mkdir -p "$memory_dir/src"
  cat > "$memory_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "match-memory"
MANIFEST
  cat > "$memory_dir/src/Main.scuzz" <<'SOURCE'
enum MemoryPacket:
  case Fields(text: String, count: Int)

record MemoryRecord(text: String, count: Int)

def size(text: String): Int =
  MemoryPacket.Fields(text, 1) match {
    case MemoryPacket.Fields(label, _) => Str.len(label)
  }

def value(text: String): String =
  MemoryPacket.Fields(text, 1) match {
    case MemoryPacket.Fields(label, _) => label
  }

def recordSize(text: String): Int =
  MemoryRecord(text, 1) match {
    case MemoryRecord(label, _) => Str.len(label)
  }

def recordValue(text: String): String =
  MemoryRecord(text, 1) match {
    case MemoryRecord(label, _) => label
  }

def tailSize(text: String, count: Int): Int =
  MemoryPacket.Fields(Str.concat(text, ""), count) match {
    case MemoryPacket.Fields(label, n) if n > 0 => tailSize(label, n - 1)
    case MemoryPacket.Fields(label, _) => Str.len(label)
  }

def tailValue(text: String, count: Int): String =
  MemoryRecord(Str.concat(text, ""), count) match {
    case MemoryRecord(label, n) if n > 0 => tailValue(label, n - 1)
    case MemoryRecord(label, _) => label
  }

def guardValue(raw: String): String =
  Str.concat(raw, "").require(text => Str.len(text) > 0)

def reviewReady(raw: String, complete: Bool): Bool =
  for {
    parsed = Json.parse(raw)
    pieces = Str.split(raw, ",")
    label = Str.concat(raw, "!")
  } yield complete && List.len(pieces) > 0 && Str.len(label) > 0 && (parsed match {
    case Result.Ok(j) => Json.getBool(j, "complete", false)
    case Result.Err(_) => false
  })

@main def main: IO[Unit] =
  IO.println(value("ok"))
SOURCE
  "$SCUZZ" build --full "$memory_dir"
  python3 - "$memory_dir/build/match-memory.ll" <<'PY_IR'
from pathlib import Path
import sys
p = Path(sys.argv[1])
s = p.read_text()
for ty, name in (("i64", "size"), ("ptr", "value"), ("i64", "recordSize"), ("ptr", "recordValue"), ("i64", "tailSize"), ("ptr", "tailValue"), ("i64", "reviewReady"), ("ptr", "guardValue")):
    old = f"define internal {ty} @sz_user_Main_{name}("
    assert s.count(old) == 1
    s = s.replace(old, f"define {ty} @sz_user_Main_{name}(")
s = s.replace("define i32 @main(", "define i32 @scuzz_memory_main(")
p.write_text(s)
PY_IR
  cat > "$memory_dir/probe.c" <<'C_SOURCE'
#include "scuzz_rt.h"
#include <stdio.h>
#include <assert.h>
extern SzString *sz_user_Main_guardValue(SzString *);
extern int64_t sz_user_Main_reviewReady(SzString *, int64_t);
extern int64_t sz_user_Main_size(SzString *);
extern SzString *sz_user_Main_value(SzString *);
extern int64_t sz_user_Main_recordSize(SzString *);
extern SzString *sz_user_Main_recordValue(SzString *);
extern int64_t sz_user_Main_tailSize(SzString *, int64_t);
extern SzString *sz_user_Main_tailValue(SzString *, int64_t);
int main(void) {
  SzString *input = sz_string_from_cstr("payload");
  SzString *report = sz_string_from_cstr("{\"complete\":true}");
  size_t before, after;
  {
    /* Warm up: interned string literals pin on first evaluation. */
    SzString *warm = sz_user_Main_value(input);
    sz_release(warm);
    warm = sz_user_Main_guardValue(input);
    assert(sz_string_eq(input, warm));
    sz_release(warm);
    warm = sz_user_Main_recordValue(input);
    sz_release(warm);
    warm = sz_user_Main_tailValue(input, 1);
    sz_release(warm);
    assert(sz_user_Main_reviewReady(report, 1));
    assert(!sz_user_Main_reviewReady(report, 0));
    assert(sz_user_Main_size(input) == 7);
    assert(sz_user_Main_recordSize(input) == 7);
    assert(sz_user_Main_tailSize(input, 1) == 7);
  }
  sz_alloc_stats(&before, NULL);
  for (int i = 0; i < 1000; ++i) {
    assert(sz_user_Main_reviewReady(report, 1));
    assert(!sz_user_Main_reviewReady(report, 0));
    assert(sz_user_Main_size(input) == 7);
    SzString *output = sz_user_Main_guardValue(input);
    assert(sz_string_eq(input, output));
    sz_release(output);
    output = sz_user_Main_value(input);
    assert(sz_string_eq(input, output));
    sz_release(output);
    assert(sz_user_Main_recordSize(input) == 7);
    output = sz_user_Main_recordValue(input);
    assert(sz_string_eq(input, output));
    sz_release(output);
  }
  assert(sz_user_Main_tailSize(input, 10000) == 7);
  SzString *tail = sz_user_Main_tailValue(input, 10000);
  assert(sz_string_eq(input, tail));
  sz_release(tail);
  sz_alloc_stats(&after, NULL);
  printf("live allocation delta: %zu bytes\n", after-before);
  sz_release(input);
  sz_release(report);
  return after == before ? 0 : 1;
}
C_SOURCE
  local memory_host_link=()
  if [ "$(uname -s)" = Darwin ]; then
    memory_host_link=(-framework CoreFoundation -L/opt/homebrew/opt/openssl@3/lib -L/usr/local/opt/openssl@3/lib)
  fi
  clang -O2 "${memory_host_link[@]}" -I crates/runtime/include "$memory_dir/probe.c" \
    "$memory_dir/build/match-memory.ll" crates/runtime/build/libscuzz_rt.a \
    -lssl -lcrypto -lz -lbz2 -lm -lpthread -ldl -o "$memory_dir/probe"
  "$memory_dir/probe"
  rm -rf "$memory_dir"
}

slice_compiler_cases() (
  need_scuzz
  need_cmd python3 'Install Python 3 for generated compiler checks'
  local cases_dir
  cases_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-compiler-cases.XXXXXX")"
  trap 'status=$?; if [ "$status" -eq 0 ]; then rm -rf "$cases_dir"; else echo "Generated compiler artifacts: $cases_dir" >&2; fi' EXIT
  "$SCUZZ" run examples/compiler-cases > "$cases_dir/export.log"
  python3 - "$SCUZZ" "$cases_dir" <<'PY_CASES'
import json
import pathlib
import subprocess
import sys

cli = pathlib.Path(sys.argv[1]).resolve()
root = pathlib.Path(sys.argv[2])
rows = [json.loads(line) for line in (root / 'export.log').read_text().splitlines() if line.startswith('[')]
assert len(rows) == 1
cases = rows[0]
assert len(cases) == 54 and len({c['seed'] for c in cases}) == 54
assert {c['family'] for c in cases} == set(range(6))
assert {c['input'] for c in cases} == {-8, 0, 8}

# The host model does not parse source or call either execution engine.
def reference(seed):
    n = seed % 4096
    x = n // 6 % 17 - 8
    def expression_value(choice, levels):
        if levels == 0:
            return x
        value = expression_value(choice // 4, levels - 1)
        amount = choice % 5 + 1
        return (value + amount, value - amount, abs(value), value * 2)[choice % 4]
    value = expression_value(n // 6, n // 102 % 3 + 1)
    result = (f'match:{value}', f'closure:{value + 3}', f'box:{value}:1',
              f'heap:{value}:{value + 1}', f'list:{value * 3 + 3}', f'io:{value}')[n % 6]
    return result, result + ':2' if n % 6 == 5 else result

def command(args, label, success=True):
    result = subprocess.run([str(cli), *map(str, args)], capture_output=True, text=True, timeout=180)
    (root / (label + '.stdout')).write_text(result.stdout)
    (root / (label + '.stderr')).write_text(result.stderr)
    if (result.returncode == 0) != success:
        raise AssertionError(f'{label}: exit {result.returncode}\n{result.stdout}\n{result.stderr}')
    return result

def require_output(actual, expected, label):
    if actual != expected:
        raise AssertionError(f'{label}: expected {expected!r}, got {actual!r}')

def native(package, label):
    command(['run', package], label + '-build')
    result = subprocess.run([str(package / 'build/generated')], capture_output=True, text=True, timeout=20)
    (root / (label + '.native')).write_text(result.stdout)
    assert result.returncode == 0, result.stderr
    return result.stdout

package = root / 'valid'
(package / 'src').mkdir(parents=True)
(package / 'scuzz.toml').write_text('[package]\nname="generated"\n')
main = []
claims = []
expected = ''
for i, case in enumerate(cases):
    value, output = reference(case['seed'])
    assert case['result'] == value and case['output'] == output, case
    (package / f'src/Case{i}.scuzz').write_text(case['body'])
    argument = str(case['input']) if case['input'] >= 0 else f"(0 - {-case['input']})"
    main.append(f'    _ <- Case{i}.emit({argument})')
    claims.append(f'oracle case{i}(): Bool =\n  Case{i}.f({argument}) == {json.dumps(value)}\n')
    expected += output + '\n'
(package / 'src/Main.scuzz').write_text('@main def main: IO[Unit] =\n  for {\n' + '\n'.join(main) + '\n  } yield ()\n')
(package / 'generated.scuzz_verify').write_text('\n'.join(claims))
(root / 'expected.txt').write_text(expected)
require_output(command(['eval', package], 'original-eval').stdout, expected, 'original evaluator model')
command(['fmt', package], 'valid-format')
formatted = {path: path.read_bytes() for path in (package / 'src').glob('*.scuzz')}
command(['fmt', package], 'valid-format-again')
assert all(path.read_bytes() == content for path, content in formatted.items()), 'formatter is not stable'
command(['check', package], 'valid-check')
require_output(command(['eval', package], 'valid-eval').stdout, expected, 'evaluator model')
require_output(native(package, 'valid'), expected, 'native model')
# Check separate module types with both declaration orders.
for case in cases[:6]:
    for reverse in [False, True]:
        label = f"modules-{case['seed']}-{int(reverse)}"
        modules = root / label
        (modules / 'src').mkdir(parents=True)
        (modules / 'scuzz.toml').write_text('[package]\nname="generated"\n')
        for stem, source in case['modules']:
            if reverse:
                stem = {'A': 'Z', 'B': 'A'}.get(stem, stem)
                source = source.replace('A.', 'Z.').replace('B.', 'A.').replace('shadow(A: Holder)', 'shadow(Z: Holder)')
            (modules / f'src/{stem}.scuzz').write_text(source)
        module_claims = '\n'.join([
            'oracle fromA(c: A.Choice[String]): Bool =\n  c match { case A.Choice.Wrap(A.Box(text)) => A.read(c) == Str.len(text) case A.Choice.End => A.read(c) == 0 }\n',
            'oracle fromB(c: B.Choice[Int]): Bool =\n  c match { case B.Choice.Wrap(marker, B.Box(_, value)) => B.read(c) == marker + value case B.Choice.End => B.read(c) == 0 }\n',
        ])
        if reverse:
            module_claims = module_claims.replace('A.', 'Z.').replace('B.', 'A.')
        (modules / 'modules.scuzz_verify').write_text(module_claims)
        expected_module = str(21 + 2 * (case['seed'] % 6)) + '\n'
        command(['fmt', modules], label + '-format')
        command(['check', modules], label + '-check')
        require_output(command(['eval', modules], label + '-eval').stdout, expected_module, label + ' evaluator')
        require_output(native(modules, label), expected_module, label + ' native')
        command(['fuzz', '--iterations', '6', '--seed', '41', modules], label + '-campaign')
        module_summary = json.loads((modules / 'build/fuzz/summary.json').read_text())
        assert module_summary['fuzz']['ok'] and module_summary['fuzz']['search'] > 0

command(['fuzz', '--iterations', '16', '--seed', '41', package], 'valid-campaign')
summary = json.loads((package / 'build/fuzz/summary.json').read_text())
assert summary['fuzz']['ok'] and summary['fuzz']['search'] == 10
assert summary['mutate']['ran'] > 0 and summary['mutate']['killed'] > 0

invalid = root / 'invalid'
(invalid / 'src').mkdir(parents=True)
(invalid / 'scuzz.toml').write_text('[package]\nname="invalid"\n')
for i, case in enumerate(cases):
    (invalid / 'src/Main.scuzz').write_text(case['invalid'])
    command(['fmt', invalid], f'invalid-{i}-format')
    rejected = command(['check', '--message-format=json', invalid], f'invalid-{i}-check', success=False)
    assert 'type error' in rejected.stdout and 'unexpected token' not in rejected.stdout, rejected.stdout

# Both engines can agree on a wrong result. The model and oracle must reject it.
mutant = root / 'mutant'
(mutant / 'src').mkdir(parents=True)
(mutant / 'scuzz.toml').write_text('[package]\nname="generated"\n')
case = cases[0]
body = case['body']
start = body.index('def f(x: Int): String =')
end = body.index('def emit(x: Int): IO[Unit] =', start)
(mutant / 'src/Case0.scuzz').write_text(body[:start] + 'def f(x: Int): String =\n  s"wrong:$x"\n\n' + body[end:])
(mutant / 'src/Main.scuzz').write_text('@main def main: IO[Unit] =\n  Case0.emit(0 - 8)\n')
(mutant / 'generated.scuzz_verify').write_text(claims[0])
command(['fmt', mutant], 'mutant-format')
ev = command(['eval', mutant], 'mutant-eval').stdout
compiled = native(mutant, 'mutant')
assert ev == compiled
for label, actual in [('evaluator', ev), ('native', compiled)]:
    try:
        require_output(actual, reference(case['seed'])[1] + '\n', label)
    except AssertionError:
        pass
    else:
        raise AssertionError(f'{label}: model accepts a wrong result')
command(['fuzz', '--iterations', '0', mutant], 'mutant-campaign', success=False)
rejected = json.loads((mutant / 'build/fuzz/summary.json').read_text())
assert not rejected['fuzz']['ok'] and rejected['corpus']['failures'] == 1
print('Generated compiler checks: 54 cases, six families, three input signs, three depths, invalid types, 12 module checks, evaluator/native models, search, mutation, and wrong-result rejection')
PY_CASES
  mkdir -p "$cases_dir/generator/src"
  cp examples/compiler-cases/src/*.scuzz "$cases_dir/generator/src/"
  cp examples/compiler-cases/*.scuzz_verify "$cases_dir/generator/"
  python3 - "$ROOT" "$cases_dir/generator" <<'PY_GENERATOR'
import os
import pathlib
import sys
root, target = map(pathlib.Path, sys.argv[1:])
compiler = os.path.relpath(root / 'examples/compiler', target)
(target / 'scuzz.toml').write_text('[package]\nname="compiler-cases"\n[dependencies]\ncompiler={path="' + compiler + '"}\n')
PY_GENERATOR
  "$SCUZZ" fuzz --iterations 16 --seed 41 "$cases_dir/generator" > "$cases_dir/generator-campaign.log" 2>&1
  python3 - "$cases_dir/generator/build/fuzz/summary.json" <<'PY_SUMMARY'
import json
import pathlib
import sys
summary = json.loads(pathlib.Path(sys.argv[1]).read_text())
assert summary['fuzz']['ok'] and summary['fuzz']['search'] == 10
assert summary['mutate']['ran'] > 0 and summary['mutate']['killed'] > 0
print(f"Compiler generator: {summary['fuzz']['search']} search cases, {summary['mutate']['killed']} mutations killed")
PY_SUMMARY
)

slice_hello_outdir() {
  need_scuzz
  "$SCUZZ" run --out-dir /tmp/scuzz-cli-hello examples/hello | tee /tmp/cli-hello.out
  grep -q "Hello, Scuzz!" /tmp/cli-hello.out
  grep -q "ready." /tmp/cli-hello.out
}

slice_fixedpoint() {
  need_scuzz
  ./scripts/fixedpoint-ll.sh
}

slice_kernel() {
  need_scuzz
  maybe_wipe
  ./scripts/ci-kernel.sh
}

slice_package() {
  # package_release.sh builds the product CLI when that binary is missing.
  # Subshell: do not leak PREFIX PATH into later slices in one local run.
  (
    triple="$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
    ./scripts/package_release.sh
    test -f "dist/scuzz-${triple}.tar.gz"
    test -x "dist/scuzz-${triple}/bin/scuzz"
    skia_triple="$(./scripts/skia_triple.sh)"
    test -f "dist/scuzz-${triple}/third_party/skia/prebuilt/${skia_triple}/libsk_capi.a"
    prefix=/tmp/scuzz-prefix
    rm -rf "$prefix"
    RELEASE_TGZ="$PWD/dist/scuzz-${triple}.tar.gz" PREFIX="$prefix" ./scripts/install.sh
    export PATH="$prefix/bin:$PATH"
    unset SCUZZ_HOME SCUZZ_RUNTIME || true
    test "$(command -v scuzz)" = "$prefix/bin/scuzz"
    rm -rf /tmp/scuzz-release-app
    scuzz new --ui --path /tmp scuzz-release-app
    scuzz fuzz --iterations 0 /tmp/scuzz-release-app
    grep -qx skia "$prefix/share/scuzz/crates/ffi-skia/build/sk_capi_backend"
    scuzz run --target headless --exec "" /tmp/scuzz-release-app
    test -f /tmp/scuzz-release-app/build/snapshot.png
    python3 - "$prefix" <<'PY'
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

prefix = Path(sys.argv[1])
home = prefix / "share/scuzz"
root = Path("/tmp/scuzz-release-app")
out = root / "build/ide-host"
out.mkdir(parents=True, exist_ok=True)
inject = out / "inject.json"
inject.write_text(json.dumps({"v": 1, "kind": "inject", "events": [
    {"op": "tap", "id": "choicechip:Session"},
    {"op": "text", "i": 0, "value": "Increase the counter"},
    {"op": "text", "i": 1, "value": "src/Main.scuzz"},
    {"op": "tap", "id": "choicechip:Review"},
    {"op": "tap", "id": "button:Start"}]}))
env = dict(os.environ, SCUZZ_UI_SCRIPT=str(inject))
with (out / "host.log").open("w") as log:
    proc = subprocess.Popen([str(prefix / "bin/scuzz"), "ide", "--target", "headless",
                             "--out-dir", str(out)], cwd=root, env=env,
                            stdout=log, stderr=subprocess.STDOUT)
    try:
        deadline = time.monotonic() + 180
        while time.monotonic() < deadline:
            assert proc.poll() is None, (out / "host.log").read_text()
            request = root / "build/ide/request.json"
            debug = out / "debug.json"
            if request.exists() and debug.exists():
                ui = json.loads(debug.read_text())
                if any(v.get("label", "").startswith("Ready cards:")
                       for v in ui.get("taps", [])) or any(
                       v.get("value", "").startswith("Ready cards:")
                       for v in ui.get("signals", []) if v.get("type") == "str"):
                    break
            time.sleep(0.1)
        else:
            raise AssertionError("SDK review preparation does not finish")
        expected = hashlib.sha256((home / "bin/scuzz").read_bytes()).hexdigest()
        assert json.loads(request.read_text())["compiler"] == expected
        assert (home / "ui-host/build/ui-host").is_file()
    finally:
        (out / "inject.json").write_text(json.dumps({"v": 1, "kind": "inject",
                                                      "events": [{"op": "quit"}]}))
        try:
            proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            proc.terminate()
            proc.wait(timeout=20)
PY
    scuzz run examples/hello | tee /tmp/rel-hello.out
    grep -q "Hello, Scuzz!" /tmp/rel-hello.out
    grep -q "ready." /tmp/rel-hello.out
    scuzz run --out-dir /tmp/scuzz-rel-cli examples/cli | tee /tmp/rel-cli.out
    grep -q "cli-ok" /tmp/rel-cli.out
    scuzz check "$prefix/share/scuzz/ide"
  )
}

slice_ui() {
  need_scuzz
  need_cmd python3 "sudo apt-get install -y python3"
  maybe_wipe
  "$SCUZZ" run --target headless --exec "" examples/counter
  test -f examples/counter/build/snapshot.png
  "$SCUZZ" run --target headless --exec "" --dump examples/counter/build/session.json examples/counter
  python3 - <<'PY'
import json
with open("examples/counter/build/session.json") as f:
    d = json.load(f)
assert d["v"] == 2 and d["kind"] == "dump"
assert any(s.get("name") == "count" and s.get("type") == "int" and s.get("value") == 0 for s in d["signals"])
state = [s for s in d["signals"] if s.get("name") == "state"][0]
assert state["type"] == "value" and isinstance(state["value"], dict) and "tag" in state["value"]
assert d["taps"] and d["taps"][0]["label"] == "+1"
assert isinstance(d["views"], list) and d["views"]
assert any(n.get("role") == "button" and n.get("label") == "+1" for n in d["a11y"])
assert isinstance(d["fields"], list) and isinstance(d["scrolls"], list)
PY
  printf '%s\n' '{"v":1,"kind":"inject","events":[{"op":"tap","id":"button:+1"}]}' > examples/counter/build/inject.json
  "$SCUZZ" run --target headless --exec examples/counter/build/inject.json --dump examples/counter/build/session.json examples/counter
  python3 - <<'PY'
import json
with open("examples/counter/build/session.json") as f:
    d = json.load(f)
assert any(s.get("name") == "count" and s.get("value") == 1 for s in d["signals"])
PY
  rm -f examples/counter/build/inject.json
  # Daemon headless: no --exec stays live. scuzz exec drives the channel.
  rm -f examples/counter/build/debug.json examples/counter/build/live.png
  "$SCUZZ" run --target headless examples/counter &
  daemon_pid=$!
  trap 'kill $daemon_pid 2>/dev/null || true' EXIT
  for i in $(seq 1 100); do [ -f examples/counter/build/debug.json ] && break; sleep 0.2; done
  test -f examples/counter/build/debug.json
  "$SCUZZ" exec examples/counter tap id:button:+1
  for i in $(seq 1 50); do grep -q '"name":"count","value":1' examples/counter/build/debug.json 2>/dev/null && break; sleep 0.2; done
  python3 - <<'PY'
import json
with open("examples/counter/build/debug.json") as f:
    d = json.load(f)
assert any(s.get("name") == "count" and s.get("value") == 1 for s in d["signals"])
assert d.get("last_hit", {}).get("desc") == "button:+1"
PY
  "$SCUZZ" exec examples/counter snapshot "$ROOT/examples/counter/build/live.png"
  for i in $(seq 1 50); do [ -f examples/counter/build/live.png ] && break; sleep 0.2; done
  test -f examples/counter/build/live.png
  "$SCUZZ" exec examples/counter quit
  wait $daemon_pid
  trap - EXIT
  "$SCUZZ" fuzz --iterations 0 examples/counter
  "$SCUZZ" run --target headless --exec "" examples/studio
  test -f examples/studio/build/snapshot.png
  "$SCUZZ" fuzz --iterations 0 examples/studio
  python3 - <<'PY'
import json
with open("examples/studio/build/fuzz/summary.json") as f:
    d = json.load(f)
b = d["coverage"]["branches"]
assert b["total"] > 0 and b["reached"] > 0
assert all("location" in r and "reached" in r for r in b["regions"])
PY
  # The editor seeds scuzz.toml and src/ into the CWD at boot.
  # Run it from a scratch dir so the worktree root stays clean. SCUZZ_HOME
  # keeps crates/ anchored at the checkout from that CWD.
  mkdir -p scratchpad/editor
  (cd scratchpad/editor && SCUZZ_HOME="$ROOT" "$SCUZZ" run --target headless --exec "" "$ROOT/examples/editor")
  test -f examples/editor/build/snapshot.png
  python3 - <<'PY'
import json
import os
from pathlib import Path
import subprocess

repo = Path.cwd()
root = repo / "scratchpad/editor"
script = root / "layout.json"
dump = root / "layout-dump.json"
script.write_text(json.dumps({"v": 1, "kind": "inject", "events": [
    {"op": "tap", "id": "choicechip:Session"},
    {"op": "xy", "x": 200, "y": 350},
    {"op": "key", "key": "x", "text": "scope"}]}))
env = dict(os.environ, SCUZZ_HOME=str(repo), SCUZZ_UI_RUNTIME="headless",
           SCUZZ_UI_WIDTH="960", SCUZZ_UI_HEIGHT="560", SCUZZ_UI_SCALE="1",
           SCUZZ_LIVE_FRAMES="2", SCUZZ_UI_SCRIPT=str(script),
           SCUZZ_UI_DEBUG_DUMP=str(dump))
subprocess.run([str(repo / "examples/editor/build/editor")], cwd=root,
               env=env, check=True, timeout=30)
ui = json.loads(dump.read_text())
assert ui["fields"][1]["value"].endswith("scope"), "source scope field is clipped"
PY
  (cd scratchpad/editor && SCUZZ_HOME="$ROOT" "$SCUZZ" fuzz --iterations 0 "$ROOT/examples/editor")
  # Live in-memory review: the editor diffs an edited buffer against disk on
  # the evaluator, with no git and no native build of the target package.
  rm -rf scratchpad/review scratchpad/shared
  mkdir -p scratchpad/review
  cp -R examples/counter/. scratchpad/review/
  cp -R examples/shared scratchpad/shared
  rm -rf scratchpad/review/build scratchpad/shared/build
  python3 - <<'PY'
import json
src = open("scratchpad/review/src/Main.scuzz").read()
i = src.index('"Counter"') + len('"Counter')
ops = {"v": 1, "kind": "inject", "events": [
    {"op": "tap", "id": "choicechip:Code"},
    {"op": "caret", "offset": i},
    {"op": "type", "value": " app"},
    {"op": "tap", "id": "choicechip:Review"}, {"op": "tap", "id": "choicechip:More evidence"},
    {"op": "tap", "id": "outlined:Compare buffers"},
    {"op": "pump", "k": 50},
]}
open("scratchpad/review/ops.json", "w").write(json.dumps(ops))
PY
  (cd scratchpad/review && SCUZZ_HOME="$ROOT" "$SCUZZ" run --target headless --exec ops.json "$ROOT/examples/editor")
  python3 - <<'PY'
import json
with open("scratchpad/review/build/ide/report.json") as f:
    r = json.load(f)
assert r["kind"] == "diff" and r["rev"] == "buffers", r
assert r["counts"]["diverged"] >= 1, r["counts"]
assert r["review"]["complete"] and r["review"]["required"] == r["review"]["completed_required"], r["review"]
assert r["search"]["ran"] == r["search"]["iterations"] == 32, r["search"]
assert r["review"]["recorded_workloads"] == len(r["workloads"])
assert all(w["why_b"] is None for w in r["workloads"]), r["workloads"]
row = r["workloads"][0]
assert row["class"] == "diverged", row
sections = {c["section"] for c in row["delta"]["changes"]}
assert "signals" in sections, row
PY
  python3 - <<'PYREVIEWIO'
import difflib, hashlib, json, os, shutil, subprocess
from pathlib import Path
repo = Path.cwd()
cli = Path(os.environ["SCUZZ"])
proof = repo / "scratchpad/review-io"
shutil.rmtree(proof, ignore_errors=True)
target = proof / "target"
for directory in (proof / "src", target / "src", target / "corpus"):
    directory.mkdir(parents=True, exist_ok=True)
(proof / "scuzz.toml").write_text('[package]\nname = "review-proof"\n[dependencies]\ncompiler = { path = "../../examples/compiler" }\n')
(target / "scuzz.toml").write_text('[package]\nname = "review-io"\n')
baseline = 'def value(n: Int): String =\n  if (n == 3) "one" else "same"\n\n@main def main: IO[Unit] =\n  IO.pure(())\n'
(target / "src/Main.scuzz").write_text(baseline)
(target / "scenario.scuzz_scenario").write_text('def setup(): IO[Unit] =\n  IO.pure(())\n\ndef emit(n: Int where n >= 1 && n <= 3): IO[Unit] =\n  Fs.write("value.txt", Main.value(n))\n\n')
corpus = target / "corpus/only.toml"
corpus.write_text('[fuzz]\nschedule_seed = "2"\nevents = ["drive emit 3"]\n')
(proof / "src/Main.scuzz").write_text("""@main def main: IO[Unit] =
  for {
    root <- Sys.getenv("SCUZZ_REVIEW_TARGET")
    cli <- Sys.getenv("SCUZZ_REVIEW_CLI")
    same <- Sys.getenv("SCUZZ_REVIEW_SAME")
    context <- Sys.getenv("SCUZZ_REVIEW_CONTEXT")
    toml <- Fs.read(Fs.join(root, "scuzz.toml"))
    baseline <- Fs.read(Fs.join(root, "src/Main.scuzz"))
    candidate = if (same == "1") baseline else Str.replace(baseline, "one", "two")
    _ <- Diff.reviewSets(root, toml, [("Main", baseline)], [("Main", candidate)], 1, 32, cli, context)
  } yield ()
""")
env = dict(os.environ, SCUZZ_REVIEW_TARGET=str(target), SCUZZ_REVIEW_CLI=str(cli))
def review_context():
    files = [(str(p.relative_to(target)), p.read_text()) for p in sorted(target.rglob("*")) if p.is_file() and "build" not in p.relative_to(target).parts]
    return hashlib.sha256(json.dumps([hashlib.sha256(cli.read_bytes()).hexdigest(), files], separators=(',', ':')).encode()).hexdigest()
env["SCUZZ_REVIEW_CONTEXT"] = review_context()
def run(*args, **kwargs):
    return subprocess.run([str(cli), *map(str, args)], env=env, check=True, timeout=180, stdout=subprocess.DEVNULL, **kwargs)
run("fmt", target)
env["SCUZZ_REVIEW_CONTEXT"] = review_context()
run("run", proof)
exe = proof / "build/review-proof"
def review():
    env["SCUZZ_REVIEW_CONTEXT"] = review_context()
    subprocess.run([str(exe)], env=env, check=True, timeout=180, stdout=subprocess.DEVNULL)
    report = json.loads((target / "build/ide/report.json").read_text())
    assert report["review"]["complete"] and report["search"]["ran"] == 32
    assert all(w["why_b"] is None for w in report["workloads"])
    return report
report = json.loads((target / "build/ide/report.json").read_text())
assert next(w for w in report["workloads"] if w["label"].startswith("idle"))["class"] == "same"
assert next(w for w in report["workloads"] if w["label"].startswith("corpus/"))["class"] == "diverged"
shutil.rmtree(target / "build/ide/baseline")
cold = review()
assert cold["review"]["baseline_probes"] > 0, cold["review"]
warm = review()
assert warm["review"]["baseline_probes"] == 0 and warm["review"]["baseline_reused"] >= warm["review"]["required"] + 32, warm["review"]
assert warm["workloads"] == cold["workloads"] and warm["witnesses"] == cold["witnesses"]
assert warm["review"]["reached_a"] == cold["review"]["reached_a"]
cache_files = list((target / "build/ide/baseline").glob("*/*.json"))
assert cache_files
for path in cache_files:
    path.write_text("interrupted cache write")
recovered = review()
assert recovered["review"]["baseline_probes"] > 0 and recovered["workloads"] == cold["workloads"]
print("IO baseline reuse: exact workloads and witnesses; damaged cache requires fresh probes")
corpus.unlink()
report = review()
assert report["review"]["corpus"] == report["review"]["seeds"] == 0
assert next(w for w in report["workloads"] if w["label"].startswith("idle"))["class"] == "same"
assert report["search"]["divergent"] > 0 and report["witnesses"]
question = min((w for w in report["workloads"] if w["label"].startswith("witness ")), key=lambda w: len(w["events"]))
assert len(question["events"]) == 1
witness = Path(report["witnesses"][0]["path"])
# Compile and replay both sides of the same retained witness.
for side, source in (("a", baseline), ("b", baseline.replace("one", "two"))):
    (target / "src/Main.scuzz").write_text(source)
    run("fuzz", "--replay", witness, target)
    timeline = proof / ("compiled-" + side + ".txt")
    probe_env = dict(env, SCUZZ_TESTRT="1", SCUZZ_SERVE="1", SCUZZ_KIT="sealed", SCUZZ_SCHED_SEED="0", SCUZZ_DRIVE_SCRIPT=str(target / "build/fuzz/drive.json"), SCUZZ_TIMELINE_DUMP=str(timeline))
    subprocess.run([str(target / "build/review-io")], env=probe_env, check=True, timeout=20, stdout=subprocess.DEVNULL)
    expected = (target / "build/ide" / question["timelines"][side]).read_text()
    actual = timeline.read_text()
    assert actual == expected, "".join(difflib.unified_diff(expected.splitlines(True), actual.splitlines(True)))
(target / "src/Main.scuzz").write_text(baseline)
env["SCUZZ_REVIEW_SAME"] = "1"
report = review()
assert all(w["class"] == "same" for w in report["workloads"])
print("IO review: corpus, search, shrinking, compiled parity, and complete no-difference budget")
PYREVIEWIO
  python3 - <<'PYLSP'
import json, os, select, subprocess, time
from pathlib import Path
root = (Path.cwd() / "scratchpad/review").resolve()
proc = subprocess.Popen([os.environ["SCUZZ"], "lsp"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
buffer = bytearray()
def frame(message):
    body = json.dumps(message, ensure_ascii=False, separators=(',', ':')).encode()
    return b'Content-Length: ' + str(len(body)).encode() + b'\r\n\r\n' + body
def result(want):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        cut = buffer.find(b'\r\n\r\n')
        width = 4
        if cut < 0:
            cut = buffer.find(b'\n\n')
            width = 2
        if cut >= 0:
            size = int(bytes(buffer[:cut]).split(b':', 1)[1])
            end = cut + width + size
            if len(buffer) >= end:
                message = json.loads(bytes(buffer[cut + width:end]))
                del buffer[:end]
                if message.get('id') == want:
                    return message
                continue
        assert proc.poll() is None, "LSP exits before its result"
        if select.select([proc.stdout], [], [], max(0, deadline - time.monotonic()))[0]:
            buffer.extend(os.read(proc.stdout.fileno(), 8192))
    raise AssertionError("LSP frame deadline")
try:
    initial = frame({'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {}})
    assert len(initial) < 256
    proc.stdin.write(initial[:8])
    proc.stdin.flush()
    proc.stdin.write(initial[8:])
    proc.stdin.flush()
    assert 'capabilities' in result(1)['result']
    uri = (root / 'src/Main.scuzz').as_uri()
    source = (root / 'src/Main.scuzz').read_text().replace('"Counter"', '"Café"', 1)
    opened = frame({'jsonrpc': '2.0', 'method': 'textDocument/didOpen', 'params': {'textDocument': {'uri': uri, 'languageId': 'scuzz', 'version': 1, 'text': source}}})
    tokens = frame({'jsonrpc': '2.0', 'id': 2, 'method': 'textDocument/semanticTokens/full', 'params': {'textDocument': {'uri': uri}}})
    again = frame({'jsonrpc': '2.0', 'id': 3, 'method': 'textDocument/semanticTokens/full', 'params': {'textDocument': {'uri': uri}}})
    proc.stdin.write(opened + tokens + again)
    proc.stdin.flush()
    data = result(2)['result']['data']
    assert data and result(3)['result']['data'] == data
finally:
    proc.terminate()
    proc.wait(timeout=10)
PYLSP
  python3 - <<'PYUILIFECYCLE'
import json, os, pathlib, shutil, subprocess
root = pathlib.Path.cwd() / "scratchpad/ui-lifecycle"
shutil.rmtree(root, ignore_errors=True)
(root / "src").mkdir(parents=True)
(root / "corpus").mkdir()
(root / "scuzz.toml").write_text('[package]\nname="ui-lifecycle"\n[ui]\ndefault_runtime="headless"\n')
(root / "src/Main.scuzz").write_text('''@main def main: IO[Unit] =
  for {
    active = Signal.make(1)
    seen = Signal.make(0)
    catalog = Signal.make([Json.Obj([("id", Json.Str("one")), ("size", Json.Int(5680522464)), ("cached", Json.Bool(false)), ("profiles", Json.Arr([Json.Null(), Json.Str("cpu")]))])])
    _ <- IO.ensure(Ui.run(_ => View.column(View.bindText(Signal.map(active, n => Str.fromInt(n))), View.each(catalog, m => View.text(Json.getStr(m, "id", ""))), View.button("Observe", _ => IO.pure(()).map(_ => for {
  local = Signal.make(1)
  derived = Signal.map(local, n => n + 1)
  _ = Signal.set(seen, Signal.get(active) + Signal.get(derived) - 2)
} yield ())))), IO.pure(()).map(_ => Signal.set(active, 0)))
  } yield ()
''')
(root / "lifecycle.scuzz_verify").write_text('''def activeAtInput(t: Timeline): Verdict =
  Verdict.onHit(t, "button:Observe", (a, b) => Timeline.signalInt(t, b, "active") == 1)

def observeCompletes(t: Timeline): Verdict =
  if (!Timeline.exists(t, i => Timeline.hit(t, i, "button:Observe"))) Verdict.ok() else if (Timeline.signalInt(t, Timeline.len(t) - 1, "seen") == 1 && Timeline.signalInt(t, Timeline.len(t) - 1, "local") == 1 && Timeline.signalInt(t, Timeline.len(t) - 1, "derived") == 2 && Timeline.signalInt(t, Timeline.len(t) - 1, "active") == 1) Verdict.ok() else Verdict.fail(Timeline.len(t) - 1, "callback signals complete during the active session")
''')
(root / "corpus/observe.toml").write_text('[fuzz]\nevents=["tap button:Observe"]\n')
result = subprocess.run([os.environ["SCUZZ"], "fuzz", "--iterations", "0", str(root)], capture_output=True, text=True, timeout=240)
assert result.returncode == 0, result.stdout + result.stderr
assert "probes run compiled" not in result.stdout + result.stderr, result.stdout + result.stderr
summary = json.loads((root / "build/fuzz/summary.json").read_text())
assert summary["fuzz"]["ok"] and summary["corpus"]["entries"] == 1 and summary["corpus"]["failures"] == 0
print("UI lifecycle: evaluator and compiled JSON snapshots match; active state and callback signal cleanup pass")
PYUILIFECYCLE
  # Fixed catalog inspection and bounded private transport use no weights.
  python3 - <<'PYGENERATION'
import hashlib, json, os, pathlib, shutil, signal, subprocess, time
repo = pathlib.Path.cwd()
cli = pathlib.Path(os.environ["SCUZZ"])
root = repo / "scratchpad/generation-contract"
shutil.rmtree(root, ignore_errors=True)
(root / "src").mkdir(parents=True)
cache = root / "scuzz-models"
cache.mkdir()
env = dict(os.environ, SCUZZ_MODEL_CACHE=str(cache))
listed = subprocess.run([str(cli), "models", "list", "--message-format=json"], env=env, check=True, capture_output=True, text=True, timeout=30)
models = json.loads(listed.stdout)["models"]
assert [m["id"] for m in models] == ["qwen3.5-4b", "qwen3.5-9b"]
assert all(m["status"] == "unavailable" for m in models)
assert not list(cache.iterdir())
no_tools = dict(env, PATH="")
for key in ("SCUZZ_MODEL_CACHE", "XDG_CACHE_HOME", "HOME"):
 no_tools.pop(key, None)
for locations, expected in (({"SCUZZ_MODEL_CACHE": str(cache), "XDG_CACHE_HOME": str(root / "xdg"), "HOME": str(root / "home")}, cache), ({"XDG_CACHE_HOME": str(root / "xdg"), "HOME": str(root / "home")}, root / "xdg/scuzz/models"), ({"HOME": str(root / "home")}, root / "home/.cache/scuzz/models")):
 offline = subprocess.run([str(cli), "models", "list", "--message-format=json"], env=dict(no_tools, **locations), capture_output=True, text=True, check=True, timeout=30)
 rows = json.loads(offline.stdout)["models"]
 assert [m["id"] for m in rows] == ["qwen3.5-4b", "qwen3.5-9b"]
 assert all(m["cache"] == str(expected) and m["status"] == "unavailable" for m in rows)
 assert not list(cache.iterdir())
missing_cache = subprocess.run([str(cli), "models", "list", "--message-format=json"], env=no_tools, capture_output=True, text=True, timeout=30)
assert missing_cache.returncode != 0 and not missing_cache.stdout and "user cache location is unavailable" in missing_cache.stderr
default = subprocess.run([str(cli), "models", "list", "--message-format=json"], env=dict(no_tools, HOME=str(root / "home")), capture_output=True, text=True, check=True, timeout=30)
assert all(m["cache"] == str(root / "home/.cache/scuzz/models") for m in json.loads(default.stdout)["models"])
partial = pathlib.Path(models[0]["path"])
partial.parent.mkdir(parents=True)
partial.write_bytes(b"incomplete")
again = subprocess.run([str(cli), "models", "list", "--message-format=json"], env=env, check=True, capture_output=True, text=True, timeout=30)
assert all(m["status"] == "unavailable" for m in json.loads(again.stdout)["models"])
assert partial.read_bytes() == b"incomplete"
unknown = subprocess.run([str(cli), "models", "download", "unlisted-model"], env=env, capture_output=True, timeout=30)
assert unknown.returncode != 0
(root / "scuzz.toml").write_text('[package]\nname="generation-contract"\nversion="0.1.0"\n[dependencies]\ngeneration={path="../../examples/editor/generation"}\n')
server = root / "server.py"
server.write_text(r'''import http.server,json,os,pathlib,sys,time
key,port,mode,pidfile=sys.argv[1:]
pathlib.Path(pidfile).write_text(str(os.getpid()))
(pathlib.Path(pidfile).parent.parent / "pid").write_text(str(os.getpid()))
class Handler(http.server.BaseHTTPRequestHandler):
 def log_message(self,*args): pass
 def reply(self,value):
  body=json.dumps(value).encode();self.send_response(200);self.send_header("Content-Length",str(len(body)));self.end_headers();self.wfile.write(body)
 def do_GET(self):
  assert self.headers["Authorization"]=="Bearer "+key
  self.reply({"data":[{"id":key}]})
 def do_POST(self):
  assert self.headers["Authorization"]=="Bearer "+key
  assert self.headers["Prefer"]=="wait=900"
  body=json.loads(self.rfile.read(int(self.headers["Content-Length"])))
  if self.path=="/apply-template": self.reply({"prompt":"controlled prompt"});return
  if self.path=="/tokenize": self.reply({"tokens":list(range(3073 if mode=="context" else 10))});return
  assert body["stream"] is False and body["max_tokens"]==1024 and body["reasoning_effort"]=="none"
  schema=body["response_format"]["json_schema"]["schema"]
  assert schema["additionalProperties"] is False and schema["properties"]["edits"]["maxItems"]==3
  req=json.JSONDecoder().raw_decode(body["messages"][1]["content"])[0]
  reply={"change":"Print two.","edits":[{"path":"src/Main.scuzz","search":req["sources"][0]["content"].strip(),"replacement":"@main def main: IO[Unit] = IO.println(2)"}]}
  if mode=="stale": reply["request"]="stale"
  if mode=="timeout": time.sleep(5)
  if mode=="interrupt": time.sleep(120)
  self.reply({"choices":[{"finish_reason":"length" if mode=="truncated" else "stop","message":{"content":"<think>reasoning" if mode=="reasoning" else json.dumps(reply)}}],"usage":{},"timings":{}})
http.server.HTTPServer(("127.0.0.1",int(port)),Handler).serve_forever()
''')
(root / "src/Main.scuzz").write_text('''@main def main: IO[Unit] =
  for {
    mode <- Sys.getenv("GENERATION_PROOF_MODE")
    _ <- if (mode == "request") exportRequest() else transport()
  } yield ()

def exportRequest(): IO[Unit] =
  for {
    root <- Sys.getenv("GENERATION_PROOF_ROOT")
    compiler <- Sys.getenv("COMMAND_PROOF_COMPILER")
    request <- Proposal.make(root, "Check finite command refusals", ["src/Main.scuzz"], compiler, [])
    _ <- Fs.mkdirs(Fs.join(root, "build"))
    _ <- Fs.write(Fs.join(root, "build/command-request.json"), Proposal.text(request))
  } yield ()

def transport(): IO[Unit] =
  for {
    root <- Sys.getenv("GENERATION_PROOF_ROOT")
    mode <- Sys.getenv("GENERATION_PROOF_MODE")
    id <- Sys.getenv("GENERATION_PROOF_MODEL")
    port <- Random.nextInt(20000)
    key <- Uuid.v4()
    dir = Fs.join(root, "owned")
    _ <- Fs.mkdirs(dir)
    exited <- Ref.of(false)
    script = Fs.join(root, "server.py")
    command = List.join(["exec python3", Models.quote(script), Models.quote(key), Str.fromInt(port + 30000), Models.quote(mode), Models.quote(Fs.join(dir, "pid"))], " ")
    job <- Fiber.fork(IO.ensure(Sys.exec(command).map(_ => ()), Ref.set(exited, true)))
    process = Generate.Process(job, exited, Str.concat("http://127.0.0.1:", Str.fromInt(port + 30000)), key, dir, "cpu")
    model <- Models.select(id)
    request = Json.Obj([("request", Json.Str("controlled")), ("baseline", Json.Str(Hash.sha256("baseline"))), ("kind", Json.Str("behavior")), ("allowed", Json.Arr([Json.Str("src/Main.scuzz")])), ("sources", Proposal.pairsJson([("src/Main.scuzz", "@main def main: IO[Unit] = IO.println(1)")]))])
    result <- IO.ensure(IO.attempt(IO.timeout(if (mode == "timeout") 500 else 5000, Generate.ready(process).flatMap(_ => Generate.complete(process, request, model, _ => IO.pure(()))))), Generate.stop(process))
    _ <- result match {
      case Result.Ok(_) => Models.need(mode == "ok", "invalid controlled reply passed")
      case Result.Err(e) => if (mode == "ok") IO.fail(e) else IO.pure(())
    }
    _ <- IO.println("controlled transport ok")
  } yield ()
''')
# Build once. Each native run owns a fresh process and private key.
subprocess.run([str(cli), "run", str(root)], env=dict(env, GENERATION_PROOF_ROOT=str(root), GENERATION_PROOF_MODE="ok", GENERATION_PROOF_MODEL="qwen3.5-4b"), check=True, timeout=240, stdout=subprocess.DEVNULL)
exe = root / "build/generation-contract"
subprocess.run([str(exe)], env=dict(env, GENERATION_PROOF_ROOT=str(root), GENERATION_PROOF_MODE="request", COMMAND_PROOF_COMPILER=hashlib.sha256(cli.read_bytes()).hexdigest()), check=True, timeout=30)
request = json.loads((root / "build/command-request.json").read_text())
source = (root / "src/Main.scuzz").read_bytes()
existing = root / "build/proposals/existing"
existing.mkdir(parents=True)
(existing / "preserve").write_text("existing publication")
cases = []
for name, key, value in (("version", "v", 2), ("baseline", "baseline", "0" * 64), ("compiler", "compiler", "0" * 64), ("scope", "allowed", ["../Main.scuzz"]), ("limits", "limits", {}), ("feedback", "feedback", {}), ("unknown-field", "extra", 1)):
 changed = dict(request)
 changed[key] = value
 cases.append((name, changed, "qwen3.5-4b", root / "build/proposals" / name))
cases.extend((("unknown-model", request, "unlisted", root / "build/proposals/unknown-model"), ("existing", request, "qwen3.5-4b", existing), ("source-output", request, "qwen3.5-4b", root / "src/forbidden"), ("build-output", request, "qwen3.5-4b", root / "build/forbidden"), ("unavailable", request, "qwen3.5-4b", root / "build/proposals/unavailable")))
for name, body, model, out in cases:
 file = root / "build" / (name + ".json")
 file.write_text(json.dumps(body))
 refused = subprocess.run([str(cli), "ide", "generate-suggestion", str(root), "--request", str(file), "--model", model, "--out", str(out)], env=env, capture_output=True, timeout=30)
 assert refused.returncode != 0 and not refused.stdout and refused.stderr, (name, refused.stdout, refused.stderr)
 assert b"Loading model" not in refused.stderr
 assert (root / "src/Main.scuzz").read_bytes() == source
 assert (existing / "preserve").read_text() == "existing publication"
 if name != "existing": assert not out.exists(), name
def alive(pid):
 try:
  os.kill(pid,0)
  state = pathlib.Path("/proc") / str(pid) / "stat"
  return not (state.exists() and state.read_text().rsplit(")",1)[1].split()[0] == "Z")
 except (ProcessLookupError,FileNotFoundError): return False
unrelated = subprocess.Popen(["sleep", "120"])
try:
 for model in ("qwen3.5-4b", "qwen3.5-9b"):
  for sig in (signal.SIGINT, signal.SIGTERM):
   (root / "pid").unlink(missing_ok=True)
   runenv = dict(env, GENERATION_PROOF_ROOT=str(root), GENERATION_PROOF_MODE="interrupt", GENERATION_PROOF_MODEL=model)
   child = subprocess.Popen([str(exe)], env=runenv, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
   try:
    deadline = time.monotonic()+5
    while not (root / "pid").exists() and time.monotonic()<deadline:
     assert child.poll() is None
     time.sleep(.005)
    pid = int((root / "pid").read_text())
    assert alive(pid)
    child.send_signal(sig)
    assert child.wait(timeout=5) == 128 + sig
    deadline = time.monotonic()+2
    while alive(pid) and time.monotonic()<deadline: time.sleep(.02)
    assert not alive(pid), (model,sig,pid)
    assert not (root / "owned").exists() and unrelated.poll() is None
   finally:
    if child.poll() is None:
     child.kill();child.wait(timeout=5)
 for model in ("qwen3.5-4b", "qwen3.5-9b"):
  for mode in ("ok", "stale", "truncated", "reasoning", "context", "timeout"):
   (root / "pid").unlink(missing_ok=True)
   runenv = dict(env, GENERATION_PROOF_ROOT=str(root), GENERATION_PROOF_MODE=mode, GENERATION_PROOF_MODEL=model)
   subprocess.run([str(exe)], env=runenv, check=True, timeout=15, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
   pid = int((root / "pid").read_text())
   deadline = time.monotonic()+2
   while alive(pid) and time.monotonic()<deadline: time.sleep(.02)
   assert not alive(pid), (model,mode,pid)
   assert not (root / "owned").exists()
   assert unrelated.poll() is None
 assert partial.read_bytes() == b"incomplete" and unrelated.poll() is None
finally:
 unrelated.terminate();unrelated.wait(timeout=5)
print("Generation: offline catalog, finite command refusals, both controlled profiles, strict schema, context, timeout, signals, private HTTP, and owned cleanup")
PYGENERATION
  # Controlled artifacts exercise the shared finite command without weights.
  python3 - <<'PYGENCOMMAND'
import hashlib, io, json, os, pathlib, platform, re, shlex, shutil, signal, subprocess, tarfile, tempfile, time

repo = pathlib.Path.cwd()
cli = pathlib.Path(os.environ["SCUZZ"])
root = pathlib.Path(tempfile.mkdtemp(prefix='scuzz-gate3-command-live-')).resolve()
generation = root / 'generation'
shutil.copytree(repo / 'examples/editor/generation', generation, ignore=shutil.ignore_patterns('build'))
(generation / 'scuzz.toml').write_text('[package]\nname="generation"\n[dependencies]\ncompiler={path="' + os.path.relpath(repo / 'examples/compiler', generation) + '"}\n')
server = b'''#!/usr/bin/env python3
import http.server,json,os,pathlib,sys,time
if '--version' in sys.argv:
 print('build 11146, commit 7fe450e19');sys.exit(0)
if '--list-devices' in sys.argv:
 print('Available devices:\\n  Vulkan0: Controlled GPU');sys.exit(0)
def arg(name): return sys.argv[sys.argv.index(name)+1]
assert arg('--host')=='127.0.0.1' and arg('--parallel')=='1' and arg('--ctx-size')=='4096'
assert arg('--device')=='Vulkan0' and arg('--n-gpu-layers')=='auto'
key=arg('--api-key')
pathlib.Path(os.environ['CONTROLLED_PID']).write_text(str(os.getpid()))
class Handler(http.server.BaseHTTPRequestHandler):
 def log_message(self,*args): pass
 def reply(self,value):
  data=json.dumps(value).encode();self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
 def do_GET(self):
  assert self.headers['Authorization']=='Bearer '+key
  self.reply({'data':[{'id':key}]})
 def do_POST(self):
  assert self.headers['Authorization']=='Bearer '+key and self.headers['Prefer']=='wait=900'
  body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
  if self.path=='/apply-template': self.reply({'prompt':'controlled prompt'});return
  if self.path=='/tokenize': self.reply({'tokens':[1,2,3]});return
  assert body['model']==key and not body['stream'] and body['max_tokens']==1024
  schema=body['response_format']['json_schema']['schema']
  request=json.JSONDecoder().raw_decode(body['messages'][1]['content'])[0]
  proposal={'change':'Print two.','edits':[{'path':'src/Main.scuzz','search':request['sources'][0]['content'].strip(),'replacement':'@main def main: IO[Unit] = IO.println(2)'}]}
  pathlib.Path(os.environ['CONTROLLED_RECEIVED']).write_text('ready')
  if os.environ.get('CONTROLLED_ACTION')=='hang': time.sleep(120)
  self.reply({'choices':[{'finish_reason':'stop','message':{'content':json.dumps(proposal)}}]})
http.server.HTTPServer(('127.0.0.1',int(arg('--port'))),Handler).serve_forever()
'''
archive = io.BytesIO()
with tarfile.open(fileobj=archive, mode='w:gz') as tar:
 entry = tarfile.TarInfo('llama-b11146/llama-server')
 entry.size = len(server)
 entry.mode = 0o700
 tar.addfile(entry, io.BytesIO(server))
artifact = archive.getvalue()
weights = b'controlled weights'
models = generation / 'src/Models.scuzz'
source = models.read_text()
for count, digest in ((2740937888, '00fe7986ff5f6b463e62455821146049db6f9313603938a70800d1fb69ef11a4'), (5680522464, '03b74727a860a56338e042c4420bb3f04b2fec5734175f4cb9fa853daf52b7e8')):
 source = source.replace(str(count), str(len(weights))).replace(digest, hashlib.sha256(weights).hexdigest())
source = re.sub(r'Backend\("([^"]+)", "([^"]+)", [0-9]+, "[a-f0-9]+"\)', lambda m: 'Backend("' + m[1] + '", "' + m[2] + '", ' + str(len(artifact)) + ', "' + hashlib.sha256(artifact).hexdigest() + '")', source)
models.write_text(source)
cache = root / 'cache'
backend_platform = platform.system() + '-' + platform.machine()
if backend_platform in ('Linux-x86_64', 'Linux-aarch64'): backend_platform += '-vulkan'
backend = cache / 'scuzz/tools/llama/b11146' / backend_platform
backend.mkdir(parents=True)
(backend / 'release.tar.gz').write_bytes(artifact)
model_cache = root / 'models-cache'
for model_id, revision, filename in [('qwen3.5-4b','e87f176479d0855a907a41277aca2f8ee7a09523','Qwen3.5-4B-Q4_K_M.gguf'),('qwen3.5-9b','3885219b6810b007914f3a7950a8d1b469d598a5','Qwen3.5-9B-Q4_K_M.gguf')]:
 path = model_cache / model_id / revision / filename
 path.parent.mkdir(parents=True)
 path.write_bytes(weights)
(root / 'src').mkdir()
(root / 'scuzz.toml').write_text('[package]\nname="controlled-command"\n[dependencies]\ngeneration={path="generation"}\n')
(root / 'src/Main.scuzz').write_text('''@main def main: IO[Unit] =
  for {
    root <- Sys.getenv("CONTROLLED_ROOT")
    model <- Sys.getenv("CONTROLLED_MODEL")
    compiler <- Sys.getenv("SCUZZ_EXECUTABLE_SHA256")
    deadline <- Sys.getenv("CONTROLLED_TIMEOUT")
    request <- Proposal.make(root, "Print two", ["src/Main.scuzz"], compiler, [])
    file = Fs.join(root, "request.json")
    _ <- Fs.write(file, Proposal.text(request))
    _ <- if (model == "") IO.pure(()) else if (deadline == "yes") IO.timeout(3000, Generate.command(root, file, model, Fs.join(root, "candidate"), false)) else Generate.command(root, file, model, Fs.join(root, "candidate"), false)
  } yield ()
''')
target = root / 'target'
(target / 'src').mkdir(parents=True)
(target / 'scuzz.toml').write_text('[package]\nname="target"\n')
original = b'@main def main: IO[Unit] = IO.println(1)'
(target / 'src/Main.scuzz').write_bytes(original)
tools = root / 'tools'
tools.mkdir()
real_awk = shutil.which('awk')
assert real_awk
(tools / 'awk').write_text('#!/bin/sh\ncase "$1" in *MemAvailable:*) printf \'34359738368\\n\';; *"Pages free:"*) printf \'34359738368\\n\';; *) exec ' + shlex.quote(real_awk) + ' "$@";; esac\n')
(tools / 'awk').chmod(0o700)
env = dict(os.environ, PATH=str(tools)+os.pathsep+os.environ['PATH'], XDG_CACHE_HOME=str(cache), SCUZZ_MODEL_CACHE=str(model_cache), CONTROLLED_ROOT=str(target), CONTROLLED_PID=str(root/'pid'), CONTROLLED_RECEIVED=str(root/'received'), CONTROLLED_MODEL='qwen3.5-4b')
subprocess.run([str(cli), 'run', str(root)], env=dict(env, CONTROLLED_MODEL=''), check=True, timeout=240)
exe = root / 'build/controlled-command'
unrelated = subprocess.Popen(['sleep','120'])
try:
 for model in ('qwen3.5-4b','qwen3.5-9b'):
  result = subprocess.run([str(exe)], env=dict(env, CONTROLLED_MODEL=model), check=True, capture_output=True, text=True, timeout=30)
  output = json.loads(result.stdout)
  assert output['result']=='published' and output['model']==model and output['behavioral_review']=='pending' and not output['accepted']
  candidate = target / 'candidate'
  metadata = json.loads((candidate / 'proposal.json').read_text())
  content = (candidate / 'src/Main.scuzz').read_bytes()
  assert metadata['request']==output['request'] and metadata['baseline']==output['baseline'] and metadata['files'][0]['sha256']==hashlib.sha256(content).hexdigest()
  assert (target / 'src/Main.scuzz').read_bytes()==original and content!=original
  pid = int((root / 'pid').read_text())
  try: os.kill(pid,0)
  except ProcessLookupError: pass
  else: raise AssertionError(('owned backend remains',pid))
  assert unrelated.poll() is None and not list((cache/'scuzz/requests').iterdir())
  print('Controlled finite command:',model,'structured publication, no source writes, pending review, owned cleanup',flush=True)
  shutil.rmtree(candidate)
 for model in ('qwen3.5-4b','qwen3.5-9b'):
  for sig in (signal.SIGINT, signal.SIGTERM):
   (root / 'received').unlink(missing_ok=True)
   child = subprocess.Popen([str(exe)], env=dict(env, CONTROLLED_MODEL=model, CONTROLLED_ACTION='hang'), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
   try:
    deadline = time.monotonic()+10
    while not (root / 'received').exists() and time.monotonic()<deadline:
     assert child.poll() is None
     time.sleep(.01)
    assert (root / 'received').exists()
    pid = int((root / 'pid').read_text())
    child.send_signal(sig)
    stdout, stderr = child.communicate(timeout=5)
    assert child.returncode == 128 + sig and not stdout, (model,sig,child.returncode,stderr)
   finally:
    if child.poll() is None:
     child.kill();child.wait(timeout=5)
   try: os.kill(pid,0)
   except ProcessLookupError: pass
   else: raise AssertionError(('interrupted backend remains',pid))
   assert not (target / 'candidate').exists() and (target / 'src/Main.scuzz').read_bytes()==original
   assert not list((cache/'scuzz/requests').iterdir()) and unrelated.poll() is None
  (root / 'received').unlink(missing_ok=True)
  timed = subprocess.run([str(exe)], env=dict(env, CONTROLLED_MODEL=model, CONTROLLED_ACTION='hang', CONTROLLED_TIMEOUT='yes'), capture_output=True, text=True, timeout=10)
  assert timed.returncode != 0 and not timed.stdout and 'timeout' in timed.stderr and (root / 'received').exists(), (model,timed.returncode,timed.stderr)
  pid = int((root / 'pid').read_text())
  try: os.kill(pid,0)
  except ProcessLookupError: pass
  else: raise AssertionError(('timed-out backend remains',pid))
  assert not (target / 'candidate').exists() and (target / 'src/Main.scuzz').read_bytes()==original
  assert not list((cache/'scuzz/requests').iterdir()) and unrelated.poll() is None
  assert (backend/'release.tar.gz').read_bytes()==artifact
  assert not list(root.glob('.scuzz-publish-*'))
  print('Controlled finite command:',model,'interruptions and timeout preserve source and cache, remove incomplete output, and stop only owned work',flush=True)
finally:
 unrelated.terminate();unrelated.wait(timeout=5)
PYGENCOMMAND
  # Live choices select the displayed lane and retain complete local evidence.
  python3 - <<'PYDECK'
import hashlib, json, os, subprocess, time
from pathlib import Path
repo = Path.cwd()
root = (repo / "scratchpad/review").resolve()
src = (root / "src/Main.scuzz").read_text()
prop = src.replace('"Counter"', '"Counter app"', 1)
d = root / "build/proposals/p1"
(d / "src").mkdir(parents=True, exist_ok=True)
(d / "src/Main.scuzz").write_text(prop)
session_dir = root / ".scuzz/ide"
session_dir.mkdir(parents=True, exist_ok=True)
(session_dir / "session.json").write_text(json.dumps({"v": 1, "objective": "Review behavior", "allowed": ["src/Main.scuzz", "src/Other.scuzz"], "mode": "external", "model": "qwen3.5-4b", "requests": 0}))
records = [{"path": "src/" + p.name, "content": p.read_text()} for p in sorted((root / "src").glob("*.scuzz"))]
baseline = hashlib.sha256(json.dumps(records, ensure_ascii=False, separators=(',', ':')).encode()).hexdigest()

ops = {"v": 1, "kind": "inject", "events": [{"op": "tap", "id": "button:Start"}]}
path = root / "ops-deck.json"
path.write_text(json.dumps(ops))
env = dict(os.environ, SCUZZ_HOME=str(repo), SCUZZ_UI_RUNTIME="headless", SCUZZ_UI_WIDTH="960", SCUZZ_UI_HEIGHT="560", SCUZZ_UI_SERVE="1", SCUZZ_LIVE_FRAMES="15000", SCUZZ_UI_SCRIPT=str(path), SCUZZ_UI_INJECT=str(path), SCUZZ_UI_DEBUG_DUMP=str(root / "deck-debug.json"))
with open(root / "deck-live.log", "w") as log:
    proc = subprocess.Popen([str(repo / "examples/editor/build/editor")], cwd=root, env=env, stdout=log, stderr=log)
    def await_state(predicate):
        deadline = time.monotonic() + 180
        while time.monotonic() < deadline:
            assert proc.poll() is None, (root / "deck-live.log").read_text()[-3000:]
            try:
                result = predicate()
            except (FileNotFoundError, json.JSONDecodeError):
                result = False
            if result:
                return result
            time.sleep(0.05)
        raise AssertionError("live lane choice deadline")
    def ready():
        ui = json.loads((root / "deck-debug.json").read_text())
        return any(v.get("name") == "deckArmed" and v.get("value") for v in ui["signals"]) and any(v.get("name") == "deckBusy" and v.get("value") == 0 for v in ui["signals"]) and "Choose left" in json.dumps(ui.get("taps", []))
    def inject(events):
        temp = root / "ops-deck.tmp"
        temp.write_text(json.dumps({"v": 1, "kind": "inject", "events": events}))
        temp.replace(path)
    try:
        await_state(lambda: (root / "build/ide/request.json").is_file())
        request = json.loads((root / "build/ide/request.json").read_text())
        assert request["objective"] == "Review behavior"
        (d / "proposal.json").write_text(json.dumps({"v": 1, "request": request["request"], "baseline": request["baseline"], "kind": "behavior", "generator": "manual", "files": [{"path": "src/Main.scuzz", "sha256": hashlib.sha256(prop.encode()).hexdigest()}]}))
        inject([{"op": "tap", "id": "choicechip:Review"}, {"op": "tap", "id": "choicechip:More evidence"}, {"op": "tap", "id": "outlined:Proposals"}])
        await_state(ready)
        inject([{"op": "tap", "id": "button:Choose left"}])
        await_state(lambda: len(list((root / ".scuzz/ide/records").glob("*.json"))) == 1)
        inject([{"op": "quit"}])
        assert proc.wait(timeout=15) == 0
    finally:
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=15)
PYDECK
  python3 - <<'PYDECK'
import json
from pathlib import Path
root = Path("scratchpad/review")
records = list((root / ".scuzz/ide/records").glob("*.json"))
assert len(records) == 1, records
d = json.loads(records[0].read_text())
c = json.loads((root / ".scuzz/ide/cards" / d["card"] / "card.json").read_text())
assert d["proposal"] == "p1" and d["lane"] == 0, d
accepted = d["flip"] == 1
assert d["decision"] == ("accept" if accepted else "baseline"), d
expected = {f["path"]: f["content"] for f in c["proposed"] if accepted} if accepted else {f["path"]: f["content"] for f in c["baseline"]}
assert (root / "src/Main.scuzz").read_text() == expected["src/Main.scuzz"]
assert len(c["a"]) == 64 and len(c["b"]) == 64 and c["a"] != c["b"], c
assert c["status"] == "ready" and c["evidence"]["workloads"], c
for w in c["evidence"]["workloads"]:
    for side in ["a", "b"]:
        assert (root / ".scuzz/ide/cards" / c["id"] / w["timelines"][side]).is_file()
assert not (root / "build/ide/decisions.jsonl").exists()
PYDECK
  # Replay a retained UI witness on both compiled source sets.
  python3 - <<'PYREVIEWUI'
import difflib, json, os, shutil, subprocess, tomllib
from pathlib import Path
repo = Path.cwd()
cli = Path(os.environ["SCUZZ"])
root = repo / "scratchpad/review"
record = json.loads(next((root / ".scuzz/ide/records").glob("*.json")).read_text())
card_dir = root / ".scuzz/ide/cards" / record["card"]
card = json.loads((card_dir / "card.json").read_text())
assert card["evidence"]["witnesses"]
witness = card_dir / card["evidence"]["witnesses"][0]["path"]
script = tomllib.loads(witness.read_text())["fuzz"]
question = next(w for w in card["evidence"]["workloads"] if w["label"].startswith("witness ") and w["events"] == script.get("events", []))
target = repo / "scratchpad/review-ui-parity"
shutil.rmtree(target, ignore_errors=True)
shutil.copytree(repo / "examples/counter", target, ignore=shutil.ignore_patterns("build", ".scuzz"))
env = dict(os.environ, SCUZZ_HOME=str(repo))
for side, files in (("a", card["baseline"]), ("b", card["proposed"])):
    for item in card["baseline"]:
        (target / item["path"]).write_text(item["content"])
    for item in files:
        (target / item["path"]).write_text(item["content"])
    subprocess.run([str(cli), "fuzz", "--replay", str(witness), str(target)], env=env, check=True, timeout=180, stdout=subprocess.DEVNULL)
    timeline = target / ("compiled-" + side + ".txt")
    probe_env = dict(env, SCUZZ_TESTRT="1", SCUZZ_SERVE="1", SCUZZ_KIT="sealed", SCUZZ_SCHED_SEED=script.get("schedule_seed", "0"), SCUZZ_UI_RUNTIME="headless", SCUZZ_UI_SCRIPT=str(target / "build/fuzz/drive.json"), SCUZZ_FUZZ_DUMP=str(target / "compiled-dump.json"), SCUZZ_TIMELINE_DUMP=str(timeline))
    probe_env["SCUZZ_SCHED_PICKS"] = ";".join(script.get("schedule_picks", []))
    probe_env["SCUZZ_FAULT_SEED"] = script.get("fault_seed", "")
    subprocess.run([str(target / "build/counter")], cwd=target, env=probe_env, check=True, timeout=20, stdout=subprocess.DEVNULL)
    expected = (card_dir / question["timelines"][side]).read_text()
    actual = timeline.read_text()
    assert actual == expected, "".join(difflib.unified_diff(expected.splitlines(True), actual.splitlines(True)))
print("UI review: retained witness has exact compiled parity on both sides")
PYREVIEWUI
  # A real write failure leaves a journal. Restart and Undo use that journal.
  python3 - <<'PYINTEGRITY'
import hashlib, json, os, shutil, subprocess, time
from pathlib import Path
repo = Path.cwd()
cli = Path(os.environ["SCUZZ"])
editor = repo / "examples/editor/build/editor"
root = repo / "scratchpad/integrity"
shutil.rmtree(root, ignore_errors=True)
shutil.copytree(repo / "examples/counter", root, ignore=shutil.ignore_patterns("build", ".scuzz"))
original = (root / "src/Main.scuzz").read_text()
proposed = original.replace('"Counter"', '"Counter proof"', 1)

def read_ui(where):
    deadline = time.monotonic() + 5
    while True:
        try:
            return json.loads((where / "debug.json").read_text())
        except json.JSONDecodeError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.005)

def publish(where, files):
    dest = where / "build/proposals/p"
    (dest / "src").mkdir(parents=True, exist_ok=True)
    session_dir = where / ".scuzz/ide"
    session_dir.mkdir(parents=True, exist_ok=True)
    (session_dir / "session.json").write_text(json.dumps({"v": 1, "objective": "Review behavior", "allowed": ["src/Main.scuzz", "src/Other.scuzz"], "mode": "external", "model": "qwen3.5-4b", "requests": 0}))
    for path, content in files:
        (dest / path).write_text(content)

def bind_publication(where):
    request = json.loads((where / "build/ide/request.json").read_text())
    dest = where / "build/proposals/p"
    files = [{"path": "src/" + p.name, "sha256": hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted((dest / "src").glob("*.scuzz"))]
    (dest / "proposal.json").write_text(json.dumps({"v": 1, "request": request["request"], "baseline": request["baseline"], "kind": "behavior", "generator": "manual", "files": files}))

def write_ops(where, events):
    path = where / "inject.json"
    temp = where / "inject.tmp"
    temp.write_text(json.dumps({"v": 1, "kind": "inject", "stamp": time.monotonic_ns(), "events": events}))
    temp.replace(path)
    return path

def batch(where, events):
    if len(events) > 1 and events[0] == {"op": "tap", "id": "button:Start"}:
        return started_batch(where, events)
    path = write_ops(where, events)
    env = dict(os.environ, SCUZZ_HOME=str(repo), SCUZZ_UI_RUNTIME="headless", SCUZZ_UI_WIDTH="960", SCUZZ_UI_HEIGHT="560", SCUZZ_UI_SCRIPT=str(path), SCUZZ_UI_DEBUG_DUMP=str(where / "debug.json"))
    env.pop("SCUZZ_UI_SERVE", None)
    subprocess.run([str(editor)], cwd=where, env=env, check=True, timeout=180, stdout=subprocess.DEVNULL)

def started_batch(where, events):
    path = write_ops(where, events[:1])
    (where / "debug.json").unlink(missing_ok=True)
    env = dict(os.environ, SCUZZ_HOME=str(repo), SCUZZ_UI_RUNTIME="headless", SCUZZ_UI_WIDTH="960", SCUZZ_UI_HEIGHT="560", SCUZZ_UI_SERVE="1", SCUZZ_LIVE_FRAMES="15000", SCUZZ_UI_SCRIPT=str(path), SCUZZ_UI_INJECT=str(path), SCUZZ_UI_DEBUG_DUMP=str(where / "debug.json"))
    log = open(where / "batch.log", "w")
    proc = subprocess.Popen([str(editor)], cwd=where, env=env, stdout=log, stderr=log)
    def await_ui(predicate):
        deadline = time.monotonic() + 180
        while time.monotonic() < deadline:
            assert proc.poll() is None, (where / "batch.log").read_text()[-3000:]
            try:
                ui = json.loads((where / "debug.json").read_text())
                if predicate(ui): return ui
            except (FileNotFoundError, json.JSONDecodeError):
                pass
            time.sleep(.05)
        raise AssertionError("started review deadline: " + str(where))
    try:
        ui = await_ui(lambda ui: "External proposals: publish a complete directory" in json.dumps(ui) or "unsafe resolved path" in json.dumps(ui))
        if "unsafe resolved path" not in json.dumps(ui):
            write_ops(where, events[1:])
            def preparation_done(ui):
                idle = any(v.get("name") == "deckBusy" and v.get("value") == 0 for v in ui["signals"])
                status = json.dumps(ui)
                ready = any(v.get("name") == "deckArmed" and v.get("value") for v in ui["signals"])
                excluded = any(json.loads(p.read_text()).get("decision") == "no-observed-difference" for p in (where / ".scuzz/ide/records").glob("*.json"))
                failed = "Retry preparation explicitly" in status or "unsafe resolved path" in status
                return idle and (ready or excluded or failed)
            await_ui(preparation_done)
        write_ops(where, [{"op": "quit"}])
        assert proc.wait(timeout=15) == 0
    finally:
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=15)
        log.close()

publish(root, [("src/Main.scuzz", proposed), ("src/Other.scuzz", "def other(): Int =\n  3\n\n")])
fault = root / "fault.c"
fault.write_text(r'''#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <string.h>
int rename(const char *from, const char *to) {
  static int failed;
  const char *suffix = "/src/Other.scuzz";
  size_t n = strlen(to), m = strlen(suffix);
  if (!failed && (strcmp(to, "src/Other.scuzz") == 0 || (n >= m && strcmp(to + n - m, suffix) == 0))) {
    failed = 1;
    errno = EIO;
    return -1;
  }
  int (*next)(const char *, const char *) = dlsym(RTLD_NEXT, "rename");
  return next(from, to);
}
''')
# This host proof uses an owned process with a frame limit and a wall deadline.
if os.uname().sysname == "Linux":
    library = root / "fault.so"
    subprocess.run(["clang", "-shared", "-fPIC", str(fault), "-ldl", "-o", str(library)], check=True)
    inject = write_ops(root, [{"op": "tap", "id": "button:Start"}])
    env = dict(os.environ, SCUZZ_HOME=str(repo), SCUZZ_UI_RUNTIME="headless", SCUZZ_UI_WIDTH="960", SCUZZ_UI_HEIGHT="560", SCUZZ_UI_SERVE="1", SCUZZ_LIVE_FRAMES="15000", SCUZZ_UI_SCRIPT=str(inject), SCUZZ_UI_INJECT=str(inject), SCUZZ_UI_DEBUG_DUMP=str(root / "debug.json"), LD_PRELOAD=str(library))
    log = open(root / "live.log", "w")
    review_wall_start = time.monotonic()
    proc = subprocess.Popen([str(editor)], cwd=root, env=env, stdout=log, stderr=log)
    def await_state(predicate):
        deadline = time.monotonic() + 180
        while time.monotonic() < deadline:
            assert proc.poll() is None, (root / "live.log").read_text()[-3000:]
            try:
                result = predicate()
            except (json.JSONDecodeError, FileNotFoundError):
                result = None
            if result:
                return result
            time.sleep(0.05)
        raise AssertionError("live review deadline")
    def ready_card():
        for path in (root / ".scuzz/ide/cards").glob("*/card.json"):
            c = json.loads(path.read_text())
            debug = root / "debug.json"
            if c["status"] == "ready" and debug.exists():
                ui = json.loads(debug.read_text())
                idle = any(v.get("name") == "deckBusy" and v.get("value") == 0 for v in ui.get("signals", []))
                armed = any(v.get("name") == "deckArmed" and v.get("value") == c["id"] for v in ui.get("signals", []))
                if idle and armed and "Choose left" in json.dumps(ui.get("taps", [])):
                    return c
    try:
        await_state(lambda: (root / "build/ide/request.json").is_file())
        write_ops(root, [{"op": "tap", "id": "choicechip:Code"}])
        await_state(lambda: read_ui(root).get("editors"))
        write_ops(root, [{"op": "caret", "offset": len(original)}, {"op": "key", "key": "x", "text": "x"}])
        await_state(lambda: any(v.get("name") == "buf" and v.get("value") == original + "x" for v in read_ui(root)["signals"]))
        bind_publication(root)
        write_ops(root, [{"op": "tap", "id": "choicechip:Review"}, {"op": "tap", "id": "choicechip:More evidence"}, {"op": "tap", "id": "outlined:Proposals"}])
        c = await_state(ready_card)
        ui = read_ui(root)
        values = {v["name"]: v["value"] for v in ui["signals"]}
        assert values["playOn"] == 0 and values["evidenceDetails"] == 0, values
        evidence = c["evidence"]
        questions = [w for w in evidence["workloads"] if w["class"] != "same"]
        selected = next(w for w in questions if w["label"] == values["questionWork"])
        assert len(selected["events"]) == min(len(w["events"]) for w in questions), selected
        assert c["question"]["workload"] == values["questionWork"] and c["question"]["step"] == values["step"]
        assert values["resultLeft"] and values["resultRight"]
        assert "review-host" in {f["path"] for f in c["inputs"]}
        for witness in evidence.get("witnesses", []):
            assert (root / ".scuzz/ide/cards" / c["id"] / witness["path"]).is_file()
        print("Counter first card readiness (fresh target; cached host):", round((time.monotonic() - review_wall_start) * 1000, 1), "ms wall; freeze to ready:", c["ready"] - c["started"], "ms; required:", evidence["review"]["required"], "; search:", evidence["search"]["ran"])
        successor_source = original.replace('"Counter"', '"Counter successor"', 1)
        successor_dir = root / "build/proposals/p-next"
        (successor_dir / "src").mkdir(parents=True)
        (successor_dir / "src/Main.scuzz").write_text(successor_source)
        request = json.loads((root / "build/ide/request.json").read_text())
        (successor_dir / "proposal.json").write_text(json.dumps({"v": 1, "request": request["request"], "baseline": request["baseline"], "kind": "behavior", "generator": "controlled-successor", "files": [{"path": "src/Main.scuzz", "sha256": hashlib.sha256(successor_source.encode()).hexdigest()}]}))
        await_state(lambda: "Ready cards: 2 / 2; reserved: 0" in json.dumps(read_ui(root)))
        assert next(v["value"] for v in read_ui(root)["signals"] if v.get("name") == "deckArmed") == c["id"]
        unread = root / "build/proposals/p-unread"
        unread.mkdir()
        (unread / "proposal.json").write_text("incomplete independent publication")
        time.sleep(.4)
        assert "Ready cards: 2 / 2; reserved: 0" in json.dumps(read_ui(root))
        assert not any(json.loads(path.read_text()).get("proposal") == "p-unread" for path in (root / ".scuzz/ide/cards").glob("*/card.json"))
        start = time.monotonic()
        write_ops(root, [{"op": "tap", "id": "choicechip:More evidence"}, {"op": "tap", "id": "outlined:Randomize"}])
        await_state(lambda: next(v["value"] for v in read_ui(root)["signals"] if v.get("name") == "deckArmed") != c["id"])
        card_ms = (time.monotonic() - start) * 1000
        write_ops(root, [{"op": "tap", "id": "choicechip:More evidence"}, {"op": "tap", "id": "outlined:Randomize"}])
        await_state(lambda: next(v["value"] for v in read_ui(root)["signals"] if v.get("name") == "deckArmed") == c["id"])
        print("Counter cached card navigation:", round(card_ms, 1), "ms; 250 ms target:", "met" if card_ms < 250 else "unmet")
        # Navigation uses the loaded evidence while its files are unavailable.
        other = max(questions, key=lambda w: len(w["events"]))
        assert other["label"] != selected["label"] and other["events"]
        cached = root / ".scuzz/ide/cards" / c["id"]
        held = cached.with_name(cached.name + "-held")
        build = root / "build"
        held_build = root / "build-held"
        cached.rename(held)
        build.rename(held_build)
        try:
            time.sleep(.55)
            assert next(v["value"] for v in read_ui(root)["signals"] if v.get("name") == "deckArmed") == c["id"]
            write_ops(root, [{"op": "tap", "id": "choicechip:More evidence"}])
            await_state(lambda: any(v.get("name") == "evidenceDetails" and v.get("value") == 1 for v in read_ui(root)["signals"]))
            start = time.monotonic()
            write_ops(root, [{"op": "tap", "id": "textbutton:" + other["label"]}])
            await_state(lambda: any(v.get("name") == "questionWork" and v.get("value") == other["label"] for v in read_ui(root)["signals"]))
            work_ms = (time.monotonic() - start) * 1000
            current = {v["name"]: v["value"] for v in read_ui(root)["signals"]}
            index = 0 if current["step"] != 0 else 1
            assert len(current["rail"]) > index
            start = time.monotonic()
            write_ops(root, [{"op": "tap", "id": "textbutton:" + current["rail"][index]}])
            await_state(lambda: any(v.get("name") == "step" and v.get("value") == index for v in read_ui(root)["signals"]))
            step_ms = (time.monotonic() - start) * 1000
            current = {v["name"]: v["value"] for v in read_ui(root)["signals"]}
            assert current["playOn"] == 0 and current["deckArmed"] == c["id"] and current["resultLeft"] and current["resultRight"]
            print("Counter cached navigation:", round(work_ms, 1), "ms workload;", round(step_ms, 1), "ms step; 250 ms target:", "met" if max(work_ms, step_ms) < 250 else "unmet")
        finally:
            held_build.rename(build)
            held.rename(cached)
        dirty = next(v["value"] for v in ui["signals"] if v.get("name") == "buf")
        assert dirty == original + "x", dirty
        lane = "Choose left" if c["flip"] == 1 else "Choose right"
        write_ops(root, [{"op": "tap", "id": "choicechip:Question"}, {"op": "tap", "id": "button:" + lane}])
        def refused_dirty():
            ui = read_ui(root)
            return any(v.get("name") == "deltaStatus" and "dirty buffer:" in json.dumps(v.get("value")) for v in ui.get("signals", []))
        await_state(refused_dirty)
        time.sleep(.7)
        assert refused_dirty()
        ui = read_ui(root)
        assert any(v.get("name") == "deckArmed" and not v.get("value") for v in ui["signals"])
        assert "Retry preparation explicitly" in json.dumps(ui)
        assert not any(v.get("label") in ("Choose left", "Choose right", "Can't decide") for v in ui.get("taps", []))
        assert (root / "src/Main.scuzz").read_text() == original
        assert not (root / "src/Other.scuzz").exists()
        assert not list((root / ".scuzz/ide/records").glob("*.json"))
        ui = read_ui(root)
        assert any(v.get("name") == "buf" and v.get("value") == dirty for v in ui["signals"])
        write_ops(root, [{"op": "quit"}])
        assert proc.wait(timeout=15) == 0
        assert successor_dir.is_dir() and unread.is_dir()
        previous_request = (root / "build/ide/request.json").read_text()
        previous_card = c
        previous_report = (root / "build/ide/report.json").read_bytes()
        write_ops(root, [{"op": "tap", "id": "button:Start"}])
        (root / "debug.json").unlink()
        review_wall_start = time.monotonic()
        proc = subprocess.Popen([str(editor)], cwd=root, env=env, stdout=log, stderr=log)
        await_state(lambda: "External proposals: publish a complete directory" in json.dumps(read_ui(root)))
        bind_publication(root)
        write_ops(root, [{"op": "tap", "id": "choicechip:Review"}, {"op": "tap", "id": "choicechip:More evidence"}, {"op": "tap", "id": "outlined:Proposals"}])
        c = await_state(ready_card)
        if c["id"] != previous_card["id"]:
            write_ops(root, [{"op": "tap", "id": "choicechip:More evidence"}, {"op": "tap", "id": "outlined:Randomize"}])
            c = await_state(lambda: (candidate if (candidate := ready_card()) and candidate["id"] == previous_card["id"] else None))
        assert "Ready cards: 2 / 2; reserved: 0" in json.dumps(read_ui(root))
        assert successor_dir.is_dir() and unread.is_dir()
        successor_dir.rename(root / "retained-successor")
        unread.rename(root / "retained-independent")
        assert c == previous_card and (root / "build/ide/request.json").read_text() == previous_request
        assert (root / "build/ide/report.json").read_bytes() == previous_report
        print("Counter warm card readiness:", round((time.monotonic() - review_wall_start) * 1000, 1), "ms wall; saved card identity and evidence reused")
        lane = "Choose left" if c["flip"] == 1 else "Choose right"
        write_ops(root, [{"op": "tap", "id": "choicechip:Question"}, {"op": "tap", "id": "button:" + lane}])
        journal = root / ".scuzz/ide/journal.json"
        await_state(lambda: journal.exists() and journal.read_text() and (root / "src/Main.scuzz").read_text() == proposed)
        assert not (root / "src/Other.scuzz").exists()
        write_ops(root, [{"op": "quit"}])
        assert proc.wait(timeout=15) == 0
    finally:
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=15)
        log.close()
    batch(root, [])
    assert (root / "src/Main.scuzz").read_text() == proposed
    assert any(v.get("name") == "buf" and v.get("value") == proposed for v in read_ui(root)["signals"])
    assert (root / "src/Other.scuzz").read_text() == "def other(): Int =\n  3\n\n"
    assert journal.read_text() == ""
    records = list((root / ".scuzz/ide/records").glob("*.json"))
    assert len(records) == 1, records
    shutil.rmtree(root / "build")
    batch(root, [])
    assert len(list((root / ".scuzz/ide/records").glob("*.json"))) == 1
    batch(root, [{"op": "tap", "id": "choicechip:Review"}, {"op": "tap", "id": "outlined:Undo"}])
    assert (root / "src/Main.scuzz").read_text() == original
    assert any(v.get("name") == "buf" and v.get("value") == original for v in read_ui(root)["signals"])
    assert not (root / "src/Other.scuzz").exists()
    assert len(list((root / ".scuzz/ide/records").glob("*.json"))) == 2
    publish(root, [("src/Main.scuzz", proposed), ("src/Other.scuzz", "def other(): Int =\n  3\n\n")])
    env.pop("LD_PRELOAD", None)
    write_ops(root, [{"op": "tap", "id": "button:Start"}])
    log = open(root / "live.log", "a")
    review_wall_start = time.monotonic()
    proc = subprocess.Popen([str(editor)], cwd=root, env=env, stdout=log, stderr=log)
    try:
        await_state(lambda: (root / "build/ide/request.json").is_file())
        bind_publication(root)
        write_ops(root, [{"op": "tap", "id": "choicechip:Review"}, {"op": "tap", "id": "choicechip:More evidence"}, {"op": "tap", "id": "outlined:Proposals"}])
        c = await_state(ready_card)
        lane = "Choose left" if c["flip"] == 1 else "Choose right"
        write_ops(root, [{"op": "tap", "id": "button:" + lane}, {"op": "tap", "id": "button:" + lane}])
        def accepted_clean():
            ui = read_ui(root)
            idle = any(v.get("name") == "deckBusy" and v.get("value") == 0 for v in ui["signals"])
            return idle and len(list((root / ".scuzz/ide/records").glob("*.json"))) == 3 and any(v.get("name") == "buf" and v.get("value") == proposed for v in ui["signals"])
        await_state(accepted_clean)
        write_ops(root, [{"op": "tap", "id": "choicechip:Verify"}, {"op": "tap", "id": "outlined:Check"}])
        await_state(lambda: any(v.get("name") == "checkedSource" and v.get("value") == proposed for v in read_ui(root)["signals"]))
        write_ops(root, [{"op": "tap", "id": "choicechip:Develop"}, {"op": "tap", "id": "choicechip:Code"}])
        def checked_acceptance():
            ui = read_ui(root)
            checked = any(v.get("name") == "checkedSource" and v.get("value") == proposed for v in ui["signals"])
            return checked and any(v.get("tokens", 0) > 0 for v in ui.get("editors", []))
        await_state(checked_acceptance)
        assert (root / "src/Main.scuzz").read_text() == proposed
        assert (root / "src/Other.scuzz").read_text() == "def other(): Int =\n  3\n\n"
        assert len(list((root / ".scuzz/ide/records").glob("*.json"))) == 3
        write_ops(root, [{"op": "quit"}])
        assert proc.wait(timeout=15) == 0
    finally:
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=15)
        log.close()
else:
    print("live filesystem fault injection: unverified on this host")

failure = repo / "scratchpad/review-failure"
shutil.rmtree(failure, ignore_errors=True)
shutil.copytree(repo / "examples/counter", failure, ignore=shutil.ignore_patterns("build", ".scuzz"))
(failure / "failure.scuzz_verify").write_text('def sharedFailure(t: Timeline): Verdict =\n  Verdict.fail(0, "shared failure")\n\n')
publish(failure, [("src/Main.scuzz", proposed)])
batch(failure, [{"op": "tap", "id": "button:Start"}])
bind_publication(failure)
batch(failure, [{"op": "tap", "id": "button:Start"}, {"op": "tap", "id": "choicechip:Review"}, {"op": "tap", "id": "choicechip:More evidence"}, {"op": "tap", "id": "outlined:Proposals"}])
report = json.loads((failure / "build/ide/report.json").read_text())
assert not report["review"]["complete"] and any(w["why_b"] for w in report["workloads"]), report
assert (failure / "src/Main.scuzz").read_text() == original
assert not list((failure / ".scuzz/ide/records").glob("*.json"))
assert all(json.loads(p.read_text())["status"] != "ready" for p in (failure / ".scuzz/ide/cards").glob("*/card.json"))

source_only = repo / "scratchpad/review-source-only"
shutil.rmtree(source_only, ignore_errors=True)
shutil.copytree(repo / "examples/counter", source_only, ignore=shutil.ignore_patterns("build", ".scuzz"))
publish(source_only, [("src/Main.scuzz", original.replace("noteDrive(n:", "noteDrive(steps:").replace("steps: Int where n >= 0", "steps: Int where steps >= 0"))])
batch(source_only, [{"op": "tap", "id": "button:Start"}])
bind_publication(source_only)
batch(source_only, [{"op": "tap", "id": "button:Start"}, {"op": "tap", "id": "choicechip:Review"}, {"op": "tap", "id": "choicechip:More evidence"}, {"op": "tap", "id": "outlined:Proposals"}])
assert (source_only / "src/Main.scuzz").read_text() == original
records = [json.loads(p.read_text()) for p in (source_only / ".scuzz/ide/records").glob("*.json")]
assert len(records) == 1 and records[0]["decision"] == "no-observed-difference" and records[0]["kind"] == "automatic", records
assert not records[0]["writes"] and records[0]["evidence"]["review"]["complete"]
assert records[0]["evidence"]["search"]["ran"] == 32
assert (source_only / "build/proposals/p/proposal.json").is_file()
excluded = [json.loads(p.read_text()) for p in (source_only / ".scuzz/ide/cards").glob("*/card.json")]
assert len(excluded) == 1 and excluded[0]["status"] == "excluded", excluded
assert excluded[0]["proposal"] == "p" and records[0]["card"] == excluded[0]["id"]
assert not (source_only / ".scuzz/ide/last.json").exists()
ui = read_ui(source_only)
assert not any(v.get("name") == "deckArmed" and v.get("value") for v in ui["signals"])

repair = repo / "scratchpad/review-repair"
shutil.rmtree(repair, ignore_errors=True)
shutil.copytree(repo / "examples/counter", repair, ignore=shutil.ignore_patterns("build", ".scuzz"))
(repair / "src/Main.scuzz").write_text(original.replace("Signal.get(count) + 1", 'Signal.get(count) + "x"'))
assert (repair / "src/Main.scuzz").read_text() != original
publish(repair, [("src/Main.scuzz", proposed)])
batch(repair, [{"op": "tap", "id": "button:Start"}])
bind_publication(repair)
batch(repair, [{"op": "tap", "id": "button:Start"}, {"op": "tap", "id": "choicechip:Review"}, {"op": "tap", "id": "choicechip:More evidence"}, {"op": "tap", "id": "outlined:Proposals"}])
cards = [json.loads(p.read_text()) for p in (repair / ".scuzz/ide/cards").glob("*/card.json")]
assert len(cards) == 1 and cards[0]["status"] == "ready", cards
assert all(w["why_a"] == "check failed" and w["why_b"] is None for w in cards[0]["evidence"]["workloads"])

for side in ("publication", "target"):
    unsafe = repo / ("scratchpad/review-unsafe-" + side)
    shutil.rmtree(unsafe, ignore_errors=True)
    shutil.copytree(repo / "examples/counter", unsafe, ignore=shutil.ignore_patterns("build", ".scuzz"))
    publish(unsafe, [("src/Main.scuzz", proposed)])
    batch(unsafe, [{"op": "tap", "id": "button:Start"}])
    bind_publication(unsafe)
    linked = unsafe / ("build/proposals/p/src/Main.scuzz" if side == "publication" else "src/Main.scuzz")
    outside = unsafe / "outside.scuzz"
    outside.write_text(linked.read_text())
    linked.unlink()
    linked.symlink_to(outside)
    batch(unsafe, [{"op": "tap", "id": "button:Start"}, {"op": "tap", "id": "choicechip:Review"}, {"op": "tap", "id": "choicechip:More evidence"}, {"op": "tap", "id": "outlined:Proposals"}])
    assert outside.read_text() == (proposed if side == "publication" else original)
    assert not list((unsafe / ".scuzz/ide/cards").glob("*/card.json"))
    assert not list((unsafe / ".scuzz/ide/records").glob("*.json"))

paused_root = repo / "scratchpad/paused-settings"
shutil.rmtree(paused_root, ignore_errors=True)
shutil.copytree(repo / "examples/counter", paused_root, ignore=shutil.ignore_patterns("build", ".scuzz"))
paused_storage = paused_root / ".scuzz/ide"
paused_storage.mkdir(parents=True)
paused_metadata = paused_storage / "session.json"
paused_metadata.write_text(json.dumps({"v": 1, "baseline_epoch": "restored-paused-source", "objective": "Review paused settings", "allowed": ["src/Main.scuzz"], "mode": "external", "model": "qwen3.5-4b", "requests": 0}))
paused_source = (paused_root / "src/Main.scuzz").read_text()
paused_inject = write_ops(paused_root, [{"op": "tap", "id": "choicechip:Session"}])
paused_env = dict(os.environ, SCUZZ_HOME=str(repo), SCUZZ_UI_RUNTIME="headless", SCUZZ_UI_WIDTH="960", SCUZZ_UI_HEIGHT="560", SCUZZ_UI_SERVE="1", SCUZZ_LIVE_FRAMES="15000", SCUZZ_UI_SCRIPT=str(paused_inject), SCUZZ_UI_INJECT=str(paused_inject), SCUZZ_UI_DEBUG_DUMP=str(paused_root / "debug.json"))
paused_log = open(paused_root / "live.log", "w")
paused_proc = subprocess.Popen([str(editor)], cwd=paused_root, env=paused_env, stdout=paused_log, stderr=paused_log)
def paused_wait(predicate):
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        assert paused_proc.poll() is None, (paused_root / "live.log").read_text()[-1000:]
        try:
            result = predicate()
            if result: return result
        except (FileNotFoundError, json.JSONDecodeError):
            pass
        time.sleep(.025)
    raise AssertionError("paused settings deadline")
def paused_saved():
    return json.loads(paused_metadata.read_text())
def paused_ready_ui():
    value = read_ui(paused_root)
    return value if any(f.get("label") == "One short objective" for f in value.get("fields", [])) else None
try:
    ui = paused_wait(paused_ready_ui)
    initial = paused_saved()
    expected = {"objective": initial["objective"], "allowed": initial["allowed"], "mode": 0, "model": initial["model"]}
    assert any(v.get("type") == "str" and v.get("value") == json.dumps(expected, separators=(',', ':')) for v in ui["signals"])
    index = next(f["i"] for f in ui["fields"] if f.get("label") == "One short objective")
    write_ops(paused_root, [{"op": "text", "i": index, "value": "Another paused objective"}])
    changed = paused_wait(lambda: (value if (value := paused_saved())["objective"] == "Another paused objective" and value["baseline_epoch"] != initial["baseline_epoch"] else None))
    time.sleep(.7)
    assert paused_saved()["baseline_epoch"] == changed["baseline_epoch"]
    write_ops(paused_root, [{"op": "text", "i": index, "value": initial["objective"]}])
    restored = paused_wait(lambda: (value if (value := paused_saved())["objective"] == initial["objective"] and value["baseline_epoch"] != changed["baseline_epoch"] else None))
    assert restored["baseline_epoch"] != initial["baseline_epoch"]
    assert restored["requests"] == 0 and not (paused_root / "build/ide/request.json").exists()
    assert (paused_root / "src/Main.scuzz").read_text() == paused_source
    write_ops(paused_root, [{"op": "quit"}])
    assert paused_proc.wait(timeout=15) == 0
    print("Headless session restores its settings. Paused objective changes advance the baseline once. No request or source write starts.")
finally:
    if paused_proc.poll() is None:
        paused_proc.terminate()
        paused_proc.wait(timeout=15)
    paused_log.close()

PYINTEGRITY
  # Live generation: mutation sites of the working tree become proposals.
  python3 - <<'PY'
import json
ops = {"v": 1, "kind": "inject", "events": [
    {"op": "tap", "id": "choicechip:Review"}, {"op": "tap", "id": "choicechip:More evidence"},
    {"op": "tap", "id": "outlined:Generate mutations"},
    {"op": "pump", "k": 50},
]}
ops["events"].insert(0, {"op": "tap", "id": "button:Start"})
open("scratchpad/review/ops-gen.json", "w").write(json.dumps(ops))
PY
  (cd scratchpad/review && SCUZZ_HOME="$ROOT" "$SCUZZ" run --target headless --exec ops-gen.json "$ROOT/examples/editor")
  python3 - <<'PY'
import os
props = [p for p in os.listdir("scratchpad/review/build/proposals") if p.startswith("m")]
assert props, os.listdir("scratchpad/review/build/proposals")
first = sorted(props)[0]
src = os.path.join("scratchpad/review/build/proposals", first, "src", "Main.scuzz")
assert os.path.isfile(src), src
disk = open("scratchpad/review/src/Main.scuzz").read()
assert open(src).read() != disk
PY
}

slice_ui_test() {
  need_scuzz
  "$SCUZZ" fuzz --iterations 0 examples/counter
  python3 - <<'PY'
import json
with open("examples/counter/build/fuzz/summary.json") as f:
    d = json.load(f)
br = d["breadth"]
assert "signals" in br["varied"], br
assert "count" in br["claimed"]["signalInt"], br
assert "signals" not in br["unclaimed"], br
PY
  "$SCUZZ" fuzz --iterations 0 examples/studio
  mkdir -p scratchpad/editor
  (cd scratchpad/editor && SCUZZ_HOME="$ROOT" "$SCUZZ" fuzz --iterations 0 "$ROOT/examples/editor")
  prove_evaluator_replay editor
}

slice_gpu() {
  need_scuzz
  need_cmd xvfb-run "sudo apt-get install -y xvfb"
  xvfb-run -a env LIBGL_ALWAYS_SOFTWARE=1 SCUZZ_SKIA=gpu make -C crates/ffi-skia test -j"$JOBS" CC=clang
  xvfb-run -a env LIBGL_ALWAYS_SOFTWARE=1 SCUZZ_SKIA=gpu "$SCUZZ" fuzz --iterations 0 examples/counter
  make -C crates/ffi-skia clean
  make -C crates/ffi-skia lib -j"$JOBS" CC=clang
}

slice_differential() {
  need_scuzz
  need_cmd xvfb-run "sudo apt-get install -y xvfb"
  need_cmd python3 "sudo apt-get install -y python3"
  xvfb-run -a env LIBGL_ALWAYS_SOFTWARE=1 "$SCUZZ" fuzz --differential --iterations 0 examples/counter
  make -C crates/ffi-skia clean
  make -C crates/ffi-skia lib -j"$JOBS" CC=clang
}

slice_fuzz() {
  need_scuzz
  need_cmd python3 "sudo apt-get install -y python3"
  maybe_wipe
  ./scripts/ci-fuzz.sh
}

slice_delta() {
  need_scuzz
  need_cmd git "sudo apt-get install -y git"
  need_cmd python3 "sudo apt-get install -y python3"
  ./scripts/ci-delta.sh
}

slice_new_ui() {
  need_scuzz
  need_cmd python3 "sudo apt-get install -y python3"
  rm -rf /tmp/scuzz-v0app
  "$SCUZZ" new --ui --path /tmp scuzz-v0app
  "$SCUZZ" fmt --check /tmp/scuzz-v0app
  "$SCUZZ" check /tmp/scuzz-v0app
  grep -q 'tap button:+1' /tmp/scuzz-v0app/corpus/plus.toml
  "$SCUZZ" fuzz --iterations 4 /tmp/scuzz-v0app
  python3 - <<'PY'
import json
with open("/tmp/scuzz-v0app/build/fuzz/summary.json") as f:
    d = json.load(f)
assert d["fuzz"]["ok"] is True
assert d["fuzz"]["iterations"] == 4
assert d["fuzz"]["search"] > 0
assert "button:+1" in d["triggers"]["declared"], d["triggers"]
assert "button:+1" in d["triggers"]["reached"], d["triggers"]
assert "button:+1" not in d["triggers"]["never"], d["triggers"]
PY
  "$SCUZZ" run --target headless --exec "" /tmp/scuzz-v0app
  test -f /tmp/scuzz-v0app/build/snapshot.png
}

slice_desktop() {
  need_cmd xvfb-run "sudo apt-get install -y xvfb libx11-dev"
  need_cmd timeout "sudo apt-get install -y coreutils"
  xvfb-run -a make -C crates/embedder-desktop test
  test -x examples/studio/build/studio || slice_ui
  timeout 30s xvfb-run -a env SCUZZ_UI_RUNTIME=desktop SCUZZ_LIVE_FRAMES=2 \
    SCUZZ_UI_WIDTH=400 SCUZZ_UI_HEIGHT=560 \
    ./examples/studio/build/studio 2>&1 | tee /tmp/win.out
  grep -q "desktop embedder" /tmp/win.out
  grep -q "X11 window" /tmp/win.out
}

slice_ios() {
  need_scuzz
  need_cmd xcrun "Install Xcode and an iOS simulator runtime"
  need_cmd python3 "Install the Xcode command-line tools"
  "$SCUZZ" fuzz --iterations 0 examples/network-ui
  python3 crates/runtime/tests/test_net_apple.py --ios
  "$SCUZZ" fuzz --iterations 0 examples/counter
  python3 crates/embedder-mobile/shells/ios/test_loop.py "$SCUZZ"
}

slice_mobile() {
  need_scuzz
  test -x examples/counter/build/counter || slice_ui
  env SCUZZ_UI_RUNTIME=mobile SCUZZ_MOBILE_SHELL=1 SCUZZ_UI_WIDTH=200 SCUZZ_UI_HEIGHT=120 \
    ./examples/counter/build/counter 2>&1 | tee /tmp/mobile.out
  grep -q "UiRuntime.Mobile" /tmp/mobile.out
  grep -q "scuzz mobile: present" /tmp/mobile.out
  host_target="linux"
  [ "$(uname -s)" = Darwin ] && host_target="macos"
  "$SCUZZ" package --target "$host_target" examples/counter
  if [ "$(uname -s)" = Darwin ]; then
    test -x examples/counter/build/package/host/counter.app/Contents/MacOS/counter
  else
    test -x examples/counter/build/package/host/run.sh
  fi
  if "$SCUZZ" package --target android examples/counter > /tmp/android-pkg.out 2>&1; then
    test -f examples/counter/build/package/android/lib/arm64-v8a/libscuzz.so
    test -f examples/counter/build/package/android/counter.apk
  else
    grep -Eq "ANDROID_NDK_HOME|ANDROID_HOME" /tmp/android-pkg.out
  fi
  if "$SCUZZ" package --target ios examples/counter > /tmp/ios-pkg.out 2>&1; then
    if [ "$(uname -s)" != Darwin ]; then
      echo "ios package succeeded without Xcode" >&2
      cat /tmp/ios-pkg.out >&2
      exit 1
    fi
  else
    grep -q "xcode-select --install" /tmp/ios-pkg.out
  fi
  if [ "$(uname -s)" = Darwin ]; then
    env SCUZZ_UI_RUNTIME=desktop SCUZZ_LIVE_FRAMES=2 SCUZZ_UI_WIDTH=200 SCUZZ_UI_HEIGHT=120 \
      ./examples/counter/build/package/host/counter.app/Contents/MacOS/counter 2>&1 | tee /tmp/pkg-host.out
    grep -q "desktop embedder" /tmp/pkg-host.out
  else
    env SCUZZ_UI_WIDTH=200 SCUZZ_UI_HEIGHT=120 \
      ./examples/counter/build/package/host/run.sh 2>&1 | tee /tmp/pkg-host.out
    grep -q "scuzz mobile: present" /tmp/pkg-host.out
  fi
}

slice_macos_app() {
  need_scuzz
  need_cmd python3 "Install the Xcode command-line tools"
  make -C crates/embedder-desktop test CC=clang
  python3 crates/embedder-desktop/tests/test_package.py "$SCUZZ"
  python3 crates/runtime/tests/test_net_apple.py
}

slice_macos_hello() {
  need_scuzz
  maybe_wipe
  "$SCUZZ" build --full examples/hello
  "$SCUZZ" fuzz --iterations 0 examples/hello
  "$SCUZZ" fuzz --iterations 0 examples/counter
  "$SCUZZ" check examples/kernel
  if "$SCUZZ" check examples/bad-intent; then
    echo "empty verify should fail check" && exit 1
  fi
}

slice_macos_smoke() {
  slice_runtime
  slice_macos_hello
  if [ "$(uname -s)" = Darwin ]; then slice_macos_app; fi
}

prove_evaluator_replay() {
  python3 - "$ROOT" "$SCUZZ" "$1" <<'PY_PARITY'
import json, os, pathlib, resource, shutil, subprocess, sys, time

repo, cli = map(pathlib.Path, sys.argv[1:3])
name = sys.argv[3]
package = repo / 'examples' / name
build = package / 'build'
source = build / 'fuzz/ev'
ui = name == 'editor'
cases = {
    'tyck': [('generated', [
        {'op': 'drive', 'name': 'tyckGenerated', 'args': [7]},
        {'op': 'drive', 'name': 'prettyGenerated', 'args': [7]}])],
    'codegen': [('generated', [
        {'op': 'drive', 'name': 'irGenerated', 'args': [7]},
        {'op': 'drive', 'name': 'evGenerated', 'args': [7]}]),
        ('lookup', [{'op': 'drive', 'name': 'evLookup', 'args': []}]),
        ('constructors', [{'op': 'drive', 'name': 'evConstructors', 'args': []}]),
        ('maps', [{'op': 'drive', 'name': 'evMaps', 'args': []}])] + [
        (f'module-{seed}', [{'op': 'drive', 'name': 'evModules', 'args': [seed]}])
        for seed in range(6)],
    'editor': [('idle', None), ('queue-cache', [
        {'op': 'drive', 'name': 'queueCacheFlow', 'args': [3]}]),
        ('protocol', [
            {'op': 'drive', 'name': 'laneFlow', 'args': [0, 1]},
            {'op': 'drive', 'name': 'journalFlow', 'args': [4]},
            {'op': 'drive', 'name': 'staleFlow', 'args': [3]},
            {'op': 'drive', 'name': 'laneFlow', 'args': [1, 0]},
            {'op': 'drive', 'name': 'unsafeFlow', 'args': [5]}])],
}[name]
base_env = {k: v for k, v in os.environ.items() if not k.startswith('SCUZZ_')}
base_env['SCUZZ_HOME'] = str(repo)

def limit():
    if sys.platform.startswith('linux'):
        resource.setrlimit(resource.RLIMIT_AS, (512 * 1024 * 1024,) * 2)

def replay(package, executable, label, events, ui=False):
    build = package / 'build'
    source = build / 'fuzz/ev'
    out = build / 'fuzz/parity' / label
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    request = {'TESTRT': '1', 'SERVE': '1', 'KIT': 'sealed'}
    if events is not None:
        script = out / 'drive.json'
        script.write_text(json.dumps({'v': 1, 'kind': 'inject', 'events': events}))
        request['UI_SCRIPT' if ui else 'DRIVE_SCRIPT'] = str(script)
    if ui:
        request.update(UI_RUNTIME='headless', UI_WIDTH='960', UI_HEIGHT='560',
                       UI_SCALE='1.0', UI_TAP='1')
    for engine in ['eval', 'native']:
        work = out / 'work'
        shutil.rmtree(work, ignore_errors=True)
        work.mkdir()
        req = dict(request, TIMELINE_DUMP=str(out / f'{engine}.timeline'),
                   CLAIM_DUMP=str(out / f'{engine}.claims'))
        env = dict(base_env)
        if ui:
            req['FUZZ_DUMP'] = str(out / f'{engine}.json')
        if engine == 'eval':
            req['PROBE_LOG'] = str(out / 'probe.log')
            path = out / 'request.txt'
            path.write_text(''.join(f'{k}={v}\n' for k, v in req.items()))
            env['SCUZZ_EV_REQUEST'] = str(path)
            command = [str(cli), 'eval', '--probe', str(source)]
            if ui:
                command = [str(repo / 'examples/ui-host/build/ui-host'), str(source)]
                env.update(SCUZZ_EVAL_MIRROR='1', SCUZZ_UI_RUNTIME='headless',
                           SCUZZ_EVAL_TAGS=str(out / 'tags.txt'))
        else:
            env.update({'SCUZZ_' + k: v for k, v in req.items()})
            command = [str(build / executable)]
        started = time.monotonic()
        result = subprocess.run(command, input='probe\n' if engine == 'eval' else None,
                                text=True, capture_output=True, env=env, cwd=work,
                                timeout=45 if engine == 'eval' else 20,
                                preexec_fn=None if engine == 'eval' else limit)
        (out / f'{engine}.log').write_text(result.stdout + result.stderr)
        detail = result.stdout + result.stderr
        if engine == 'eval' and (out / 'probe.log').exists():
            detail += (out / 'probe.log').read_text()
        assert result.returncode == 0, (name, label, engine, detail)
        if engine == 'eval':
            assert result.stdout == 'ready\n0\n', (name, label, detail)
        print(f'{name} {label} {engine}: {time.monotonic() - started:.2f} s', flush=True)
    for suffix in ['timeline', 'claims']:
        assert (out / f'eval.{suffix}').read_bytes() == (out / f'native.{suffix}').read_bytes(), (
            name, label, suffix, str(out))
    print(f'{name} {label}: exact evaluator and compiled timelines and claims', flush=True)
    return out

for label, events in cases:
    replay(package, name, label, events, ui)

if name == 'codegen':
    target = build / 'fuzz/parity/map-api'
    shutil.rmtree(target, ignore_errors=True)
    (target / 'src').mkdir(parents=True)
    (target / 'work').mkdir()
    (target / 'scuzz.toml').write_text('[package]\nname="map-parity"\n')
    (target / 'src/Main.scuzz').write_text('''enum MapKey:
  case First(n: Int)
  case Last(n: Int)

def maps(): Bool =
  for {
    order = Signal.makeN("order", "")
    m = List.toMap([("c", 3), ("a", 0), ("b", 2), ("a", 1)])
    doubled = Map.mapValues(m, v => for {
      _ = Signal.set(order, Str.concat(Signal.get(order), s"${v}"))
    } yield v * 2)
    other = List.toMap([("b", 8), ("d", 4)])
    groups = List.groupBy([3, 1, 4, 2], (v: Int) => v % 2)
    tuples = List.toMap([((2, "a"), 3), ((1, "b"), 2), ((1, "a"), 1)])
    lists = List.toMap([([2], 3), ([1, 2], 2), ([1], 1)])
    enums = List.toMap([(MapKey.Last(0), 3), (MapKey.First(2), 2), (MapKey.First(1), 1)])
    nested = List.toMap([(List.toMap([(2, 1)]), 2), (Map.empty(): Map[Int, Int], 0), (List.toMap([(1, 1)]), 1)])
    nan = 0.0 / 0.0
    floats = List.toMap([(nan, 7), (2.0, 2), (0.0, 0), ((0.0 - 1.0) * 0.0, 4), (0.0 - 1.0, 1), (nan, 9)])
    floatKeys = Map.keys(floats)
    floatSet = Set.add(Set.add(Set.add(Set.empty(), nan), 2.0), nan)
    fractional = Map.mapValues(List.toMap([("a", 1.5), ("b", 2.5)]), v => v + 0.5)
    flags = List.toMap([("a", false), ("b", true)])
  } yield Signal.get(order) == "123" && Map.keys(m) == ["a", "b", "c"] && Map.values(m) == [1, 2, 3] && Map.toList(doubled) == [("a", 2), ("b", 4), ("c", 6)] && Map.size(m) == 3 && Map.nonEmpty(m) && !Map.isEmpty(m) && Map.contains(m, "b") && Map.get(m, "b") == Some(2) && Map.get(m, "z") == None && Map.getOrElse(m, "z", 9) == 9 && Map.remove(m, "z") == m && Map.keys(Map.remove(m, "b")) == ["a", "c"] && Map.toList(Map.union(m, other)) == [("a", 1), ("b", 8), ("c", 3), ("d", 4)] && Map.toList(Map.intersect(m, other)) == [("b", 2)] && Map.keys(Map.diff(m, other)) == ["a", "c"] && Map.keys(Map.filter(m, v => v > 1)) == ["b", "c"] && Map.exists(m, v => v == 2) && Map.forall(m, v => v > 0) && Map.toList(groups) == [(0, [4, 2]), (1, [3, 1])] && Map.values(tuples) == [1, 2, 3] && Map.values(lists) == [1, 2, 3] && Map.values(enums) == [1, 2, 3] && Map.values(nested) == [0, 1, 2] && Map.size(floats) == 4 && Map.values(floats) == [1, 4, 2, 9] && List.at(floatKeys, 0) == 0.0 - 1.0 && List.at(floatKeys, 3) != List.at(floatKeys, 3) && Map.getOrElse(floats, nan, 0) == 9 && Map.getOrElse(floats, 0.0, 0) == 4 && Map.size(Map.remove(floats, nan)) == 3 && Set.size(floatSet) == 2 && Set.contains(floatSet, nan) && Set.size(Set.remove(floatSet, nan)) == 1 && Map.values(fractional) == [2.0, 3.0] && Map.exists(flags, v => v) && !Map.forall(flags, v => v) && Map.keys(Map.filter(flags, v => v)) == ["b"]

@main def main: IO[Unit] =
  IO.pure(())
''')
    (target / 'maps.scuzz_verify').write_text('oracle maps(): Bool =\n  Main.maps()\n')
    subprocess.run([str(cli), 'fmt', str(target)], env=base_env, check=True,
                   capture_output=True, text=True, timeout=30)
    result = subprocess.run([str(cli), 'fuzz', '--iterations', '0', str(target)],
                            env=base_env, cwd=target / 'work', capture_output=True,
                            text=True, timeout=180)
    detail = result.stdout + result.stderr
    (target / 'campaign.log').write_text(detail)
    assert result.returncode == 0 and 'probes run compiled' not in detail, detail
    out = replay(target, 'map-parity', 'map-api', [
        {'op': 'drive', 'name': 'maps', 'args': []}])
    signals = json.loads((out / 'eval.timeline').read_text().split('signals:\n', 1)[1].split('\n', 1)[0])
    assert [(s['name'], s['value']) for s in signals] == [('order', '123')], signals
    print('Map API: ordered callbacks, immutable values, compound keys, and NaN order', flush=True)

if name == 'codegen':
    for reverse in [False, True]:
        target = build / f'fuzz/parity/constructor-order-{int(reverse)}'
        shutil.rmtree(target, ignore_errors=True)
        (target / 'src').mkdir(parents=True)
        (target / 'work').mkdir()
        (target / 'scuzz.toml').write_text('[package]\nname="constructor-parity"\n')
        declarations = [
            'enum Packet:\n  case Wrap(value: Int = 7)\n  case Empty\nrecord Parcel(value: Int = 11)\n',
            'enum Packet:\n  case Wrap(value: Int, text: String)\n  case Empty\nrecord Parcel(value: Int, text: String)\n']
        if reverse:
            declarations.reverse()
        for stem, source_text in zip(['A', 'B'], declarations):
            (target / f'src/{stem}.scuzz').write_text(source_text)
        first, second = ('B', 'A') if reverse else ('A', 'B')
        body = (
            f'def first(): Int =\n  {first}.Packet.Wrap() match {{ case {first}.Packet.Wrap(value = n) => n + {first}.Parcel().value case _ => 0 }}\n'
            f'def second(): Int =\n  {second}.Packet.Wrap(text = "b", value = 9) match {{ case {second}.Packet.Wrap(text = label, value = n) => n + Str.len(label) + {second}.Parcel(text = "cd", value = 13).value case _ => 0 }}\n'
            'def result(): Int = first() + second()\n@main def main: IO[Unit] =\n  IO.pure(())\n')
        (target / 'src/Main.scuzz').write_text(body)
        (target / 'constructors.scuzz_verify').write_text('oracle result(): Bool =\n  Main.result() == 41\n')
        subprocess.run([str(cli), 'fmt', str(target)], env=base_env, check=True,
                       capture_output=True, text=True, timeout=30)
        result = subprocess.run([str(cli), 'fuzz', '--iterations', '0', str(target)],
                                env=base_env, cwd=target / 'work', capture_output=True,
                                text=True, timeout=180)
        detail = result.stdout + result.stderr
        (target / 'campaign.log').write_text(detail)
        assert result.returncode == 0 and 'probes run compiled' not in detail, detail
        replay(target, 'constructor-parity', 'constructors', [
            {'op': 'drive', 'name': 'result', 'args': []}])
    target = build / 'fuzz/parity/view-signals'
    shutil.rmtree(target, ignore_errors=True)
    (target / 'src').mkdir(parents=True)
    (target / 'work').mkdir()
    (target / 'scuzz.toml').write_text('[package]\nname="view-parity"\n[ui]\ndefault_runtime="headless"\nheadless_size=[960, 560]\nheadless_scale=1.0\n')
    (target / 'src/Main.scuzz').write_text('@main def main: IO[Unit] =\n  for {\n    view = Signal.makeN("view", View.text("a"))\n    views = Signal.makeN("views", [View.text("b")])\n    nested = Signal.makeN("nested", (View.text("c"), [View.text("d")]))\n    _ <- Ui.run(_ => View.button("replace", _ => Signal.set(view, View.text("updated"))))\n  } yield ()\n')
    subprocess.run([str(cli), 'fmt', str(target)], env=base_env, check=True,
                   capture_output=True, text=True, timeout=30)
    result = subprocess.run([str(cli), 'fuzz', '--iterations', '0', str(target)],
                            env=base_env, cwd=target / 'work', capture_output=True,
                            text=True, timeout=180)
    (target / 'campaign.log').write_text(result.stdout + result.stderr)
    assert result.returncode == 0, result.stdout + result.stderr
    out = replay(target, 'view-parity', 'view-signals', None, ui=True)
    signals = json.loads((out / 'eval.timeline').read_text().split('signals:\n', 1)[1].split('\n', 1)[0])
    assert [(s['name'], s['value']) for s in signals] == [
        ('view', '<handle>'), ('views', ['<handle>']),
        ('nested', ['<handle>', ['<handle>']])], signals
    print('View signals: direct, list, and nested tuple values match native handles', flush=True)

if name == 'tyck':
    target = build / 'fuzz/parity/signals'
    shutil.rmtree(target, ignore_errors=True)
    (target / 'src').mkdir(parents=True)
    (target / 'work').mkdir()
    (target / 'scuzz.toml').write_text(
        '[package]\nname="signal-parity"\n[dependencies]\ncompiler={path="' +
        os.path.relpath(repo / 'examples/compiler', target) + '"}\n')
    (target / 'src/Main.scuzz').write_text('''def signals(): Bool =
  for {
    count = Signal.makeN("count", 4)
    label = Signal.makeN("label", "ready")
    _ = Eval.load([("Main", "def answer(): Int = 42\\n")])
  } yield Signal.get(count) == 4 && Signal.get(label) == "ready"

@main def main: IO[Unit] =
  IO.pure(())
''')
    (target / 'signals.scuzz_verify').write_text('oracle signals(): Bool =\n  Main.signals()\n')
    subprocess.run([str(cli), 'fmt', str(target)], env=base_env, check=True,
                   capture_output=True, text=True, timeout=30)
    result = subprocess.run([str(cli), 'fuzz', '--iterations', '0', str(target)],
                            env=base_env, cwd=target / 'work', capture_output=True,
                            text=True, timeout=180)
    detail = result.stdout + result.stderr
    (target / 'campaign.log').write_text(detail)
    assert result.returncode == 0 and 'probes run compiled' not in detail, detail
    out = replay(target, 'signal-parity', 'signals', [
        {'op': 'drive', 'name': 'signals', 'args': []}])
    ev = (out / 'eval.timeline').read_bytes()
    signals = json.loads(ev.decode().split('signals:\n', 1)[1].split('\n', 1)[0])
    assert [(s['name'], s['value']) for s in signals] == [('count', 4), ('label', 'ready')], signals
    print('IO probe: user signals stay visible; internal signal storage stays hidden', flush=True)
PY_PARITY
}

slice_tyck_replay() {
  need_scuzz
  "$SCUZZ" fuzz --iterations 0 examples/tyck
  prove_evaluator_replay tyck
}

prove_probe_startup() {
  python3 - "$ROOT" "$SCUZZ" <<'PY_STARTUP'
import os, pathlib, subprocess, sys, tempfile, time

repo, cli = map(pathlib.Path, sys.argv[1:])
env = {k: v for k, v in os.environ.items() if not k.startswith('SCUZZ_')}
with tempfile.TemporaryDirectory(prefix='scuzz-probe-startup-') as directory:
    target = pathlib.Path(directory)
    (target / 'src').mkdir()
    (target / 'probe').mkdir()
    (target / 'scuzz.toml').write_text(
        '[package]\nname="probe-startup"\n[dependencies]\ncompiler={path="' +
        os.path.relpath(repo / 'examples/compiler', target) + '"}\n')
    (target / 'src/Main.scuzz').write_text('''import Drive.FuzzJob
import Parse.En

def waitStopped(pid: Int): IO[Int] =
  Sys.alive(pid).flatMap(alive => if (alive == 0) IO.pure(0) else IO.sleep(10).flatMap(_ => waitStopped(pid)))

@main def main: IO[Unit] =
  for {
    server <- Sys.getenv("PROBE_SERVER")
    dir <- Sys.getenv("PROBE_DIR")
    mode <- Sys.getenv("PROBE_MODE")
    srv <- Ref.of(("", 0))
    files = []: List[(String, String)]
    ens = []: List[En]
    faults = []: List[(Int, Int)]
    job = FuzzJob(dir, dir, "", "probe", false, 0, 42, false, files, false, "", false, ens, true, server, srv, faults, false, false, false)
    first <- Drive.evProbe(job, dir, "KIT=sealed\\n")
    before <- Ref.get(srv)
    second <- if (mode == "slow") Drive.evProbe(job, dir, "KIT=sealed\\n") else IO.pure(1)
    after <- Ref.get(srv)
    _ <- Drive.evStop(job)
    alive <- if (before._2 == 0) IO.pure(0) else IO.timeout(1000, waitStopped(before._2))
    valid = if (mode == "slow") first == 0 && second == 124 && before._2 != 0 && before == after else first == (if (mode == "reject") 3 else 1) && before._2 == 0 && after._2 == 0
    _ <- if (valid && alive == 0) IO.println("probe-startup-ok") else IO.fail(s"probe startup differs: first=$first second=$second before=${before._2} after=${after._2} alive=$alive")
  } yield ()
''')
    rejected = target / 'rejected'
    rejected.mkdir()
    (rejected / 'files.txt').write_text('Main\n')
    (rejected / '0.scuzz').write_text('def invalid(): String = 1\n@main def main: IO[Unit] = IO.pure(())\n')
    result = subprocess.run([str(cli), 'eval', '--probe', str(rejected)],
                            input='', env=env, capture_output=True, text=True, timeout=10)
    assert result.returncode != 0 and result.stdout.startswith('3\n'), result.stdout + result.stderr
    assert 'type error' in result.stdout + result.stderr, result.stdout + result.stderr
    print('Probe startup: invalid source reports its check failure before a request', flush=True)
    server = target / 'server'
    server.write_text('''#!/usr/bin/env python3
import os, sys, time
mode = os.environ['PROBE_MODE']
if mode == 'closed':
    sys.exit(1)
if mode in ['bad', 'reject']:
    print('3' if mode == 'reject' else 'invalid', flush=True)
    time.sleep(60)
    sys.exit(1)
time.sleep(12)
print('ready', flush=True)
for number, line in enumerate(sys.stdin):
    assert line.strip() == 'probe'
    if number == 0:
        time.sleep(19)
    print('0' if number == 0 else '124', flush=True)
''')
    server.chmod(0o755)
    subprocess.run([str(cli), 'fmt', str(target)], env=env, check=True,
                   capture_output=True, text=True, timeout=30)
    native_env = dict(env, PROBE_SERVER=str(server), PROBE_DIR=str(target / 'probe'), PROBE_MODE='closed')
    result = subprocess.run([str(cli), 'run', str(target)], env=native_env,
                            capture_output=True, text=True, timeout=180)
    assert result.returncode == 0 and 'probe-startup-ok' in result.stdout, result.stdout + result.stderr
    for mode in ['bad', 'reject', 'slow']:
        started = time.monotonic()
        native_env['PROBE_MODE'] = mode
        result = subprocess.run([str(target / 'build/probe-startup')], env=native_env,
                                capture_output=True, text=True, timeout=45)
        seconds = time.monotonic() - started
        assert result.returncode == 0 and result.stdout == 'probe-startup-ok\n', result.stdout + result.stderr
        if mode == 'slow':
            assert seconds >= 30, seconds
        print(f'Probe startup {mode}: {seconds:.2f} s; separate waits, server reuse, and shutdown pass', flush=True)
    native_env.update(PROBE_SERVER=str(cli), PROBE_DIR=str(rejected), PROBE_MODE='reject')
    result = subprocess.run([str(target / 'build/probe-startup')], env=native_env,
                            capture_output=True, text=True, timeout=10)
    assert result.returncode == 0 and result.stdout == 'probe-startup-ok\n', result.stdout + result.stderr
    print('Probe startup: the client preserves invalid-source status 3', flush=True)
PY_STARTUP
}

prove_lexer_stack() {
  python3 - "$ROOT" "$SCUZZ" <<'PY_LEXER'
import os, pathlib, resource, subprocess, sys, tempfile

repo, cli = map(pathlib.Path, sys.argv[1:])
with tempfile.TemporaryDirectory(prefix='scuzz-lexer-') as directory:
    target = pathlib.Path(directory)
    (target / 'src').mkdir()
    (target / 'scuzz.toml').write_text(
        '[package]\nname="lexer-stack"\n[dependencies]\nsyntax={path="' +
        os.path.relpath(repo / 'examples/syntax', target) + '"}\n')
    (target / 'src/Main.scuzz').write_text('''import Lexer.SpTok
import Lexer.Tok

def tokenOk(p: SpTok, i: Int, n: Int): Bool =
  p.stem == "batch" && p.off == i * 5 && (p.tok match {
    case Tok.Ident(name) => i < n && name == "item"
    case Tok.Eof => i == n
    case _ => false
  })

def batchOk(n: Int): Bool =
  for {
    text = List.join(List.fill(n, "item "), "")
    tokens = Lexer.lexStem(text, "batch")
  } yield List.len(tokens) == n + 1 && List.forall(List.zipWithIndex(tokens), row => tokenOk(row._2, row._1, n))

@main def main: IO[Unit] =
  IO.pure(()).flatMap(_ => if (List.forall([0, 1, 100000], n => batchOk(n))) IO.println("lexer-stack-ok") else IO.fail("lexer token names, offsets, or end marker differ"))
''')
    subprocess.run([str(cli), 'fmt', str(target)], check=True,
                   capture_output=True, text=True, timeout=30)
    result = subprocess.run([str(cli), 'run', str(target)],
                            capture_output=True, text=True, timeout=180)
    assert result.returncode == 0 and 'lexer-stack-ok' in result.stdout, result.stdout + result.stderr
    def limit():
        if sys.platform.startswith('linux'):
            resource.setrlimit(resource.RLIMIT_AS, (512 * 1024 * 1024,) * 2)
            resource.setrlimit(resource.RLIMIT_STACK, (8 * 1024 * 1024,) * 2)
    result = subprocess.run([str(target / 'build/lexer-stack')],
                            capture_output=True, text=True, timeout=20, preexec_fn=limit)
    assert result.returncode == 0 and result.stdout == 'lexer-stack-ok\n', result.stdout + result.stderr
    print('Native lexer: 100,000 identifiers, exact names, offsets, and end marker; 8 MiB stack', flush=True)
PY_LEXER
}

slice_codegen_replay() {
  need_scuzz
  "$SCUZZ" fuzz --iterations 0 examples/codegen
  prove_evaluator_replay codegen
  prove_lexer_stack
  prove_probe_startup
}

slice_oracles() {
  slice_hello
  slice_tyck
  slice_kits
  slice_codegen
  slice_tyck_replay
  slice_codegen_replay
  slice_hello_outdir
  slice_fixedpoint
}

slice_web() (
  need_scuzz
  need_cmd python3 'Install Python 3 to build for the web'
  python3 crates/embedder-web/test_sdk.py
  need_cmd node 'Install Node.js and Playwright 1.63.0 with Chromium'
  web_tmp="$(mktemp -d)"
  trap 'rm -rf "$web_tmp"' EXIT
  "$SCUZZ" check examples/docs
  "$SCUZZ" package --target web --out-dir "$web_tmp/docs output" examples/docs
  mkdir -p "$web_tmp/sdk/crates/embedder-web"
  cat > "$web_tmp/sdk/crates/embedder-web/build.sh" <<'SH'
echo 'web compiler failed' >&2
exit 23
SH
  if SCUZZ_HOME="$web_tmp/sdk" "$SCUZZ" package --target web \
      --out-dir "$web_tmp/failed output" examples/docs > "$web_tmp/failure.log" 2>&1; then
    echo 'web must report a failed compiler process' >&2
    exit 1
  fi
  grep -q 'web compiler failed' "$web_tmp/failure.log"
  grep -q 'web package failed with exit code 23' "$web_tmp/failure.log"
  node crates/embedder-web/test.cjs "$web_tmp/docs output/package/web"
  "$SCUZZ" fuzz --iterations 0 examples/docs
  if "$SCUZZ" package --target web examples/hello; then
    echo 'web must reject a package without [ui]' >&2
    exit 1
  fi
)

slice_pr() {
  maybe_wipe
  export CI=1
  slice_macos_smoke
  slice_oracles
  slice_kernel
  slice_ui
  slice_fuzz
  slice_delta
}

slice_linux_headless() {
  maybe_wipe
  export CI=1
  slice_install_dry
  slice_runtime
  slice_asan
  slice_skia
  slice_embedders
  slice_oracles
  slice_kernel
  slice_package
  slice_ui
  slice_gpu
  slice_differential
  slice_fuzz
  slice_delta
  slice_new_ui
  slice_desktop
  slice_mobile
}

SLICE="${1:-pr}"
case "$SLICE" in
  -h|--help|help) slice_help ;;
  pr) slice_pr ;;
  linux-headless) slice_linux_headless ;;
  macos-smoke) slice_macos_smoke ;;
  macos-hello) slice_macos_hello ;;
  install-dry) slice_install_dry ;;
  fetch-skia) prove_fetch_skia_retry ;;
  runtime) slice_runtime ;;
  asan) slice_asan ;;
  skia) slice_skia ;;
  embedders) slice_embedders ;;
  hello) slice_hello ;;
  tyck) slice_tyck ;;
  kits) slice_kits ;;
  codegen) slice_codegen ;;
  compiler-cases) slice_compiler_cases ;;
  tyck-replay) slice_tyck_replay ;;
  codegen-replay) slice_codegen_replay ;;
  hello-outdir) slice_hello_outdir ;;
  fixedpoint) slice_fixedpoint ;;
  kernel) slice_kernel ;;
  package) slice_package ;;
  ui) slice_ui ;;
  ui-test) slice_ui_test ;;
  gpu) slice_gpu ;;
  differential) slice_differential ;;
  fuzz) slice_fuzz ;;
  delta) slice_delta ;;
  new-ui) slice_new_ui ;;
  desktop) slice_desktop ;;
  mobile) slice_mobile ;;
  ios) slice_ios ;;
  macos-app) slice_macos_app ;;
  web) slice_web ;;
  oracles) slice_oracles ;;
  *)
    echo "unknown slice: $SLICE"
    echo "./scripts/ci.sh --help"
    exit 1
    ;;
esac
