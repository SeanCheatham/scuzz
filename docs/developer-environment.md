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
The Net proof uses a separate temporary simulator. It adds a test root only to that simulator. It deletes the simulator after the proof. It checks HTTP methods, limits, cancellation, and platform trust. The iOS test app writes proof output and its exit status to a file. The check requires complete proof output and status zero within the launch deadline. The network UI shows loading, failure, and retry. Input continues during a request. The slice also runs the Counter and network UI Headless claims. The iOS console stops its app by simulator ID and bundle ID before it stops the console process. macos-app proves public HTTPS with OpenSSL certificate paths disabled.
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
It checks no fault, one file failure, one interrupted write, or one error after replacement.
The fault call bound is 1 through 4. Setup does not consume this bound.
The scheduler bounds are 16 contention choices and 128 steps per driver run.
The execution budget is 1024. An unmet bound leaves the check incomplete.
Each request must release its resource once.
The checker requires one release after the first join and two after both requests.
A request waits at least one clock unit before its write.
An error before replacement must preserve the previous file.
An error after replacement must retain the complete new bytes.
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
One Linux sample uses 676 scheduler executions.
It reaches at most four contention choices per run.
Both engines reach at most 36 scheduler steps.
Checking takes 9.68 seconds. The cached package build takes 3.60 seconds.
The zero-, one-, and two-preemption protocol scopes contain 1, 3, and 5 distinct paths.
Each path set matches the same bound applied to the complete protocol schedule set.
These measurements apply to this finite experiment on this host.
The live file check injects a directory sync error after replacement.
The file retains the complete new bytes when the operation returns an error.
Separate process termination checks stop at file sync and directory sync.
They retain the previous file or the complete new file, respectively.
These checks do not establish power-loss behavior or application journal recovery.
These results do not establish an inductive proof or correctness for other programs.

### Application recovery comparison

Run the application check without a solver:

```bash
python3 scripts/proof-experiment.py --recovery
```

The report lives in `examples/proof/build/recovery/summary.json`.
Each run uses a new scratch directory. It retains logs and replay workloads.
Each source graph uses one evaluator server. Each request runs in an isolated child.
The report records graph identities, startup cost, request counts, and request cost.
Each path records native execution, evaluator execution, and comparison cost.
The script stops all owned servers when it completes or fails.
Sampled checks replay each distinct recorded path through the evaluator.
Every native execution retains its author rules and complete scheduler record.
Repeated paths must have equal rule records, scheduler traces, timelines, coverage, output, and errors.
The report names the representative replay and its reused comparisons.
Damaged observations cannot reuse a result.
Source identity collection excludes build directories before traversal.
The script selects exact function bodies from the editor recovery code and its helpers.
It compiles these bodies with the existing compiler.
It exports the evaluator graph through the existing compiler verification modules.
Source selection remains a trusted step. This check does not run the complete IDE.
The fixture source lives in `examples/proof/recovery.scuzz`.

The check uses two source files in each setup state.
The original recovery scope uses four journal states.
They cover interrupted acceptance, interrupted Undo, and an external edit before recovery.
One pending source write runs before journal recovery.
Two further recovery attempts check completion and repeated recovery.
Four more inputs cancel this operation in a worker fiber.
The caller waits for worker entry before cancellation.
External edits during recovery remain unchecked.
Separate live checks stop the process at recovery write boundaries.
Separate Undo setup states start with accepted files or an external edit.
Their first pending write restores the main source file.
They keep the original recovery setup states unchanged.

Faults cover no fault or one file failure, interrupted write, or error after replacement at calls 1 through 16.
Setup does not consume the fault bound.
The execution limit is 2048. Each driver has a 1000-step limit.
The replay record retains every contention choice.
The checker permits up to 1000 contention choices within the driver step limit.
Replay rejects missing, additional, or unavailable recorded choices.
The 16-point schedule sampling policy stays separate from replay storage.
Bounded paths compare exact scheduler choices, timelines, claims, and author rule execution.
They also compare evaluator and native terminal verdicts.
The sequential sub-scope covers 196 executions.
The complete application result stays incomplete when the time or execution budget ends.
The checker also explores separate scopes with zero, one, and two preemptions.
A preemption selects another fiber when the preceding fiber remains runnable.
A switch after blocking does not consume this bound.
Each scope has its own 60-second and 2048-execution limits.
The report keeps each verdict and its exact bounds separate.
The wider exhaustive scope remains required and can remain incomplete.
Each replay rejects a choice outside its declared preemption bound.
The small protocol compares bounded paths with its complete schedule set.
The report retains the number of pending scheduler prefixes.

The default fuzz and wider finite methods use the same author rules.
Each method has a 60-second checking limit per source variant.
Build time stays separate. Ordinary fuzzing uses 64 iterations and seed 42.
Eight corpus inputs exercise acceptance, Undo, conflict, and cancellation.
Six more corpus inputs check repeated recovery and conflict preservation.
A duplicate workload checks the completed request guard.
It compares all five operation files and an unrelated file before and after rejection.
Four Undo corpus inputs check completion, conflicts, interruption, and repeated recovery.
The comparisons read two sources, the journal, the operation record, and the Undo state.
They compare file presence and contents. A missing file differs from an empty file.
Separate drivers keep observation reads out of the existing recovery fault paths.
The report requires the added rules to execute on all six relevant fault-free inputs.
The eight existing fault-free recovery inputs also retain exact replay checks.
The search continues after a required failure to check both defect controls.
Setup writes fixed journal inputs without application helpers.
Unexpected recovery errors reject an author rule. Tool failures stay incomplete.
Its default search also includes repeated drives and persistent faults.
The driver retains a pending operation when file access remains unavailable.
The finite checker requires all six live assertions on each passing bounded path.
The default fuzz and wider finite methods have different search scopes.
Their elapsed times do not establish a speed advantage.

The acceptance comparison uses the same source graph and rules in both methods.
It uses inputs 0, 1, 4, and 5. They cover acceptance and conflicts, with optional interruption.
Fault calls are exactly 1 and 9. Fault modes are fail, partial, and after replacement.
Both methods permit at most one preemption and 1000 scheduler steps.
Both methods use a 60-second limit and a 2048-execution limit.
The exhaustive method explores every permitted choice in this scope.
The sampled method uses the existing PCT scheduler with seed 42.
The checker requires each sampled path to occur in the completed exhaustive set.
One Linux sample completes all 1286 exhaustive paths in 34.85 seconds.
The sampled method runs 2048 executions in 12.65 seconds.
It reaches 112 distinct paths. All occur in the completed exhaustive set.
Undo and other fault calls remain outside this completed scope.
The report gives each rule its method, verdict, bounds, and open obligations.
An executed rule does not establish specification completeness.

The Undo comparison uses inputs 2, 3, 6, and 7 with the `undo` driver.
They cover normal Undo and conflicts, with optional interruption.
The scope starts with a prepared Undo journal.
Both methods use fault calls 1 and 9, all three fault modes, and at most one preemption.
They keep the same 1000-step, 2048-execution, and 60-second limits.
Each passing path executes the six recovery rules.
It also checks repeated recovery across all five operation files.
It checks an unrelated file before and after repeated recovery.
The report retains every scheduler choice and compares both engines exactly.
Each sampled path must occur in the completed exhaustive Undo set.
The missing-write and external-overwrite controls must fail in both methods.
Direct defects alter the repeated Undo state and an unrelated file.
Ordinary native replay must reproduce each named author rule rejection.
One Linux sample completes all 1300 exhaustive Undo paths in 41.14 seconds.
The sampled method runs 2048 executions in 14.14 seconds.
Undo runs reach at most 373 native scheduler steps in this sample.
The sampled method reaches 112 distinct paths. All occur in the exhaustive set.
Other fault calls, concurrent external edits, and unbounded schedules remain open.

The request comparison starts with accepted files and an empty journal.
It calls the production Undo entry point.
Inputs 0 and 1 cover normal requests and conflicts.
Inputs 2 and 3 also interrupt the request worker.
Two bounded restart attempts handle cancellation and one file fault.
Each passing path checks restored files, external bytes, journal state, Undo state, and reversed writes.
It also checks expected errors, second request rejection, and file preservation.
Fault calls are 1 and 25. Call 25 reaches journal creation in the normal request.
Recorded plans use `fault_seed="fs:fail:N"`, `fs:partial:N`, or `fs:after:N`.
N is an integer from 1 through 2147483647.
Setup does not consume the call index.
The request uses the same preemption, scheduler step, execution, and time limits as the recovery comparisons.
One Linux sample completes all 567 exhaustive request paths in 19.24 seconds.
The sampled method runs 2048 executions in 12.43 seconds.
Its 107 distinct paths occur in the completed exhaustive set.
The corpus retains cancellation followed by a fault before journal creation.
Ordinary replay retains the fault plan and every scheduler choice.

The live recovery check uses the selected application functions on the host filesystem.
It checks two conflict states and every file sync and directory sync in acceptance and Undo recovery.
Each boundary permits process termination or an error after replacement.
Restart completes recovery. Repeated recovery preserves the five operation files.
The live check completes 23 cases.
The report retains source identities, native identities, logs, and file contents.
Power loss, external edits during recovery, and temporary file cleanup remain open.
This check does not run the complete IDE.

The duplicate rule workflow records its exact source and one scenario driver.
A guard defect produces an ordinary replay workload.
The report records build, native check, evaluator comparison, and replay costs.
These command costs do not measure human writing time or usefulness.

One Linux sample checks 2048 native executions and exact replay paths.
The 196 sequential paths pass. Cancellation paths reach 144 contention choices.
Checking takes 55.77 seconds and reaches the execution limit.
117268 scheduler prefixes remain pending.
The zero-preemption scope completes 392 paths in 10.27 seconds.
It reaches all three file fault outcomes. It does not reach interruption during recovery.
The one- and two-preemption scopes reach their execution limits.
Both deliberate defects fail in all three scopes.
These results do not establish cancellation safety for all executions.
The 23 fault-free rule checks and exact engine comparisons take 0.77 seconds.
Ordinary fuzzing takes 24.94 seconds. Its 25 required probes and 40 search probes pass.
It detects 15 sampled mutations. Nine sampled mutations are inert.
No sampled mutation survives or remains incomplete.
These sampled mutations affect the fixture and selected application functions.
Three direct mutation checks exercise application helpers.
They corrupt field lookup and path guards.
Their setup passes. Each check rejects the recovery error in both engines.
These three checks also reproduce through ordinary native replay.
Two more direct checks alter conflict handling and repeated recovery.
One writes the second source file before reporting a conflict.
The endpoint rules pass this defect. The file comparison rejects it in both engines.
The other clears the Undo state when the journal is empty.
The repeated recovery rule rejects it in both engines.
Ordinary native replay reproduces both author rule failures.
Two further direct checks alter the completed request guard.
One permits a duplicate request. The duplicate rejection rule rejects it.
The other changes an unrelated file before rejection. The file comparison rejects it.
Both engines reject each defect. Ordinary native replay reproduces each failure.
Two Undo checks change recovery after the journal is empty.
One changes the Undo state. The repeated recovery rule rejects it.
The other changes an unrelated file. The Undo file preservation rule rejects it.
Both engines reject these defects. Ordinary native replay reproduces each failure.
Three request checks remove journal creation, permit a repeated Undo, or retain incorrect reversed bytes.
The request rules reject each defect in both engines.
Ordinary native replay reproduces all three failures.
Each direct check reports build, native check, evaluator comparison, and replay time.
The duplicate workflow has 12 source lines and three author assertions.
Two assertions check the duplicate result and preserved files.
The third rejects unexpected recovery errors. Expected file faults leave recovery pending.
Its defect build takes 4.57 seconds in this sample.
Its native check takes less than 0.01 seconds.
Its evaluator comparison takes 0.12 seconds.
Its ordinary native replay takes 0.27 seconds.
These times exclude human effort to write or understand the rules.
One host sample does not establish a general cost bound.
Both methods reject a missing-write defect and an external-overwrite defect.
Finite counterexamples also reproduce through ordinary native replay.
The complete comparison returns exit status 2 because finite exploration remains incomplete.
These controls do not establish a new application defect or a general method advantage.
