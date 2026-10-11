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
| UBSan (`make -C crates/runtime test-ubsan`) | `libclang-rt-18-dev` (match host clang) |
| GPU (`SCUZZ_SKIA=gpu`) | `libegl1-mesa-dev libgles2-mesa-dev libgl1-mesa-dri xvfb` |
| Desktop X11 | `libx11-dev xvfb` |

ASan skips when the ASan runtime cannot link. GPU needs `LIBGL_ALWAYS_SOFTWARE=1` when the host has no GPU.

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

A pre-commit hook checks conflict markers. When you stage runtime, ffi-skia, or embedder `.c`, it compiles with `-Werror`. Bypass with `git commit --no-verify`.

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

Run all Linux slices in sequence with `./scripts/ci.sh linux-headless`. CI builds the product CLI once. Eight check jobs use that artifact in separate checkouts. The `linux-headless` check requires all eight jobs and the build to pass. Each slice prints its elapsed time. A new PR update cancels its older CI run. Apt install and artifact upload stay in `.github/workflows/ci.yml`.

| Env | Paint |
| --- | --- |
| unset | Skia CPU prebuilt |
| `SCUZZ_SKIA=sk_sw` | in-tree software |
| `SCUZZ_SKIA=gpu` | OpenGL raster. No CPU paint pass |

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
The Net proof uses a separate temporary simulator. It adds a test root only to that simulator. It deletes the simulator after the proof. It checks HTTP methods, limits, cancellation, and platform trust. The iOS test app writes proof output and its exit status to a file. The check requires complete proof output and status zero within the launch deadline. The network UI shows loading, failure, and retry. Input continues during a request. The slice also runs the Counter and network UI Headless claims. macos-app proves public HTTPS with OpenSSL certificate paths disabled.
Set `SCUZZ_IOS_DEVICE` to select a simulator name or ID.

## Compiler campaigns

Regular `./scripts/ci.sh fuzz` runs `./scripts/ci.sh compiler-cases`. This slice uses one bounded source generator. It checks 54 fixed cases across six source families, three input signs, and three expression depths. It compares evaluator and native output with an independent host model. It rejects invalid types and proves that the model rejects a wrong result when both engines agree. It runs 16-iteration search and mutation campaigns on the generated programs and the generator. The generator campaign derives its seed from the mutation sites in `Cases.evaluated`. Native subprocesses run outside simulation. Campaigns use temporary packages. A failure keeps the packages and logs.

The separate `tyck-replay` and `codegen-replay` slices preserve compiler corpus replay. Set `SCUZZ_COMPILER_FUZZ=1` to add the larger proof packages to the search campaign on source copies. These extra campaigns are slower. The bounded checks are finite. They do not prove correctness for every program.

## Formal verification experiment

`examples/proof` supplies the formal experiment in `vision.md`.
The example `Proof` module translates checked Int and Bool expressions.
It uses signed 64-bit comparisons and arithmetic modulo 2^64.
It rejects effects, recursion, implicit assumptions, and unsupported expressions.
The same `balance` oracle supplies fuzz checks and solver queries.
The optional checker report covers this experiment.
Run the required project checks separately with `./scripts/ci.sh pr`.

The experiment uses an optional Z3 executable. It does not install a solver.
For a local isolated installation, use:

```bash
python3 -m venv /tmp/scuzz-proof-solver
/tmp/scuzz-proof-solver/bin/pip install z3-solver==4.15.4.0
python3 scripts/proof-experiment.py --solver /tmp/scuzz-proof-solver/bin/z3
```

Set `SCUZZ_SOLVER` or pass `--solver` to select the executable.
Set `SCUZZ` or pass `--cli` to select the product compiler.
Run the script from any directory. It uses the checkout root.
It writes evidence to `examples/proof/build/experiment/summary.json`.
Each run replaces the previous result before checks start.
Missing solver support returns an incomplete result and exit status 2.
Unknown solver results and deadline failures cannot produce a proof result.

The check covers all signed 64-bit inputs for one pure balance invariant.
It checks assumption consistency and required trigger reach separately.
It checks an overflow defect at maximum Int.
It replays that input and minimum Int through both engines.
It compares 70 expression results with independent host arithmetic.
It also checks live input validation in both engines.
Its ordinary 16-iteration fuzz campaign uses the same author oracle.
The effect protocol starts two requests in sequence.
The first request runs in a worker fiber. Its caller can cancel it.
The second request starts after the first request settles.
The checker examines every ready fiber choice for inputs 1 and 2.
It checks no fault, one file failure, or one interrupted write.
The fault call bound is 1 through 4. Setup does not consume this bound.
The scheduler bounds are 16 contention choices and 128 steps per driver run.
The execution budget is 1024. An unmet bound leaves the check incomplete.
Each request must release its resource once.
The checker requires one release after the first join and two after both requests.
A request waits at least one clock unit before its write.
A failed write must preserve the previous file.
Each write payload has seven bytes. An interrupted write stages three bytes.
Cancellation must preserve the first request file.
The same live assertions and timeline claims run in ordinary fuzzing.
Recorded scheduler choices replay through both engines.
The check compares exact picks, timelines, claim records, rejection records,
and executed property names.
It also compares permitted scheduler choices and terminal verdicts.
It checks the scheduler step bound in each engine.
It rejects a release defect with a concrete replay workload.
These finite schedules do not prove eventual completion for all executions.
The report records queries, source identities, tool identities, trust, and cost.
The checking budget is 60 seconds after the experiment package build.
One Linux sample uses 468 scheduler executions and 252 distinct replay paths.
It reaches at most four contention choices per run.
Native runs reach at most 39 scheduler steps. Evaluator runs reach at most 41.
Checking takes 17.97 seconds. The cached package build takes 4.58 seconds.
A cold package build takes 35.91 seconds.
These measurements apply to this finite experiment on this host.
These results do not establish an inductive proof or correctness for other programs.

### Application recovery comparison

Run the application check without a solver:

```bash
python3 scripts/proof-experiment.py --recovery
```

The report lives in `examples/proof/build/recovery/summary.json`.
Each run uses a new scratch directory. It retains logs and replay workloads.
The script selects exact function bodies from the editor recovery code and its helpers.
It compiles these bodies with the existing compiler.
It exports the evaluator graph through the existing compiler verification modules.
Source selection remains a trusted step. This check does not run the complete IDE.
The fixture source lives in `examples/proof/recovery.scuzz`.

The check uses two source files and four journal states.
They cover interrupted acceptance, interrupted Undo, and an external edit before recovery.
One pending source write runs before journal recovery.
Two further recovery attempts check completion and repeated recovery.
Four more inputs cancel this operation in a worker fiber.
The caller waits for worker entry before cancellation.
External edits during recovery and live process termination remain unchecked.

Faults cover no fault or one file failure or interrupted write at calls 1 through 16.
Setup does not consume the fault bound.
The execution limit is 2048. Each driver has a 1000-step limit.
The replay record retains every contention choice.
The checker permits up to 1000 contention choices within the driver step limit.
Replay rejects missing, additional, or unavailable recorded choices.
The 16-point schedule sampling policy stays separate from replay storage.
Bounded paths compare exact scheduler choices, timelines, claims, and author rule execution.
They also compare evaluator and native terminal verdicts.
The sequential sub-scope covers 132 executions.
The complete application result stays incomplete when the checking budget ends.
The report retains the number of pending scheduler prefixes.

Both methods use the same author rules and a 60-second checking limit per source variant.
Build time stays separate. Ordinary fuzzing uses 64 iterations and seed 42.
Eight corpus inputs exercise acceptance, Undo, conflict, and cancellation.
The search continues after a required failure to check both defect controls.
Setup writes fixed journal inputs without application helpers.
Unexpected recovery errors reject an author rule. Tool failures stay incomplete.
Its default search also includes repeated drives and persistent faults.
The driver retains a pending operation when file access remains unavailable.
The finite checker requires all six live assertions on each passing bounded path.
The methods have different search scopes. Their elapsed times do not establish a speed advantage.

One Linux sample checks 727 native executions and 726 exact replay paths.
The 132 sequential paths pass. Cancellation paths reach 144 contention choices.
The 60-second budget ends with more than 37000 scheduler prefixes pending.
Ordinary fuzzing takes about 6 seconds. Its nine required probes and 40 search probes pass.
It detects all 24 sampled mutations. No sampled mutation remains incomplete.
These sampled mutations affect fixture functions.
Three direct mutation checks exercise application helpers.
They corrupt field lookup and path guards.
Their setup passes. Each check rejects the recovery error in both engines.
These three checks also reproduce through ordinary native replay.
Both methods reject a missing-write defect and an external-overwrite defect.
Finite counterexamples also reproduce through ordinary native replay.
The complete comparison returns exit status 2 because finite exploration remains incomplete.
These controls do not establish a new application defect or a general method advantage.
