# Scuzz Lang vision

Scuzz Lang is a Flutter-shaped product with a Scala-inspired language. It is not a Scala 3, Scala Native, or Maven citizen.

One doc for product intent, design locks, language direction, and open work. Keep/cut tables: [`compatibility.md`](compatibility.md). Manifest schema: run `scuzz docs manifest`. App path: run `scuzz docs start`. Kit signatures: run `scuzz docs kits`. Checkout host setup: [`developer-environment.md`](developer-environment.md). Future empirical pre-optimization: [`optimization.md`](optimization.md).

Edit this file when a decision or next-step order changes.

## Thesis

- **Language**: purposeful Scala-inspired subset for native CLI, server, desktop, and mobile apps, with **built-in effect/IO/Streaming** (ZIO-inspired, not a ZIO or cats/fs2 port). Dense `for` dialect (`for` as primary binder). Token-efficient for agents. Functional by default. Keep the dialect practical. See [Language direction](#language-direction).
- **Runtime**: custom native (LLVM). Native binaries, not a VM. No JVM. No Java interop. No classpath/Maven. GUI apps also target WebAssembly. Scuzz Docs is the first browser app.
- **UI**: a primary product path, not the only one (Flutter-shaped: GUI is first-class; so are CLI and server). One design language + Skia, as a **`Ui` effect** with Headless/Desktop/Mobile interpreters. Headless is a product runtime (agents, CI), not a test-only shim.
- **Batteries**: the language and standard kits cover common app cases. No ecosystem library sprawl. No Maven, cats, or ZIO ports.
- **Tooling**: one opinionated CLI (`scuzz`) — compile, link, assets, watch, packaging, format, check, and the whole verification stack. One formatter (`scuzz fmt`). One linter (`scuzz check`: format-verify + typecheck; further lints emit here; no `lint` subcommand). One testing strategy. Mutation, fuzzing, properties, simulation, and determinism are **first-class in the language and `scuzz`**. They are not a third-party harness. Compiler, CLI, and toolchain are Scuzz (`examples/compiler`, `examples/cli`). A rebuild uses the newest GitHub `v*` bootstrap ([Self-hosting](#self-hosting)). `scuzz ide` launches a Scuzz `[ui]` app. That app is the dogfood IDE, not the compiler.
- **Language proof**: examples that exercise the surface (`examples/`). The dogfood IDE is an app on that same surface. Self-hosted tooling is that proof: the shipped CLI is Scuzz ([Self-hosting](#self-hosting)).
- **AI-Friendly**: Headless, hot reload, and debugging tools aid agents. Headless is a peer runtime. `scuzz watch` only rebuilds. `[ui] run --watch` is hot reload: it stamp-reloads Views, writes `build/debug.dump`, and plays `build/inject.script`. Desktop/Mobile `scuzz run` exports `SCUZZ_UI_RECORD` and `SCUZZ_UI_DEBUG_DUMP`. The session records live OS input to `build/record.script` and writes `build/debug.dump`. Replay Headless with `scuzz run --headless --script build/record.script --dump build/debug.dump`. Stamp-watch keeps Signals. IO-only `run --watch` kills and reruns on source change. `scuzz lsp` wraps `scuzz check` JSON diagnostics. Panic, goto-def, and rename must use Scuzz source spans. Dump, inject, fuzz verdict, and coverage become one typed session schema. The dump surface landed: a dump path that ends in `.json` writes schema `v=1` with the same sections as the text dump. List/value signal payloads and a11y lines stay opaque strings in `v=1`. Inject, fuzz verdict, and coverage stay text. Text dump and script stay until that schema ships. Dump and script verbs: run `scuzz docs commands`.

Upstream Scala Native is a *reference*, not a dependency. Divergence is intentional.

## Defaults

| Topic | Choice |
| --- | --- |
| UI testing / CI | `*.scuzz_verify` + in-body `.require` + composed drivers through `scuzz fuzz --iterations` (primary; mutation is a phase). Authors write named Timeline claims. Universal oracles probe the live graph with no scenario wrap. `--differential` compares live dumps across Skia backends |
| Static hygiene | One linter: `scuzz check` (format-verify + typecheck; lints on this command; no `lint` subcommand). One formatter: `scuzz fmt` rewrites |
| Codegen | LLVM IR. Emit reads types from the checker. It does not guess from SSA names |
| Renderer (v0) | Skia through thin C ABI; Impeller deferred |
| Build tool | DIY Mill/Cargo-like: `scuzz` (not sbt/Maven) |
| Effects | Language + runtime builtins |
| Impurity | All nondeterminism / external I/O through blessed `IO`; no app-level `IO.delay` escape hatch. Simulation is hermetic (TestRuntime fakes; no live sockets; `Sys.exec` / `Sys.spawn` fail; `Sys.getenv` sealed except `SCUZZ_SERVE` / `SCUZZ_KIT`; `Sys.alive` / `Sys.kill` fake) |
| Tests | One built-in strategy: **mutation + fuzz + properties + sim + determinism** (TestRuntime). Claims are pure predicates over recorded **timelines**, judged at the terminal point of each run. The author surface names Signals and controls. Dump slot ids stay behind the algebra. Claims live in `*.scuzz_verify` (`Timeline => Verdict` session claims and `Bool` drive oracles) and in live bodies (`.require`, `where`, `Property.sometimes`). **Drivers** are oracle-free workloads the fuzzer composes. No classical unit-test culture. No external test frameworks |
| Modules | `scuzz.toml` package = crate; `Foo.scuzz` = module (not JVM packages) |
| Toolchain | Scuzz (`examples/cli`); one compiler. Product version lives in `VERSION`. Bootstrap fetches the newest GitHub `v*` release ([Self-hosting](#self-hosting)) |
| UI model | Pure `View` + effectful `Ui` session (`mount` / `pump` / `inject` / `snapshot`) |

## What Scuzz Lang is not

- Not Scala 3, not the JVM, not Scala.js, not a JavaScript language backend
- Not a ZIO library port
- Not a cats / cats-effect / Typelevel port
- Not SwiftUI / UIKit / WinUI wrappers
- Not “every widget rebuild is an `IO`” (`View` build stays sync/pure)
- Not imperative View trees (`View.addChild`); nested constructors only
- Not classical / example-based unit-test culture (`src/test`, Mockito, assert-equal fixtures, third-party test runners) for apps. Use **mutation + fuzz + properties + sim + determinism**, all in `scuzz`, instead. Examples survive as oracle-free **drivers** and as concrete-fact `.require` checks. The objection is fixture-diff suites as the primary strategy, not examples-as-workloads and not concrete facts stated as oracles
- Not Flutter DevTools / VM patching. `[ui] run --watch` is in-process hot reload (stamp-reload Views) with a live structural dump and stamp-driven inject. `scuzz watch` only rebuilds. IO-only `run --watch` kills and reruns.
- Not an sbt / Gradle / `pubspec` plugin DSL (`scuzz.toml` is data)
- Not Flutter platform channels
- Not a big-bang self-hosting rewrite. Self-hosting landed in staged slices. The newest GitHub `v*` bootstrap compiles the Scuzz toolchain. The product CLI is the Scuzz-emitted binary. No dual shipped product CLIs. The tagged bootstrap `scuzz` is not a second product CLI.
- Not a second IDE typer. External editors speak `scuzz lsp`. The dogfood IDE is a Scuzz `[ui]` app that consumes `scuzz check` JSON and `scuzz lsp`. It does not grow a parallel analyze frontend.

## Success bars

**v0** — Install CLI (`curl …/install.sh | sh`, or checkout `./scripts/install.sh`) → `scuzz new` (IO) or `scuzz new --ui` (Counter as `View` + builtin `IO`) → `scuzz fuzz --iterations 0`, and `scuzz run` (`--headless` for UI). Desktop when available. Language `Resource` / `Stream` / `Net.serve` ship (`examples/io`).

**v1** — Shipped `scuzz` is the Scuzz CLI (GitHub Releases; `package_release.sh` / `install.sh`). Cut a release with the GitHub `release` workflow. Kernel surface is proven by examples. `fuzz` lives on that CLI.

## Decisions

### Product name

Brand in prose: **Scuzz Lang** (short form **Scuzz**). CLI `scuzz`; compiler package `examples/compiler`; manifest `scuzz.toml`; sources `*.scuzz` (plus `*.scuzz_scenario` and `*.scuzz_verify`); C ABI `sz_` / `Sz*` / `SZ_*`. No dual names or legacy aliases.

### Tooling

One CLI. One typer. One formatter. One linter. One testing strategy. No second analyze frontend. No `*.g.scuzz` codegen. No `src/test` runner. No bolted-on mutation/fuzz/property ecosystems.

- **Watch** rebuilds when sources or `scuzz.toml` change. It does not patch running machine code. `[ui]` `run --watch` is hot reload: it recompiles `build/reload.dylib`, stamps, and swaps the View tree without resetting Signals (see `scuzz docs gui`). IO-only `run --watch` kills and reruns the process on source change.
- **Static hygiene** is `scuzz check` (the linter). `scuzz fmt` rewrites. No `lint` subcommand.
- **Verification** is built into `scuzz` and the language (properties, scenario worlds, deterministic TestRuntime, fuzz search, mutation). Not optional crates or Maven/npm test plugins. A search failure fails `scuzz fuzz`. A mutation survivor does not. A drive-script probe does not run `@main`.
- **JSON diagnostics** (`scuzz check --message-format=json`) are the editor protocol. `check` reports the real file stem, line, and column from recorded token offsets. `scuzz lsp` wraps that, overlays open buffers, and serves language-server methods from the same parse. Overlay presence is a list entry. An open empty file stays empty. didChange reads full-sync `contentChanges`. Rename returns a WorkspaceEdit for the document URI. The server advertises only methods it implements (full sync, hover, completion, definition, formatting, rename without prepare, semantic tokens full). Diagnostics run the check file list with overlays through `Check.checkFilesOwn`. Goto-def, rename, hover, and completion use that parse. They must land on the right Scuzz span. Unclaimed def, signal, and control reports are info. They do not fail `check`. Do not grow a second typer or schema. A typed agent session schema is later ([`gaps.md`](gaps.md)). `--message-format=json` stays `check` until that schema ships.
- **Dogfood IDE.** A Scuzz `[ui]` package is the in-tree IDE. `scuzz ide` on the one CLI launches that package with Desktop. Headless stays a peer (`scuzz ide --headless`). The app talks to `scuzz check` / `scuzz lsp` / `scuzz fmt` / `scuzz run` / `scuzz fuzz`. It does not reimplement them. `scuzz lsp` stays the protocol for external editors. Do not add Desktop-only editor behavior. Do not ship a second `scuzz-ide` binary.
- **`scuzz.toml` is data** — package, path deps, `[ui]`, optional `[fuzz].score_floor`. No plugin DSL. No `build.scuzz` hooks. Unknown keys rejected. Do not add `[plugins]`. `scuzz run` and `scuzz ide` export `SCUZZ_UI_RUNTIME` from `[ui].default_runtime`. `--headless` forces Headless. A raw binary with that env unset stays Headless.
- **Docs.** The README introduces Scuzz with plain language, a complete example, and one install-to-run path. It states current platform limits and links to the manual and checkout setup. `scuzz docs` prints the technical manual from `examples/manual`. Kit rows come from `examples/compiler/src/Kits.scuzz`. LSP hover uses the same kit formatter. The Docs GUI app maps the same topics. There is no `guide.md` or generated `kits.md`.
- **Fingerprint** (incremental): miss → rebuild. A hit still rebuilds when the out-dir has no `.ll`. A hit with a `.ll` is silent. The fingerprint text is VERSION, `uname -s`, a `clang -v` one-liner, `sk_capi_backend`, the toml, and the listed sources. Live (`fingerprint`) and verify (`fingerprint.verify`) share `build/` artifacts. A compile writes its mode file and deletes the sibling so a later switch rebuilds. A fuzz campaign skips the verify emit and link when `fingerprint.verify` matches and the `build/` `.ll` and exe exist. The fuzz live graph builds into `build/live/` behind its own `fingerprint` file. One enum parse feeds the drivers table, seeds, and search of a campaign. `--iterations 0` does not walk mutation sites. Native `.a` mtimes are not in the stamp. No `scuzz clean` ritual.
- **Native make:** quiet on success. A failed `make` prints the log. Clang compiles `.ll` without C `-I`. Package link uses `-O0 -Wno-override-module` (IR has no host triple). `scripts/bootstrap.sh` clang-links the product CLI at `-O2` after tagged bootstrap emit. A cli-ok proof of `examples/cli` writes a separate out-dir so it does not replace that binary. Make and clang of runtime and UI crates use `SCUZZ_HOME` when that env is set. `scuzz run` uses the fingerprint. `scuzz build PATH` writes `PATH/build`. A fuzz probe that runs longer than 20 s is killed.
- **Missing tools:** fail on the first missing tool with one install line. No `flutter doctor` mega-checklist.
- **`scuzz package`:** `--target` is host, android, ios, web, or all. Web writes static browser files. `all` selects the native package targets. Default is host. An unknown target fails at parse. Android runs `crates/embedder-mobile/shells/android/build_ndk.sh` and emits `libscuzz.so` (needs the NDK). The same command installs the APK when `adb` lists a device. No device is not a failure. iOS runs `crates/embedder-mobile/shells/ios/build_sim.sh` and emits a signed simulator `.app`. When `SCUZZ_HOME` is set, the command exports `SCUZZ` to `$SCUZZ_HOME/bin/scuzz` before those shells. Shell tokens (bundle id, script path, project dir, copy dests) are quoted. Artifacts land in `build/package/`. A package that calls Net fails: mobile shells do not link OpenSSL. Not a Gradle/CocoaPods API.

### Self-hosting

The product CLI is Scuzz (`examples/cli`). `scripts/bootstrap.sh` fetches the newest GitHub `v*` release and compiles that CLI. Do not ship two toolchains. Product version lives in `VERSION`.

`examples/syntax` holds the lexer and parser. `examples/compiler` holds the typechecker, LLVM emit, `scuzz.toml` parse, compile pipeline, and clang argv. `examples/fmt`, `examples/tyck`, and `examples/codegen` prove the printer, checker, and emitter on the kernel subset (`fmt-ok` / `tyck-ok` / `ir-ok`). `scripts/fixedpoint-ll.sh` compares two self-compiles of `cli.ll` from the rebuilt compiler.

**Bootstrap.** `VERSION` names the product (`scuzz -V`). `scripts/bootstrap.sh` fetches the newest GitHub Release matching `v[0-9]*` and compiles the Scuzz compiler (`examples/cli`). Cut a release with the GitHub `release` workflow (`patch` / `minor` / `major` / `0.2.2`). It commits `VERSION`, tags, packages, and publishes. `package_release.sh` ships that binary. A later Scuzz `scuzz` compiles the next Scuzz. Users still install one `scuzz`. Toolchain sources (`examples/cli`, `examples/compiler`, `examples/syntax`) only call builtins that the newest `v*` bootstrap release already emits. A new builtin reaches toolchain sources one release after it lands.

### GC (v0)

libc `malloc`/`free` through `sz_alloc` / `sz_free`. No collector. Heap strings, list cells, ADTs, boxed i64, map/set nodes, IO nodes, stream nodes, Resource nodes, errors, Ref, Queue, Deferred, Either, pair, Builder, and Net sockets are reference-counted (`sz_retain` / `sz_release`). Live `[heap]` dumps count and bytes per kind (`raw` plus each RC kind) and `delta_bytes` / `delta_count` since the last live dump or `resetpeak`. Live `[live]` dumps remaining blocks (`kind rc=N bytes=M`, capped).

The compiler emits release on an owned temp after last use of a kit call, and retain when a function returns a borrowed pointer of that sort. Last-use covers Str, Builder, List, Map, Set, ADT, tuple, and the kits in `scuzz docs kits`. A user function call drops owned pointer arguments after the call. The callee borrows parameters. An owned pointer return is dropped at last use. A list match arm uses its checked result type to retain a borrowed pointer. JSON array and object patterns preserve their element types. The kernel `jsonPairLifetime` oracle checks repeated field reads from parsed objects. Closure and tap packs drop after unpack. The kit retains the env.

It tracks owned `let` / `for` binders and releases them after the body. A discarded `_` let drops an owned pointer or an unused IO without inserting a local. An `if` / match phi is owned when every arm produces an owned ptr. A mixed owned/borrowed join retains the borrowed arm.

IO constructors retain inner nodes and drop the caller ref after the call. Delay env is RC. Unique run steals env. Shared run leaves env on the node. Last `sz_release` of a node drops leftover RC payload. Panic prints remaining `[heap]` and `[live]` and writes `SCUZZ_PANIC_DUMP` or the registered `*.panic` file when that path is set. It then frees remaining live blocks and abort. Values without a last-use stay allocated until panic sweep or process exit. Immutable data forms no cycles, so no cycle collector is needed.

### Skia

No vendored Skia tree. Thin `sk_capi` (measure + draw). **Default UI backend** is the pinned Skia CPU prebuilt (`third_party/skia/PIN` → `scripts/fetch_skia.sh`). `make lib` compiles the in-tree shim into that archive so save/clip/restore and RGBA peek match `sk_capi.h`. App link uses host zlib/bz2, not brotli. In-tree `sk_sw` is the explicit opt-out (`SCUZZ_SKIA=sk_sw`). `SCUZZ_SKIA=gpu` paints with `sk_sw` and presents through OpenGL (must not change `Ui` session or live structural dumps). Impeller / Skia GPU raster stay deferred. Callers depend only on `sk_capi.h`.

### IO and impurity

One failure channel: `SzError` on `IO[T]`. The fail payload is a `String` message. Direction: typed `E` on `IO` without environment `R`. Do not add `ZIO[R, E, A]`. Blessed kits only. No app-level `IO.delay`. No user FFI. Expand kits for time, regex, and hash; ranked in [`gaps.md`](gaps.md). Cooperative single-threaded fibers are the scheduler for CLI, server, and UI (no OS threads for IO). Park on sleep, empty take, incomplete get, and fd poll. Cancel runs `IO.ensure` / `Resource` finalizers. `Fiber.fork` starts a supervised child. TestRuntime (`SCUZZ_TESTRT=1`) fakes clock, random, FS, net, and console. Simulation is hermetic: no live sockets; HTTP uses stubs and a loopback mailbox; `Sys.exec` / `Sys.spawn` fail; `Sys.getenv` is sealed except `SCUZZ_SERVE` / `SCUZZ_KIT`; `Sys.alive` / `Sys.kill` use a fake process table. Live `Net.serve` binds localhost. Live HTTP client kits take `http://` and `https://` with OpenSSL. Expand `Net` on this HTTP/1.0 stack. Do not expose POSIX sockets. Do not add a second HTTP client. TLS is for `https://` on this kit. Every new `Net` op keeps a TestRuntime fake. Surface catalogs: run `scuzz docs kits`. Panics abort through `sz_panic`. A panic must print a Scuzz file and line.

`Stream` is one finite pull interpreter. Constructors and transformers are `Stream[A]`. Bind a Stream with `=`. `<-` needs `IO`. Terminals (`compileToList` / `drain` / `head` / `last` / `count` / `fold` / `exists` / `forall` / `none`) are `IO`. `take` pulls until n outputs. `head` and `forall` / `none` / `exists` / `find` stop early. `range` / `iterate` / `unfold` allocate on pull. The unfold cap is 65536 pulled steps. Do not add backpressure, publishers, or a second stream kit.

### `Ui` vs `View`

| Layer | Role | Purity |
| --- | --- | --- |
| **`View`** | Widget tree | Sync/pure `build` |
| **`Ui` / `UiSession`** | `mount` / `pump` / `inject` / `snapshot` | Effectful (`UiRuntime`) |

Headless is a **peer** of Desktop/Mobile — product runtime for agents and CI. Frame boundary is `pump`. World effects stay blessed `IO`. No UI feature without a Headless path. Prefer `Signal` + `View.each`. Derived display through `Signal.map` + `View.bindText`. `Signal.make` creates a typed cell. `Signal.get` and `Signal.set` preserve its payload type. `Signal.map` maps between payload types. `View.each` uses `Signal[List[T]]` and binds `T`. Nested declarative construction only. `Ui.run(_ => view)` is the session. Create Signals outside the factory so they stay. Desktop/Mobile records live input to `build/record.script`. Headless replays through `--script`. Dump and script verbs: run `scuzz docs commands`.

### IO apps vs Headless

| Path | Meaning |
| --- | --- |
| **Headless** | `UiRuntime` peer — still `View` / Skia / structural dumps |
| **IO-only** | No `[ui]`, no `Ui.run`; plain `@main: IO[Unit]` exec |

Missing `[ui]` ⇒ Skia omitted from the link. `scuzz fuzz` probes the live `@main` under TestRuntime and requires exit 0. IO-only is **not** a fourth runtime peer.

### Kernel dialect

The language `scuzz` implements. Proof is examples that exercise each construct (`examples/hello`, `kernel`, `io`, `counter`, `studio`, `scale`).

Locks (not an API catalog — see `scuzz docs start` and `scuzz docs kits`):

- Expression dialect only: `for` primary binder (`=` pure, `<-` effect); no `val` / statement blocks / `var`. A `for` binder may unpack a tuple of 2 through 8 slots (`(a, b) = e`, `(a, b, c) = e`, `(a, b) <- e`) or a constructor, list, or as-pattern (`Point(x, y) = p`, `Opt.Some(n) <- e`, `h :: t = xs`). A miss panics. A `for` may include `if pred` (`pred` is Bool). A miss is `IO.fail`. The `for` needs a `<-` binder.
- Optional `package`; top-level `def` / `private def` / `import`; `@main def …: IO[Unit]`
- Payload enums + `record` sugar (methods on generic records and enums) + thin traits/`impl` (static dispatch, including `trait Get[T]` / `impl Get[Int] for Point` / `impl Get[T] for Opt`) + monomorphized generics on defs/enums/records. Match arms may use `case Pat if pred =>` (`pred` is Bool). A guarded arm does not cover the pattern for exhaustiveness. Match arms may use literal patterns (`case 0 =>`, `case "ok" =>`, `case true =>`). `true` and `false` together cover Bool. Int, Float, and String literals need `_` or a name bind. Match arms may use or-patterns (`case A | B =>`). Nested or in a payload works (`case Opt.Some(0 | 1)`). The body must typecheck for every alternative. Match arms may use an as-pattern (`case n @ Opt.Some(_)`). `n` binds the whole value. `n @ A | B` binds `n` for every alternative. Match arms may use list patterns (`case [] =>`, `case x :: xs =>`, `case [a, b] =>`). `[]` and `_ :: _` together cover `List`. Match arms may use a tuple pattern (2 through 8 slots) (`case (a, b)`). `(A, B, …)` is exhaustive. `e._1` … `e._N` project slots of any tuple expression. A `for` binder and a lambda may unpack the same way (`(a, b) = e`, `(a, b) <- e`, `(a, b) =>`, `Point(x, y) = p`, `Opt.Some(n) <- e`, `h :: t = xs`, `(Opt.Some(n)) =>`). Nested tuples are allowed. A miss panics. Match arms may use named field patterns (`case Point(x = n)`, `case Opt.Some(x = n)`). Omitted fields are `_`. Positionals come first. Match arms may use a bare constructor name that starts with an uppercase letter (`case None`, `case Some(n)`, `case Red | Blue`). The scrutinee type selects the case. A lowercase name still binds. A lambda may pin its parameter with `(x: T) =>`. The annotation must match the expected kit or `A => B` parameter type. A kit or `A => B` argument may use one `_` hole (`List.map(xs, _ + 1)`, `List.map(xs, Str.fromInt(_))`). `_ + _` is rejected. `_ =>` still discards. A kit or `A => B` argument may be a case lambda (`{ case Opt.Some(n) => n case Opt.None => 0 }`, `{ case Some(n) => n case None => 0 }`). It matches the bound value. Exhaustiveness is the same as match. A kit or `A => B` argument may be a unary def (`List.map(xs, Str.fromInt)`, `apply(id, 3)`). A def with 2 through 8 params eta-expands as `(A, B, …) => R` (`List.foldLeft(xs, 0, add)`, `applyPair(add, 2, 3)`). A `for` binder or def may name an `A => B` value (`inc = (_ + 1): Int => Int`, `typed = (n: Int) => n + 1`, `def addN(n: Int): Int => Int = (m: Int) => n + m`). `f(x)` applies that value. `f(x, y)` applies `(A, B) => C`. An `A => B` expression applies with `e(x)` on one line (`plusOne()(5)`, `addN(3)(4)`, `((n: Int) => n + 1)(6)`). An `(A, B) => C` expression applies with `e(x, y)` (`((a, b) => a + b)(2, 3)`). Pass a named Fun to a kit or `A => B` parameter (`List.map(xs, eta)`, `apply(inc, 5)`, `apply(plusOne(), 6)`). `{ … }` without `case` is illegal.
- File-stem modules; enums namespaced by stem; `import Module.name` for bare disambiguation. `import Module.name as alias` binds a local name. `import Module.*` binds every public def, enum, and type alias. `check` reports an unused import, an unused local, an unused parameter, and an unused private def. A name that starts with `_` is kept on purpose. Lone `_` discards. A record value rebuilds with `.copy` (`p.copy(y = 9)`, `p.copy(1)`). Omitted fields keep the receiver values. Positionals fill fields in order. A file may declare `type Name = T` or `type Name[T] = List[T]`. The checker expands the alias in params, returns, and `e: T`.
- Types: `Unit`, `Int`, `Float`, `String`, `Bool`, `List[T]`, `Option[T]`, `Map[K, V]`, `Set[T]`, `(A, B, …)`, `IO[T]`, `IO[E, A]`, `A => B`, blessed handles (`Fiber[A]`, `Ref[A]`, `Queue[A]`, `Deferred[A]`, `Resource[A]`, `Stream[A]`), nominal enums. `true` / `false` are `Bool`. `Bool` is not `Int`. An `if` may omit `else` when the then arm is `Unit` or `IO[Unit]`. `IO.attempt(io)` returns `IO[Result[E, A]]`. `Result.Err` keeps the error payload. `Result.Ok` keeps the success payload. `List.head` is `Option[T]`. Empty is `None`. `Map.get` is `Option[V]`. A miss is `None`. `IO[A]` means `IO[String, A]`. `IO.fail(e)` takes `E`. `handleErrorWith` binds `E`. `flatMap` keeps one `E`. Unary `!` is Bool. Unary `-` is Int or Float. Unary `~` and bitwise `&` / `|` / `^` / `<<` / `>>` are Int. `h :: t` is `List.cons(h, t)` (right-associative). Hex (`0xFF`) and binary (`0b1010`) are Int literals. Underscore may separate digits (`1_000`, `0xFF_00`, `0b1010_0001`). Scientific notation is a Float (`1.5e-3`, `1e10`). Triple-quoted strings (`"""…"""`) keep newlines. Use `s"…$x…"` for interpolation. The dialect has no line comments. Identifiers may start with `_` (`_n`). Lone `_` discards. Call arguments may use `name = expr`. Positionals come first. Named arguments fill the remaining parameters by name. A `def` parameter may have a default (`m: Int = 1`). Omitted trailing arguments use the default. Named calls may omit any parameter that has a default. Defaults are closed expressions (no parameter names). Properties, traits, and methods do not take defaults. A call may end with a comma. An expression may pin its type with `e: T`. A typed position may construct with a bare uppercase case name (`None: Option[Int]`, `Some(1)`, `if (ok) Some(n) else None`). `==` / `!=` on String, List, Map, Set, tuples, enums, and records compare by value. Blessed handles compare by identity.
- Blessed kits + `Signal` / `View` / `Ui` / `Property.*` / `.require` as documented in the guide. `IO.fail(e)` is `IO[E, A]`. `IO[A]` means `IO[String, A]`. An `if` / match / `IO.race` arm may be `IO.fail` next to another `IO[T]`. An `if` may omit `else` when the then arm is `Unit` or `IO[Unit]`. `io.map(f)` maps a success. The body is pure `B`. The result keeps the receiver error type: `IO[E, B]`, or `IO[B]` when `E` is `String`. Kit lambdas bind the list element type (`List.filter` / `List.filterNot` / `List.map` / `List.flatMap` / `List.find` / `List.findLast` / `List.exists` / `List.count` / `List.takeWhile` / `List.dropWhile` / `List.forall` / `List.indexWhere` / `List.lastIndexWhere` / `List.span` / `List.partition` / `List.prefixLength` / `List.segmentLength` / `List.sortBy` / `List.maxBy` / `List.minBy` / `List.groupBy` / `List.distinctBy` / `IO.foreach` / `IO.foreachDiscard`), the fold pair (`List.foldLeft` / `List.foldRight` / `List.scanLeft` / `List.scanRight` / `List.reduceLeft` / `List.reduceRight`), the map value (`Map.filter` / `Map.exists` / `Map.forall` / `Map.mapValues`), the set element (`Set.filter` / `Set.exists` / `Set.forall` / `Set.map`), the ref cell (`Ref.update` / `Ref.updateAndGet`), `String` (`View.each`), the request tuple (`Net.serve`), the stream or resource payload (`Stream.*` / `Resource.make` / `Resource.use`), the fold pair (`Stream.scan` / `Stream.fold` / `Stream.zipWith`), or `Int` (`List.tabulate`). The lambda body must return the kit result (`View`, `Bool`, `String`, `T` for `List.map` / `List.tabulate` / `Map.mapValues` / `Stream.map` / `Ref.update` / `Ref.updateAndGet`, `Int` for `List.sortBy` / `List.maxBy` / `List.minBy`, `Int` or `String` for `List.groupBy` / `List.distinctBy` / `Set.map`. `Set.map` and `List.toSet` take the key kind from the key cell. `List` for `List.flatMap` / `Stream.mapConcat` / `Stream.unfold`, `Stream` for `Stream.flatMap`, or `IO`). A lambda may pin that bind with `(x: T) =>`. A lambda may unpack a tuple of 2 through 8 slots with `(a, b) =>` or a constructor with `(Opt.Some(n)) =>`. A kit or `A => B` argument may use one `_` hole (`List.map(xs, _ + 1)`, `Str.fromInt(_)`). `_ + _` is rejected. `_ =>` still discards. A kit or `A => B` argument may be a case lambda (`{ case Opt.Some(n) => n case Opt.None => 0 }`, `{ case Some(n) => n case None => 0 }`). It matches the bound value. Exhaustiveness is the same as match. A kit or `A => B` argument may be a unary def (`List.map(xs, Str.fromInt)`, `apply(id, 3)`). A def with 2 through 8 params eta-expands as `(A, B, …) => R` (`List.foldLeft(xs, 0, add)`, `applyPair(add, 2, 3)`). A `for` binder or def may name an `A => B` value (`inc = (_ + 1): Int => Int`, `typed = (n: Int) => n + 1`, `def addN(n: Int): Int => Int = (m: Int) => n + m`). `f(x)` applies that value. `f(x, y)` applies `(A, B) => C`. An `A => B` expression applies with `e(x)` on one line (`plusOne()(5)`, `addN(3)(4)`, `((n: Int) => n + 1)(6)`). An `(A, B) => C` expression applies with `e(x, y)` (`((a, b) => a + b)(2, 3)`). Pass a named Fun to a kit or `A => B` parameter (`List.map(xs, eta)`, `apply(inc, 5)`, `apply(plusOne(), 6)`). `{ … }` without `case` is illegal.
- No macros, no implicits, no HKT beyond `IO`, no null

## Language direction

Expression-only, effect-sequenced dialect. Dense. Deterministic. Verification-friendly without becoming a proof assistant. **`for` is the kernel binder**. No `val`/statement blocks.

### Expr dialect

- **`for` as primary binder**: `x = e` (pure alias), `x <- e` (effect). `(a, b) = e` / `(a, b, c) = e` and `(a, b) <- e` unpack a tuple of 2 through 8 slots. `Point(x, y) = p`, `Opt.Some(n) <- e`, and `h :: t = xs` unpack the same way. A miss panics. `if pred` keeps the rest when `pred` is true. A miss is `IO.fail`. The `for` needs a `<-` binder.
- **No statement blocks**, no `var`, no `val`. `{ case … }` is a lambda, not a block.
- Branch arms stay expressions. An `if` may omit `else` when the then arm is `Unit` or `IO[Unit]`. Nested `for` when an arm needs names.
- Surface sugar elaborates to a small core (`Let`, `FlatMap`, `match`, ADTs, `IO`). The compiler and checkers target the core.

App-shaped Counter (full surface: `scuzz docs gui`):

```scala
@main def main: IO[Unit] =
  for {
    count = Signal.make(0)
    label = Signal.map(count, n => s"count = $n")
    _    <- Ui.run(_ => View.column(
              View.bindText(label),
              View.button("+1", _ => Signal.set(count, Signal.get(count) + 1))
            ))
  } yield ()
```

### Density

Clear-dense, not cryptic-dense: nested declarative `View`s, inference, single-expr forms, short update verbs, enums + match. Dense source is also token-efficient for agents. Avoid implicits, deep HKT, “everything is `IO`,” and imperative `View.addChild`. Functional by default. Keep the dialect practical.

### Signal, String, and errors

- **Named Signals.** A Signal publishes its `for` binder name (`count = Signal.make(0)` → `"count"`). Claims and `Property.signal*` read that name. A missing name panics. Absence is not 0 or empty. `Verdict.alwaysHas` and `Verdict.afterHit` cover the always-visible and after-hit-shows folds. Dump slot ids stay an implementation detail.
- **`Signal[T]`.** `Signal.make(value)` creates a typed cell. `Signal.makeN(name, value)` gives it an explicit observation name. `Signal.get` and `Signal.set` preserve the payload type. `Signal.map` maps `A => B`. Derived cells cache their values and update through chains. Counter uses a record cell and derived Int and String cells. `View.each` binds a list element type. Record lists dump their count. Record cells dump their payload. Widget signatures require the correct Signal type.
- **UTF-8 `String`.** `Str.*` indexes Unicode code points (`len` / `charAt` / `slice` / `take` / `drop` / `takeRight` / `dropRight` / `reverse` / `indexOf` / `lastIndexOf`). An isolated or invalid byte counts as one code point (same walk as decode). `Str.byteLen` / `Str.byteSlice` keep bytes for protocol framing. `Str.padLeft` / `Str.padRight` measure width in code points and cycle complete code points. `Str.fromBool` is `"true"` / `"false"`. Case maps stay ASCII. The `Str` kit is closed: unknown `Str.*` fails `check`. String-producing `Str` ops type as `String`. TextField and editor caret offsets stay bytes. ASCII strings take a byte fast path in the runtime.
- **Typed fail.** Check encodes `IO[E, A]`. `IO[A]` means `IO[String, A]`. `IO.fail(e)` takes `E`. `handleErrorWith` binds `E`. `flatMap` keeps one `E`. Kits still fail with `String`. The runtime retains the typed payload through failure and recovery. `examples/io` proves Int, record, and enum recovery. `IO.attempt(io)` preserves both types in `Result[E, A]`. Do not add environment `R`. Do not add user FFI.

### Modules and source shape

Scala **nouns**, Rust/Cargo **verbs**. No JVM packages. Rust `struct`/`impl` is not the primary story.

| Layer | Meaning |
| --- | --- |
| Package (`scuzz.toml`) | Dependency / link boundary (crate) |
| File module (`Todo.scuzz`) | Namespacing + visibility |
| Optional deeper `mod` tree | Only when a single file gets heavy |

Direction: payload **enums** / **`record`** + thin **traits**-as-interfaces. Monomorphize generics early. No classes (mutable identity). No `var`. No classpath packages. Path deps remain the unit of reuse. `private def` is module-local (default public). No `pub` yet. Details and examples: run `scuzz docs language`. Keep/cut: [`compatibility.md`](compatibility.md).

### Properties, simulation, mutation, and verification

See [Scenario worlds](#scenario-worlds).

App correctness is **not** classical unit tests. Prefer **mutation, fuzzing, properties, simulation, and determinism**. All are first-class in the language and `scuzz` tooling. The split is **oracles in `*.scuzz_verify` and live bodies, drivers as the test surface**:

- **A claim is a pure predicate over a `Timeline`.** A timeline is the recorded linear history of one execution. Claims quantify over the multiverse of timelines a campaign explores: ∀ claims hold on every timeline, ∃ claims (`Property.sometimes`) demand one. Most claims pair a trigger with an expectation. Relation claims relate two timelines: a verify def with two `Timeline` params returns `Verdict` and judges the pair (first axis: same script under two PCT schedules via `scuzz fuzz --relate`). Claims evaluate in memory at the terminal point of each run. `scuzz fuzz` constructs that point with a quiesce phase: stop new events, inject shutdown, pump until fibers settle. `.require` stays a live point assertion that aborts mid-run. Point assertions abort. Timeline claims judge. The author surface is `Timeline => Verdict`. A `Verdict` is valid or invalid. An invalid verdict carries the violating state index and evidence. A run ends settled or quiesce-budget-tripped. A tripped budget fails the run before claims judge. Claims judge settled timelines. Claims read the timeline forward and stay monotone under event removal so prefix shrinking keeps failures. Named observations are the claim surface. Four `Verdict` helpers cover the repeated folds: always-visible, after-hit-shows, on-hit consecutive-state relations, and step-every consecutive-state relations. `Verdict.alwaysHas` reads a11y. It skips states with no view tree (setup before `Ui.run`). Do not add a temporal-operator calculus. Dump slot ids stay behind the algebra. Existential interleaving search (linearizability-style) is not a claim kind.
- **Only identities and aggregates persist.** A timeline's identity is its seed, event script, and fault plan. Determinism re-derives the full recording by replay. Claim verdicts fold in streaming during the campaign. Timeline and `State` types stay serializable under dump schema `v=2` (loaders also accept `v=1`) so replays and claims stay stable across compiler releases. Campaigns write no timelines. Observation strings intern. Checkpoint flags mark retain points. Claims read a state at an index through the algebra (`tl_at`). `SCUZZ_TIMELINE_COMPACT=1` drops non-checkpoint states before dump and judge. `Timeline.nearestCheckpoint` and live replay restore observation from the nearest retain point. Retention stays an implementation choice behind the algebra.
- **Author claims live in `*.scuzz_verify`.** Files may sit anywhere in the package. They do not pair with live sources. A def with one `Timeline` parameter is a session claim and returns `Verdict`. A def with two `Timeline` parameters is a relation claim and returns `Verdict`. Other public defs return `Bool`. The runtime judges it over the recorded timeline at the terminal point. A def with at most three generator-friendly params (`Int` / `String` / `Bool` / `List` / record / enum) is a drive oracle. The compiler wraps it as a `drive` target. A zero-argument oracle writes `drive <name>` into `build/seeds.txt`. `scuzz check` and verify/fuzz builds load the files. A present empty file fails `check`. Duplicate predicate names fail. A leftover `*.scuzz_intent` file fails resolve. The file must not define `@main`, enums, aliases, traits, impls, or imports. Bodies must not call `Property.*` or `.require`. Humans and agents write claims by hand. No ML inside `scuzz`. English intent may return later with mining. It is not current work.
- **Assertions and coverage stay in live function bodies.** Explicit `.require(pred)` on values / `IO` (type-preserving; residual `Property.check` / sequenced `Property.assert` under verify), reachability `Property.sometimes(name)`, generation labels `Property.classify(name, hit)` (always `true` so it does not filter the oracle), and `where` refinements on `def` params and `record` fields. All erase from live builds. Armed under TestRuntime / fuzz / mutation. There is no `property` keyword.
- **Concrete facts are legitimate oracles.** A `.require` may state one known input/output pair (the VAT rate for `"DE"` is `0.19`). Mutation arms it like any oracle. Do not contort a concrete fact into a general claim. Do not grow fixture-diff suites out of concrete facts. A zero-argument `*.scuzz_verify` oracle writes `drive <name>` into `build/seeds.txt`. `scuzz fuzz` replays those seeds with `corpus/`.
- **Metamorphic oracles close the remaining gap.** When no absolute oracle exists, relate two runs of the same def (a permuted input keeps the total). Metamorphic relations are drive oracles in `*.scuzz_verify` (swap) or `.require` idioms. Do not keep a second Scuzz body as a spec.
- **Drivers in `*.scuzz_scenario` do things.** Impure, parameterized, oracle-free steps. `scuzz fuzz` composes them (generated args, random order / interleaving) alongside the UI event alphabet. `check` rejects `Property.*` and `.require` in scenario files. Drive names are unique in the package (`drive` has no module). A driver name must not match another driver or a verify drive oracle. At most 32 drive names. An assert inside a driver is a unit test in disguise.
- **Simulation is hermetic.** Fuzz, mutation, and TestRuntime keep impurity inside fakes. No live sockets. HTTP client kits use stubs. A loopback URL with no stub parks on a virtual mailbox. `Net.serve` / `Net.serveOnce` accept injected requests and those mailbox items. TCP and UDP kits use a mailbox. Persistent fake serve drains and stops when injects and the mailbox are empty. `serveOnce` parks so `IO.both(Net.serveOnce, Net.httpGet)` completes in either order. Stubs win over loopback. A Net fault fails before loopback. `Sys.exec` / `Sys.spawn` fail under TestRuntime so a child cannot open the network. `Sys.getenv` does not read the host map. Install copies `SCUZZ_SERVE` and `SCUZZ_KIT` into the sealed overlay. `Sys.alive` / `Sys.kill` do not touch host pids. Live HTTP client kits may leave the host. Scheduler ownership, not address, is the determinism boundary.
- **`Property.sometimes` keeps composition honest.** Reachability accumulates across a fuzz *campaign*. Declared-but-never-reached states fail the campaign. Oracle-free drivers cannot pass vacuously. It is a coverage/fitness signal for corpus guidance, alongside Headless dump novelty. It is a path marker (`Unit`), not a value method.
- **Mutation pressures the oracles.** `scuzz fuzz` mutates live `def` and impl method bodies after search. Residual oracles must detect behavioral changes. The summary separates killed, survived, inert, and invalid mutants. The score is killed / (ran - invalid - inert). It is absent when that denominator is zero. `--oracles` mutates residual predicates. Count and apply share one walk, so site numbers match. A typecheck or link failure is an invalid mutant. It does not abort the campaign. Each mutant gets an idle probe, then replay of corpus+seeds entries that passed on the original. Kill = idle fails, or a passing entry fails. A surviving mutant prints its kind and source location (`Module.def:offset`). Survivors stay advisory unless the package sets `[fuzz].score_floor`. The campaign fails when the score drops below that floor. `--iterations 0` does not mutate. Mutation covers every site when `sites <= 64`. A larger set rotates by the campaign seed. Survivors with bit-identical replayed timelines are inert and unreported.
- **Expected-fail proof** is the `examples/bad-*` packages. Each pins a known-wrong campaign in `corpus/`. Search must fail and write `repro.toml`. `examples/bad-intent` fails `scuzz check`. The others keep `scuzz check` green. `--no-fail-fast` still mutates. Inert mutants stay unreported. Catalog: run `scuzz docs verify`.

| Phase | Role |
| --- | --- |
| `scuzz check` | Format-verify and typecheck live + `.require` / `where` + scenario + `*.scuzz_verify`. Drivers stay `IO` and oracle-free. Drive-oracle params stay generator-friendly. An empty verify file or leftover `*.scuzz_intent` fails. A leftover `*.scuzz_sim` or `*.scuzz_drivers` file fails. Scenario replacements match live IO targets. Reports every parse and type diagnostic. Unclaimed defs, signals, and controls are info. They do not fail `check` |
| `scuzz build` (verify graph) | Apply `*.scuzz_scenario` replacements; compile setup and drivers; residualize `.require` / `where` checks (armed under TestRuntime / fuzz / mutation only); compile `*.scuzz_verify` timeline claims and drive oracles |
| `scuzz fuzz` | `--iterations N` campaign. Replay corpus then seeds. `--iterations 0` is corpus-only. Each run quiesces, then claims judge the terminal timeline in memory. Search failure fails the campaign. `--no-fail-fast` keeps that repro, finishes search, then mutates. `--oracles` mutates residual predicates. Survivors remain advisory unless `[fuzz].score_floor` is set. Inert mutants are unreported. Writes `build/fuzz/summary.toml`. Flags and campaign steps: run `scuzz docs verify` |
| Later (optional) | Discharge trivial `.require` / `where` fragments statically; leave the rest as search |

Direction beyond this (not current work): fuzz-verified `*.scuzz_tune` — [`optimization.md`](optimization.md). Pivot slices and status: [`gaps.md`](gaps.md).

**Current file convention (one scenario file, free-standing verify, no attribute tags):**

```text
src/
  Todo.scuzz              # live module: defs + where + .require + sometimes
todo.scuzz_scenario        # one world: setup, replacements, drivers
count.scuzz_verify        # Timeline => Verdict session claims and Bool drive oracles (anywhere in the package)
```

- Live/`scuzz run` loads `*.scuzz` only. Verify predicates, `.require`, inline checks, and refinements erase.
- Claims live in `*.scuzz_verify` as `Timeline => Verdict` defs and as generator-friendly `Bool` drive oracles. Local assertions and `Property.sometimes` stay in live function bodies.
- Fuzz layers the scenario, then arms oracles. Mutation is a fuzz phase. Mutation skips verify predicates and drive wrappers.
- No free-floating `tests/` package roots. No third-party test or mutation frameworks.
- Prefer swapping IO values you own through `*.scuzz_scenario` replacements. Blessed kits stay one implementation + TestRuntime fakes. Do not stub pure helpers, `View` builders, or `Signal` cells. TestRuntime does not open live sockets.
- Driver params stay generator-friendly (`Int` / `String` / `Bool` / `List` / record / enum). Drivers may call live defs but never assert correctness.
- Observation surface: signal store + View/a11y dump — not Skia pixels. Kernel/runtime keep ordinary example tests. This strategy is for **Scuzz apps**.

### Scenario worlds

A scenario defines one world in which the fuzzer exercises the app. Claims in `*.scuzz_verify` state app contracts across worlds. A contract can have a condition: an acknowledged save must survive restart. A scenario does not select or disable claims. A passing campaign provides evidence for the executions it explores. It does not prove correctness for all possible worlds.

One `*.scuzz_scenario` file per project that uses scenarios. That file contains one setup entrypoint, simulation replacements, all workload drivers, private helpers, and resource cleanup. Multiple named scenarios and campaign selection remain later work. One scenario can still generate many initial states through driver arguments. Setup takes no generated params. Generated setup inputs stay with later multiple-worlds work.

| File | Target role |
| --- | --- |
| `*.scuzz` | Production behavior, in-body assertions, and reachability markers |
| `*.scuzz_scenario` | One world: replacements, setup, typed context, drivers, and resource ownership |
| `*.scuzz_verify` | App contracts: Timeline claims, relation claims, and drive oracles |

A leftover `*.scuzz_sim` or `*.scuzz_drivers` file fails `check`. Scenario files do not pair with live file stems. Each replacement uses a qualified name (`def Module.name(...)`). The compiler checks target existence, signature, and purity. Do not replace pure helpers, View builders, or Signal cells.

Scenario syntax:

- `def setup(): IO[T]` is required. It takes no params.
- If `T` is `Unit`, public IO drivers keep their generator params.
- If `T` is a named type (record, not Int/String/Bool/List/Unit), every public IO driver takes `T` as the first param. That param is not generated.
- Qualified defs replace live IO functions. Private defs are helpers. Records and enums are allowed.
- No `@main`, aliases, traits, impls, or imports. No `Property.*` or `.require`.

The compiler wraps a context driver so corpus lines stay `drive name args` without the context value. `Scenario.context()` returns the setup result. Live `scuzz run` has no wrap module, so setup does not run. A scenario world records setup and replacements. It is not the live graph. Every campaign probes that live graph with no wrap. When a package has a scenario file, `scuzz fuzz` skips the silent live/verify dump compare. An IO drive script snapshots setup at once. A UI app keeps the setup drive on the first painted state. It does not snapshot an empty view before `Ui.run`.

Each execution follows this lifecycle:

1. Create a fresh, isolated world. Do not share state between executions.
2. Run setup once. Acquire resources, initialize data, start managed services, and wait for readiness.
3. Complete setup with a typed context. This boundary enables workload drivers and fault injection. Setup failure or a readiness timeout fails the execution. Do not discard it as an unused input.
4. Fuzz the scenario's drivers. Pass the context to each driver. Generate its other arguments. Explore driver order, repetition, and permitted interleavings. Do not generate resource handles.
5. Stop new workload events and settle the workload. Keep dependencies available during settlement. Release scenario resources through the existing Resource and fiber ownership rules. Judge the recorded timeline. Settlement or cleanup failure fails the execution.

Keep the setup resource scope open through the workload. Setup must not return handles to resources that it has already released. Disable injected faults during setup by default. Keep setup deterministic and recorded. Keep live assertions active. To test app startup under faults, initialize only its dependencies in setup and start the app through a driver.

Scenario setup and drivers are oracle-free. They can call production functions that contain assertions. Correctness assertions belong in live bodies or `*.scuzz_verify`. Claims can inspect setup and workload observations. Report setup and workload coverage separately. Distinguish a claim that is exercised and passes from a claim whose trigger is not reached. Setup success alone does not satisfy workload coverage.

Keep simulation hermetic. Dependencies use managed Scuzz fibers or TestRuntime fakes behind blessed effects. A scenario does not enable live sockets or `Sys.exec` / `Sys.spawn`. Headless remains a peer runtime. Do not add a process or container orchestration system.

Replay records the scenario identity, generated setup inputs, and the choices needed to repeat initialization and the workload. Recreate setup for corpus replay, relation runs, and mutation probes. A shrink candidate must initialize successfully and reproduce the same claim failure before it replaces the original. Do not rely on event removal to preserve a failure.

Keep one `scuzz fuzz` entrypoint. Do not add scenario inheritance, dependency graphs, or snapshot caching to the MVP.

### Verification posture

Keep purity checkable (pure `A` vs `IO` vs session). Total expr core. Signals as an explicit store. Immutable data by default. Errors as values. Refinements attach to **positions** (`def` params, `record` fields), not a refined-type algebra. Checked dynamically at call / construction under the verify graph. Erased live. `.require(pred)` attaches an oracle to a **value** (or `IO[A]` sequenced after the effect) while preserving the receiver type — even when `pred` is `IO[Bool]`. They are the fragment that migrates to static discharge later with no author rewrite. Defer dependent types and runtime-heap proofs. Residual oracles + fuzz + mutation first. Static proof only where cheap. Timeline claims stay pure predicates over the recording.

### Oracle authority

Oracles divide into three tiers by the judgment they need.

- **Universal oracles need no intent.** Panic, `SzError`, and nondeterminism reject a run with no human review. This tier: double-run same-seed determinism, heap block count and retain balance return to baseline after session teardown (byte drift at equal count is retained end-state, not a leak), acquire/release pairing within a timeline, finalizers run on cancel, no parked fibers at quiescence, live graph still paints or IO still exits 0 with no scenario wrap, verify-graph idle paint for `[ui]`, live/verify dump equality on packages with no scenario file. Rules stay mechanical.
- **Claims need stated intent.** Humans and agents write `Timeline => Verdict` predicates and drive oracles in `*.scuzz_verify`. The verify-file diffs are the review checkpoint.
- **Descriptive observations carry no authority.** `classify` counts stay machine-maintained.

**Coverage directs attention on three axes.** Reachability: `sometimes` names and claim triggers must fire in the campaign. Breadth: `scuzz check` reports defs, signals, and controls with no claim, and the campaign reports reached states that vary in `State` fields no claim reads. Strength: a surviving mutant that changes observable behavior prints its kind and source location. Survivors stay advisory unless the package sets a score floor. Survivors with bit-identical replayed timelines are inert and unreported. Gaps report to the author. The campaign reports original source locations and reached flags for live function bodies in `summary.toml`. It maps verification locations back to source. Simulation replacements and mutant probes do not count as live body hits. Branch coverage stays open. `sometimes` stays a path marker. Source coverage is not a substitute for named claims.

**Deferred: mining and the judgment loop.** There is no `scuzz mine`. Claim mining from campaign observations, in-campaign candidate synthesis, a forced-choice judgment queue, a judgment log, citation-gated agent accepts, an English renderer with a round-trip oracle, and claims as tool-maintained English source are all deferred. They are not current work and may never land. Any future revival builds on the timeline claim kernel and keeps ML outside `scuzz`.

### `scuzz fuzz`

Deterministic TestRuntime + (for `[ui]`) Headless event scripts (plus scenario replacements when present). The fuzz alphabet is `[taps]` targets, text fields, and scrolls **plus declared drivers** **plus verify drive oracles** (same `drive` verb). Oracles: `*.scuzz_verify` timeline predicates and drive oracles, plus in-body `.require` / `where`, first. Panic/`SzError` still fails. Implicit oracles fail a run on leak, deadlock, heap baseline, acquire/release pairing, finalizer-on-cancel, leftover parked fibers after quiesce, a live graph that does not paint or exit 0, a verify graph with no view tree, and a silent live/verify split on packages with no scenario file. `--differential` compares live structural dumps across `skia` / `sk_sw` / `gpu`. `Property.sometimes` reachability judges the campaign. `Property.classify` counts are visibility only. `repro.toml` records a **shrunk** event list + driver invocations + optional schedule and fault plan so replay is generator-independent. Every campaign loads `<pkg>/corpus/*.toml` then `build/seeds.txt` before search. A failing stored entry is a search failure. `--iterations 0` replays corpus and seeds and stops. Search rotates through stored workloads. It changes event order and repetition. It generates typed driver arguments with bounded ADT depth and simple Int bounds. It walks `SCUZZ_SCHED_SEED` 0, 1, 2, … for the search budget. Iter 0 keeps no fault. Later iters set `SCUZZ_FAULT_SEED` to that same counter. A failure records the exact workload and seeds. Event reduction removes steps while the failure remains. Replay does not use the generator. `tap N` / `scroll N` follow a11y preorder, not a pixel scan. Stable control keys for inject stay a later gap. Named observations apply to claims first. `pump` is time. No hidden nondeterminism. Search explores `SCUZZ_FAULT_SEED` like `SCUZZ_SCHED_SEED`. Seed `0` is no fault. Iter `0` stays no-fault. Default stops at the first search failure. `--no-fail-fast` keeps that repro, finishes search, then mutates. Survivors with bit-identical replayed timelines are inert and unreported. `--oracles` mutates residual predicates. The command writes `build/fuzz/summary.toml`. Flags, script verbs, schedule seeds, and fault seeds: run `scuzz docs verify`.

Runs end in a quiesce phase and claims judge the complete timeline at the terminal point, in memory. The campaign persists identities (corpus entries, `repro.toml`) and aggregates (`summary.toml`). Full timelines re-derive by replay. `Property.sometimes` verdicts match Antithesis campaign aggregation: a name must occur at least once. Scuzz does not emit an Antithesis SDK JSONL stream. Push/PR CI stays a bounded deterministic campaign. Long-budget campaigns stay local. No corpus auto-commit; failures report to authors.

### UI design language

Scuzz Style is the default UI design language. Printed posters guide its visual style.
Use warm paper surfaces, dark text, square controls, and clear borders.
Use yellow for primary actions and selection backgrounds. Use dark rust for
accent text. Keep body text, form fields, and editor content clear. Do not add
noise, random distress, flicker, or tilted controls to these areas.

The shared runtime theme supplies the default colors. Primary buttons have a
small solid shadow inside their bounds. A pointer press moves the button face
into that shadow. Keyboard focus has a light and dark inset border. Text fields
and editors use dark carets. Editor selection and bracket marks use a yellow
background. Checkboxes use an ink checkmark inside a bordered yellow square.
Radio controls use a smaller ink square inside the yellow inset. Switches use
an ink thumb that moves inside a bordered track. Slider thumbs have an ink
border. These marks show selection through shape and position as well as color.
Compact controls and list tiles use the same marks. Progress bars use a thin
ink border around a paper track. A heavier ink border marks the completed
part around its yellow fill. Square progress rings use a thick ink band for
completion and a thin muted track for the remaining part. The amount stays
visible without the accent color. Headless, Desktop, and Mobile use the same
paint path.

Default text colors have at least 4.5:1 contrast against their paper surfaces.
Control borders and essential marks have at least 3:1 contrast. Native checks
calculate these ratios from the theme colors. They also check text on primary
and selection fills. These limits follow [WCAG 2.2](https://www.w3.org/TR/WCAG22/).
Color ratios do not prove full accessibility conformance.

Selected chips and segmented controls use a printed tab shape. A heavy ink
underline and a solid triangle at the top right mark the selected state.
Both marks stay inside the control bounds. The yellow fill supports the marks.
Labels use an ellipsis when space is limited. Each segment keeps its label
inside its own half. Accessibility labels keep the complete text. These defaults
apply to all apps through the shared renderer.

Badges place a bordered count ticket beside the child control. The ticket uses
body-size text and scales with the display. Counts above 99 show `99+`.
Accessibility output keeps the full count. The ticket reserves its width so
count changes do not move the child or cover its label.

Body text uses 14 logical pixels with a 20 pixel line height. Default controls
have a 40 pixel height. Use `View.fontSize(28, ...)` for a screen title and
`View.fontSize(20, ...)` for a section heading. These sizes use the same font and
measurement path as body text. Control labels keep the default body size.

Use `View.wrap` for action groups that must fit narrow windows. Use
`View.maxSize` to cap a form field width. Its compiler call uses the shared
runtime size constraint. Studio groups its working pages in an Index Book. The editor uses outlined secondary actions and text buttons for files.
Primary actions retain the yellow fill. Text fields clip content to their bounds. Button labels use an ellipsis when
they exceed the available width. Their accessibility labels stay complete.
Text insets, carets, and scroll offsets scale with the display.
State controls show their text labels. Long labels use an ellipsis inside the
available width. List titles reserve space for trailing content. Expansion
titles reserve space for the disclosure mark. Accessibility labels stay
complete. Indexed scroll scripts move the selected scroll container even when
it contains another scroll container.

App theme selection, dark mode, and motion settings remain open. Keep accent
colors separate from text selection colors. App content can use the existing
explicit style constructors.

### Index Book navigation

`View.indexBook` groups named `View.section` pages around a persistent index.
The index stays beside the page at widths of 640 logical pixels or more.
At smaller widths, the index wraps above the page. A long index scrolls in its
own space. Numbered printed tabs identify each section and mark selection. The index stays visible
while the page scrolls. Each page keeps its scroll offset and app Signals.
Only the selected page contributes content to hit testing and accessibility.
The index supports focus-group arrow keys and Enter or Space activation.

Use an Index Book for top-level app sections. Pass a selection Signal and a
nonempty column of sections with unique IDs and displayed titles. The selection is a zero-based index and must
stay inside the section range. Supply the selected page heading in its content
or in the app bar.
The book owns page scrolling. Studio uses Home, Tasks, and Preferences as
separate working pages. Native proofs cover page switching, hidden controls,
scroll retention, keyboard navigation, and wide-to-narrow resize. Studio
Timeline claims check the active page and persistent index.

### App structure

`View.appShell(bar, body)` reserves the bar height. It gives the body the
remaining window height. Use a scrolling View, Index Book, or tabs as the body.
`View.appBar(title, actions)` supplies a level-one title and an action area.
Use `View.wrap` for actions. Actions sit beside the title when they fit.
They move below the title at narrow widths. The bar stays outside body scrolling.
Docs and Studio use bound page titles and persistent navigation actions.
Docs renders each topic as one list of sibling blocks. Block Signals are created
once before the UI session. Each block keeps the same page margin.
Docs page content shares an 800-pixel width limit and stays left-aligned.
The content shrinks to fit narrow windows. The app bar spans the window.

`View.tabs(selected, sections)` groups fixed local panels. It accepts the same
named sections as Index Book. The tabs stay above the panel at all widths.
Each panel keeps its scroll offset and Signals. Only the selected panel
contributes content to input and accessibility. Left, Right, Home, and End move
tab focus. Enter or Space selects the focused tab. Browser Tab enters the
selected tab and then the panel. Local tabs do not change the browser URL.
Code blocks place text and Copy in one row with an eight-pixel gap.
Text uses the remaining width. Multiline code sets the row height.
Docs shows live text input and source panels. Studio shows counter and status
panels. Headless claims and native layout checks cover these shared components.
Browser checks cover tab roles, keyboard input, and state retention.

Menus, automatic action overflow, closable document tabs, and list-detail
navigation remain later slices.

### Layout model

**Flutter-style constraints** (constraints down, sizes up). Tight slots: `sized`, `aspectRatio`, percent axes on `fraction`, `expanded` flex, and opt-in `stretch` (cross axis). Scroll content is unbounded on the pan axis (`max` 0). Column/row do not stretch non-flex children unless wrapped in `View.stretch`. Device-pixel paint multiplies author px by the backing scale so taps match the pixels. Desktop and Mobile present that pixel buffer into a point-sized window. Taps stay in logical points. Nested constructors only. Do not drift into CSS-ish ad-hoc rules. Do not grow Flutter-style constraint-overflow dumps. Diagnose through structural dumps + `*.scuzz_verify` + `.require`. Widget catalog: run `scuzz docs kits`.

### UI testing

**`*.scuzz_verify` + in-body `.require` + composed drivers through `scuzz fuzz`** are primary for `[ui]` apps. Mutation is a phase of that command. Authors write named Timeline claims. Universal oracles probe the live graph with no scenario wrap: the binary must paint a view tree. The verify graph must also paint. Live and verify dumps match only when the package has no scenario file. Do not grow dump-fixture suites as the test strategy. `--differential` compares live dumps across Skia backends. IO packages: `*.scuzz_verify` + `.require` + drivers + scenario under TestRuntime when present. The live `@main` must still exit 0.

## Open work

Ranked list: [`gaps.md`](gaps.md). Next: add the typed session schema. The fuzz collect and coverage phases still re-parse package sources in one campaign. Profile that path before more campaign-cost work. Keep type preservation through kits and emitted code as the first gate. Extend native composition proofs before more kit surface. Accepted compositions must compile and preserve values. Invalid compositions must fail `check`.

Hardware device runs stay open. Impeller / Skia GPU raster stay deferred. Real phone and screen-reader checks on the browser target remain open. Browser limits: [`compatibility.md`](compatibility.md#browser-target).

Deferred, not current work: mining and the judgment loop (see [Oracle authority](#oracle-authority)). HTTP status, bind, HTTPS serve, registry, Windows, OS threads, `scuzz eval`, and kit docs stay later.

App authors: run `scuzz docs start`. Vertical slices over breadth. No Desktop-only UI features. UI is a primary path among CLI/server/desktop/mobile. It is not the only v0 bar. GUI apps also target WebAssembly. Scuzz Docs is the first browser app.

## Risks

| Risk | Mitigation |
| --- | --- |
| Language + UI + tooling is huge | Small subset. Vertical slices. Counter before generality |
| Dialect unexercised by apps | Kernel examples that stress each construct. `check` / `fuzz` on the passing `examples/`. `examples/bad-*` are expected-fail campaigns |
| Effects too weak or too heavy | Builtin IO. Pure `View`. `Ui` at session boundary |
| Hidden nondeterminism | Closed impurity + hermetic TestRuntime + deterministic `*.scuzz_scenario`. No live sockets under sim. `Sys.exec` / `Sys.spawn` fail under TestRuntime. `Sys.getenv` is sealed except `SCUZZ_SERVE` / `SCUZZ_KIT`. `Sys.alive` / `Sys.kill` use a fake process table. Double-run same-seed determinism is a universal oracle |
| Properties become brittle dump goldens | Named observations on Signals and controls. Four `Verdict` helpers for the repeated folds. Strict replacement/live pairing in `check`. Mutation kills weak oracles. Universal live-graph paint is not a dump fixture |
| `String` as bytes mangles UI text | `Str.*` indexes Unicode code points; `Str.byteLen` / `Str.byteSlice` keep bytes for framing. Case maps stay ASCII |
| LSP span misses the token | One JSON schema. Check diagnostics and LSP goto-def, rename, hover, completion, and tokens use recorded spans. Panic prints the enclosing def file and line. The dogfood IDE consumes that schema |
| Sim becomes Mockito | Only qualified IO replacements. No stubbing pure `View`/`Signal`. Kits stay TestRuntime |
| Drivers become integration tests | `check` rejects `Property.*` in scenario files. Correctness lives in `*.scuzz_verify` and live-module `.require` |
| Drivers pass vacuously | `Property.sometimes` reachability fails the campaign when declared states are never reached |
| Verification tool sprawl | One `scuzz` strategy — mutation/fuzz/properties/sim/determinism in-tree. No external test frameworks |
| Slow fuzz inner loop pushes authors back to ad-hoc testing | Corpus-only replay tier answers in seconds. Full campaigns stay in CI |
| `Property.sometimes` verdicts vary with iteration budget | Checked-in corpus keeps reaching prefixes. Summary separates never-reached from not-reached-in-budget |
| Concrete business facts have no home without unit tests | Concrete-fact `.require` checks are sanctioned oracles. Zero-argument verify oracles seed the campaign |
| “Almost Scala” confusion | Explicit non-goals. Language direction above. Run `scuzz docs language`. |
| Watch confused with hot reload | `scuzz watch` rebuilds. `[ui]` `run --watch` is hot reload (stamp-reload Views). IO-only `run --watch` kills and reruns |
| IDE typer ≠ batch typer | One JSON schema. LSP wraps `scuzz check`. The dogfood IDE consumes that schema. It does not grow a second typer |
| Dogfood IDE before editor primitives | `scuzz ide` launches the bundled editor (`examples/editor`). Headless stays a peer. Do not add a `scuzz-ide` binary |
| Skia weight | pinned CPU prebuilt default. `sk_sw` opt-out |
| Desktop-only features | Headless peer rule. Editor keys, caret, selection, clipboard, compose, and inject verbs share one input alphabet |
| Treating UI as the only product | UI is a primary path (Flutter-shaped). CLI/server/desktop/mobile are peers |
| GC vs frame budget | `pump` boundary. Measure |
| Mobile packaging | Host Mobile peer first. `scuzz package` drives Android/iOS shells. Hardware USB later |
| Self-hosting stalls or forks the toolchain | Staged slices behind prerequisites ([Self-hosting](#self-hosting)). Bootstrap fetches the newest GitHub `v*` release. The Scuzz CLI is the product. No dual toolchains |
