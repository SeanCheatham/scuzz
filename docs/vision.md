# Scuzz Lang vision

Scuzz Lang is a Flutter-shaped product with a Scala-inspired language. It is not a Scala 3, Scala Native, or Maven citizen.

One doc for product intent, design locks, language direction, and open work. Keep/cut tables: [`compatibility.md`](compatibility.md). Manifest schema: [`schemas/scuzz-toml.md`](schemas/scuzz-toml.md). App path and surface catalogs: [`guide.md`](guide.md). Checkout host setup: [`developer-environment.md`](developer-environment.md). Future empirical pre-optimization: [`optimization.md`](optimization.md).

Edit this file when a decision or next-step order changes.

## Thesis

- **Language**: purposeful Scala-inspired subset for native CLI, server, desktop, and mobile apps, with **built-in effect/IO/Streaming** (ZIO-inspired, not a ZIO or cats/fs2 port). Dense `for` dialect (`for` as primary binder). Token-efficient for agents. Functional by default. Keep the dialect practical. See [Language direction](#language-direction).
- **Runtime**: custom native (LLVM). Native binaries, not a VM. No JVM. No Java interop. No classpath/Maven. Web is not a current target.
- **UI**: a primary product path, not the only one (Flutter-shaped: GUI is first-class; so are CLI and server). One design language + Skia, as a **`Ui` effect** with Headless/Desktop/Mobile interpreters. Headless is a product runtime (agents, CI), not a test-only shim.
- **Batteries**: the language and standard kits cover common app cases. No ecosystem library sprawl. No Maven, cats, or ZIO ports.
- **Tooling**: one opinionated CLI (`scuzz`) — compile, link, assets, watch, packaging, format, check, and the whole verification stack. One formatter (`scuzz fmt`). One linter (`scuzz check`: format-verify + typecheck; further lints emit here; no `lint` subcommand). One testing strategy. Mutation, fuzzing, property/laws, simulation, and determinism are **first-class in the language and `scuzz`**. They are not a third-party harness. Compiler, CLI, and toolchain are **Rust** (`crates/compiler`, `crates/cli`).
- **Language proof**: examples that exercise the surface (`examples/`), not a self-hosted compiler.
- **AI-Friendly**: Headless, hot reload, and debugging tools aid agents. Headless is a peer runtime. `scuzz watch` only rebuilds. `[ui] run --watch` is hot reload: it stamp-reloads Views, writes `build/debug.dump` (including `[taps]` frames / `[fields]` live strings / `[scrolls]` / `[last_hit]` after a TAP, live `View.bindText`, `[session]` kind/size/lifecycle/pumps, and `[heap]` alloc stats with kind census, delta, and `[live]` remaining blocks), and plays `build/inject.script` (`tap` / `xy` / `text` / `type` / `pump` / `scroll` / `backspace` / `dump` / `reload` / `quit` / `resetpeak`). `quit` stops the live session. `resetpeak` sets peak bytes to live and marks the heap delta. Panic prints remaining `[heap]` and `[live]` and writes `*.panic` when a dump path is set. Desktop/Mobile `scuzz run` records live OS input to `build/record.script` and writes `build/debug.dump`. Replay Headless with `scuzz run --headless --script build/record.script --dump build/debug.dump`. `[ui]` build emits `build/reload.dylib`. Stamp-watch `dlopen`s it so a source View-label change appears live (Signals stay). IO-only `run --watch` kills and reruns on source change. `scuzz lsp` wraps `scuzz check` JSON diagnostics. Language-server methods use the same parse (catalog: [Tooling](#tooling)).

Upstream Scala Native is a *reference*, not a dependency. Divergence is intentional.

## Defaults

| Topic | Choice |
| --- | --- |
| UI testing / CI | In-source laws + composed drivers through `scuzz fuzz --iterations` (primary; mutation is a phase); structural goldens as regression face; PNG optional through `--pixels` |
| Static hygiene | One linter: `scuzz check` (format-verify + typecheck; lints on this command; no `lint` subcommand). One formatter: `scuzz fmt` rewrites |
| Codegen | LLVM IR |
| Renderer (v0) | Skia through thin C ABI; Impeller deferred |
| Build tool | DIY Mill/Cargo-like: `scuzz` (not sbt/Maven) |
| Effects | Language + runtime builtins |
| Impurity | All nondeterminism / external I/O through blessed `IO`; no app-level `IO.delay` escape hatch. Simulation is hermetic (TestRuntime fakes; no live sockets; `Sys.exec` / `Sys.spawn` fail; `Sys.getenv` sealed; `Sys.alive` / `Sys.kill` fake) |
| Tests | One built-in strategy: **mutation + fuzz + laws (property) + sim + determinism** (TestRuntime). Oracles live **in source** (laws, inline checks, refinements). **Drivers** are oracle-free workloads the fuzzer composes. No classical unit-test culture. No external test frameworks |
| Modules | `scuzz.toml` package = crate; `Foo.scuzz` = module (not JVM packages) |
| Toolchain | Rust (`crates/cli`); one compiler |
| UI model | Pure `View` + effectful `Ui` session (`mount` / `pump` / `inject` / `snapshot`) |

## What Scuzz Lang is not

- Not Scala 3, not the JVM, not Scala.js, not a web/browser product
- Not a ZIO library port
- Not a cats / cats-effect / Typelevel port
- Not SwiftUI / UIKit / WinUI wrappers
- Not “every widget rebuild is an `IO`” (`View` build stays sync/pure)
- Not imperative View trees (`View.addChild`); nested constructors only
- Not classical / example-based unit-test culture (`src/test`, Mockito, assert-equal fixtures, third-party test runners) for apps. Use **mutation + fuzz + laws + sim + determinism**, all in `scuzz`, instead. Examples survive only as oracle-free **drivers**. The objection is example-based *oracles*, not examples-as-workloads
- Not Flutter DevTools / VM patching. `[ui] run --watch` is in-process hot reload (stamp-reload Views). A live structural dump and stamp-driven inject are in. `scuzz watch` only rebuilds. IO-only `run --watch` kills and reruns.
- Not an sbt / Gradle / `pubspec` plugin DSL (`scuzz.toml` is data)
- Not Flutter platform channels
- Not a self-hosted compiler as a product bar. `scuzz` is Rust. Scuzz programs are apps and examples.

## Success bars

**v0** — Install CLI (`curl …/install.sh | sh`, or checkout `./scripts/install.sh`) → `scuzz new` (IO) or `scuzz new --ui` (Counter as `View` + builtin `IO`) → `scuzz test --update` then `scuzz test`, and `scuzz run` (`--headless` for UI). Desktop when available. Language `Resource` / `Stream` / `Net.serve` ship (`examples/io`).

**v1** — Shipped `scuzz` is the Rust CLI (GitHub Releases; `package_release.sh` / `install.sh`). Kernel surface is proven by examples. `fuzz` lives on that CLI.

## Decisions

### Product name

Brand in prose: **Scuzz Lang** (short form **Scuzz**). CLI / cargo package `scuzz`; compiler crate `scuzz-compiler`; manifest `scuzz.toml`; sources `*.scuzz` (plus stem-paired `*.scuzz_sim` / `*.scuzz_drivers`); C ABI `sz_` / `Sz*` / `SZ_*`. No dual names or legacy aliases.

### Tooling

One CLI. One typer. One formatter. One linter. One testing strategy. No second analyze frontend. No `*.g.scuzz` codegen. No `src/test` runner. No bolted-on mutation/fuzz/property ecosystems.

- **Watch** rebuilds when sources or `scuzz.toml` change. It does not patch running machine code. `[ui]` `run --watch` is hot reload: it recompiles `build/reload.dylib`, stamps, and swaps the View tree without resetting Signals (see [`guide.md`](guide.md)). IO-only `run --watch` kills and reruns the process on source change.
- **Static hygiene** is `scuzz check` (the linter). `scuzz fmt` rewrites. No `lint` subcommand.
- **Verification** is built into `scuzz` and the language (laws, sim overlays, deterministic TestRuntime, fuzz search, mutation). Not optional crates or Maven/npm test plugins.
- **JSON diagnostics** (`scuzz check --message-format=json`) are the editor protocol. `scuzz lsp` wraps that, overlays open buffers, and serves hover, completion, definition, document symbols, references, rename, workspace symbols, signature help, document highlights, folding ranges, format, selection ranges, inlay hints, semantic tokens (full and range), code actions, pull diagnostics, call hierarchy, type hierarchy, type definition, implementation, code lenses, document links, and `workspace/executeCommand` (`scuzz.references`) from the same parse. Quickfix actions attach the check diagnostic they fix. `codeAction/resolve` fills the edit from action data. `workspace/diagnostic` lists every src file. `textDocument/declaration` jumps to the import that bound a name. `workspace/willRenameFiles` rewrites `import Module` and qualified `Module.name` when a `.scuzz` stem changes. Unknown-function diagnostics include a related "did you mean" location. Non-exhaustive match diagnostics include the enum def. Unused import, unused local, unused parameter, and unused private def diagnostics include a quickfix. Do not grow a second typer or schema.
- **`scuzz.toml` is data** — package, path deps, `[ui]`. No plugin DSL. No `build.scuzz` hooks. Unknown keys rejected. Do not add `[plugins]`.
- **Fingerprint** (incremental): miss → rebuild. The fingerprint includes compiler/runtime identity, native sources, target, clang version, Skia backend, and verify mode. Live (`fingerprint`) and verify (`fingerprint.verify`) share `build/` artifacts. A compile writes its mode file and deletes the sibling so a later switch rebuilds. No `scuzz clean` ritual.
- **Native make:** quiet on success. A failed `make` prints the log. Clang compiles `.ll` without C `-I`. Link uses `-Wno-override-module` (IR has no host triple).
- **Missing tools:** fail on the first missing tool with one install line. No `flutter doctor` mega-checklist.
- **`scuzz package`:** Android runs `crates/embedder-mobile/shells/android/build_ndk.sh` and emits `libscuzz.so` (needs the NDK). iOS runs `crates/embedder-mobile/shells/ios/build_sim.sh` and emits a signed simulator `.app`. Not a Gradle/CocoaPods API.

### GC (v0)

libc `malloc`/`free` through `sz_alloc` / `sz_free`. No collector. Heap strings, list cells, ADTs, boxed i64, map/set nodes, IO nodes, stream nodes, Resource nodes, errors, Ref, Queue, Deferred, Either, and pair are reference-counted (`sz_retain` / `sz_release`). Live `[heap]` dumps count and bytes per kind (`raw` plus each RC kind) and `delta_bytes` / `delta_count` since the last live dump or `resetpeak`. Live `[live]` dumps remaining blocks (`kind rc=N bytes=M`, capped).

The compiler emits release on an owned temp after last use of a kit call, and retain when a function returns a borrowed pointer of that sort. Last-use covers Str, List, Map, Set, ADT construct/match, list literals, tuple construct/match/project, tuple and constructor `for` / lambda unpack, and the IO / Fiber / Stream / Resource / Net / Sys / Fs / View / Signal kits in [`guide.md`](guide.md). A user function call drops owned pointer arguments after the call. The callee borrows parameters. A String, List, Map, Set, ADT, or tuple return is owned so a later last-use drops it. An `IO[T]` return marks the payload owned when `T` is one of those types. List/Map/Set filter and map closure packs drop after unpack. `List.foldLeft` / `List.foldRight` / `List.scanLeft` / `List.scanRight` / `List.reduceLeft` / `List.reduceRight` packs drop after unpack. `IO.foreach` / `IO.foreachDiscard` packs drop the same way after unpack. `Ref.update` / `Ref.updateAndGet` packs drop the same way after unpack. The kit retains the env. A `List.map` / `List.flatMap` / `List.tabulate` / `List.sortBy` / `List.maxBy` / `List.minBy` / `List.groupBy` / `List.distinctBy` / `List.foldLeft` / `List.foldRight` / `List.scanLeft` / `List.scanRight` / `List.reduceLeft` / `List.reduceRight` / `Map.mapValues` / `Set.map` / `Stream.map` / `Ref.update` / `Ref.updateAndGet` mapper returns an owned pointer. A borrowed body retains before return. The runtime takes that mapper ref. `List.append` retains the element like `cons`. The caller drops an owned element after the call. A tap lambda drops a leftover owned pointer, or the run result after `unsafe_run`. `Law.check` / `Law.assert` / `Law.sometimes` / `Law.a11yHas` drop an owned name or needle. `Law.check` keeps the value owned. `Law.signalStr` / `Law.signalListAt` return an owned string. `Law.force` drops the unbox box after the run.

`Stream.emit` retains the payload and drops an owned value or a boxed Int/Float. `Stream.emits` retains payloads and drops an owned list. `Stream.eval` retains the IO and drops the caller ref after the call. Stream nodes are reference-counted. Combinators retain the inner stream and drop an owned input after the call. `Stream.compileToList` / `Stream.drain` / `Stream.exists` retain the stream for the IO and drop an owned input after the call. `Signal.list` / `Signal.setList` retain the list and drop an owned input after the call. `Signal.str` / `Signal.setStr` copy bytes and drop an owned string after the call. View constructors that copy a string drop the owned input after the call. Tap constructors unpack `cons(fn, cons(env, nil))` and drop the owned pack after the call. They retain the env list. `sz_view_free` releases it. `View.each` mappers, `Signal.map`, and `Ui.run` rebuild packs drop the same way after unpack. They retain the env. The View, mapped Signal, or session releases it. `Stream.filter` / `Stream.map` / `Stream.evalMap`, `Resource.make` / `Resource.use`, and `Net.serve` / `Net.serveOnce` drop the callback pack after unpack. Stream combinators, `Resource`, and `Net.serve` retain the env. The resource, use IO, stream, or server releases it.

`Resource.make` retains the acquire IO and drops an owned acquire after the call. It marks the result owned. `Resource.use` retains the resource and the acquire IO. The resource keeps acquire after use constructs or runs. After release, last-use drops the acquired payload. It drops an owned resource after the call. It keeps the resource and use env in a pair until start. The live use pack is RC so HANDLE last-use drops the pack. Resource free releases acquire.

It tracks owned `let` / `for` binders and releases them after the body when the body is a scalar, `IO`, a fresh owned ptr, or an `if` / match phi (retain the result, then drop the binder). A discarded `_` let drops an owned pointer or an unused IO without inserting a local. Nested `_` binders do not share a slot. An unused named IO let drops after the body. A discarded `_ <-` with an owned payload releases `%value` after the body. Capture packs drop a boxed Int or Float after cons. It marks an `if` / match phi owned when every arm produces an owned ptr, so a later last-use drops the taken arm. A mixed owned/borrowed `if` retains the borrowed arm at the join and marks the phi owned. A mixed owned/borrowed match retains each borrowed arm at its join and marks the phi owned. List cells retain heads and shared tails. Map/Set trees share subtrees.

IO constructors take child IO nodes. `IO.forever` / `IO.repeatN` / `IO.retryN` / `Fiber.fork` / `IO.timeout` / `IO.when` / `IO.unless` / `handleErrorWith` / `flatMap` / `IO.attempt` retain inner and drop the caller ref after the call. `IO.when` / `IO.unless` keep inner only when the cond selects it. The compiler retains a `flatMap` / `handleErrorWith` capture list and drops the pack after the call. The runtime retains that env and drops it after the continuation. `handleErrorWith` marks the `sz_error_message` binder owned and drops it after the handler body. `sz_error_message` retains the error's message and returns it. A null error yields a fresh string. `sz_either_left` retains the error and drops the caller ref after the call. `sz_either_right` retains the value and drops the caller ref after the call. Either and pair cells are reference-counted. `IO.attempt` / `IO.both` mark the payload owned so last-use `sz_release` drops the cell. `sz_either_free` / `sz_pair_free` are `sz_release`. Last `sz_release` of an Either releases the Right value or the Left error, then frees the cell. Last `sz_release` of a pair releases both fields, then frees the cell. `sz_pair_new` retains both sides and drops the caller refs after the call. `sz_ref_make` retains the initial value and drops the caller ref after the call. `Ref.of` / `Ref.set` / `Queue.offer` / `Deferred.complete` / `Stream.emit` box an Int or Float payload. `Ref.get` / `Queue.take` / `Deferred.get` / `Fiber.join` unbox that payload. Ref, Queue, and Deferred cells are reference-counted. `Ref.of` / `Queue.unbounded` / `Deferred.empty` mark the handle owned so last-use `sz_release` drops the cell. `sz_ref_free` / `sz_queue_free` / `sz_deferred_free` are `sz_release`. `sz_ref_set` retains the new value and drops the caller ref after the call. It releases the previous value when the set runs, including when both are the same pointer. `sz_queue_offer` retains the value and drops the caller ref after the call. `Queue.offer` / `Deferred.complete` / `Ref.set` / `Fs.write` / `Net.httpGet` keep the payload in a pair. Last-use of the delay node drops leftover retains. `Net.httpGet` keeps the URL in a pair until dispatch. The live get pack is RC so HANDLE last-use drops the pack. `Sys.exec` keeps the command in a pair until start. The live exec pack is RC so HANDLE last-use drops the pack. `Net.serve` keeps the handler env and the serve spec in a pair until start. The live serve pack is RC so HANDLE last-use drops the pack. Last `sz_release` of a Queue releases leftover items, then frees the cell. `Queue.take` transfers that offer retain. Do not retain again in take. The compiler marks the take payload owned and drops the binder after last use. `sz_deferred_complete` retains the value and drops the caller ref after the call. Last `sz_release` of a Deferred releases a completed value (and a failed error), then frees the cell. `sz_ref_get` retains the current value so the run result does not alias the Ref slot. The compiler drops an owned get binder after the body. `sz_deferred_get` retains the completed value so the run result does not alias the Deferred slot. The compiler drops an owned get binder after the body. `Fiber.join` retains the completed value so the run result does not alias the fiber slot. The compiler marks the join payload owned and drops the binder after last use. Fiber free releases leftover `result_value` and `result_error`. `IO.race` / `IO.both` / `IO.timeout` retain child results the same way. They copy a child error instead of taking it. `IO.pure` retains the payload and drops an owned payload after the call. The compiler marks a pointer payload owned and drops the binder after last use. Run retains so last-use does not free the IO slot. Last `sz_release` of the IO node drops a leftover RC payload. Delay result boxes (`Net` / `Sys` / `Fs` / TestRuntime) are RC. Unwrap takes ok/err and drops the box. `IO.forever` / `IO.repeatN` drop a discarded iteration value before the next run. `IO.fail` retains the error and drops an owned error after the call. Run retains so `fiber_fail` does not free the IO slot. Last `sz_release` of the IO node drops a leftover error. `IO.retryN` drops a discarded iteration error before the next run. `sz_io_delay` retains a RC env and drops an owned env after the call. A delay thunk borrows env. Unique run (not a BOX env) steals env and drops it after the thunk. Shared run leaves env on the node so a loop can rerun. Last `sz_release` of the delay node drops leftover RC env or a leftover `sz_alloc` env. It does not load an RC header from a string literal or a small integer env. `Sys.getenv` keeps the key in a pair until run. Last-use of unused getenv drops leftover key retains. `Sys.alive` / `Sys.kill` keep the pid in an RC box as delay env. Last-use of unused alive/kill drops the pack. `Sys.spawn` keeps the command in a pair until start. Last-use of unused spawn drops leftover command retains. `Sys.write` keeps the payload in a pair until dispatch. Last-use of unused write drops leftover string retains. `Sys.read` keeps n in an RC box as flatMap env. Last-use of unused read drops the pack. `Random.nextInt` keeps the bound in an RC box as delay env. Last-use of unused nextInt drops the pack. `Fs.read` / `Fs.list` / `Fs.mkdirs` / `Fs.canonicalize` keep the path in a pair until dispatch. Last-use of unused read/list/mkdirs/canonicalize drops leftover path retains. TestRuntime `Sys.read` keeps n in an RC box as delay env. Last-use of unused TestRuntime read drops the pack. `Ui.run` keeps the rebuild env in a pair until start. Last-use of unused run drops leftover env retains. `IO.ensure` / `IO.race` / `IO.both` retain both child IO nodes and drop the caller refs after the call. `unsafe_run` takes the root result (it nulls the slot) and drops leftover fiber results when it frees the fibers. Panic prints remaining `[heap]` census and `[live]` rows. It writes `SCUZZ_PANIC_DUMP` or the registered `*.panic` file when that path is set. It then frees remaining live blocks and abort. Clear ownership still frees `Resource` / Views on last-use. Values without a last-use stay allocated until panic sweep or process exit. Immutable data forms no cycles, so no cycle collector is needed.

### Skia

No vendored Skia tree. Thin `sk_capi` (measure + draw). **Default UI backend** is the pinned Skia CPU prebuilt (`third_party/skia/PIN` → `scripts/fetch_skia.sh`). App link uses host zlib/bz2, not brotli. In-tree `sk_sw` is the explicit opt-out (`SCUZZ_SKIA=sk_sw`). `SCUZZ_SKIA=gpu` paints with `sk_sw` and presents through OpenGL (must not change `Ui` session or logical goldens). Impeller / Skia GPU raster stay deferred. Callers depend only on `sk_capi.h`.

### IO and impurity

One failure channel: `SzError` on `IO`. Blessed kits only. No app-level `IO.delay`. Cooperative single-threaded fibers (no OS threads for IO). `sleep` / empty `Queue.take` / incomplete `Deferred.get` / fd poll park. Cancel (race loser / `IO.timeout` / `Fiber.interrupt`) runs `IO.ensure` / `Resource` finalizers, including an unstepped `IO.ensure` still on `cur`. Cancel releases in-flight flatMap/handle env. `Fiber.fork` starts a supervised child (`join` parks; unjoined children cancel when the root completes). `IO.timeout(ms, inner)` is a blessed race of sleep-fail vs inner and keeps inner's `IO[T]`. `IO.forever(inner)` reruns until failure or cancel (`IO[Unit]`, never succeeds). `IO.repeatN(n, inner)` runs inner once plus `n` extra times (last success). `IO.retryN(n, inner)` retries on failure up to `n` extra times. TestRuntime (`SCUZZ_TESTRT=1`) fakes clock/random/FS/net/console. Simulation is hermetic: TestRuntime does not open live sockets; `Net.httpGet` uses stubs; `Net.serve` uses injected paths; a missing stub fails; `Sys.exec` and `Sys.spawn` fail; `Sys.getenv` reads a sealed map; `Sys.alive` / `Sys.kill` use a fake process table. Live `Net.serve` binds localhost (`127.0.0.1` and/or `::1`). Live `Net.httpGet` is `http://` only. DNS uses answer RRs for the queried name. A 2xx response finishes on `Content-Length` or EOF. Bodies cap at 1 MiB. Live `Net.httpGet` may leave the host. Surface catalogs: [`guide.md`](guide.md). Panics abort through `sz_panic`.

### `Ui` vs `View`

| Layer | Role | Purity |
| --- | --- | --- |
| **`View`** | Widget tree | Sync/pure `build` |
| **`Ui` / `UiSession`** | `mount` / `pump` / `inject` / `snapshot` | Effectful (`UiRuntime`) |

Headless is a **peer** of Desktop/Mobile — product runtime for agents and CI. Frame boundary is `pump`. World effects stay blessed `IO`. Bridge into signals through `sz_ui_bridge_post_*`. No UI feature without a Headless path. Prefer `Signal.list` + `View.each`. Derived display through `Signal.map` + `View.bindText`. Nested declarative construction only. `Ui.run(_ => view)` is the session. The factory re-runs on stamp-watch. Create Signals outside the factory so they stay. Live Desktop/Mobile drain appends to `build/record.script` (`tap N` / `xy x y` / `type` / `backspace` / `scroll N dy`). Headless replays that file through `SCUZZ_UI_SCRIPT` (`--script`). Dump `[taps]` includes frames; `[last_hit]` shows the last TAP target or `NULL`.

### IO apps vs Headless

| Path | Meaning |
| --- | --- |
| **Headless** | `UiRuntime` peer — still `View` / Skia / structural dumps |
| **IO-only** | No `[ui]`, no `Ui.run`; plain `@main: IO[Unit]` exec |

Missing `[ui]` ⇒ Skia omitted from the link. `scuzz test` is TESTRT exit-0 smoke (not a11y goldens). IO-only is **not** a fourth runtime peer.

### Kernel dialect

The language `scuzz` implements. Proof is examples that exercise each construct (`examples/hello`, `kernel`, `io`, `counter`, `studio`).

Locks (not an API catalog — see [`guide.md`](guide.md)):

- Expression dialect only: `for` primary binder (`=` pure, `<-` effect); no `val` / statement blocks / `var`. A `for` binder may unpack a tuple of 2 through 8 slots (`(a, b) = e`, `(a, b, c) = e`, `(a, b) <- e`) or a constructor, list, or as-pattern (`Point(x, y) = p`, `Opt.Some(n) <- e`, `h :: t = xs`). A miss panics.
- Optional `package`; top-level `def` / `private def` / `import`; `@main def …: IO[Unit]`
- Payload enums + `record` sugar (methods on generic records and enums) + thin traits/`impl` (static dispatch, including `trait Get[T]` / `impl Get[Int] for Point` / `impl Get[T] for Opt`) + monomorphized generics on defs/enums/records. Match arms may use `case Pat if pred =>` (`pred` is Bool). A guarded arm does not cover the pattern for exhaustiveness. Match arms may use literal patterns (`case 0 =>`, `case "ok" =>`, `case true =>`). `true` and `false` together cover Bool. Int, Float, and String literals need `_` or a name bind. Match arms may use or-patterns (`case A | B =>`). Nested or in a payload works (`case Opt.Some(0 | 1)`). The body must typecheck for every alternative. Match arms may use an as-pattern (`case n @ Opt.Some(_)`). `n` binds the whole value. `n @ A | B` binds `n` for every alternative. Match arms may use list patterns (`case [] =>`, `case x :: xs =>`, `case [a, b] =>`). `[]` and `_ :: _` together cover `List`. Match arms may use a tuple pattern (2 through 8 slots) (`case (a, b)`). `(A, B, …)` is exhaustive. `p._1` … `p._N` project slots. A `for` binder and a lambda may unpack the same way (`(a, b) = e`, `(a, b) <- e`, `(a, b) =>`, `Point(x, y) = p`, `Opt.Some(n) <- e`, `h :: t = xs`, `(Opt.Some(n)) =>`). Nested tuples are allowed. A miss panics. Match arms may use named field patterns (`case Point(x = n)`, `case Opt.Some(x = n)`). Omitted fields are `_`. Positionals come first. A lambda may pin its parameter with `(x: T) =>`. The annotation must match the expected kit or `A => B` parameter type. A kit or `A => B` argument may use one `_` hole (`List.map(xs, _ + 1)`, `List.map(xs, Str.fromInt(_))`). `_ + _` is rejected. `_ =>` still discards.
- File-stem modules; enums namespaced by stem; `import Module.name` for bare disambiguation. `import Module.name as alias` binds a local name. `import Module.*` binds every public def, enum, and type alias. `check` reports an unused import, an unused local, an unused parameter, and an unused private def. A name that starts with `_` is kept on purpose. Lone `_` discards. A record value rebuilds with `.copy` (`p.copy(y = 9)`, `p.copy(1)`). Omitted fields keep the receiver values. Positionals fill fields in order. A file may declare `type Name = T` or `type Name[T] = List[T]`. The checker expands the alias in params, returns, and `e: T`.
- Types: `Unit`, `Int`, `Float`, `String`, `Bool`, `List[T]`, `Map[K, V]`, `Set[T]`, `(A, B, …)`, `IO[T]`, `A => B`, blessed handles (`Fiber[A]`, `Ref[A]`, `Queue[A]`, `Deferred[A]`, `Resource[A]`, `Stream[A]`), nominal enums. `true` / `false` are `Bool`. `Bool` is not `Int`. `attempt` is `IO[Result[A]]`. `handleErrorWith` binds a `String` error message. Unary `!` is Bool. Unary `-` is Int or Float. Unary `~` and bitwise `&` / `|` / `^` / `<<` / `>>` are Int. `h :: t` is `List.cons(h, t)` (right-associative). Hex (`0xFF`) and binary (`0b1010`) are Int literals. Underscore may separate digits (`1_000`, `0xFF_00`, `0b1010_0001`). Scientific notation is a Float (`1.5e-3`, `1e10`). Triple-quoted strings (`"""…"""` / `s"""…$x…"""`) keep newlines. Identifiers may start with `_` (`_n`). Lone `_` discards. Call arguments may use `name = expr`. Positionals come first. Named arguments fill the remaining parameters by name. A `def` parameter may have a default (`m: Int = 1`). Omitted trailing arguments use the default. Named calls may omit any parameter that has a default. Defaults are closed expressions (no parameter names). Laws, traits, and methods do not take defaults. A call may end with a comma. An expression may pin its type with `e: T`. `==` / `!=` on String, List, Map, Set, tuples, enums, and records compare by value. Blessed handles compare by identity.
- Blessed kits + `Signal` / `View` / `Ui` / `Law.*` / `.require` as documented in the guide. Kit lambdas bind the list element type (`List.filter` / `List.filterNot` / `List.map` / `List.flatMap` / `List.find` / `List.findLast` / `List.exists` / `List.count` / `List.takeWhile` / `List.dropWhile` / `List.forall` / `List.indexWhere` / `List.lastIndexWhere` / `List.span` / `List.partition` / `List.prefixLength` / `List.segmentLength` / `List.sortBy` / `List.maxBy` / `List.minBy` / `List.groupBy` / `List.distinctBy` / `IO.foreach` / `IO.foreachDiscard`), the fold pair (`List.foldLeft` / `List.foldRight` / `List.scanLeft` / `List.scanRight` / `List.reduceLeft` / `List.reduceRight`), the map value (`Map.filter` / `Map.exists` / `Map.forall` / `Map.mapValues`), the set element (`Set.filter` / `Set.exists` / `Set.forall` / `Set.map`), the ref cell (`Ref.update` / `Ref.updateAndGet`), `String` (`View.each` / `Net.serve`), the stream or resource payload (`Stream.*` / `Resource.make` / `Resource.use`), or `Int` (`Signal.map` / `List.tabulate`). The lambda body must return the kit result (`View`, `Bool`, `String`, `T` for `List.map` / `List.tabulate` / `Map.mapValues` / `Stream.map` / `Ref.update` / `Ref.updateAndGet`, `Int` for `List.sortBy` / `List.maxBy` / `List.minBy`, `Int` or `String` for `List.groupBy` / `List.distinctBy` / `Set.map`, `List` for `List.flatMap`, or `IO`). A lambda may pin that bind with `(x: T) =>`. A lambda may unpack a tuple of 2 through 8 slots with `(a, b) =>` or a constructor with `(Opt.Some(n)) =>`. A kit or `A => B` argument may use one `_` hole (`List.map(xs, _ + 1)`, `Str.fromInt(_)`). `_ + _` is rejected. `_ =>` still discards.
- No macros, no implicits, no HKT beyond `IO`, no null

## Language direction

Expression-only, effect-sequenced dialect. Dense. Deterministic. Verification-friendly without becoming a proof assistant. **`for` is the kernel binder**. No `val`/statement blocks.

### Expr dialect

- **`for` as primary binder**: `x = e` (pure alias), `x <- e` (effect). `(a, b) = e` / `(a, b, c) = e` and `(a, b) <- e` unpack a tuple of 2 through 8 slots. `Point(x, y) = p`, `Opt.Some(n) <- e`, and `h :: t = xs` unpack the same way. A miss panics.
- **No statement blocks**, no `var`, no `val`
- Branch arms stay expressions. Nested `for` when an arm needs names.
- Surface sugar elaborates to a small core (`Let`, `FlatMap`, `match`, ADTs, `IO`). The compiler and checkers target the core.

App-shaped Counter (full surface: [`guide.md`](guide.md)):

```scala
@main def main: IO[Unit] =
  for {
    count = Signal.int(0)
    label = Signal.map(count, n => s"count = $n")
    _    <- Ui.run(_ => View.column(
              View.bindText(label),
              View.button("+1", _ => Signal.set(count, Signal.get(count) + 1))
            ))
  } yield ()
```

### Density

Clear-dense, not cryptic-dense: nested declarative `View`s, inference, single-expr forms, short update verbs, enums + match. Dense source is also token-efficient for agents. Avoid implicits, deep HKT, “everything is `IO`,” and imperative `View.addChild`. Functional by default. Keep the dialect practical.

### Modules and source shape

Scala **nouns**, Rust/Cargo **verbs**. No JVM packages. Rust `struct`/`impl` is not the primary story.

| Layer | Meaning |
| --- | --- |
| Package (`scuzz.toml`) | Dependency / link boundary (crate) |
| File module (`Todo.scuzz`) | Namespacing + visibility |
| Optional deeper `mod` tree | Only when a single file gets heavy |

Direction: payload **enums** / **`record`** + thin **traits**-as-interfaces. Monomorphize generics early. No classes (mutable identity). No `var`. No classpath packages. Path deps remain the unit of reuse. `private def` is module-local (default public). No `pub` yet. Details and examples: [`guide.md`](guide.md). Keep/cut: [`compatibility.md`](compatibility.md).

### Laws, simulation, mutation, and verification

App correctness is **not** classical unit tests. Prefer **mutation, fuzzing, property-oriented laws, simulation, and determinism**. All are first-class in the language and `scuzz` tooling. The split is **oracles in source, drivers as the test surface**:

- **Oracles live in the live module.** Top-level `law` declarations (pure `Bool` predicates; nullary or generator-friendly `Int` / `String` / `Bool` params), explicit `.require(pred)` on values / `IO` (type-preserving; residual `Law.check` / sequenced `Law.assert` under verify), reachability `Law.sometimes(name)`, and `where` refinements on `def` params and `record` fields. All erase from live builds. Armed under TestRuntime / fuzz / mutation. Nullary laws apply at the call site through `.require`. Parameterized laws are instantiated by `scuzz fuzz`.
- **Drivers (`*.scuzz_drivers`) do things.** Impure, parameterized, oracle-free steps. `scuzz fuzz` composes them (generated args, random order / interleaving) alongside the UI event alphabet. `check` rejects `Law.*` and `.require` in driver files. An assert inside a driver is a unit test in disguise.
- **Simulation is hermetic.** Fuzz, mutation, and TestRuntime keep impurity inside fakes. No live sockets. `Net.httpGet` beyond the stub map fails. `Sys.exec` / `Sys.spawn` fail under TestRuntime so a child cannot open the network. `Sys.getenv` does not read the host map. `Sys.alive` / `Sys.kill` do not touch host pids. Live `Net.httpGet` may leave the host.
- **`Law.sometimes` keeps composition honest.** Reachability accumulates across a fuzz *campaign*. Declared-but-never-reached states fail the campaign. Oracle-free drivers cannot pass vacuously. It is a coverage/fitness signal for corpus guidance, alongside Headless dump novelty. It is a path marker (`Unit`), not a value method.
- **Mutation pressures the oracles.** `scuzz fuzz` mutates live `def` bodies after search. Residual oracles must kill each mutant. `--oracles` mutates residual predicates. Surviving mutants mean weak, unreached, or missing laws/refinements.
- **Expected-fail proof** is `examples/bad-example`. `bump` is wrong. Search drives `bumpIncreases` and writes `repro.toml`. `scuzz check` and `scuzz test` still pass. `--no-fail-fast` still mutates. A failing search keeps no passing corpus, so idle mutants survive when residual oracles do not fire on `@main`. `scale` has no oracle. After you fix `bump`, search passes and mutants of `scale` still survive.

| Phase | Role |
| --- | --- |
| `scuzz check` | Format-verify `src/`; typecheck live + laws/refinements + sim + drivers; laws pure, drivers `IO`, no oracles in drivers; every nullary `law` must appear in a `.require`; parameterized laws stay generator-friendly; sim bindings match live types/purity. Reports every parse and type diagnostic in the run |
| `scuzz build` (verify graph) | Layer `*.scuzz_sim` over live defs; compile drivers; residualize `.require` / `where` checks (armed under TestRuntime / fuzz / mutation only); publish parameterized laws as drive targets |
| `scuzz fuzz` | `--iterations N` campaign. Search: probe, then exhaustive `[ui]` deepening while the next full depth fits, then coverage-guided random. Kept prefixes / schedule seeds form the corpus. Shrink a failing prefix before `repro.toml`. Default stops at the first search failure. `--no-fail-fast` keeps that repro, finishes search, then mutates. `[ui]` kept prefixes run twice; dump mismatch fails. Then mutation: idle probe plus corpus replay. `--oracles` mutates residual `Law.check` / `Law.assert` / `.require` predicates. Per-run oracles: `.require` residuals, refinements, panic/`SzError`. Per-campaign: `Law.sometimes` reachability. Survivors fail. Writes `build/fuzz/summary.toml` |
| Later (optional) | Discharge trivial law/refinement fragments statically; leave the rest as search |

Direction beyond this (not current work): fuzz-verified `*.scuzz_tune` — [`optimization.md`](optimization.md). Pivot slices and status: [`gaps.md`](gaps.md).

**File convention (stem-paired, no attribute tags):**

```text
src/
  Todo.scuzz              # live module: defs + laws + refinements (all oracles)
  Todo.scuzz_sim          # same names replace live defs under sim / fuzz / mutation
  Todo.scuzz_drivers      # oracle-free workloads composed by scuzz fuzz
```

- Live/`scuzz run` loads `*.scuzz` only. Laws, `.require`, inline checks, and refinements erase.
- No separate laws file. Oracles belong next to the code they constrain, in `*.scuzz`.
- Fuzz / test layers sim, then arms oracles. Mutation is a fuzz phase. Rename drift fails `check`.
- No free-floating `tests/` package roots. No third-party test or mutation frameworks.
- Prefer swapping values you own through `*.scuzz_sim`. Blessed kits stay one implementation + TestRuntime fakes. Do not stub pure helpers, `View` builders, or `Signal` cells. TestRuntime does not open live sockets.
- Driver params stay generator-friendly (`Int` / `String` / `Bool`). Drivers may call live defs but never assert correctness.
- Observation surface: signal store + View/a11y dump — not Skia pixels. Kernel/runtime keep ordinary example tests. This strategy is for **Scuzz apps**.

### Verification posture

Keep purity checkable (pure `A` vs `IO` vs session). Total expr core. Signals as an explicit store. Immutable data by default. Errors as values. Refinements attach to **positions** (`def` params, `record` fields), not a refined-type algebra. Checked dynamically at call / construction under the verify graph. Erased live. `.require(pred)` attaches an oracle to a **value** (or `IO[A]` sequenced after the effect) while preserving the receiver type — even when `pred` is `IO[Bool]`. They are the fragment that migrates to static discharge later with no author rewrite. Defer dependent types and runtime-heap proofs. Residual oracles + fuzz + mutation first. Static proof only where cheap.

### `scuzz fuzz`

Deterministic TestRuntime + (for `[ui]`) Headless event scripts (plus sim overlays when present). The fuzz alphabet is `[taps]` targets, text fields, and scrolls **plus declared drivers** (`drive <name> [args]` extends the script line protocol; the verify build publishes the driver table alongside the a11y dump) **plus parameterized laws** (same `drive` verb). Oracles: in-source **laws/refinements** first. Panic/`SzError` still fails. `Law.sometimes` reachability judges the campaign. Structural dumps aid diagnosis (PNG last). `repro.toml` records a **shrunk** event list + driver invocations, so replay is generator-independent. `[ui]` kept prefixes run twice; a dump mismatch fails the campaign. `tap N` / `scroll N` follow a11y preorder (`[taps]` / `[scrolls]`), not a pixel scan. `tap N` activates that target directly so a control below the viewport still fires. Headless fuzz stays stable across fonts. `pump` is time. No hidden nondeterminism. Determinism makes any failing prefix replayable. Seeded `--iterations N` splits the budget: about two thirds search, the rest mutation slots. Search tries exhaustive `[ui]` event scripts depth by depth while a full depth fits, then keeps `[ui]` prefixes (with their schedule seed) that hit new `Law.sometimes` names or a new Headless `dump.txt`, and IO-only schedule seeds that hit new sometimes names, then extends/perturbs them (CLI-only; no runtime machinery). Search always includes `pump`. Unused search budget moves to mutation. Default stops at the first search failure. `--no-fail-fast` keeps that repro, finishes search, then mutates. Mutation compiles one mutant per slot, idle-probes, then replays the corpus. `--replay` restores a shrunk event list. `--oracles` mutates residual predicates. The command writes `build/fuzz/summary.toml` (coverage and mutation). Flags, script verbs, and schedule seeds: [`guide.md`](guide.md).

### Layout model

**Flutter-style constraints** (constraints down, sizes up). Tight slots: `sized`, `aspectRatio`, percent axes on `fraction`, `expanded` flex, and opt-in `stretch` (cross axis). Scroll content is unbounded on the pan axis (`max` 0). Column/row do not stretch non-flex children unless wrapped in `View.stretch`. Device-pixel paint multiplies author px by the backing scale so taps match the pixels. Desktop and Mobile present that pixel buffer into a point-sized window. Taps stay in logical points. Nested constructors only. Do not drift into CSS-ish ad-hoc rules. Do not grow Flutter-style constraint-overflow dumps. Diagnose through structural dumps + laws. Widget catalog: [`guide.md`](guide.md).

### UI testing

**In-source laws + composed drivers through `scuzz fuzz`** are primary for `[ui]` apps. Mutation is a phase of that command. **Structural goldens** (Headless signal + a11y dumps) are a regression face — few, live graph, not a substitute for laws. PNG optional (`scuzz test --pixels`). IO packages: laws + drivers + sim under TestRuntime when present. Otherwise compile + TESTRT exit-0 smoke.

## Open work

Unknowns and known gaps: [`gaps.md`](gaps.md). `scuzz package --target ios` and `--target android` drive the platform shells. `SCUZZ_SKIA=gpu` presents through OpenGL. `Sys.getenv` is sealed under TestRuntime. `Sys.alive` / `Sys.kill` use a fake process table. Device packaging and Impeller / Skia GPU raster stay deferred.

App authors: [`guide.md`](guide.md). Vertical slices over breadth. No Desktop-only UI features. UI is a primary path among CLI/server/desktop/mobile. It is not the only v0 bar. Web is not a current target. `scuzz package --target ios` builds a signed simulator `.app` when Xcode is present. The iOS shell feeds typed text into TextField. `scuzz package --target android` packs a debug APK when the NDK and SDK are present. The Android shell blits frames onto a SurfaceView and feeds taps and typed text into the pump. Device builds stay open.

## Risks

| Risk | Mitigation |
| --- | --- |
| Language + UI + tooling is huge | Small subset. Vertical slices. Counter before generality |
| Dialect unexercised by apps | Kernel examples that stress each construct. `check` / `test` / `fuzz` on the passing `examples/`. `examples/bad-example` is the expected-fail fuzz campaign |
| Effects too weak or too heavy | Builtin IO. Pure `View`. `Ui` at session boundary |
| Hidden nondeterminism | Closed impurity + hermetic TestRuntime + deterministic `*.scuzz_sim`. No live sockets under sim. `Sys.exec` / `Sys.spawn` fail under TestRuntime. `Sys.getenv` is sealed. `Sys.alive` / `Sys.kill` use a fake process table |
| Laws become brittle dump goldens | Laws talk to named module/signal surface. Strict sim/live pairing in `check`. Mutation kills weak oracles |
| Sim becomes Mockito | Only top-level same-name overlays. No stubbing pure `View`/`Signal`. Kits stay TestRuntime |
| Drivers become integration tests | `check` rejects `Law.*` in driver files. Correctness lives only in live-module oracles |
| Drivers pass vacuously | `Law.sometimes` reachability fails the campaign when declared states are never reached |
| Verification tool sprawl | One `scuzz` strategy — mutation/fuzz/laws/sim/determinism in-tree. No external test frameworks |
| “Almost Scala” confusion | Explicit non-goals. Language direction above. [guide.md](guide.md) |
| Watch confused with hot reload | `scuzz watch` rebuilds. `[ui]` `run --watch` is hot reload (stamp-reload Views). IO-only `run --watch` kills and reruns |
| IDE typer ≠ batch typer | One JSON schema. LSP wraps `scuzz check` |
| Skia weight | pinned CPU prebuilt default. `sk_sw` opt-out |
| Desktop-only features | Headless peer rule |
| Treating UI as the only product | UI is a primary path (Flutter-shaped). CLI/server/desktop/mobile are peers |
| GC vs frame budget | `pump` boundary. Measure |
| Mobile packaging | Host Mobile peer first. Device toolchains later |
