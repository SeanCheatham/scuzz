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
| OpenSSL | Live `https://` on the built-in HTTP kit |

```bash
# Debian/Ubuntu
sudo apt-get install -y clang make curl zlib1g-dev libbz2-dev libssl-dev

# macOS (Homebrew keeps OpenSSL keg-only; the Makefiles and link lines use its prefix)
brew install openssl@3
```

If `clang` cannot find libstdc++ when it links Skia:

```bash
export LIBRARY_PATH=/usr/lib/gcc/x86_64-linux-gnu/13${LIBRARY_PATH:+:$LIBRARY_PATH}
```

## Optional CI slices

| Slice | Packages |
| --- | --- |
| ASan (`make -C crates/runtime test-asan`) | `libclang-rt-18-dev` (match host clang) |
| UBSan (`make -C crates/runtime test-ubsan`) | `libclang-rt-18-dev` (match host clang) |
| GPU (`SCUZZ_SKIA=gpu`) | `libegl1-mesa-dev libgles2-mesa-dev libgl1-mesa-dri xvfb` |
| Desktop X11 | `libx11-dev libdbus-1-dev pkg-config dbus-daemon xvfb` |

ASan skips when the ASan runtime cannot link.
The target first checks that ASan rejects a known heap buffer overflow.
It keeps test binaries and file fixtures in `crates/runtime/build/asan-tests/`.
It preserves the normal build and the `lib-asan` archive.
C tests disable leak detection. They still check invalid memory access.
Compiled corpus replay uses `lib-asan` and `scuzz fuzz --asan` with leak detection.
GPU needs `LIBGL_ALWAYS_SOFTWARE=1` when the host has no GPU.

UBSan fails when compile or link support is unavailable.
The target rejects a known signed overflow before the runtime checks.
It compiles and links with `-fsanitize=undefined -fno-sanitize-recover=undefined`.
See [Clang UBSan](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html).
It instruments runtime C, C tests, and reload libraries.
It keeps binaries in `crates/runtime/build/ubsan/`.
Fixtures with relative `build/` paths use this directory.
The Skia wrapper and prebuilt Skia code keep their normal build flags.
These checks do not establish that all runtime behavior is correct.

## Git hooks

```bash
./scripts/install-githooks.sh
```

The pre-commit hook checks the index snapshot for conflict markers.
It compiles staged host C sources with `-Werror` against headers from that snapshot.
An unstaged edit cannot change the result.
Bypass the hook with `git commit --no-verify`.

## Prove the host

```bash
./scripts/bootstrap.sh
./examples/cli/build/cli run examples/hello
make -C crates/runtime test CC=clang
make -C crates/runtime test-asan CC=clang   # skip if ASan cannot link
make -C crates/runtime test-ubsan CC=clang
```

Same slices as GitHub. List them, then run the required PR path:

```bash
./scripts/ci.sh --help
./scripts/ci.sh pr
```

`pr` runs macos-smoke, oracles, kernel, ui, fuzz, and delta. The Darwin smoke includes the relocated UI app bundle and source reload proof. A local run removes example `build/` directories except `examples/cli/build`. This gives the checks a clean start. GitHub sets `CI=true` and skips this removal. Each output directory has its own live and verification cache stamps. Each stamp includes the executing compiler identity. Jobs that fetch the Skia CPU prebuilt restore `third_party/skia/prebuilt` from a cache keyed by OS, arch, and `third_party/skia/PIN`. `scripts/fetch_skia.sh` retries HTTP 502, 503, and 504, and a truncated gzip. The ffi-skia include fetch uses the same download retry.
The fixed-point check uses one temporary directory per run.
A successful run removes it. A failed run prints its path and retains its IR and logs.
Renderer changes select the required archive without deleting the Skia header cache.

Run all Linux slices in sequence with `./scripts/ci.sh linux-headless`. CI builds the product CLI once. Eight check jobs use that artifact in separate checkouts. The `linux-headless` check requires all eight jobs and the build to pass. Each slice prints its elapsed time. A new PR update cancels its older CI run. Apt install and artifact upload stay in `.github/workflows/ci.yml`.

## Native libraries and paint

`crates/runtime` supplies the C runtime, IO scheduler, simulation, and UI sessions.
Its public interfaces are `include/scuzz_rt.h` and `include/scuzz_ui.h`.
`crates/ffi-skia/include/sk_capi.h` defines the paint interface.
Callers do not need Skia headers.
The Desktop and Mobile interfaces are in their crate `include/` directories.
Headless remains a peer runtime.

| Environment | Paint |
| --- | --- |
| unset | Pinned Skia CPU archive |
| `SCUZZ_SKIA=sk_sw` | In-tree software renderer |
| `SCUZZ_SKIA=gpu` | OpenGL renderer. Pixel reads use the framebuffer |

```bash
make -C crates/ffi-skia test
SCUZZ_SKIA=sk_sw make -C crates/ffi-skia test
SCUZZ_SKIA=gpu make -C crates/ffi-skia test
make -C crates/embedder-desktop test
make -C crates/embedder-mobile test
```

The GPU and Desktop checks need the host packages in [Optional CI slices](#optional-ci-slices).
C test builds keep assertions active, including when `CFLAGS` defines `NDEBUG`.
Runtime tests relink after paint selection. They use the archive selected for that run.
The test link includes the math library for each renderer.
The differential CI slice also runs `python3 scripts/test_png_max_delta.py`.
It checks all five PNG filters, chunk integrity, incomplete image rejection, and comparator exit codes.
The comparator reads static RGBA images with eight bits per channel and no interlacing.
Run normal, ASan, and UBSan runtime tests in sequence.
Their network checks use the same local ports.

`scripts/fetch_skia.sh` reads `third_party/skia/PIN`.
The `url` field selects the CPU archive. The `hdr_url` field selects its headers.
`{triple}` becomes the host triple from `scripts/skia_triple.sh`.
Use `SCUZZ_SKIA_TRIPLE` to select another triple.
Use `SCUZZ_SKIA_URL` to select another archive URL.
The fetch fails if it cannot obtain the archive.
It installs `third_party/skia/prebuilt/<triple>/libsk_capi.a`.
The ffi-skia build replaces the archive's interface objects with the local implementation.
It records the selected renderer in `build/sk_capi_backend`.
No Skia source tree is stored in the repository.

The CPU archive contains Skia objects, HarfBuzz, and embedded fonts.
The paint build fetches matching HarfBuzz headers and pinned SheenBidi source.
`third_party/skia/PIN` supplies source revisions and SHA-256 values.
The build verifies each source archive before it extracts files.
The build keeps these sources under `crates/ffi-skia/build/`.
`scripts/fetch_fonts.py` fetches the Noto fallback fonts from Google Fonts.
It also fetches fixed DejaVu files for native Latin and monospace text.
It verifies these files before use. The native build does not select host fonts.
`third_party/fonts/PIN` supplies each revision and SHA-256 value.
Native Skia embeds these files. Web packages supply them as font assets.
The release includes their SIL Open Font License files.
Native application packages include the font and text shaper licenses.
The macOS bundle contains these files before signing.
Linux links C++, math, zlib, and bzip2 libraries.
macOS also links CoreFoundation, CoreGraphics, CoreText, Foundation, and Carbon.
The archive build disables WOFF2 and rejects undefined Brotli symbols.
`scripts/package_release.sh` includes the pinned host archive unless `SCUZZ_SKIA=sk_sw`.

Desktop uses X11 on Linux and Cocoa on macOS.
Linux desktop motion preferences use the Settings portal through D-Bus.
Install its development files before a native UI build:

```bash
sudo apt-get install -y pkg-config libdbus-1-dev
```
AppKit runs on the process main thread. The IO worker sends work to that thread.
The Mobile host shell writes frame and keyboard records to stderr when `SCUZZ_MOBILE_SHELL=1`.
It has no input queue and exits after one frame. Pixels remain offscreen.
Link the Mobile archive with `--whole-archive` on Linux or `-force_load` on macOS.
These options select its implementation instead of the runtime's weak symbols.
Android and iOS shells live in `crates/embedder-mobile/shells/`.
Use a test emulator for the Android motion check:

```bash
python3 crates/embedder-mobile/shells/android/test_motion.py emulator-5554
```

The check builds a temporary app from `examples/motion`.
It checks the initial motion preference, live changes, visible input, resume, and restart.
It also checks keyboard insets, typing, dismissal, reopening, and retained text.
It restores the animation setting and removes the temporary app.
App instructions: run `scuzz docs gui`, `scuzz docs android`, or `scuzz docs ios`.

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

For a real phone check, serve the web output through HTTPS. Open the walkthrough
on the phone. Copy code on Check with a long press. Zoom with two fingers.
Scroll the page. Focus the Run editor. Check that the keyboard does not cover
the field. Enter accented text, emoji, and IME text. Paste text. Rotate the
phone. Switch stages and return to check Signal state. Emulation does not prove
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
The Net proof uses a separate temporary simulator. It adds a test root only to that simulator. It deletes the simulator after the proof. It checks HTTP methods, limits, cancellation, and platform trust. The iOS test app writes proof output and its exit status to a file. The check requires complete proof output and status zero within the launch deadline. The network UI shows loading, failure, and retry. Input continues during a request. The slice also runs the Counter and network UI Headless claims. The iOS console stops its app by simulator ID and bundle ID before it stops the console process. macos-app proves public HTTPS with OpenSSL certificate paths disabled.
Set `SCUZZ_IOS_DEVICE` to select a simulator name or ID.

## Compiler campaigns

Regular `./scripts/ci.sh fuzz` runs `./scripts/ci.sh compiler-cases`. This slice uses one bounded source generator. It checks 54 fixed cases across six source families, three input signs, and three expression depths. It compares evaluator and native output with an independent host model. It rejects invalid types and proves that the model rejects a wrong result when both engines agree. It runs 16-iteration search and mutation campaigns on the generated programs and the generator. The generator campaign derives its seed from the mutation sites in `Cases.evaluated`. Native subprocesses run outside simulation. Campaigns use temporary packages. A failure keeps the packages and logs.

The separate `tyck-replay` and `codegen-replay` slices preserve compiler corpus replay. Set `SCUZZ_COMPILER_FUZZ=1` to add the larger proof packages to the search campaign on source copies. These extra campaigns are slower. The bounded checks are finite. They do not prove correctness for every program.

## Formal verification experiment

`examples/proof` checks the optional methods in
[`vision.md`](vision.md#formal-verification-methods).
The default application check remains `scuzz fuzz`.
Run required project checks separately with `./scripts/ci.sh pr`.
Proof scope, measurements, and open limits live in
[`gaps.md`](gaps.md#formal-verification).

The pure expression check needs an optional Z3 executable.
The script does not install a solver. For an isolated installation, run:

```bash
python3 -m venv /tmp/scuzz-proof-solver
/tmp/scuzz-proof-solver/bin/pip install z3-solver==4.15.4.0
python3 scripts/proof-experiment.py --solver /tmp/scuzz-proof-solver/bin/z3
```

Set `SCUZZ_SOLVER` or pass `--solver` to select the solver.
Set `SCUZZ` or pass `--cli` to select the product compiler.
Run the script from any directory. It uses the checkout root.
The report is `examples/proof/build/experiment/summary.json`.
Each run replaces the previous result before checks start.

### Application recovery comparison

The recovery check needs no solver. Run:

```bash
python3 scripts/proof-experiment.py --recovery
```

The report is `examples/proof/build/recovery/summary.json`.
Each run uses a new scratch directory and retains logs and replay workloads.
The fixture is `examples/proof/recovery.scuzz`.
The script compiles selected production editor functions and their helpers.
It exports their evaluator graph through the shared compiler modules.
Source selection remains a trusted step. The check does not run the complete IDE.

Each source graph uses one evaluator server at a time.
Requests run in isolated children. The script stops its servers on exit.
A closed request stays incomplete. A replacement server keeps earlier evidence
and uses a separate log. It does not change an incomplete scope to complete.

Both reports record source and tool identities, bounds, trust, and command costs.
They retain scheduler choices, timelines, claims, and author rule execution.
Repeated paths reuse comparisons only when their complete observations agree.
Replay rejects missing, additional, or unavailable choices.
Setup does not consume file fault bounds.
A recorded file fault uses `fs:fail:N`, `fs:partial:N`, or `fs:after:N`.
N is an integer from 1 through 2147483647.

The commands return these statuses:

| Status | Meaning |
| --- | --- |
| 0 | All required checks complete and pass |
| 1 | A required check fails in the correct program |
| 2 | No correct scope fails, but required evidence is incomplete |

Missing solver support, unknown solver results, and deadlines remain incomplete.
Expected rejection of a deliberate defect does not fail the correct program.
