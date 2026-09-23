# Gaps and unknowns

What is unproven or missing, ranked by how much it threatens the thesis in [`philosophy.md`](philosophy.md).

- **Unknowns** — claims not yet shown within our constraints. A bad outcome invalidates later work.
- **Known gaps** — settled design. Work is unfinished or deferred on purpose.

State what is missing. Do not record what landed. When a gap closes or its assessment changes, update this file. If direction changes, also update `philosophy.md`.

## Unknowns

### 1. Mobile on real devices

**Unproven.** JNI/ObjC embedding on hardware. Touch and soft-keyboard text input on hardware. OpenSSL on the Android NDK. iOS clients use the Apple transport. Hardware runs need provisioning.

**Proof.** One example (counter) runs on one device or simulator with `scuzz package` plus the platform toolchain. That bar stays host-gated. Hardware device runs stay open.

The local iOS loop targets arm64 simulators on iOS 16 or later. Source edits reload the View and preserve app state. Manifest changes and the r command restart the app. Physical device signing and release distribution remain open. iOS supports Net clients with platform certificate trust. Net HTTP servers remain host-only. Android packages reject Net calls because they do not link OpenSSL.

### 2. Evaluator parity and speed

**Partly proven.** An evaluator written in Scuzz produces the same observable output as the emitted binary on every example. `scuzz fuzz` on `examples/webhook` writes the same `summary.json` on both engines (`scripts/ci-fuzz.sh`). `examples/io` also matches on both engines. `examples/api-report` matches on mutation and corpus. Evaluator branch reach is a superset. A scheduler step is one effect, so the extra `IO` wrapping in the evaluator does not move the interleaving. One `scuzz eval --probe` server per file set checks the package once and forks a child per probe. `scuzz fuzz --iterations 320 examples/api-report` takes 63 s on the evaluator and 60 s compiled on this host. Mutation and corpus match. Evaluator branch reach is a superset. A self-tail `Int` function runs as a compiled loop when its body is `if (n <= 0) k else f(n - 1)` or `n match { case 0 => k; case _ => f(n - 1) }` and `k` is an `Int` literal. Coverage and distance keep the same final record. The evaluator idle probe on `examples/kernel` takes 0.05 s on this host. `scuzz fuzz --iterations 16 examples/kernel` takes 14 s on the evaluator and 26 s compiled. A zero-delay retry reaches the scheduler step cap in 13 s on the evaluator and in 0.7 s compiled. A mutated page limit still hits the 20 s probe deadline on both engines. A call outside the Int tail loop still interprets each step. Compiled corpus replay takes 9 s of that 14 s (`--iterations 0`). A slower host falls back to compiled probes at the idle gate.

**Proof.** CI diffs `scuzz eval` against `scuzz run` on `examples/hello`, `examples/kernel`, and `examples/io`. `scripts/ci-fuzz.sh` prints wall clock for both engines on `examples/webhook`, `examples/api-report`, and `examples/io`. It diffs the full summary for `examples/webhook` and `examples/io`. For `examples/api-report` it checks mutation and corpus equality and that evaluator reach is a superset. Distance feedback reaches the page-cap arm on the evaluator. Compiled search does not. The arm returns the same page. The kernel campaign completes faster than compiled. The api-report campaign takes 63 s on the evaluator and 60 s compiled. The remaining cost is the interpreted scheduler step in a zero-delay retry. Do not add scheduler-step snapshots or expression coverage until a proof needs them. Locks: [`philosophy.md`](philosophy.md#evaluator).

## Known gaps

Next work makes the language usable for general programs. The next gap is compile time. Re-time the two commands in that gap before another show-and-parse change. Standard kits follow that. Locks: [`philosophy.md`](philosophy.md). Order: [`vision.md`](vision.md).

### Cuts

Do not add user FFI, `extern`, or plugins. Determinism and effect capture are not settled.

Do not add library publishing, git or registry deps, or `scuzz add`. Path deps stay. A hosted registry may never ship.

### Thesis-critical

Resolve these gaps when they prevent ordinary language use.

1. **Compile-time performance** — `scuzz check examples/compiler` is 12 s on this host. A cold `scuzz build examples/tyck` is 13 s on this host. Re-time both commands before another show-and-parse slice. Emitted string literals intern to pinned allocations. Kit signatures parse to `Ty` when the table is built. Generic kit calls compare those `Ty` values. `zipCheck` and `checkKnownRet` keep `Ty`. Env lookup returns `Ty`. A borrowed tail of a live parameter does not retain. A single-arm match on a borrowed value does not retain a borrowed result. Owned concat reuses a unique left value. A shared or pinned left value is copied. Remaining cost is spread across the compiler. String allocation has the largest single sample site in the cold build. `emitProg` builds the LLVM text. `concreteTy` still parses a shown type. Coverage uses the live program when compiled files match live.

### Table-stakes

Required for CLI, server, and desktop applications.

OS threads.

### Verification

The one testing strategy is mutation, fuzz, properties, simulation, coverage, and determinism. Locks: [`philosophy.md`](philosophy.md#verification-posture).

### Later

Do not start FFI, plugins, or a package registry. Other later items stay parked.

Generated setup inputs. Multiple named scenarios and campaign selection. Session event journal and live time ops. Stable scroll keys. Windows desktop. OS IME candidate windows. macOS release packaging in default CI. Developer ID signing and notarization. Full web accessibility. Real phone and screen-reader checks. Hot reload on web. Multiple UI factories in host hot reload. Oracle mining. Emit scalar fallbacks. Dogfood IDE: native file dialogs, menus, multi-window, multi-cursor, minimap, Git UI, debugger, plugin host, custom canvas kit.
