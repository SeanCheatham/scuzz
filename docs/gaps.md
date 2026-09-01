# Gaps and unknowns

What is unproven or missing, ranked by how much it threatens the thesis in [`vision.md`](vision.md).

- **Unknowns** — claims not yet shown within our constraints. A bad outcome invalidates later work.
- **Known gaps** — settled design. Work is unfinished or deferred on purpose.

When a gap closes or its assessment changes, update this file. If direction changes, also update `vision.md`.

## Unknowns

### 1. Mobile on real devices

**Status.** iOS simulator proven: `scuzz package --target ios` runs `crates/embedder-mobile/shells/ios/build_sim.sh` and signs a `.app`. `examples/counter` mounts `UiRuntime.Mobile` in a booted sim. The iOS shell feeds typed insert and backspace into `SZ_INPUT_TEXT_EDIT`. Android emulator proven: `scuzz package --target android` packs a debug APK (`arm64-v8a`, plus `x86_64` when that NDK clang is present) and installs it when `adb` lists a device. A USB serial wins over an emulator. No device is not a failure. After install, JNI loads `libscuzz.so` and presents frames (`scuzz android: present`). SurfaceView taps become `SZ_INPUT_POINTER`. A hidden `EditText` maps insert and backspace to `SZ_INPUT_TEXT_EDIT`. Missing NDK or SDK fails with one install line. Hardware devices stay open (provisioning).

**Unproven.** JNI/ObjC embedding on hardware. Touch and soft-keyboard text input on hardware.

**Proof.** One example (counter) runs on one device or simulator with `scuzz package` plus the platform toolchain. iOS sim meets that bar. Android meets the emulator present + tap/TextField bar. Hardware device runs stay open.

### 2. GPU presenters (Impeller / Skia GPU)

**Status.** An OpenGL presenter is in (`SCUZZ_SKIA=gpu`): CPU `sk_sw` paint, GPU upload and readback behind `sk_capi`. Headless structural goldens match the CPU path. `scuzz test --differential` compares structural dumps across `skia`, `sk_sw`, and `gpu` per host. Impeller and Skia GPU raster stay deferred.

**Unproven.** A GPU rasterizer (Impeller or Skia GPU) behind `sk_capi` keeps identical structural dumps and tolerance-bounded pixels without a CPU paint pass.

**Proof.** `SCUZZ_SKIA=gpu` renders `examples/counter` with unchanged structural goldens. Pixel goldens match `sk_sw` when `--pixels` is on. Unused GPU stubs do not close the proof.

## Known gaps

### Residuals

- **Concurrency** — cooperative fibers only. Later: OS threads, supervision trees.
- **Memory** — Last-use retain/release is locked in [`vision.md`](vision.md) GC. Values with no last-use stay allocated until panic sweep or process exit. No cycle collector.
- **LSP for external editors** — Product `scuzz lsp` is a stdio JSON-RPC server. It wraps `check`. JSON diagnostics stay the single schema. The dogfood IDE consumes that schema. It does not replace it. Prerequisites: [Dogfood IDE](#dogfood-ide).

### Dependency forms beyond `path`

Path deps only (`Manifest.scuzz`). Git, versioned, and hosted artifacts are direction. There is no registry.

**Deferred (last on purpose).** Do not add ecosystem theater before there is an ecosystem. Revisit after path deps and file-as-module are the proven reuse story.

### Deferred by decision

- **Windows desktop embedder** — same session protocol as X11/Cocoa. Secondary platform.
- **OS IME candidate windows** — focused TextField caret uses measured advance (`sz_view_caret_rect`). Embedders do not place OS IME candidate UI from it. Desktop already maps XIM preedit and Cocoa marked text into `SZ_INPUT_COMPOSE`.
- **macOS in default CI** — `macos-smoke` runs on push/PR (runtime tests, hello). Full macOS packaging stays `workflow_dispatch`. A Darwin UI link uses `-lc++`, `-framework Cocoa -lobjc`, `-force_load`, and the Skia frameworks.
- **Web apps** — not a current target.
- **HTTPS serve** — HTTP client kits take `https://` with OpenSSL and the system trust store. Handshake parks on poll. TestRuntime stubs `https://` like `http://`. There is no HTTPS `Net.serve`. Cert file load and self-signed serve would add product surface. Live serve stays plaintext HTTP on localhost.
- **Net bind and status** — Live listen is localhost. There is no `0.0.0.0` bind. Client kits fail on non-2xx. Status is not a success value. Chunked encoding and HTTP/1.1 keep-alive stay out. POSIX sockets stay inside the runtime.
- **Oracle idioms in `guide.md`** — English grammar, Given rows, and intent thunks stay deferred with mining. They are not current work. Authors write `Timeline => Verdict` and drive oracles in `*.scuzz_verify`.

### Dogfood IDE

A Scuzz `[ui]` package is the in-tree IDE. `scuzz ide` on the one CLI launches it with Desktop. Headless stays a peer. The app consumes `scuzz check`, `scuzz lsp`, `scuzz fmt`, `scuzz run`, and `scuzz fuzz`. It does not reimplement the compiler. Locks: [`vision.md`](vision.md#tooling). Proof: `examples/editor` and the SDK `ide/` tree.

Open and deferred:

- OS IME candidate-window placement stays deferred.
- Do not add `Fs.watch` or an exec stub map. File change detection stays Clock plus Fs poll.
- Live `Sys.exec` / `Sys.spawn` still fail under TestRuntime. Fuzz overlays `analyze`, `lspCall`, `runProject`, and `fuzzProject`.
- Every new editor or chrome widget has a Headless path. No Desktop-only shortcut.
- In-app open-folder UI is enough. Native OS file dialogs, native menus, and multi-window stay later.
- Multi-cursor, minimap, Git UI, debugger, plugin host, custom canvas kit, Windows desktop embedder, and web stay later.
- Flutter DevTools / VM patching is an explicit non-goal.
