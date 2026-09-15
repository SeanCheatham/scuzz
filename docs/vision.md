# Scuzz Lang vision

Scuzz Lang is a Flutter-shaped product with a Scala-inspired language. It is not a Scala 3, Scala Native, or Maven citizen.

One doc for product intent, design locks, language direction, and open work. Keep/cut: [`compatibility.md`](compatibility.md). Manifest: `scuzz docs manifest`. App path: `scuzz docs start`. Kits: `scuzz docs kits`. Host setup: [`developer-environment.md`](developer-environment.md). Later tune work: [`optimization.md`](optimization.md).

Edit this file when a decision or next-step order changes.

## Thesis

- **Language**: a purposeful Scala-inspired subset for native CLI, server, desktop, and mobile apps. Effects use built-in `IO` (ZIO-inspired, not a ZIO or cats port). `for` is the primary binder. Dense and token-efficient. Functional by default. Keep it practical. See [Language direction](#language-direction).
- **Runtime**: custom native (LLVM). Native binaries, not a VM. No JVM. No Java interop. No classpath or Maven. GUI apps also target WebAssembly. Scuzz Docs is the first browser app.
- **UI**: a primary product path, not the only one. Flutter-shaped: GUI is first-class; so are CLI and server. One design language plus Skia, as a `Ui` effect with Headless, Desktop, and Mobile interpreters. Headless is a product runtime for agents and CI. It is not a test-only shim.
- **Batteries**: the language and standard kits cover common app cases. No ecosystem library sprawl. No Maven, cats, or ZIO ports.
- **Tooling**: one CLI (`scuzz`). One formatter (`scuzz fmt`). One linter (`scuzz check`; no `lint` subcommand). One testing strategy. Mutation, fuzz, properties, simulation, and determinism are first-class. Compiler and CLI are Scuzz (`examples/compiler`, `examples/cli`). Bootstrap uses the newest GitHub `v*` release ([Self-hosting](#self-hosting)). `scuzz ide` launches the dogfood `[ui]` app. It is not the compiler.
- **Language proof**: examples that exercise the surface (`examples/`). The shipped CLI is Scuzz ([Self-hosting](#self-hosting)).
- **AI-Friendly**: Headless, hot reload, and debugging tools aid agents. Headless is a peer runtime. `scuzz watch` only rebuilds. `[ui] run --watch` is hot reload: it stamp-reloads Views. Dump and inject ops: run `scuzz docs commands`.

Upstream Scala Native is a reference, not a dependency. Divergence is intentional.

## Defaults

| Topic | Choice |
| --- | --- |
| UI testing / CI | `*.scuzz_verify` + in-body `.require` + drivers through `scuzz fuzz --iterations`. Mutation is a phase. `--differential` compares live dumps across Skia backends |
| Static hygiene | One linter: `scuzz check` (format-verify + typecheck; lints on this command; no `lint` subcommand). One formatter: `scuzz fmt` rewrites live sources, scenarios, and claims |
| Codegen | LLVM IR. Emit reads types from the checker. It does not guess from SSA names |
| Renderer (v0) | Skia through thin C ABI; Impeller deferred |
| Build tool | DIY Mill/Cargo-like: `scuzz` (not sbt/Maven) |
| Effects | Language + runtime builtins |
| Impurity | All nondeterminism / external I/O through blessed `IO`. No app-level `IO.delay`. Simulation is hermetic |
| Tests | One strategy: mutation + fuzz + properties + sim + determinism. Claims in `*.scuzz_verify` (`Timeline => Verdict`). Drivers are oracle-free. `.require` in live bodies. No `src/test` culture |
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
- Not classical unit-test culture (`src/test`, Mockito, fixture-diff suites). Use mutation + fuzz + properties + sim + determinism in `scuzz`. Drivers stay oracle-free. Concrete facts may use `.require`.
- Not Flutter DevTools / VM patching. `[ui] run --watch` is hot reload. `scuzz watch` only rebuilds. IO-only `run --watch` kills and reruns.
- Not an sbt / Gradle / `pubspec` plugin DSL (`scuzz.toml` is data)
- Not Flutter platform channels
- Not a dual shipped product CLI. The product CLI is Scuzz. Bootstrap uses the newest GitHub `v*` release. The tagged bootstrap `scuzz` is not a second product CLI.
- Not a second IDE typer. External editors speak `scuzz lsp`. The dogfood IDE consumes `scuzz check` JSON. It does not grow a parallel analyze frontend.

## Success bars

**v0** — Install CLI (`curl …/install.sh | sh`, or checkout `./scripts/install.sh`) → `scuzz new` (IO) or `scuzz new --ui` (Counter as `View` + builtin `IO`) → `scuzz fuzz --iterations 0`, and `scuzz run` (`--headless` for UI). Desktop when available. Language `Resource` / `Stream` / `Net.serve` ship (`examples/io`).

**v1** — Shipped `scuzz` is the Scuzz CLI (GitHub Releases; `package_release.sh` / `install.sh`). Cut a release with the GitHub `release` workflow. Kernel surface is proven by examples. `fuzz` lives on that CLI.

## Decisions

### Product name

Brand in prose: **Scuzz Lang** (short form **Scuzz**). CLI `scuzz`; compiler package `examples/compiler`; manifest `scuzz.toml`; sources `*.scuzz` (plus `*.scuzz_scenario` and `*.scuzz_verify`); C ABI `sz_` / `Sz*` / `SZ_*`. No dual names or legacy aliases.

### Tooling

One CLI. One typer. One formatter. One linter. One testing strategy. No second analyze frontend. No `*.g.scuzz` codegen. No `src/test` runner. No bolted-on mutation/fuzz/property ecosystems.

- **Watch** rebuilds when sources or `scuzz.toml` change. `[ui]` `run --watch` is hot reload. See `scuzz docs gui`. IO-only `run --watch` kills and reruns.
- **Static hygiene** is `scuzz check` (the linter). `scuzz fmt` rewrites. No `lint` subcommand.
- **Verification** is built into `scuzz` and the language. A search failure fails `scuzz fuzz`. A mutation survivor does not. The driver registry grows with the package. Drive names must be unique. Catalog: run `scuzz docs verify`.
- **JSON diagnostics** (`scuzz check --message-format=json`) are the editor protocol. `scuzz lsp` wraps `check`. Panic, goto-def, and rename must use Scuzz source spans. Do not grow a second typer or schema.
- **Dogfood IDE.** `scuzz ide` launches a Scuzz `[ui]` package. Headless stays a peer. Editor landmarks stay unnumbered. Docs may use Index Book. The app consumes `scuzz check` / `lsp` / `fmt` / `run` / `fuzz`. Do not add Desktop-only editor behavior. Do not ship a second `scuzz-ide` binary.
- **`scuzz.toml` is data** — package, path deps, `[ui]`, optional `[fuzz].score_floor`. No plugin DSL. Unknown keys rejected. `--headless` forces Headless. No `scuzz add`. No git or registry deps. No library publishing. A hosted registry may never ship.
- **Docs.** `scuzz docs` prints the technical manual from `examples/manual`. Kit rows come from `examples/compiler/src/Kits.scuzz`. There is no `guide.md`. Run `scuzz docs kits` and `scuzz docs language`.
- **Fingerprint** (incremental): miss → rebuild. Native make stays quiet on success. Fail on the first missing tool with one install line.
- **`scuzz package`:** `--target` is host, android, ios, web, or all. Hardware device runs stay open ([`gaps.md`](gaps.md)).

### Self-hosting

The product CLI is Scuzz (`examples/cli`). `scripts/bootstrap.sh` fetches the newest GitHub `v*` release and compiles that CLI. Do not ship two toolchains. Product version lives in `VERSION`.

`examples/syntax` is the lexer and parser. `examples/compiler` is the checker, emit, and compile pipeline. `examples/fmt`, `examples/tyck`, and `examples/codegen` prove printer, checker, and emitter. Toolchain sources only call builtins that the newest `v*` bootstrap already emits.

### GC (v0)

libc `malloc`/`free` through `sz_alloc` / `sz_free`. No collector. Heap values are reference-counted. Immutable data forms no cycles.

### Skia

No vendored Skia tree. Thin `sk_capi` (measure + draw). **Default UI backend** is the pinned Skia CPU prebuilt. `SCUZZ_SKIA=sk_sw` is the explicit opt-out. `SCUZZ_SKIA=gpu` paints with `sk_sw` and presents through OpenGL. Impeller / Skia GPU raster stay deferred. Callers depend only on `sk_capi.h`.

### IO and impurity

One failure channel: `SzError` on `IO[T]`. Typed `E` on `IO` without environment `R`. Do not add `ZIO[R, E, A]`. Blessed kits only. No app-level `IO.delay`. No user FFI, `extern`, or plugins. Determinism and effect capture are not settled. Cooperative single-threaded fibers are the scheduler. Simulation is hermetic. No live sockets under sim. Persistent HTTP servers wait for new requests until cancellation in live and simulation runtimes. Live HTTP uses this HTTP/1.0 stack plus OpenSSL. A response is `(Int, Map[String, String], String)`. Serve binds `0.0.0.0` and `::`. `Net.serveTls` and `Net.serveOnceTls` terminate TLS with a process cert. A loopback `https://` client does not verify that cert. Do not expose POSIX sockets. Do not add a second HTTP client. `Clock.iso8601` formats UTC from epoch milliseconds. No parse. No time zone kit. `Str.matches` is POSIX ERE full-string match on UTF-8 bytes. No capture. No replace. `Hash.sha256` returns lowercase hex of the SHA-256 of UTF-8 bytes. Software SHA-256. No OpenSSL. No HMAC. No other digests. `Hex.encode` returns lowercase hex of UTF-8 bytes. `Hex.decode` reverses that encoding. Odd length or a bad digit yields the empty string. `Base64.encode` returns RFC 4648 of UTF-8 bytes. `Base64.decode` reverses that encoding. Bad length, digit, or pad yields the empty string. No URL-safe alphabet. `Uuid.v4` returns an RFC 4122 version-4 UUID as lowercase hex with hyphens. It uses the blessed Random stream. No parse. No other versions. `Bytes.fromStr` copies UTF-8 bytes. `Bytes.len` is the byte count. No Fs or Net Bytes. Kits: run `scuzz docs kits`. A panic must print a Scuzz file and line.

Each HTTP client call takes a request header map after the URL. Body methods take the body last. A server request is `(String, String, Map[String, String], String)`: path, method, headers, and body. Request header names use lowercase in handlers. The runtime owns Host, Content-Length, Connection, and Transfer-Encoding. Client maps cannot set these fields. Invalid names, control bytes other than tab, duplicate names with different case, and maps over 16 KiB fail before network dispatch. Simulation carries app headers through the virtual server. Header values do not enter the effect log.

`Stream` is one finite pull interpreter. Bind a Stream with `=`. `<-` needs `IO`. Do not add backpressure, publishers, or a second stream kit.

### `Ui` vs `View`

| Layer | Role | Purity |
| --- | --- | --- |
| **`View`** | Widget tree | Sync/pure `build` |
| **`Ui` / `UiSession`** | `mount` / `pump` / `inject` / `snapshot` | Effectful (`UiRuntime`) |

Headless is a **peer** of Desktop/Mobile. Frame boundary is `pump`. A live loop paints when the session is dirty. It waits when nothing changes. World effects stay blessed `IO`. No UI feature without a Headless path. Nested declarative construction only. `Ui.run(_ => view)` is the session. Dump and inject ops: run `scuzz docs commands`.

### IO apps vs Headless

| Path | Meaning |
| --- | --- |
| **Headless** | `UiRuntime` peer — still `View` / Skia / structural dumps |
| **IO-only** | No `[ui]`, no `Ui.run`; plain `@main: IO[Unit]` exec |

Missing `[ui]` ⇒ Skia omitted from the link. IO-only is **not** a fourth runtime peer.

### Kernel dialect

The language `scuzz` implements. Proof is examples (`examples/hello`, `kernel`, `io`, `counter`, `studio`, `scale`).

Locks (not an API catalog — run `scuzz docs language` and `scuzz docs kits`):

- Expression dialect only: `for` primary binder (`=` pure, `<-` effect); no `val` / statement blocks / `var`
- Optional `package`; top-level `def` / `private def` / `import`; `@main def …: IO[Unit]`
- Payload enums + `record` sugar + thin traits/`impl` (static dispatch) + monomorphized generics
- File-stem modules; enums namespaced by stem
- Blessed kits + `Signal` / `View` / `Ui` / `Property.*` / `.require`
- No macros, no implicits, no HKT beyond `IO`, no null

## Language direction

Expression-only dialect. **`for` is the kernel binder**. `=` aliases a pure value. `<-` sequences an effect. No `val`. No `var`. No statement blocks. `{ case … }` is a lambda, not a block. Surface sugar elaborates to a small core.

Scala **nouns**, Rust/Cargo **verbs**. No JVM packages. Direction: payload **enums** / **`record`** + thin **traits**. Monomorphize generics early. No classes. Path deps remain the unit of reuse. Do not add a package registry. Details: run `scuzz docs language`. Keep/cut: [`compatibility.md`](compatibility.md).

## Verification posture

App correctness is not classical unit tests. Prefer mutation, fuzzing, properties, simulation, and determinism. All are first-class in the language and in `scuzz`.

- **Claims** live in `*.scuzz_verify`. The author surface is `Timeline => Verdict`. Dump slot ids stay behind the algebra. `Timeline.fileTextIs` compares a file with expected text at a recorded state. It observes the hermetic filesystem. Missing files and directories return false. Do not add a temporal-operator calculus.
- **`.require`** and `Property.sometimes` stay in live function bodies. They erase from live builds.
- **Drivers** live in one `*.scuzz_scenario`. They are impure, parameterized, and oracle-free. `check` rejects `Property.*` and `.require` in scenario files.
- **Simulation is hermetic.** Fuzz, mutation, and TestRuntime keep impurity inside fakes. No live sockets. Scheduler ownership, not address, is the determinism boundary.
- **Probe limits.** Each probe has a 20-second deadline and a 512 MiB virtual memory limit. Simulation stops after 1000000 scheduler steps per IO run. A limit failure fails the probe. Process cancellation kills the shell and its process group.
- **One `scuzz fuzz`.** `--iterations N` is the campaign. `--iterations 0` is corpus-only. Mutation is a phase of that command. Search failure fails the campaign. Catalog: run `scuzz docs verify`.

```text
src/
  Todo.scuzz              # live module: defs + where + .require + sometimes
todo.scuzz_scenario        # one world: setup, replacements, drivers
count.scuzz_verify        # Timeline => Verdict session claims and Bool drive oracles
```

One `*.scuzz_scenario` file per project that uses scenarios. Multiple named scenarios and generated setup stay later. Live `scuzz run` loads `*.scuzz` only. No free-floating `tests/` package roots. Direction beyond this: [`optimization.md`](optimization.md). Ranked gaps: [`gaps.md`](gaps.md).

## UI design language

Scuzz Style is the default UI design language. Use warm paper, dark text, square controls, and clear borders. Use yellow for primary actions. Use dark rust for accent text. Headless, Desktop, and Mobile use the same paint path. Color ratios do not prove full accessibility conformance.

`View.indexBook` groups named pages around a persistent index. Docs may use Index Book. The editor uses unnumbered landmarks. It does not paint Index Book chapter numbers.

**Flutter-style constraints** (constraints down, sizes up). Nested constructors only. Do not drift into CSS-ish ad-hoc rules. Diagnose through structural dumps + `*.scuzz_verify` + `.require`. Widget catalog: run `scuzz docs kits`. GUI catalog: run `scuzz docs gui`.

GUI apps also target WebAssembly. Scuzz Docs is the first browser app. Full web accessibility stays later. Browser limits: [`compatibility.md`](compatibility.md#browser-target).

## Open work

Next: real tooling measured through `examples/api-report`. This example fetches authenticated JSON records and writes an open-record report. It retries HTTP 429, 502, 503, and 504 at most twice. A numeric Retry-After header sets the delay in seconds. Without that header, HTTP 429 waits 1000 ms and gateway errors wait 100 ms. Invalid, ambiguous, or HTTP-date Retry-After values stop the request. One timeout covers all attempts and delays. Its corpus covers recovery, retry exhaustion, rejected credentials, invalid JSON, failed status, and timeout. File claims check report contents and preserve the prior report on failed requests. Not checker Fun-string residuals. Not GUI. Not user FFI. Not a package registry.

Ranked list: [`gaps.md`](gaps.md).

## Risks

| Risk | Mitigation |
| --- | --- |
| Language + UI + tooling is huge | Small subset. Vertical slices. Counter before generality |
| Dialect unexercised by apps | Kernel examples. `check` / `fuzz` on passing `examples/`. `examples/bad-*` are expected-fail |
| Effects too weak or too heavy | Builtin IO. Pure `View`. `Ui` at session boundary |
| Hidden nondeterminism | Closed impurity + hermetic TestRuntime. No live sockets under sim |
| Properties become brittle dump goldens | Named observations. Mutation kills weak oracles. Live-graph paint is not a dump fixture |
| `String` as bytes mangles UI text | `Str.*` indexes Unicode code points; `Str.byteLen` / `Str.byteSlice` keep bytes for framing. Case maps stay ASCII |
| LSP span misses the token | One JSON schema. Check diagnostics and LSP use recorded spans. The dogfood IDE consumes that schema |
| Sim becomes Mockito | Only qualified IO replacements. No stubbing pure `View`/`Signal`. Kits stay TestRuntime |
| Drivers become integration tests | `check` rejects `Property.*` in scenario files. Correctness lives in `*.scuzz_verify` and live-module `.require` |
| Drivers pass vacuously | `Property.sometimes` reachability fails the campaign when declared states are never reached |
| Verification tool sprawl | One `scuzz` strategy — mutation/fuzz/properties/sim/determinism in-tree. No external test frameworks |
| Slow fuzz inner loop pushes authors back to ad-hoc testing | Corpus-only replay tier answers in seconds. Full campaigns stay in CI |
| `Property.sometimes` verdicts vary with iteration budget | Checked-in corpus keeps reaching prefixes. Summary separates never-reached from not-reached-in-budget |
| Concrete business facts have no home without unit tests | Concrete-fact `.require` checks are sanctioned oracles. Zero-argument verify oracles seed the campaign |
| “Almost Scala” confusion | Explicit non-goals. Language direction above. Run `scuzz docs language`. |
| Watch confused with hot reload | `scuzz watch` rebuilds. `[ui]` `run --watch` is hot reload (stamp-reload Views). IO-only `run --watch` kills and reruns |
| IDE typer ≠ batch typer | One JSON schema. LSP wraps `scuzz check`. No second typer |
| Dogfood IDE before editor primitives | `scuzz ide` launches the bundled editor (`examples/editor`). Headless stays a peer. Do not add a `scuzz-ide` binary |
| Skia weight | pinned CPU prebuilt default. `sk_sw` opt-out |
| Desktop-only features | Headless peer rule. One input alphabet for editor keys, caret, selection, clipboard, compose, and inject |
| Treating UI as the only product | UI is a primary path (Flutter-shaped). CLI/server/desktop/mobile are peers |
| GC vs frame budget | `pump` boundary. Measure |
| Mobile packaging | Host Mobile peer first. `scuzz package` drives Android/iOS shells. Hardware USB later |
| Self-hosting stalls or forks the toolchain | Bootstrap fetches the newest GitHub `v*` release. The Scuzz CLI is the product. No dual toolchains |
