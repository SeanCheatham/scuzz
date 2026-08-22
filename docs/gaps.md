# Gaps and unknowns

What is unproven or missing, ranked by how much it threatens the thesis in [`vision.md`](vision.md).

- **Unknowns** — claims not yet shown within our constraints. A bad outcome invalidates later work.
- **Known gaps** — settled design. Work is unfinished or deferred on purpose.

When a gap closes or its assessment changes, update this file. If direction changes, also update `vision.md`.

## Unknowns

### 1. Mobile on real devices

**Status.** iOS simulator proven: `scuzz package --target ios` runs `crates/embedder-mobile/shells/ios/build_sim.sh` and signs a `.app`. `examples/counter` mounts `UiRuntime.Mobile` in a booted sim. The iOS shell feeds typed insert and backspace into `SZ_INPUT_TEXT_EDIT`. Android emulator proven: `scuzz package --target android` packs a debug APK. SurfaceView taps become `SZ_INPUT_POINTER`. A hidden `EditText` maps insert and backspace to `SZ_INPUT_TEXT_EDIT`. Missing NDK or SDK fails with one install line. Real devices stay open (provisioning).

**Unproven.** JNI/ObjC embedding on hardware. Touch and soft-keyboard text input on hardware.

**Proof.** One example (counter) runs on one device or simulator with `scuzz package` plus the platform toolchain. iOS sim meets that bar. Android meets the emulator present + tap/TextField bar. Device runs stay open.

### 2. GPU presenters (Impeller / Skia GPU)

**Status.** An OpenGL presenter is in (`SCUZZ_SKIA=gpu`): CPU `sk_sw` paint, GPU upload and readback behind `sk_capi`. Headless structural goldens match the CPU path. Impeller and Skia GPU raster stay deferred.

**Unproven.** A GPU rasterizer (Impeller or Skia GPU) behind `sk_capi` keeps identical structural dumps and tolerance-bounded pixels without a CPU paint pass.

**Proof.** `SCUZZ_SKIA=gpu` renders `examples/counter` with unchanged structural goldens. Pixel goldens match `sk_sw` when `--pixels` is on. Unused GPU stubs do not close the proof.

## Known gaps

### Residuals

- **Concurrency** — cooperative fibers only. Later: OS threads, supervision trees.
- **Memory** — Last-use retain/release is locked in [`vision.md`](vision.md) GC. Values with no last-use stay allocated until panic sweep or process exit. No cycle collector.
- **LSP / editor tooling** — `scuzz lsp` wraps `check`. JSON diagnostics stay the single schema. Method catalog: [`vision.md`](vision.md#tooling).

### Dependency forms beyond `path`

Path deps only (`manifest.rs`). Git, versioned, and hosted artifacts are direction. There is no registry.

**Deferred (last on purpose).** Do not add ecosystem theater before there is an ecosystem. Revisit after path deps and file-as-module are the proven reuse story.

### Deferred by decision

- **Windows desktop embedder** — same session protocol as X11/Cocoa. Secondary platform.
- **OS IME candidate windows** — focused TextField caret uses measured advance (`sz_view_caret_rect`). Embedders do not yet place OS IME UI from it.
- **macOS in default CI** — `macos-smoke` runs on push/PR (runtime tests, compiler tests, hello). Full macOS packaging stays `workflow_dispatch`.
- **Web apps** — not a current target.
- **Fault injection and schedule exploration** — TestRuntime fakes can fail the Nth operation and drop stub responses. PCT-style schedule exploration bounds race depth for fiber interleavings. See [`vision.md`](vision.md#open-work).
- **Oracle idioms in `guide.md`** — model agreement, metamorphic relations, and concrete-fact properties ride the existing property surface. The guide does not yet document them.
