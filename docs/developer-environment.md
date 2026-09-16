# Developer environment

Host setup for a checkout. App author path: run `scuzz docs start`. Product locks: [`philosophy.md`](philosophy.md).

Fail on the first missing tool with one install line.

## Required

| Tool | Role |
| --- | --- |
| `clang` | C11 runtime, Skia ABI, embedders, LLVM IR link |
| `make` | `crates/runtime`, `ffi-skia`, embedders |
| `curl` | fetch tagged bootstrap `scuzz` (`scripts/bootstrap.sh`; newest GitHub `v*` release) |
| `zlib` + `bzip2` | Linux Skia CPU prebuilt |
| OpenSSL | Live `https://` on the blessed HTTP kit |

```bash
# Debian/Ubuntu
sudo apt-get install -y clang make curl zlib1g-dev libbz2-dev libssl-dev

# macOS (Homebrew keeps OpenSSL keg-only; the Makefiles and link lines use its prefix)
brew install openssl@3
```

If `clang` cannot find libstdc++ when it links Skia:

```bash
export LIBRARY_PATH=/usr/lib/gcc/x86_64-linux-gnu/13
```

## Optional CI slices

| Slice | Packages |
| --- | --- |
| ASan (`make -C crates/runtime test-asan`) | `libclang-rt-18-dev` (match host clang) |
| GPU (`SCUZZ_SKIA=gpu`) | `libegl1-mesa-dev libgles2-mesa-dev libgl1-mesa-dri xvfb` |
| Desktop X11 | `libx11-dev xvfb` |

ASan skips when the ASan runtime cannot link. GPU needs `LIBGL_ALWAYS_SOFTWARE=1` when the host has no GPU.

## Git hooks

```bash
./scripts/install-githooks.sh
```

A pre-commit hook checks conflict markers. When you stage runtime, ffi-skia, or embedder `.c`, it compiles with `-Werror`. Bypass with `git commit --no-verify`.

## Prove the host

```bash
./scripts/bootstrap.sh
./examples/cli/build/cli run examples/hello
make -C crates/runtime test CC=clang
make -C crates/runtime test-asan CC=clang   # skip if ASan cannot link
```

Same slices as GitHub. List them, then run the required PR path:

```bash
./scripts/ci.sh --help
./scripts/ci.sh pr
```

`pr` runs macos-smoke, oracles, kernel, ui, and fuzz. The Darwin smoke includes the relocated UI app bundle and source reload proof. A local run wipes example `build/` dirs except `examples/cli/build`. That wipe stops a rebuilt CLI from a fingerprint hit on a stale `.ll`. GitHub sets `CI=true` and skips the wipe. Jobs that fetch the Skia CPU prebuilt restore `third_party/skia/prebuilt` from a cache keyed by OS, arch, and `third_party/skia/PIN`. `scripts/fetch_skia.sh` retries HTTP 502, 503, and 504, and a truncated gzip.

Run all Linux slices in sequence with `./scripts/ci.sh linux-headless`. CI builds the product CLI once. Eight check jobs use that artifact in separate checkouts. The `linux-headless` check requires all eight jobs and the build to pass. Each slice prints its elapsed time. A new PR update cancels its older CI run. Apt install and artifact upload stay in `.github/workflows/ci.yml`.

| Env | Paint |
| --- | --- |
| unset | Skia CPU prebuilt |
| `SCUZZ_SKIA=sk_sw` | in-tree software |
| `SCUZZ_SKIA=gpu` | software paint, OpenGL present |

## Web build tools

The macOS UI package target and the browser package target need Python 3. It downloads the pinned Emscripten
SDK on the first build and reuses it from the host cache. It does not need
`emcc` on `PATH` or a shell activation command. Cache paths and cleanup:
run `scuzz docs web`.
The native install script does not install this SDK.

Build the product CLI with `./scripts/bootstrap.sh`. Then run:

```bash
./examples/cli/build/cli package --target web examples/docs
```

The `web` CI slice also needs Node.js and Playwright 1.63.0 with Chromium, Firefox, and WebKit.
Install the browser binaries and host libraries with
`playwright install --with-deps chromium firefox webkit`.
Set `NODE_PATH` to the directory that contains the installed Playwright module.
Run `./scripts/ci.sh web`. The browser check starts a temporary local HTTP
server and closes it when the check ends. The checks include phone emulation.

For a real phone check, serve the web output through HTTPS. Open Docs on the
phone. Copy a command with a long press. Zoom with two fingers. Scroll the page.
Open GUI and focus each edit field. Check that the keyboard does not cover the
field. Enter accented text, emoji, and IME text. Paste text. Rotate the phone.
Switch sections and return to check the stored text. Emulation does not prove
these OS keyboard and selection behaviors.

## iOS simulator loop

Use Xcode on an Apple Silicon Mac. Install an iOS simulator runtime in Xcode.
Build the product CLI with `./scripts/bootstrap.sh`. Then run:

```bash
./examples/cli/build/cli devices
./examples/cli/build/cli run --target ios --watch examples/counter
```

Use `--device` with an exact simulator name or ID. Enter `r` and press Return
to rebuild and restart. Enter `q` and press Return to stop. Ctrl+C stops the
session and app. The simulator stays available. App source edits reuse native
objects. The Headless verification path stays required. App instructions and
target limits: run `scuzz docs ios`.

Source edits reload the View and preserve Signals. Manifest changes restart the app.
The session writes live `debug.json` and `record.json` to the build directory.
Write live input to `inject.json` in that directory. Replay recorded input with Headless.

Run `./scripts/ci.sh ios` for the simulator proof. The Darwin PR job runs this slice.
The loop proof checks dependency edits, source diagnostics, error recovery,
state preservation, capture rejection, native object reuse, manual restart, quit, and interruption.
The UIKit proof checks safe areas, viewport changes, software keyboard input,
and keyboard dismissal. It restores the keyboard preference after the run.
Both proofs use the selected simulator. Each proof removes its app.
The Net proof uses a separate temporary simulator. It adds a test root only to that simulator. It deletes the simulator after the proof. It checks HTTP methods, limits, cancellation, and platform trust. The network UI shows loading, failure, and retry. Input continues during a request. The slice also runs the Counter and network UI Headless claims. macos-app proves public HTTPS with OpenSSL certificate paths disabled.
Set `SCUZZ_IOS_DEVICE` to select a simulator name or ID.

## Compiler campaigns

Default `./scripts/ci.sh fuzz` replays `examples/tyck` and `examples/codegen` with `scuzz fuzz --iterations 0`. That path uses generated-program oracles plus fixture seeds.

Set `SCUZZ_COMPILER_FUZZ=1` to run a short search campaign on a source copy. That path is slower. Use it for a nightly or local extra check. The env var is the real opt-in. `./scripts/ci-fuzz.sh` invokes it.
