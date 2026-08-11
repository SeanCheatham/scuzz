# Gaps and unknowns

What is unproven or missing, ranked by how much it threatens the thesis in [`vision.md`](vision.md).

- **Unknowns** — bets not yet demonstrated within our constraints. A bad outcome invalidates downstream work.
- **Known gaps** — settled design; unfinished or deliberately deferred work.

When a gap closes or its assessment changes, update this file and (if direction changes) `vision.md`.

## Unknowns

### 1. Real text rendering through `sk_capi`

**Status.** Blocked on a hosted Skia prebuilt. In-tree text is an 8×8 ASCII bitmap (`FONT8` in `crates/ffi-skia/src/sk_sw.c`); layout measures with `strlen * font_px` (`text_width` in `view.c`). `sk_capi.h` is draw-only (`sk_canvas_draw_string`); no measure API. `scripts/fetch_skia.sh` can unpack under `third_party/skia/prebuilt/` when `SCUZZ_SKIA_URL` is set, but nothing links a real backend — CI always builds `sk_sw`. Vision forbids vendoring a Skia tree.

**Unproven.** Whether thin `sk_capi` can absorb real Skia (metrics, shaping, AA, proportional widths) without breaking layout or structural goldens. The UI face is calibrated against fake monospace text; this gates GPU presenters (unknown 4) and Flutter-style constraints.

**Proof.** A `[ui]` app links a fetched Skia prebuilt behind `sk_capi.h`, measures via real font metrics, a11y goldens stay logical-only and stable, and `sk_sw` remains the zero-dependency CI fallback. Prep that does not close the proof: measure API wiring, Makefile dual-backend switch, tarball contract docs.

### 2. Compiler IR onto payload ADTs

**Status.** Open in-repo (large). User programs already have N-field `Int`/`String` payload ADTs and file-stem def namespaces on Stage 0 and self-host (`examples/adt`, `examples/modules`). Compiler sources still use List+string-tag IR; Format/Typ/Codegen assume that encoding. No records, traits, or user generics yet (`compatibility.md` keeps early-monomorphized generics as direction).

**Unproven.** That rewriting the compiler’s own AST/IR onto payload ADTs (and later generics) keeps the self-host ratchet intact.

**Proof.** At least one AST node family rewritten onto payload ADTs with `scripts/selfhost.sh` green and byte-identical Stage-2/3 IR.

### 3. Mobile on real devices

**Status.** Blocked on Android NDK and/or Xcode. `crates/embedder-mobile/` is a host shell (`SCUZZ_MOBILE_SHELL=1`) plus packaging stubs (`shells/android/`, `shells/ios/`). No NDK/Xcode build has run; no toolchain invocation in Makefiles or CI.

**Unproven.** Cross-compiling LLVM app + C runtime + `sk_sw` for arm64, JNI/ObjC embedding, surface/present, and touch/soft-keyboard on hardware.

**Proof.** One example (counter) runs on one device or simulator via `scuzz package` plus the platform toolchain. Further stubs do not close the proof.

### 4. GPU presenters (Impeller / Skia GPU)

**Status.** Blocked on unknown 1. Only the CPU raster path (`sk_sw`) exists. Presenters must not change `Ui` session or logical goldens — asserted, not demonstrated.

**Unproven.** That a GPU backend behind `sk_capi` keeps identical structural dumps and tolerance-bounded pixels.

**Proof.** A second presenter renders the golden examples with unchanged structural goldens. Do not attempt until unknown 1 is unblocked; unused GPU stubs do not close the proof.

## Known gaps

### Residuals (closed bets)

- **Concurrency** — cooperative fibers + TestRuntime virtual-time jumps cover the determinism thesis (`test_io.c`). Out of scope / later: OS threads, interruptible cancel mid-`nanosleep`, supervision trees, Scuzz bindings for Queue/Deferred.
- **Memory** — counter-shaped Headless pumps stay flat under alloc accounting (`test_ui.c`, optional `test-asan`). Later: `Signal.list` collection, exit-time signal ownership / LSan-clean examples, a collector if list-churn demands it.
- **File-as-module** — stem namespaces on Stage 0 and self-host (`examples/modules`). Later: `private`/`pub`, `import`, enum-per-module.
- **Diagnostics spans** — Stage 0 and self-host emit `file:line:column` (and JSON). Done.

### Dependency forms beyond `path`

Path deps only (`manifest.rs`, `Toml.scuzz`). Git / versioned / hosted artifacts are direction; no registry.

**Deferred (deliberately last).** No ecosystem theater before there is an ecosystem. Revisit after path deps + file-as-module are the proven reuse story.

### Deferred by decision

- **Flutter-style constraint layout** — recursive stacker today (`layout_node` in `view.c`). Locked direction: constraints down, sizes up. Wait for real text metrics (unknown 1).
- **Windows desktop embedder** — same session protocol as X11/Cocoa; secondary platform.
- **LSP / editor tooling** — `fmt`, `check --message-format=json`, and `watch` exist; no language server.
- **macOS in default CI** — macOS job is `workflow_dispatch`-only; Darwin regressions surface late.
