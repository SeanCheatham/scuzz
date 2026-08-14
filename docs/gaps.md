# Gaps and unknowns

What is unproven or missing, ranked by how much it threatens the thesis in [`vision.md`](vision.md).

- **Unknowns** — bets not yet demonstrated within our constraints. A bad outcome invalidates downstream work.
- **Known gaps** — settled design; unfinished or deliberately deferred work.

When a gap closes or its assessment changes, update this file and (if direction changes) `vision.md`.

## Unknowns

### 1. Mobile on real devices

**Status.** Blocked on Android NDK and/or Xcode. `crates/embedder-mobile/` is a host shell (`SCUZZ_MOBILE_SHELL=1`) plus packaging stubs (`shells/android/`, `shells/ios/`). No NDK/Xcode build has run; no toolchain invocation in Makefiles or CI.

**Unproven.** Cross-compiling LLVM app + C runtime + `sk_sw` for arm64, JNI/ObjC embedding, surface/present, and touch/soft-keyboard on hardware.

**Proof.** One example (counter) runs on one device or simulator via `scuzz package` plus the platform toolchain. Further stubs do not close the proof.

### 2. GPU presenters (Impeller / Skia GPU)

**Status.** Unblocked on text/metrics. Only the CPU raster path exists today (`sk_sw` or fetched Skia CPU prebuilt). Presenters must not change `Ui` session or logical goldens — asserted, not demonstrated.

**Unproven.** That a GPU backend behind `sk_capi` keeps identical structural dumps and tolerance-bounded pixels.

**Proof.** A second presenter renders the golden examples with unchanged structural goldens. Unused GPU stubs do not close the proof.

## Known gaps

### Residuals

- **Concurrency** — cooperative fibers only. Later: OS threads, supervision trees.
- **Memory** — counter-shaped Headless pumps stay flat under alloc accounting; `Signal.list` frees unshared cons spines. Later: a collector if list-churn still demands it.

### Dependency forms beyond `path`

Path deps only (`manifest.rs`). Git / versioned / hosted artifacts are direction; no registry.

**Deferred (deliberately last).** No ecosystem theater before there is an ecosystem. Revisit after path deps + file-as-module are the proven reuse story.

### Deferred by decision

- **Flutter-style constraint layout** — recursive stacker today (`layout_node` in `view.c`). Locked direction: constraints down, sizes up. Widget set is in [`vision.md`](vision.md#layout-model); do not grow CSS-ish rules or Flutter-style constraint-overflow dumps.
- **Windows desktop embedder** — same session protocol as X11/Cocoa; secondary platform.
- **OS IME candidate windows** — focused TextField caret uses measured advance (`sz_view_caret_rect`); embedders do not yet place OS IME UI from it.
- **LSP / editor tooling** — `fmt`, `check --message-format=json`, `watch`, and `scuzz lsp` exist. LSP wraps disk `check` (didOpen/didSave/didChange); no hover/completion and no unsaved-buffer overlay. JSON diagnostics stay the single schema.
- **macOS in default CI** — macOS job is `workflow_dispatch`-only; Darwin regressions surface late.
