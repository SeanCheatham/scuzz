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
  pr              macos-smoke + oracles + kernel + ui + fuzz
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
  hello-outdir    hello via --out-dir
  fixedpoint      LLVM IR fixed-point
  kernel          ./scripts/ci-kernel.sh
  package         release tarball + install smoke
  ui              headless counter/studio/editor + fuzz --iterations 0
  ui-test         corpus-only fuzz for counter/studio/editor
  gpu             GPU presenter (needs xvfb)
  differential    skia vs sk_sw vs gpu dumps (needs xvfb)
  fuzz            ./scripts/ci-fuzz.sh
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
prove_fetch_skia_retry() {
  need_cmd python3 "sudo apt-get install -y python3"
  _prove_fetch_skia_retry
}

_prove_fetch_skia_retry() (
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

@main def main: IO[Unit] =
  IO.println(value("ok"))
SOURCE
  "$SCUZZ" build --full "$memory_dir"
  python3 - "$memory_dir/build/match-memory.ll" <<'PY_IR'
from pathlib import Path
import sys
p = Path(sys.argv[1])
s = p.read_text()
for ty, name in (("i64", "size"), ("ptr", "value"), ("i64", "recordSize"), ("ptr", "recordValue"), ("i64", "tailSize"), ("ptr", "tailValue")):
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
extern int64_t sz_user_Main_size(SzString *);
extern SzString *sz_user_Main_value(SzString *);
extern int64_t sz_user_Main_recordSize(SzString *);
extern SzString *sz_user_Main_recordValue(SzString *);
extern int64_t sz_user_Main_tailSize(SzString *, int64_t);
extern SzString *sz_user_Main_tailValue(SzString *, int64_t);
int main(void) {
  SzString *input = sz_string_from_cstr("payload");
  size_t before, after;
  {
    /* Warm up: interned string literals pin on first evaluation. */
    SzString *warm = sz_user_Main_value(input);
    sz_release(warm);
    warm = sz_user_Main_recordValue(input);
    sz_release(warm);
    warm = sz_user_Main_tailValue(input, 1);
    sz_release(warm);
    assert(sz_user_Main_size(input) == 7);
    assert(sz_user_Main_recordSize(input) == 7);
    assert(sz_user_Main_tailSize(input, 1) == 7);
  }
  sz_alloc_stats(&before, NULL);
  for (int i = 0; i < 1000; ++i) {
    assert(sz_user_Main_size(input) == 7);
    SzString *output = sz_user_Main_value(input);
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
  need_scuzz
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
    scuzz run examples/hello | tee /tmp/rel-hello.out
    grep -q "Hello, Scuzz!" /tmp/rel-hello.out
    grep -q "ready." /tmp/rel-hello.out
    scuzz run --out-dir /tmp/scuzz-rel-cli examples/cli | tee /tmp/rel-cli.out
    grep -q "cli-ok" /tmp/rel-cli.out
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
  (cd scratchpad/editor && SCUZZ_HOME="$ROOT" "$SCUZZ" fuzz --iterations 0 "$ROOT/examples/editor")
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
}

slice_gpu() {
  need_scuzz
  need_cmd xvfb-run "sudo apt-get install -y xvfb"
  xvfb-run -a env SCUZZ_SKIA=gpu make -C crates/ffi-skia test -j"$JOBS" CC=clang
  xvfb-run -a env SCUZZ_SKIA=gpu "$SCUZZ" fuzz --iterations 0 examples/counter
  make -C crates/ffi-skia clean
  make -C crates/ffi-skia lib -j"$JOBS" CC=clang
}

slice_differential() {
  need_scuzz
  need_cmd xvfb-run "sudo apt-get install -y xvfb"
  xvfb-run -a "$SCUZZ" fuzz --differential --iterations 0 examples/counter
  make -C crates/ffi-skia clean
  make -C crates/ffi-skia lib -j"$JOBS" CC=clang
}

slice_fuzz() {
  need_scuzz
  need_cmd python3 "sudo apt-get install -y python3"
  maybe_wipe
  ./scripts/ci-fuzz.sh
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

slice_tyck_replay() {
  need_scuzz
  "$SCUZZ" fuzz --iterations 0 examples/tyck
}

slice_codegen_replay() {
  need_scuzz
  "$SCUZZ" fuzz --iterations 0 examples/codegen
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
