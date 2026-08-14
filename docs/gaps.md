# Gaps and unknowns

What is unproven or missing, ranked by how much it threatens the thesis in [`vision.md`](vision.md).

- **Unknowns** — claims not yet shown within our constraints. A bad outcome invalidates later work.
- **Known gaps** — settled design. Work is unfinished or deferred on purpose.

When a gap closes or its assessment changes, update this file. If direction changes, also update `vision.md`.

## Unknowns

### 1. Mobile on real devices

**Status.** Blocked on Android NDK and/or Xcode. `crates/embedder-mobile/` is a host shell (`SCUZZ_MOBILE_SHELL=1`) plus packaging stubs (`shells/android/`, `shells/ios/`). No NDK/Xcode build has run. Makefiles and CI do not invoke those toolchains.

**Unproven.** Cross-compile of LLVM app + C runtime + `sk_sw` for arm64, JNI/ObjC embedding, surface/present, and touch/soft-keyboard on hardware.

**Proof.** One example (counter) runs on one device or simulator with `scuzz package` plus the platform toolchain. More stubs do not close the proof.

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

- **Flutter-style constraint layout** — `layout_node_ex` passes min/max down. Tight slots: `View.sized`, `View.aspectRatio`, percent axes on `View.fraction`, `View.expanded` flex, and opt-in `View.stretch` (cross axis). `View.minSize` raises min. `View.maxSize` lowers max (`0` = no cap). Incoming max still wins when tighter. `View.clip` clips paint to its frame. Scroll uses the same clip. `View.opacity` scales paint alpha (`0`–`100`). Nested opacity multiplies. `View.maxLines` caps wrapped text lines (`0` = no cap). Nested caps take the tighter value. `View.ellipsis` keeps extra lines off the paint. Without a positive `maxLines` it keeps one line. With `maxLines` it paints `...` on the last visible line when more text remains. `View.textColor` paints `View.text` with an ARGB color. Nested `textColor` uses the inner color. `View.gap` sets Column/Row/List spacing (`0` = none). Nested `gap` uses the inner value. `Color.rgb` is opaque. `Color.rgba` sets alpha. `View.ignorePointer` skips hit-test so taps pass through. `View.absorbPointer` blocks taps without firing the child. `View.excludeSemantics` omits the subtree from the a11y dump and from TextField collect. Taps still hit the child. `View.text` wraps at newlines and at max width. Residual: Flutter-style constraint-overflow dumps (do not add).
- **Windows desktop embedder** — same session protocol as X11/Cocoa. Secondary platform.
- **OS IME candidate windows** — focused TextField caret uses measured advance (`sz_view_caret_rect`). Embedders do not yet place OS IME UI from it.
- **LSP / editor tooling** — `fmt`, `check --message-format=json`, `watch`, and `scuzz lsp` exist. LSP wraps `check` (didOpen/didChange overlay open buffers; didClose returns to disk). Hover, completion, and definition use that parse. JSON diagnostics stay the single schema.
- **macOS in default CI** — macOS job is `workflow_dispatch` only. Darwin regressions show late.
