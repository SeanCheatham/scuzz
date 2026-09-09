#!/usr/bin/env bash
# Same slices as `.github/workflows/ci.yml`. Run one slice locally.
# Fingerprint does not include the product CLI. These slices wipe example
# `build/` dirs (not `examples/cli/build`) so a rebuilt compiler cannot reuse a stale `.ll`.
set -eo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
SCUZZ="${SCUZZ:-$ROOT/examples/cli/build/cli}"
export SCUZZ
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

# GitHub sets CI=true. A clean runner has no stale .ll. Local runs wipe so a
# rebuilt CLI cannot reuse a fingerprint hit from an older compiler.
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
  macos-hello     hello build/test, kernel check, bad-intent
  oracles         hello, tyck, kits, codegen, hello-outdir, fixedpoint
  install-dry     installer and bump_version dry-run
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
  ui              headless counter/studio/editor + goldens
  ui-test         golden tests without headless run
  pixels          sk_sw pixel goldens
  gpu             GPU presenter (needs xvfb)
  differential    skia vs sk_sw vs gpu dumps (needs xvfb)
  fuzz            ./scripts/ci-fuzz.sh
  new-ui          scuzz new --ui path
  desktop         Desktop peer + X11 (needs xvfb)
  mobile          mobile shell + package targets
  web             Docs package + browser input + Headless claims

Examples:
  ./scripts/bootstrap.sh
  ./scripts/ci.sh pr
  ./scripts/ci.sh ui
  ./scripts/ci.sh fuzz
  SCUZZ=./examples/cli/build/cli ./scripts/ci.sh macos-smoke
EOF
}

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
}

slice_runtime() {
  make -C crates/runtime test CC=clang
}

slice_asan() {
  make -C crates/runtime test-asan CC=clang
}

slice_skia() {
  make -C crates/ffi-skia test CC=clang
  grep -qx skia crates/ffi-skia/build/sk_capi_backend
}

slice_embedders() {
  make -C crates/ffi-skia lib CC=clang
  grep -qx skia crates/ffi-skia/build/sk_capi_backend
  make -C crates/embedder-desktop lib CC=clang
  make -C crates/embedder-mobile lib CC=clang
}

slice_hello() {
  need_scuzz
  maybe_wipe
  "$SCUZZ" run examples/hello | tee /tmp/hello.out
  grep -q "Hello, Scuzz!" /tmp/hello.out
  grep -q "ready." /tmp/hello.out
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
  "$SCUZZ" run examples/codegen | tee /tmp/codegen.out
  grep -q "ir-ok" /tmp/codegen.out
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
    scuzz test --update /tmp/scuzz-release-app
    scuzz test /tmp/scuzz-release-app
    test -f /tmp/scuzz-release-app/goldens/scuzz-release-app.dump
    grep -qx skia "$prefix/share/scuzz/crates/ffi-skia/build/sk_capi_backend"
    scuzz run --headless /tmp/scuzz-release-app
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
  maybe_wipe
  "$SCUZZ" run --headless examples/counter
  test -f examples/counter/build/snapshot.png
  "$SCUZZ" test examples/counter
  "$SCUZZ" run --headless examples/studio
  test -f examples/studio/build/snapshot.png
  "$SCUZZ" test examples/studio
  "$SCUZZ" run --headless examples/editor
  test -f examples/editor/build/snapshot.png
  "$SCUZZ" test examples/editor
}

slice_ui_test() {
  need_scuzz
  "$SCUZZ" test examples/counter
  "$SCUZZ" test examples/studio
  "$SCUZZ" test examples/editor
}

slice_pixels() {
  need_scuzz
  SCUZZ_SKIA=sk_sw make -C crates/ffi-skia clean lib CC=clang
  rm -rf examples/counter/build
  SCUZZ_SKIA=sk_sw "$SCUZZ" test --pixels examples/counter
  make -C crates/ffi-skia clean lib CC=clang
}

slice_gpu() {
  need_scuzz
  need_cmd xvfb-run "sudo apt-get install -y xvfb"
  xvfb-run -a env SCUZZ_SKIA=gpu make -C crates/ffi-skia test CC=clang
  xvfb-run -a env SCUZZ_SKIA=gpu "$SCUZZ" test examples/counter
  make -C crates/ffi-skia clean lib CC=clang
}

slice_differential() {
  need_scuzz
  need_cmd xvfb-run "sudo apt-get install -y xvfb"
  xvfb-run -a "$SCUZZ" test --differential examples/counter
  make -C crates/ffi-skia clean lib CC=clang
}

slice_fuzz() {
  need_scuzz
  maybe_wipe
  ./scripts/ci-fuzz.sh
}

slice_new_ui() {
  need_scuzz
  rm -rf /tmp/scuzz-v0app
  "$SCUZZ" new --ui --path /tmp scuzz-v0app
  "$SCUZZ" fmt --check /tmp/scuzz-v0app
  "$SCUZZ" check /tmp/scuzz-v0app
  "$SCUZZ" fuzz --iterations 0 /tmp/scuzz-v0app
  "$SCUZZ" test --update /tmp/scuzz-v0app
  test -f /tmp/scuzz-v0app/goldens/scuzz-v0app.dump
  test -f /tmp/scuzz-v0app/goldens/scuzz-v0app_after_tap.dump
  "$SCUZZ" test /tmp/scuzz-v0app
  "$SCUZZ" run --headless /tmp/scuzz-v0app
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

slice_mobile() {
  need_scuzz
  test -x examples/counter/build/counter || slice_ui
  env SCUZZ_UI_RUNTIME=mobile SCUZZ_MOBILE_SHELL=1 SCUZZ_UI_WIDTH=200 SCUZZ_UI_HEIGHT=120 \
    ./examples/counter/build/counter 2>&1 | tee /tmp/mobile.out
  grep -q "UiRuntime.Mobile" /tmp/mobile.out
  grep -q "scuzz mobile: present" /tmp/mobile.out
  "$SCUZZ" package --target host examples/counter
  test -x examples/counter/build/package/host/run.sh
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
  env SCUZZ_UI_WIDTH=200 SCUZZ_UI_HEIGHT=120 \
    ./examples/counter/build/package/host/run.sh 2>&1 | tee /tmp/pkg-host.out
  grep -q "scuzz mobile: present" /tmp/pkg-host.out
}

slice_macos_hello() {
  need_scuzz
  maybe_wipe
  "$SCUZZ" build --full examples/hello
  "$SCUZZ" test examples/hello
  "$SCUZZ" check examples/kernel
  if "$SCUZZ" check examples/bad-intent; then
    echo "empty verify should fail check" && exit 1
  fi
}

slice_macos_smoke() {
  slice_runtime
  slice_macos_hello
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
  slice_pixels
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
  pixels) slice_pixels ;;
  gpu) slice_gpu ;;
  differential) slice_differential ;;
  fuzz) slice_fuzz ;;
  new-ui) slice_new_ui ;;
  desktop) slice_desktop ;;
  mobile) slice_mobile ;;
  web) slice_web ;;
  oracles) slice_oracles ;;
  *)
    echo "unknown slice: $SLICE"
    echo "./scripts/ci.sh --help"
    exit 1
    ;;
esac
