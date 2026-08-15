# Gaps and unknowns

What is unproven or missing, ranked by how much it threatens the thesis in [`vision.md`](vision.md).

- **Unknowns** — claims not yet shown within our constraints. A bad outcome invalidates later work.
- **Known gaps** — settled design. Work is unfinished or deferred on purpose.

When a gap closes or its assessment changes, update this file. If direction changes, also update `vision.md`.

## Unknowns

### 1. Mobile on real devices

**Status.** iOS simulator proven: `crates/embedder-mobile/shells/ios/build_sim.sh` cross-compiles the LLVM app + C runtime + `sk_sw` for `arm64-apple-ios-simulator`, links the ObjC shell (`crates/embedder-mobile/shells/ios/main.m` + `ScuzzShell.m`), and signs a `.app`. `examples/counter` mounts `UiRuntime.Mobile` in a booted sim and presents live frames. Android stays blocked on the NDK (`crates/embedder-mobile/shells/android/` is a manifest + JNI stub). Real devices stay open (provisioning, no simulator sandbox).

**Unproven.** Android cross-compile and JNI/ObjC embedding on hardware. Touch and soft-keyboard text input on hardware. `scuzz package --target ios` still copies templates; the CLI does not drive `build_sim.sh` yet. Soft-keyboard text input (`SZ_INPUT_TEXT_EDIT`) on sim is open.

**Proof.** One example (counter) runs on one device or simulator with `scuzz package` plus the platform toolchain. iOS sim meets the bar through the shell script; the CLI wiring, sim TextField input, and Android/device runs stay open.

### 2. GPU presenters (Impeller / Skia GPU)

**Status.** Unblocked on text/metrics. Only the CPU raster path exists (`sk_sw` or fetched Skia CPU prebuilt). Presenters must not change `Ui` session or logical goldens. That rule is asserted, not shown.

**Unproven.** A GPU backend behind `sk_capi` keeps identical structural dumps and tolerance-bounded pixels.

**Proof.** A second presenter renders the golden examples with unchanged structural goldens. Unused GPU stubs do not close the proof.

## Known gaps

### Residuals

- **Concurrency** — cooperative fibers only. Later: OS threads, supervision trees.
- **Memory** — counter-shaped Headless pumps stay flat under alloc accounting. `Signal.list` frees unshared cons spines. Later: a collector if list churn still needs it.

### Dependency forms beyond `path`

Path deps only (`manifest.rs`). Git, versioned, and hosted artifacts are direction. There is no registry.

**Deferred (last on purpose).** Do not add ecosystem theater before there is an ecosystem. Revisit after path deps and file-as-module are the proven reuse story.

### Deferred by decision

- **Flutter-style constraint layout** — constraints down, sizes up. Widget catalog: [`guide.md`](guide.md). Residual: Flutter-style constraint-overflow dumps (do not add).
- **Windows desktop embedder** — same session protocol as X11/Cocoa. Secondary platform.
- **OS IME candidate windows** — focused TextField caret uses measured advance (`sz_view_caret_rect`). Embedders do not yet place OS IME UI from it.
- **LSP / editor tooling** — `fmt`, `check --message-format=json`, `watch`, and `scuzz lsp` exist. LSP wraps `check` (didOpen/didChange overlay open buffers; didClose returns to disk). Hover, completion, and definition use that parse. JSON diagnostics stay the single schema. `check` reports more than one parse or type error per run.
- **macOS in default CI** — macOS job is `workflow_dispatch` only. Darwin regressions show late.
