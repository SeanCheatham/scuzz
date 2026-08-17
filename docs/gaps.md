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
- **Memory** — counter-shaped Headless pumps stay flat under alloc accounting. `Signal.list` and list/map/set nodes drop through RC. The compiler emits retain/release on string, List, Map / Set, and ADT temps, and on owned `let` / `for` binders after an `IO` / scalar / owned-ptr / `if`-phi body. An `if` / match phi is owned when any arm produces an owned ptr; mixed arms retain borrowed values at the join. `Map.getOrElse` retains the result and drops an owned map. It drops an owned default after retain, including when the map is borrowed. `List.map` / `List.filter` drop the closure pack after the call. A `List.map` / `Stream.map` mapper returns an owned pointer. A borrowed body retains before return. The runtime takes that mapper ref. `Stream.emits` / `Stream.emit` retain payloads and drop an owned list or value. `Stream.eval` retains the IO and drops the caller ref after the call. Stream nodes are reference-counted. Combinators retain the inner stream and drop an owned input after the call. `Stream.compileToList` / `Stream.drain` / `Stream.exists` retain the stream for the IO and drop an owned input after the call. `Signal.list` retains the list and drops an owned input after the call. `Signal.setList` retains the new list and drops an owned input after the call. `Signal.str` copies the bytes and drops an owned string after the call. `Signal.setStr` copies the bytes and drops an owned string after the call. View constructors that copy a string drop the owned input after the call (`View.text`, `View.button`, `View.iconButton`, `View.fab`, `View.outlinedButton`, `View.textButton`, `View.actionChip`, `View.chip`, `View.filterChip`, `View.inputChip`, `View.choiceChip`, `View.checkbox`, `View.radio`, `View.switch`, `View.avatar`, `View.listTile`, `View.checkboxListTile`, `View.switchListTile`, `View.radioListTile`, `View.segmented`, `View.tooltip`, `View.semantics`, `View.mergeSemantics`, `View.inkWell`, `View.expansionTile`, `View.textField` placeholder, `View.image` caption). Tap constructors unpack `cons(fn, cons(env, nil))` and drop the owned pack after the call (`View.button`, `View.iconButton`, `View.fab`, `View.outlinedButton`, `View.textButton`, `View.actionChip`, `View.inkWell`). They retain the env list. `sz_view_free` releases it. `View.each` mappers, `Signal.map`, and `Ui.run` rebuild packs drop the same way after unpack. They retain the env. The View, mapped Signal, or session releases it. `Stream.filter` / `Stream.map` / `Stream.evalMap`, `Resource.make` / `Resource.use`, and `Net.serve` / `Net.serveOnce` drop the callback pack after unpack. Stream combinators, `Resource`, and `Net.serve` retain the env. The resource, use IO, stream, or server releases it. `Resource.make` retains the acquire IO and drops an owned acquire after the call. It marks the result owned. `Resource.use` retains the resource and the acquire IO. It drops an owned resource after the call. Resource free releases acquire. `IO.forever` / `IO.repeatN` / `IO.retryN` / `Fiber.fork` / `IO.timeout` / `handleErrorWith` / `flatMap` / `IO.attempt` retain inner and drop the caller ref after the call. The compiler retains a `flatMap` / `handleErrorWith` capture list and drops the pack after the call. `IO.ensure` / `IO.race` / `IO.both` retain both child IO nodes and drop the caller refs after the call. IO graphs drop after `unsafe_run`. `List.setAt` drops an owned input list and marks the result owned. Out of range returns `xs` with an extra retain so that drop is safe. Capture lists stay with the View, session, stream, or server until that owner drops. Tap, `View.each`, `Signal.map`, `Ui.run` rebuild, `Resource`, `Net.serve`, and Stream callback env lists drop when that owner frees. Values without a last-use stay allocated. No cycle collector. Panic may leak.
- **LSP / editor tooling** — `fmt`, `check --message-format=json`, `watch`, and `scuzz lsp` exist. LSP wraps `check` (didOpen/didChange overlay open buffers; didClose returns to disk). Positions are UTF-16. Hover, completion, and definition use that parse. Unknown methods return JSON-RPC `-32601`. JSON diagnostics stay the single schema. `check` reports more than one parse or type error per run.

### Dependency forms beyond `path`

Path deps only (`manifest.rs`). Git, versioned, and hosted artifacts are direction. There is no registry.

**Deferred (last on purpose).** Do not add ecosystem theater before there is an ecosystem. Revisit after path deps and file-as-module are the proven reuse story.

### Deferred by decision

- **Windows desktop embedder** — same session protocol as X11/Cocoa. Secondary platform.
- **OS IME candidate windows** — focused TextField caret uses measured advance (`sz_view_caret_rect`). Embedders do not yet place OS IME UI from it.
- **macOS in default CI** — `macos-smoke` runs on push/PR (runtime tests, compiler tests, hello). Full macOS packaging stays `workflow_dispatch`.
- **Web apps** — not a current target.
