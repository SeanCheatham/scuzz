# Scuzz Lang vision

Scuzz Lang is a Flutter-shaped product with a Scala-inspired language. It is not a Scala 3, Scala Native, or Maven citizen.

One doc for product intent, design locks, language direction, and open work. Keep/cut tables: [`compatibility.md`](compatibility.md). Manifest schema: [`schemas/scuzz-toml.md`](schemas/scuzz-toml.md). App path and surface catalogs: [`guide.md`](guide.md). Future empirical pre-optimization: [`optimization.md`](optimization.md).

Edit this file when a decision or next-step order changes.

## Thesis

- **Language**: purposeful Scala-inspired subset for native CLI, server, desktop, and mobile apps, with **built-in effect/IO/Streaming** (ZIO-inspired, not a ZIO or cats/fs2 port). Dense `for` dialect (`for` as primary binder). Token-efficient for agents. Functional by default. Keep the dialect practical. See [Language direction](#language-direction).
- **Runtime**: custom native (LLVM). Native binaries, not a VM. No JVM. No Java interop. No classpath/Maven. Web is not a current target.
- **UI**: a primary product path, not the only one (Flutter-shaped: GUI is first-class; so are CLI and server). One design language + Skia, as a **`Ui` effect** with Headless/Desktop/Mobile interpreters. Headless is a product runtime (agents, CI), not a test-only shim.
- **Batteries**: the language and standard kits cover common app cases. No ecosystem library sprawl. No Maven, cats, or ZIO ports.
- **Tooling**: one opinionated CLI (`scuzz`) — compile, link, assets, watch, packaging, format, check, and the whole verification stack. One formatter (`scuzz fmt`). One linter (`scuzz check`: format-verify + typecheck; further lints emit here; no `lint` subcommand). One testing strategy. Mutation, fuzzing, property/laws, simulation, and determinism are **first-class in the language and `scuzz`**. They are not a third-party harness. Compiler, CLI, and toolchain are **Rust** (`crates/compiler`, `crates/cli`).
- **Language proof**: examples that exercise the surface (`examples/`), not a self-hosted compiler.
- **AI-Friendly**: Headless, hot reload, and debugging tools aid agents. Headless is a peer runtime. `scuzz watch` only rebuilds. `[ui] run --watch` is hot reload: it stamp-reloads Views, writes `build/debug.dump` (including `[taps]` frames / `[fields]` live strings / `[scrolls]` / `[last_hit]` after a TAP, and live `View.bindText`), and plays `build/inject.script`. Desktop/Mobile `scuzz run` records live OS input to `build/record.script` and writes `build/debug.dump`. Replay Headless with `scuzz run --headless --script build/record.script --dump build/debug.dump`. `[ui]` build emits `build/reload.dylib`. Stamp-watch `dlopen`s it so a source View-label change appears live (Signals stay). IO-only `run --watch` kills and reruns on source change. `scuzz lsp` wraps `scuzz check` JSON diagnostics. Hover, completion, definition, document symbols, and references use the same parse.

Upstream Scala Native is a *reference*, not a dependency. Divergence is intentional.

## Defaults

| Topic | Choice |
| --- | --- |
| UI testing / CI | In-source laws + composed drivers through `scuzz fuzz` (primary); mutation as law-strength gate; structural goldens as regression face; PNG optional through `--pixels` |
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

**v1** — Shipped `scuzz` is the Rust CLI (GitHub Releases; `package_release.sh` / `install.sh`). Kernel surface is proven by examples. `fuzz` / `mutate` live on that CLI.

## Decisions

### Product name

Brand in prose: **Scuzz Lang** (short form **Scuzz**). CLI / cargo package `scuzz`; compiler crate `scuzz-compiler`; manifest `scuzz.toml`; sources `*.scuzz` (plus stem-paired `*.scuzz_sim` / `*.scuzz_drivers`); C ABI `sz_` / `Sz*` / `SZ_*`. No dual names or legacy aliases.

### Tooling

One CLI. One typer. One formatter. One linter. One testing strategy. No second analyze frontend. No `*.g.scuzz` codegen. No `src/test` runner. No bolted-on mutation/fuzz/property ecosystems.

- **Watch** rebuilds when sources or `scuzz.toml` change. It does not patch running machine code. `[ui]` `run --watch` is hot reload: it recompiles `build/reload.dylib`, stamps, and swaps the View tree without resetting Signals (see [`guide.md`](guide.md)). IO-only `run --watch` kills and reruns the process on source change.
- **Static hygiene** is `scuzz check` (the linter). `scuzz fmt` rewrites. No `lint` subcommand.
- **Verification** is built into `scuzz` and the language (laws, sim overlays, deterministic TestRuntime, fuzz search, mutation). Not optional crates or Maven/npm test plugins.
- **JSON diagnostics** (`scuzz check --message-format=json`) are the editor protocol. `scuzz lsp` wraps that, overlays open buffers, and serves hover, completion, definition, document symbols, and references from the same parse. Do not grow a second typer or schema.
- **`scuzz.toml` is data** — package, path deps, `[ui]`. No plugin DSL. No `build.scuzz` hooks. Unknown keys rejected. Do not add `[plugins]`.
- **Fingerprint** (incremental): miss → rebuild. The fingerprint includes compiler/runtime identity, native sources, target, clang version, Skia backend, and verify mode. No `scuzz clean` ritual.
- **Missing tools:** fail on the first missing tool with one install line. No `flutter doctor` mega-checklist.
- **`scuzz package`:** Android runs `crates/embedder-mobile/shells/android/build_ndk.sh` and emits `libscuzz.so` (needs the NDK). iOS runs `crates/embedder-mobile/shells/ios/build_sim.sh` and emits a signed simulator `.app`. Not a Gradle/CocoaPods API.

### GC (v0)

libc `malloc`/`free` through `sz_alloc` / `sz_free`. No collector. Heap strings, list cells, ADTs, boxed i64, map/set nodes, IO nodes, stream nodes, Resource nodes, errors, Ref, Queue, Deferred, Either, and pair are reference-counted (`sz_retain` / `sz_release`). The compiler emits release on owned string temps after concat, slice, trim, `Str.contains` / `Str.endsWith` / `Str.toInt` / `Str.replace`, `IO.println`, `Fs.read` / `Fs.list` / `Fs.mkdirs` / `Fs.canonicalize` / `Fs.write`, `Sys.write` / `Sys.exec` / `Sys.spawn` / `Sys.getenv`, and `Net.httpGet`, and retain when a function returns a borrowed `String`. It emits release on owned List temps after last use (`List.len` / `cons` / `append` / `map` / `filter` / `setAt` / `take` / `drop` / `find` / `exists` / `takeWhile` / `dropWhile` / `forall` and list literals), including the `map` / `filter` / `find` / `exists` / `takeWhile` / `dropWhile` / `forall` closure pack, and retain when a function returns a borrowed `List`. `List.append` retains the element like `cons`. The caller drops an owned element after the call. A `List.map` / `Stream.map` mapper returns an owned pointer. A borrowed body retains before return. The runtime takes that mapper ref. `Stream.emits` / `Stream.emit` retain payloads and drop an owned list or value. `Stream.eval` retains the IO and drops the caller ref after the call. Stream nodes are reference-counted. Combinators retain the inner stream and drop an owned input after the call. `Stream.compileToList` / `Stream.drain` / `Stream.exists` retain the stream for the IO and drop an owned input after the call. `Signal.list` retains the list and drops an owned input after the call. `Signal.setList` retains the new list and drops an owned input after the call. `Signal.str` copies the bytes and drops an owned string after the call. `Signal.setStr` copies the bytes and drops an owned string after the call. View constructors that copy a string drop the owned input after the call (`View.text`, `View.button`, `View.iconButton`, `View.fab`, `View.outlinedButton`, `View.textButton`, `View.actionChip`, `View.chip`, `View.filterChip`, `View.inputChip`, `View.choiceChip`, `View.checkbox`, `View.radio`, `View.switch`, `View.avatar`, `View.listTile`, `View.checkboxListTile`, `View.switchListTile`, `View.radioListTile`, `View.segmented`, `View.tooltip`, `View.semantics`, `View.mergeSemantics`, `View.inkWell`, `View.expansionTile`, `View.textField` placeholder, `View.image` caption). Tap constructors unpack `cons(fn, cons(env, nil))` and drop the owned pack after the call (`View.button`, `View.iconButton`, `View.fab`, `View.outlinedButton`, `View.textButton`, `View.actionChip`, `View.inkWell`). They retain the env list. `sz_view_free` releases it. `View.each` mappers, `Signal.map`, and `Ui.run` rebuild packs drop the same way after unpack. They retain the env. The View, mapped Signal, or session releases it. `Stream.filter` / `Stream.map` / `Stream.evalMap`, `Resource.make` / `Resource.use`, and `Net.serve` / `Net.serveOnce` drop the callback pack after unpack. Stream combinators, `Resource`, and `Net.serve` retain the env. The resource, use IO, stream, or server releases it. `Resource.make` retains the acquire IO and drops an owned acquire after the call. It marks the result owned. `Resource.use` retains the resource and the acquire IO. The resource keeps acquire after use constructs or runs. After release, last-use drops the acquired payload. It drops an owned resource after the call. It keeps the resource and use env in a pair until start. The live use pack is RC so HANDLE last-use drops the pack. Resource free releases acquire. It emits release on owned Map / Set temps after last use (`Map.set` / `Set.add` / `contains` / `Map.getOrElse`, including the default, `Map.remove` / `Set.remove` / `Map.keys` / `Set.toList` / `Map.size` / `Set.size`), and retain when a function returns a borrowed `Map` / `Set`. It emits release on owned ADT temps after last use (construct + match), and retain when a function returns a borrowed ADT. It tracks owned `let` / `for` binders and releases them after the body when the body is a scalar, `IO`, a fresh owned ptr, or an `if` / match phi (retain the result, then drop the binder). A discarded `_ <-` with an owned payload releases `%value` after the body. It marks an `if` / match phi owned when every arm produces an owned ptr, so a later last-use drops the taken arm. A mixed owned/borrowed `if` retains the borrowed arm at the join and marks the phi owned. A mixed owned/borrowed match retains each borrowed arm at its join and marks the phi owned. List cells retain heads and shared tails. Map/Set trees share subtrees. IO constructors take child IO nodes. `IO.forever` / `IO.repeatN` / `IO.retryN` / `Fiber.fork` / `IO.timeout` / `handleErrorWith` / `flatMap` / `IO.attempt` retain inner and drop the caller ref after the call. The compiler retains a `flatMap` / `handleErrorWith` capture list and drops the pack after the call. The runtime retains that env and drops it after the continuation. `handleErrorWith` marks the `sz_error_message` binder owned and drops it after the handler body. `sz_error_message` retains the error's message and returns it. A null error yields a fresh string. `sz_either_left` retains the error and drops the caller ref after the call. `sz_either_right` retains the value and drops the caller ref after the call. Either and pair cells are reference-counted. `IO.attempt` / `IO.both` mark the payload owned so last-use `sz_release` drops the cell. `sz_either_free` / `sz_pair_free` are `sz_release`. Last `sz_release` of an Either releases the Right value or the Left error, then frees the cell. Last `sz_release` of a pair releases both fields, then frees the cell. `sz_pair_new` retains both sides and drops the caller refs after the call. `sz_ref_make` retains the initial value and drops the caller ref after the call. Ref, Queue, and Deferred cells are reference-counted. `Ref.of` / `Queue.unbounded` / `Deferred.empty` mark the handle owned so last-use `sz_release` drops the cell. `sz_ref_free` / `sz_queue_free` / `sz_deferred_free` are `sz_release`. `sz_ref_set` retains the new value and drops the caller ref after the call. It releases the previous value when the set runs. `sz_queue_offer` retains the value and drops the caller ref after the call. `Queue.offer` / `Deferred.complete` / `Ref.set` / `Fs.write` / `Net.httpGet` keep the payload in a pair. Last-use of the delay node drops leftover retains. `Net.httpGet` keeps the URL in a pair until dispatch. The live get pack is RC so HANDLE last-use drops the pack. `Sys.exec` keeps the command in a pair until start. The live exec pack is RC so HANDLE last-use drops the pack. `Net.serve` keeps the handler env and the serve spec in a pair until start. The live serve pack is RC so HANDLE last-use drops the pack. Last `sz_release` of a Queue releases leftover items, then frees the cell. `Queue.take` transfers that offer retain. Do not retain again in take. The compiler marks the take payload owned and drops the binder after last use. `sz_deferred_complete` retains the value and drops the caller ref after the call. Last `sz_release` of a Deferred releases a completed value (and a failed error), then frees the cell. `sz_ref_get` retains the current value so the run result does not alias the Ref slot. The compiler drops an owned get binder after the body. `sz_deferred_get` retains the completed value so the run result does not alias the Deferred slot. The compiler drops an owned get binder after the body. `Fiber.join` retains the completed value so the run result does not alias the fiber slot. The compiler marks the join payload owned and drops the binder after last use. Fiber free releases leftover `result_value` and `result_error`. `IO.race` / `IO.both` / `IO.timeout` retain child results the same way. They copy a child error instead of taking it. `IO.pure` retains the payload and drops an owned payload after the call. The compiler marks a pointer payload owned and drops the binder after last use. Run retains so last-use does not free the IO slot. Last `sz_release` of the IO node drops a leftover payload. `IO.forever` / `IO.repeatN` drop a discarded iteration value before the next run. `IO.fail` retains the error and drops an owned error after the call. Run retains so `fiber_fail` does not free the IO slot. Last `sz_release` of the IO node drops a leftover error. `IO.retryN` drops a discarded iteration error before the next run. `sz_io_delay` retains a RC env and drops an owned env after the call. Run steals the env before the thunk so the thunk owns it. Last `sz_release` of the delay node drops leftover RC env or a leftover `sz_alloc` env. It does not load an RC header from a string literal or a small integer env. `Sys.getenv` keeps the key in a pair until run. Last-use of unused getenv drops leftover key retains. `Sys.alive` / `Sys.kill` keep the pid in an RC box as delay env. Last-use of unused alive/kill drops the pack. `Sys.spawn` keeps the command in a pair until start. Last-use of unused spawn drops leftover command retains. `Sys.write` keeps the payload in a pair until dispatch. Last-use of unused write drops leftover string retains. `Sys.read` keeps n in an RC box as flatMap env. Last-use of unused read drops the pack. `Random.nextInt` keeps the bound in an RC box as delay env. Last-use of unused nextInt drops the pack. `Fs.read` / `Fs.list` / `Fs.mkdirs` / `Fs.canonicalize` keep the path in a pair until dispatch. Last-use of unused read/list/mkdirs/canonicalize drops leftover path retains. TestRuntime `Sys.read` keeps n in an RC box as delay env. Last-use of unused TestRuntime read drops the pack. `Ui.run` keeps the rebuild env in a pair until start. Last-use of unused run drops leftover env retains. `IO.ensure` / `IO.race` / `IO.both` retain both child IO nodes and drop the caller refs after the call. `unsafe_run` takes the root result (it nulls the slot) and drops leftover fiber results when it frees the fibers. Panic may leak. Clear ownership still frees `Resource` / Views. Values without a last-use stay allocated. Immutable data forms no cycles, so no cycle collector is needed.

### Skia

No vendored Skia tree. Thin `sk_capi` (measure + draw). **Default UI backend** is the pinned Skia CPU prebuilt (`third_party/skia/PIN` → `scripts/fetch_skia.sh`). App link uses host zlib/bz2, not brotli. In-tree `sk_sw` is the explicit opt-out (`SCUZZ_SKIA=sk_sw`). `SCUZZ_SKIA=gpu` paints with `sk_sw` and presents through OpenGL (must not change `Ui` session or logical goldens). Impeller / Skia GPU raster stay deferred. Callers depend only on `sk_capi.h`.

### IO and impurity

One failure channel: `SzError` on `IO`. Blessed kits only. No app-level `IO.delay`. Cooperative single-threaded fibers (no OS threads for IO). `sleep` / empty `Queue.take` / incomplete `Deferred.get` / fd poll park. Cancel (race loser / `IO.timeout` / `Fiber.interrupt`) runs `IO.ensure` / `Resource` finalizers. `Fiber.fork` starts a supervised child (`join` parks; unjoined children cancel when the root completes). `IO.timeout(ms, inner)` is a blessed race of sleep-fail vs inner and keeps inner's `IO[T]`. `IO.forever(inner)` reruns until failure or cancel (`IO[Unit]`, never succeeds). `IO.repeatN(n, inner)` runs inner once plus `n` extra times (last success). `IO.retryN(n, inner)` retries on failure up to `n` extra times. TestRuntime (`SCUZZ_TESTRT=1`) fakes clock/random/FS/net/console. Simulation is hermetic: TestRuntime does not open live sockets; `Net.httpGet` uses stubs; `Net.serve` uses injected paths; a missing stub fails; `Sys.exec` and `Sys.spawn` fail; `Sys.getenv` reads a sealed map; `Sys.alive` / `Sys.kill` use a fake process table. Live `Net.serve` binds localhost (`127.0.0.1` and `::1`). Live `Net.httpGet` may leave the host. Surface catalogs: [`guide.md`](guide.md). Panics abort through `sz_panic`.

### `Ui` vs `View`

| Layer | Role | Purity |
| --- | --- | --- |
| **`View`** | Widget tree | Sync/pure `build` |
| **`Ui` / `UiSession`** | `mount` / `pump` / `inject` / `snapshot` | Effectful (`UiRuntime`) |

Headless is a **peer** of Desktop/Mobile — product runtime for agents and CI. Frame boundary is `pump`. World effects stay blessed `IO`. Bridge into signals through `sz_ui_bridge_post_*`. No UI feature without a Headless path. Prefer `Signal.list` + `View.each`. Derived display through `Signal.map` + `View.bindText`. Nested declarative construction only. `Ui.run(_ => view)` is the session. The factory re-runs on stamp-watch. Create Signals outside the factory so they stay. Live Desktop/Mobile drain appends to `build/record.script` (`tap N` / `xy x y` / `type` / `backspace`). Headless replays that file through `SCUZZ_UI_SCRIPT` (`--script`). Dump `[taps]` includes frames; `[last_hit]` shows the last TAP target or `NULL`.

### IO apps vs Headless

| Path | Meaning |
| --- | --- |
| **Headless** | `UiRuntime` peer — still `View` / Skia / structural dumps |
| **IO-only** | No `[ui]`, no `Ui.run`; plain `@main: IO[Unit]` exec |

Missing `[ui]` ⇒ Skia omitted from the link. `scuzz test` is TESTRT exit-0 smoke (not a11y goldens). IO-only is **not** a fourth runtime peer.

### Kernel dialect

The language `scuzz` implements. Proof is examples that exercise each construct (`examples/hello`, `kernel`, `io`, `counter`, `studio`).

Locks (not an API catalog — see [`guide.md`](guide.md)):

- Expression dialect only: `for` primary binder (`=` pure, `<-` effect); no `val` / statement blocks / `var`
- Optional `package`; top-level `def` / `private def` / `import`; `@main def …: IO[Unit]`
- Payload enums + `record` sugar (methods on generic records and enums) + thin traits/`impl` (static dispatch, including `trait Get[T]` / `impl Get[Int] for Point` / `impl Get[T] for Opt`) + monomorphized generics on defs/enums/records
- File-stem modules; enums namespaced by stem; `import Module.name` for bare disambiguation
- Types: `Unit`, `Int`, `Float`, `String`, `Bool`, `List[T]`, `Map[K, V]`, `Set[T]`, `IO[T]`, `A => B`, blessed handles (`Fiber[A]`, `Ref[A]`, `Queue[A]`, `Deferred[A]`, `Resource[A]`, `Stream[A]`), nominal enums. `true` / `false` are `Bool`. `Bool` is not `Int`. `attempt` is `IO[Result[A]]`. `handleErrorWith` binds a `String` error message.
- Blessed kits + `Signal` / `View` / `Ui` / `Law.*` / `.require` as documented in the guide. Kit lambdas bind the list element type (`List.filter` / `List.map` / `List.find` / `List.exists` / `List.takeWhile` / `List.dropWhile` / `List.forall`), `String` (`View.each` / `Stream.*` / `Resource` / `Net.serve`), or `Int` (`Signal.map`). The lambda body must return the kit result (`View`, `Bool`, `String`, `T` for `List.map`, or `IO`)
- No macros, no implicits, no HKT beyond `IO`, no null

## Language direction

Expression-only, effect-sequenced dialect. Dense. Deterministic. Verification-friendly without becoming a proof assistant. **`for` is the kernel binder**. No `val`/statement blocks.

### Expr dialect

- **`for` as primary binder**: `x = e` (pure alias), `x <- e` (effect)
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
- **Mutation pressures the oracles.** Default `scuzz mutate` mutates live code and requires residual oracles to kill the mutant. `--oracles` mutates residual predicates. Surviving mutants mean weak, unreached, or missing laws/refinements.

| Phase | Role |
| --- | --- |
| `scuzz check` | Format-verify `src/`; typecheck live + laws/refinements + sim + drivers; laws pure, drivers `IO`, no oracles in drivers; every nullary `law` must appear in a `.require`; parameterized laws stay generator-friendly; sim bindings match live types/purity. Reports every parse and type diagnostic in the run |
| `scuzz build` (verify graph) | Layer `*.scuzz_sim` over live defs; compile drivers; residualize `.require` / `where` checks (armed under TestRuntime / fuzz / mutation only); publish parameterized laws as drive targets |
| `scuzz fuzz` | Compose drivers + parameterized laws + event scripts / IO schedules. Shrink a failing prefix before `repro.toml`. `[ui]` kept prefixes run twice; dump mismatch fails. Per-run oracles: `.require` residuals, refinements, panic/`SzError`. Per-campaign oracle: `Law.sometimes` reachability → `repro.toml` |
| `scuzz mutate` | Mutate live `def` bodies (flip ops, swap `if` arms, `0`↔`1`, sibling constructors); keep residual oracles armed. `--oracles` mutates residual `Law.check` / `Law.assert` / `.require` predicates. Idle probe plus `--iters` fuzz; surviving mutants mean weak or unreached oracles |
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
- Fuzz / mutation / test layers sim, then arms oracles. Rename drift fails `check`.
- No free-floating `tests/` package roots. No third-party test or mutation frameworks.
- Prefer swapping values you own through `*.scuzz_sim`. Blessed kits stay one implementation + TestRuntime fakes. Do not stub pure helpers, `View` builders, or `Signal` cells. TestRuntime does not open live sockets.
- Driver params stay generator-friendly (`Int` / `String` / `Bool`). Drivers may call live defs but never assert correctness.
- Observation surface: signal store + View/a11y dump — not Skia pixels. Kernel/runtime keep ordinary example tests. This strategy is for **Scuzz apps**.

### Verification posture

Keep purity checkable (pure `A` vs `IO` vs session). Total expr core. Signals as an explicit store. Immutable data by default. Errors as values. Refinements attach to **positions** (`def` params, `record` fields), not a refined-type algebra. Checked dynamically at call / construction under the verify graph. Erased live. `.require(pred)` attaches an oracle to a **value** (or `IO[A]` sequenced after the effect) while preserving the receiver type — even when `pred` is `IO[Bool]`. They are the fragment that migrates to static discharge later with no author rewrite. Defer dependent types and runtime-heap proofs. Residual oracles + fuzz + mutation first. Static proof only where cheap.

### `scuzz fuzz`

Deterministic TestRuntime + (for `[ui]`) Headless event scripts (plus sim overlays when present). The fuzz alphabet is the typed event surface (buttons, text fields) **plus declared drivers** (`drive <name> [args]` extends the script line protocol; the verify build publishes the driver table alongside the a11y dump) **plus parameterized laws** (same `drive` verb). Oracles: in-source **laws/refinements** first. Panic/`SzError` still fails. `Law.sometimes` reachability judges the campaign. Structural dumps aid diagnosis (PNG last). `repro.toml` records a **shrunk** event list + driver invocations, so replay is generator-independent. `[ui]` kept prefixes run twice; a dump mismatch fails the campaign. `tap N` / `scroll N` follow a11y preorder (`[taps]` / `[scrolls]`), not a pixel scan. `tap N` activates that target directly so a control below the viewport still fires. Headless fuzz stays stable across fonts. `pump` is time. No hidden nondeterminism. Determinism makes any failing prefix replayable. Seeded `--iters` keeps `[ui]` prefixes that hit new `Law.sometimes` names or a new Headless `dump.txt`, and IO-only schedule seeds that hit new sometimes names, then extends/perturbs them (CLI-only; no runtime machinery). Flags, script verbs, and schedule seeds: [`guide.md`](guide.md).

### Layout model

**Flutter-style constraints** (constraints down, sizes up). Tight slots: `sized`, `aspectRatio`, percent axes on `fraction`, `expanded` flex, and opt-in `stretch` (cross axis). Scroll content is unbounded on the pan axis (`max` 0). Column/row do not stretch non-flex children unless wrapped in `View.stretch`. Device-pixel paint multiplies author px by the backing scale so taps match the pixels. Nested constructors only. Do not drift into CSS-ish ad-hoc rules. Do not grow Flutter-style constraint-overflow dumps. Diagnose through structural dumps + laws. Widget catalog: [`guide.md`](guide.md).

### UI testing

**In-source laws + composed drivers through `scuzz fuzz` + built-in mutation** are primary for `[ui]` apps. **Structural goldens** (Headless signal + a11y dumps) are a regression face — few, live graph, not a substitute for laws. PNG optional (`scuzz test --pixels`). IO packages: laws + drivers + sim under TestRuntime when present. Otherwise compile + TESTRT exit-0 smoke.

## Open work

Unknowns and known gaps: [`gaps.md`](gaps.md). Next slices: [`plans.md`](plans.md). `scuzz package --target ios` and `--target android` drive the platform shells. `SCUZZ_SKIA=gpu` presents through OpenGL. `Sys.getenv` is sealed under TestRuntime. `Sys.alive` / `Sys.kill` use a fake process table. Device packaging and Impeller / Skia GPU raster stay deferred.

App authors: [`guide.md`](guide.md). Vertical slices over breadth. No Desktop-only UI features. UI is a primary path among CLI/server/desktop/mobile. It is not the only v0 bar. Web is not a current target. `scuzz package --target ios` builds a signed simulator `.app` when Xcode is present. The iOS shell feeds typed text into TextField. `scuzz package --target android` packs a debug APK when the NDK and SDK are present. The Android shell blits frames onto a SurfaceView and feeds taps and typed text into the pump. Device builds stay open.

## Risks

| Risk | Mitigation |
| --- | --- |
| Language + UI + tooling is huge | Small subset. Vertical slices. Counter before generality |
| Dialect unexercised by apps | Kernel examples that stress each construct. `check` / `test` / `fuzz` on `examples/` |
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
