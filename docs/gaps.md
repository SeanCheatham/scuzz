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

### Core value types

`Map` and `Set` are in (`examples/kernel`). Apps get `Int`, `Float`, `String`, `Bool`, `List[T]`, `Map[K, V]`, `Set[T]`, and enums.

- **Float** — scalar through the lexer, typer, formatter, and codegen (LLVM `double`). Proof: `examples/kernel`.
- **Map / Set** — persistent trees. Keys are `Int` or `String`. List cells retain heads and tails. Proof: `examples/kernel`.

### Residuals

- **Concurrency** — cooperative fibers only. Later: OS threads, supervision trees.
- **Memory** — counter-shaped Headless pumps stay flat under alloc accounting. `Signal.list` and list/map/set nodes drop through RC. The compiler emits retain/release on string, List, Map / Set, and ADT temps, and on owned `let` / `for` binders after an `IO` / scalar / owned-ptr / `if`-phi body. An `if` / match phi is owned when any arm produces an owned ptr; mixed arms retain borrowed values at the join. `Map.getOrElse` retains the result and drops an owned map. It drops an owned default after retain, including when the map is borrowed. `List.map` / `List.filter` drop the closure pack after the call. `Stream.emits` / `Stream.emit` retain payloads and drop an owned list or value. `Stream.eval` retains the IO and drops the caller ref after the call. `Signal.list` retains the list and drops an owned input after the call. `Signal.setList` retains the new list and drops an owned input after the call. `Signal.str` copies the bytes and drops an owned string after the call. `Signal.setStr` copies the bytes and drops an owned string after the call. IO graphs drop after `unsafe_run`. `List.setAt` drops an owned input list and marks the result owned. Out of range returns `xs` with an extra retain so that drop is safe. Stream nodes are not RC, so `Stream.compileToList` does not drop the stream. Stream and View lambda packs stay allocated until the graph drops. Values without a last-use stay allocated. No cycle collector. Panic may leak.
- **LSP / editor tooling** — `fmt`, `check --message-format=json`, `watch`, and `scuzz lsp` exist. LSP wraps `check` (didOpen/didChange overlay open buffers; didClose returns to disk). Positions are UTF-16. Hover, completion, and definition use that parse. Unknown methods return JSON-RPC `-32601`. JSON diagnostics stay the single schema. `check` reports more than one parse or type error per run.

### Dependency forms beyond `path`

Path deps only (`manifest.rs`). Git, versioned, and hosted artifacts are direction. There is no registry.

**Deferred (last on purpose).** Do not add ecosystem theater before there is an ecosystem. Revisit after path deps and file-as-module are the proven reuse story.

### Deferred by decision

- **Windows desktop embedder** — same session protocol as X11/Cocoa. Secondary platform.
- **OS IME candidate windows** — focused TextField caret uses measured advance (`sz_view_caret_rect`). Embedders do not yet place OS IME UI from it.
- **macOS in default CI** — `macos-smoke` runs on push/PR (runtime tests, compiler tests, hello). Full macOS packaging stays `workflow_dispatch`.
- **Web apps** — not a current target.
