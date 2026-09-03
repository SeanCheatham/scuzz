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

Close thesis-critical gaps before table-stakes kits. Close table-stakes before later items. Locks: [`vision.md`](vision.md).

### Thesis-critical

These gaps keep the distinctive claims kernel-shaped. Close them in this order.

1. **`Signal[T]` and `View.each` over records** — In: `Signal.list` holds `List[T]` over records and enums; `View.each` binds the element type; studio keeps tasks as `List[Item]`; a record list dumps `list[N] name = <count>`. Open: one generic cell — `Signal.map` stays `Int => String` and there is no non-list `Signal[T]` cell. Direction: one generic cell and `View.each` over the element type.

2. **UTF-8 `String`** — In: `Str.*` indexes code points; `Str.byteLen` / `Str.byteSlice` keep bytes for framing; the kernel `utf8Ops` drive oracle proves multibyte ops. Case maps stay ASCII by design. Caret offsets in TextField/editor stay bytes. The editor and toolchain LSP framing uses `Str.byteLen` / `Str.byteSlice`. LLVM `[N x i8]` string sizing uses `Str.byteLen`.

3. **Scuzz spans on panic and LSP** — In: `check` JSON diagnostics use recorded file stem, line, and column. No substring search. No hardcoded file. Product `scuzz lsp` is a stdio JSON-RPC server. It wraps `check`. JSON diagnostics stay the single schema. Goto-def uses `Fun.off`. Rename replaces lexer ident tokens. Hover names the ident under the caret. Completion filters by the caret prefix. Semantic tokens come from the lexer. Panic prints `scuzz panic: Main.scuzz:2:14: <msg>` from that def's file, line, and column. The dogfood IDE consumes that schema. It does not replace it.

4. **Typed fail `E`** — In: check encodes `IO[E, A]`. `IO[A]` means `IO[String, A]`. `IO.fail(e)` takes `E`. `handleErrorWith` binds `E`. `flatMap` keeps one `E`. Kits still fail with `String`. Open: the C `SzError` wire is still a string. Do not add `ZIO[R, E, A]`. Do not add user `IO.delay`.

5. **Typed agent session schema** — Live debug is `build/debug.dump`, `inject.script`, `record.script`, `repro.toml`, and `summary.toml`. `--message-format=json` applies to `check` only. Direction: one JSON schema for dump, inject, fuzz verdict, and coverage. Text dump and script stay until that schema ships.

6. **Source-region coverage** — Campaign coverage is `Property.sometimes` names plus dump novelty. Direction: report source-region coverage of live `def` bodies in `summary.toml`. `sometimes` stays a path marker.

### Table-stakes

Needed before a real CLI, server, or desktop app stays.

- **HTTP as a server** — Client kits return a body on 2xx (1 MiB cap). Live serve is localhost plaintext. Handler is `(path, method, body) => String`. Status, headers, and `0.0.0.0` bind stay out. HTTPS `Net.serve` stays out. POSIX sockets stay inside the runtime. Expand `Net` on this HTTP/1.0 stack. Do not add a second client.
- **Missing kits** — No calendar time, regex, hash, hex/base64, or UUID. `Map` / `Set` keys are `Int` or `String`. Expand blessed kits. No user FFI.
- **`scuzz eval`** — No worksheet. A one-file eval helps humans and agents try one def.
- **Kit docs** — No `scuzz doc`. Hover must show the same text as generated kit docs.
- **Generators** — Drivers take 0 or 1 generator-friendly param. Direction: `Gen[T]` combinators and shrinking that keeps `where` bounds. Stateful model generators stay later.
- **Scheduler lock** — Cooperative fibers on one thread are the scheduler for CLI, server, and UI. OS threads and supervision trees stay later.

### Later

Do not start these before thesis-critical gaps close.

- **Stable inject keys** — `tap N` / `scroll N` follow a11y preorder. A refactor can miss a stored corpus entry. Named control keys for inject stay after named claim observations.
- **Simulation world** — TestRuntime seals the wire (no live sockets; Nth Fs / Net / Queue fault; PCT on fibers; Clock and Fs fakes). Clock skew, partitions, and a model to relate against stay later.
- **Mutation depth** — Mutation flips ops, swaps `if` arms, and replaces an ADT construct with a sibling. Inert mutants stay unreported. Semantic mutants stay later.
- **Memory** — Last-use retain/release is locked in [`vision.md`](vision.md) GC. Values with no last-use stay allocated until panic sweep or process exit. No cycle collector.
- **Dependency forms beyond `path`** — Path deps only (`Manifest.scuzz`). Git, versioned, and hosted artifacts are direction. There is no registry. Revisit after path deps and file-as-module stay the reuse story. A lockfile identity can land before a registry.
- **Windows desktop embedder** — same session protocol as X11/Cocoa. Secondary platform.
- **OS IME candidate windows** — focused TextField caret uses measured advance (`sz_view_caret_rect`). Embedders do not place OS IME candidate UI from it. Desktop already maps XIM preedit and Cocoa marked text into `SZ_INPUT_COMPOSE`.
- **macOS in default CI** — `macos-smoke` runs on push/PR (runtime tests, hello). Full macOS packaging stays `workflow_dispatch`. A Darwin UI link uses `-lc++`, `-framework Cocoa -lobjc`, `-force_load`, and the Skia frameworks.
- **Web apps** — not a current target.
- **HKT and environment `R`** — Thin generics. No `F[_]` beyond `IO`. No `ZIO[R, E, A]`.
- **Oracle idioms in `guide.md`** — English grammar, Given rows, and intent thunks stay deferred with mining. They are not current work. Authors write `Timeline => Verdict` and drive oracles in `*.scuzz_verify`.
- **Type heuristics on SSA names** — In: `==` on ptr-producing call results compares by value (`sz_ptr_eq`); the i64 unbox path no longer eats string/list/ADT comparisons. Open: `List.at(xs, 0).field` reads through a first-record heuristic and mis-emits when that record lacks the field. `io.map(r => r._1)` does not box the Int result. `strish` and friends still guess operand types from SSA name substrings. Toolchain sources destructure payloads through `match` and bind call results before comparing. Direction: resolve the receiver type from the checker, not from `copyEnFirst` or name substrings.

### Dogfood IDE

A Scuzz `[ui]` package is the in-tree IDE. `scuzz ide` on the one CLI launches it with Desktop. Headless stays a peer. The app consumes `scuzz check`, `scuzz lsp`, `scuzz fmt`, `scuzz run`, and `scuzz fuzz`. It does not reimplement the compiler. Locks: [`vision.md`](vision.md#tooling). Proof: `examples/editor` and the SDK `ide/` tree. LSP span quality is thesis-critical gap 3.

Open and deferred:

- OS IME candidate-window placement stays deferred.
- Do not add `Fs.watch` or an exec stub map. File change detection stays Clock plus Fs poll.
- Live `Sys.exec` / `Sys.spawn` still fail under TestRuntime. Fuzz overlays `analyze`, `lspCall`, `runProject`, and `fuzzProject`.
- Every new editor or chrome widget has a Headless path. No Desktop-only shortcut.
- In-app open-folder UI is enough. Native OS file dialogs, native menus, and multi-window stay later.
- Multi-cursor, minimap, Git UI, debugger, plugin host, custom canvas kit, Windows desktop embedder, and web stay later.
- Flutter DevTools / VM patching is an explicit non-goal.
