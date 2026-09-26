# Gaps and unknowns

What is unproven or missing, ranked by how much it threatens the thesis in [`philosophy.md`](philosophy.md).

- **Unknowns** — claims not yet shown within our constraints. A bad outcome invalidates later work.
- **Known gaps** — settled design. Work is unfinished or deferred on purpose.

State what is missing. Do not record what landed. When a gap closes or its assessment changes, update this file. If direction changes, also update `philosophy.md`.

## Unknowns

### 1. Proposal review in the IDE

**Partly proven.** The IDE compares the open buffers with the files on disk in its own process. It checks and probes both file sets on the evaluator in forked children. No `git`. No native build of the target package. The lanes open the first diverging state with the changed sections first. A warm compare of `examples/counter` takes about 8.5 s from tap to lanes. The deck reviews one proposal at a time in blind lanes. Keep and Reject land in the working tree and in the decision record.

**Unproven.** The loop is fast enough for one decision every few seconds. A timeline with steps aligned by index shows a human what a change does. A human decides faster from blind timelines than from a source diff. Generated proposals are useful often enough to keep reviewing. How proposals are generated, and how the IDE talks to an LLM, is not decided.

**Proof.** The deck shows proposals from `build/proposals/` in random lane order. Keep writes the proposal into the working tree. The deck records each decision with both file set hashes. Measure the keep rate per region. Order: [`vision.md`](vision.md#primary-arc-proposal-review-in-the-ide). Locks: [`philosophy.md`](philosophy.md#proposal-review).

### 2. Evaluator parity and speed

**Partly proven.** The evaluator produces the same observable output as the emitted binary on every example. `scuzz fuzz` writes the same `summary.json` on both engines for `examples/webhook` and `examples/io`. `examples/api-report` matches on mutation and corpus. Compiled reach stays inside evaluator reach. The evaluator campaign on `examples/kernel` is faster than compiled.

**Missing.** A call outside the self-tail `Int` loop interprets each step. A zero-delay retry reaches the scheduler step cap in 13 s on the evaluator and in 0.7 s compiled. A mutated page limit hits the 20 s probe deadline on both engines. A slower host falls back to compiled probes at the idle gate. The IDE review loop runs every probe on the evaluator, so this cost limits the loop.

**Proof.** CI diffs `scuzz eval` against `scuzz run` on `examples/hello`, `examples/kernel`, and `examples/io`. `scripts/ci-fuzz.sh` prints wall clock for both engines and compares the summaries. Do not add scheduler-step snapshots or expression coverage until a proof needs them. Locks: [`philosophy.md`](philosophy.md#evaluator).

### 3. Mobile on real devices

**Unproven.** JNI/ObjC embedding on hardware. Touch and soft-keyboard text input on hardware. OpenSSL on the Android NDK. Hardware runs need provisioning.

**Proof.** One example (counter) runs on one device with `scuzz package` plus the platform toolchain. Simulator runs do not close this gap.

The local iOS loop targets arm64 simulators on iOS 16 or later. Physical device signing and release distribution remain open. iOS supports Net clients with platform certificate trust. Net HTTP servers remain host-only. Android packages reject Net calls because they do not link OpenSSL.

## Known gaps

The review loop in the IDE is the primary work. Compile time comes next, because the IDE links the compiler. Standard kits follow. Locks: [`philosophy.md`](philosophy.md). Order: [`vision.md`](vision.md).

### Cuts

Do not add user FFI, `extern`, or plugins. Determinism and effect capture are not settled.

Do not add library publishing, git or registry deps, or `scuzz add`. Path deps stay. A hosted registry may never ship.

### Review loop

1. **IDE subprocesses** — Run, Fuzz, and Diff start `scuzz run`, `scuzz fuzz`, and `scuzz diff`. Hover, goto-def, and rename start `scuzz lsp`.
2. **IDE Check scope** — the Check button does not run the format check or the verify-file check of `scuzz check`.

### Thesis-critical

Resolve these gaps when they prevent ordinary language use or the review loop.

1. **Compile-time performance** — `scuzz check examples/compiler` takes about 4.5 s on this host. `scuzz build --full examples/tyck` takes about 16 s. `scuzz check examples/editor` takes about 19 s cold. Measure these commands after each compile-time change. Further work must reduce the cost of checking and emitted LLVM text.

### Table-stakes

Required for CLI, server, and desktop applications.

OS threads.

### Verification

The one testing strategy is mutation, fuzz, properties, simulation, coverage, and determinism. Locks: [`philosophy.md`](philosophy.md#verification-posture).

### Later

Do not start FFI, plugins, or a package registry. Other later items stay parked.

Generated setup inputs. Multiple named scenarios and campaign selection. Session event journal and live time ops. Stable scroll keys. Windows desktop. OS IME candidate windows. macOS release packaging in default CI. Developer ID signing and notarization. Full web accessibility. Real phone and screen-reader checks. Hot reload on web. Multiple UI factories in host hot reload. Oracle mining. A model trained on review decisions. Timeline alignment beyond state index. Divergence attribution to source defs. Emit scalar fallbacks. Dogfood IDE: native file dialogs, menus, multi-window, multi-cursor, minimap, Git UI, debugger, plugin host, custom canvas kit.
