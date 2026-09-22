# Scuzz Lang philosophy

Scuzz Lang is a Flutter-shaped product with a Scala-inspired language. It is not a Scala 3, Scala Native, or Maven citizen.

One doc for product intent, design locks, and language direction. Planning arcs (open work, risks): [`vision.md`](vision.md). Keep/cut: [`compatibility.md`](compatibility.md). Manifest: `scuzz docs manifest`. App path: `scuzz docs start`. Kits: `scuzz docs kits`. Host setup: [`developer-environment.md`](developer-environment.md). Later tune work: [`optimization.md`](optimization.md).

Edit this file when a decision changes.

## Thesis

- **Language**: a purposeful Scala-inspired subset for native CLI, server, desktop, and mobile apps. Effects use built-in `IO` (ZIO-inspired, not a ZIO or cats port). `for` is the primary binder. Dense and token-efficient. Functional by default. Keep it practical. See [Language direction](#language-direction).
- **Runtime**: custom native (LLVM). Native binaries, not a VM. No JVM. No Java interop. No classpath or Maven. GUI apps also target WebAssembly. Scuzz Docs is the first browser app. One evaluator runs checked programs for `scuzz fuzz`, `scuzz eval`, and the browser playground. It is not a deploy target ([Evaluator](#evaluator)).
- **UI**: a primary product path, not the only one. Flutter-shaped: GUI is first-class; so are CLI and server. One design language plus Skia, as a `Ui` effect with Headless, Desktop, and Mobile interpreters. Headless is a product runtime for agents and CI. It is not a test-only shim.
- **Batteries**: the language and standard kits cover common app cases. No ecosystem library sprawl. No Maven, cats, or ZIO ports.
- **Tooling**: one CLI (`scuzz`). One formatter (`scuzz fmt`). One linter (`scuzz check`; no `lint` subcommand). One compiler. One evaluator. One testing strategy. Mutation, fuzz, properties, simulation, and determinism are first-class. Compiler and CLI are Scuzz (`examples/compiler`, `examples/cli`). Bootstrap uses the newest GitHub `v*` release ([Self-hosting](#self-hosting)). `scuzz ide` launches the dogfood `[ui]` app. It is not the compiler.
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
- Not a VM deploy target. The evaluator serves `fuzz`, `eval`, and the browser playground. `scuzz run` and `scuzz package` stay compiled.
- Not a second IDE typer. External editors speak `scuzz lsp`. The dogfood IDE consumes `scuzz check` JSON. It does not grow a parallel analyze frontend.

## Decisions

### Product name

Brand in prose: **Scuzz Lang** (short form **Scuzz**). CLI `scuzz`; compiler package `examples/compiler`; manifest `scuzz.toml`; sources `*.scuzz` (plus `*.scuzz_scenario` and `*.scuzz_verify`); C ABI `sz_` / `Sz*` / `SZ_*`. No dual names or legacy aliases.

### Tooling

One CLI. One typer. One formatter. One linter. One compiler. One evaluator. One testing strategy. No second analyze frontend. No `*.g.scuzz` codegen. No `src/test` runner. No bolted-on mutation/fuzz/property ecosystems.

- **Watch** rebuilds when sources or `scuzz.toml` change. `[ui]` `run --watch` is hot reload. See `scuzz docs gui`. IO-only `run --watch` kills and reruns.
- **Static hygiene** is `scuzz check` (the linter). An expression that ends before its required body or operand is a parse error. `scuzz fmt` rewrites. No `lint` subcommand.
- **Verification** is built into `scuzz` and the language. The terminal and JSON summary use the same coverage and reachability results. They report reached functions, branch arms, sometimes labels, and triggers. Write `oracle name` for a drive oracle. An oracle returns Bool. `check` rejects an oracle that does not. A public `def` that returns Bool is not an oracle. `private oracle` is a parse error. Boolean drive oracles assert the value of the complete expression. A `for` with only `=` bindings keeps the result type of its body. An equality oracle can report both operands when it fails. A search failure fails `scuzz fuzz`. A mutation survivor does not. The driver registry grows with the package. Drive names must be unique. Catalog: run `scuzz docs verify`.
- **JSON diagnostics** (`scuzz check --message-format=json`) are the editor protocol. `scuzz lsp` wraps `check`. Panic, goto-def, and rename must use Scuzz source spans. Do not grow a second typer or schema.
- **Dogfood IDE.** `scuzz ide` launches a Scuzz `[ui]` package. Headless stays a peer. Editor landmarks stay unnumbered. The Docs walkthrough does not use Index Book. Index Book stays a kit. The app consumes `scuzz check` / `lsp` / `fmt` / `run` / `fuzz`. Do not add Desktop-only editor behavior. Do not ship a second `scuzz-ide` binary.
- **`scuzz.toml` is data** — package, path deps, `[ui]`, optional `[fuzz].score_floor`. No plugin DSL. Unknown keys rejected. `run --target` and `ide --target` take an explicit platform (`linux` / `macos` / `headless` / `android` / `ios`) and override `[ui].default_runtime`. A package without `[ui]` accepts only the host platform. No `scuzz add`. No git or registry deps. No library publishing. A hosted registry may never ship.
- **Docs.** `scuzz docs` prints the technical manual from `examples/manual`. Kit rows come from `examples/compiler/src/Kits.scuzz`. There is no `guide.md`. The `[ui]` package `examples/docs` is a gated walkthrough. It is not a painted copy of the manual. The walkthrough grows one Counter. Stages are Intro, Run, View, Check, State, Search, Cover, and Mutation. Intro is a short language and tooling overview. One stage, one prompt, one live artifact, then Continue. Continue stays off until the stage gate holds. Intro and Cover have no gate. Continue stays above the stage body. The app bar title shows the stage name and `n/8`. Run's `@main` binds `inc(0)`. View mounts a `View`. `+1` does not change the label. Check opens `count.scuzz_verify` and runs `oracle incAdds`. Check and Search show nested local tabs for `Main.scuzz` and `count.scuzz_verify`. State mounts the Counter `Signal`. Continue waits for `+1`. Search fuzzes `oracle hidden` in the verify file. Cover renders two scheduler worlds of one `IO.both` race as a pair of cards with the first winner and the `leftFirst` verdict, and paints reached lines of `inc`. Mutation shows live source, mutant source, and the diff. `incAdds` rejects the mutant. Continue copies the next starter into the live editor and the verify editor when the text still matches the prior starter. Walkthrough snippets do not use an empty `@main`. The walkthrough uses `View.tabs` as a progress strip. It does not use Index Book. Hash ids are `#stage=id`. Stage IDs stay stable when titles change. Install, language, commands, manifest, iOS, web, and IDE stay in `scuzz docs`. Run `scuzz docs kits` and `scuzz docs language`.
- **Fingerprint** (incremental): miss → rebuild. Cache keys include the SHA-256 of the executing compiler. A compiler change invalidates live and verification artifacts. The runtime supplies this identity through the reserved SCUZZ_EXECUTABLE_SHA256 key in Sys.getenv. A host environment value cannot replace it. Simulation reads this key from its fake environment only. Native make stays quiet on success. Fail on the first missing tool with one install line.
- **`scuzz package`:** `--target` is linux, macos, android, ios, web, or all. linux and macos must match the host. Hardware device runs stay open ([`gaps.md`](gaps.md)).
- **iOS local loop.** `scuzz devices` lists available iOS simulators. `scuzz run --target ios` selects or boots a simulator, builds and installs the app, and streams app output. `--device` selects an exact name or ID. `--watch` reloads Views after source changes. It preserves Signals. Manifest changes and the r command rebuild and restart. A build error or an incompatible capture preserves the running app. Restart resets app state. Host and simulator reload use the same capture checks. Native UI loops yield to the IO scheduler. IO tap handlers run as session-owned fibers. Session exit cancels their work. Native object caches shorten source rebuilds. The iOS viewport excludes safe areas and the docked keyboard. UIKit layout changes send shared resize events. Live records include viewport, keyboard, and lifecycle changes. Headless replays these events. Run `scuzz docs ios`.

### Self-hosting

The product CLI is Scuzz (`examples/cli`). `scripts/bootstrap.sh` fetches the newest GitHub `v*` release. It builds a temporary compiler from the checkout. That compiler builds the product CLI with the current emission rules. The script removes the temporary compiler. Do not ship two toolchains. Product version lives in `VERSION`.

`examples/syntax` is the lexer and parser. `examples/compiler` is the checker, evaluator, emit, and compile pipeline. `examples/fmt`, `examples/tyck`, and `examples/codegen` prove printer, checker, evaluator, and emitter. Toolchain sources only call builtins that the newest `v*` bootstrap already emits.

### Evaluator

`Eval.scuzz` in `examples/compiler` evaluates a checked program. It is Scuzz. It is one module of the one compiler, not a second toolchain.

- **Scope.** `scuzz eval` runs a package on the host. `scuzz fuzz` searches, mutates, and measures coverage on the evaluator. Docs runs the evaluator compiled to WebAssembly. The Docs decision owns the walkthrough stage spec. Search calls an `oracle` at `Value`. It does not call `Fuzz.probe`. Headless claims assert on those `View`s before a browser does. Cover and Mutation construct viz when those stages open. A small evaluator warmup of the Counter snippets stays at boot. `examples/manual` is the source for `scuzz docs`. It is not the source for the walkthrough shell. `scuzz run` and `scuzz package` stay compiled.
- **Same meaning.** A program has one meaning. A difference between evaluator output and emitted output, other than speed, is a compiler bug or an evaluator bug. `scuzz fuzz` replays the corpus on the compiled binary after an evaluator campaign. A difference fails the campaign.
- **One scheduler.** The evaluator maps `IO` to native `IO`. It does not own a scheduler, fibers, fakes, faults, clocks, or timelines. TestRuntime, hermetic simulation, schedule seeds, and `Timeline` are shared with compiled programs. One probe path: the runtime registers setup, drivers, and claims as fn pointers from emitted code or as closures from the evaluator (`Fuzz.*` kits) and runs both the same way. A corpus entry records the fiber picked at each contention step in `schedule_picks`. Replay follows the recorded picks. A recorded fiber that is not ready, or a run that outlasts its recorded picks, fails the probe as schedule drift. A probe env reaches the evaluator process under the `SCUZZ_EV_` prefix, so the toolchain itself runs live until `Fuzz.probe` starts.
- **A scheduler step is one effect.** `pure`, `flatMap`, `handleError`, `attempt`, `ensure`, and loop entry run in the same step as the effect that follows them. A fiber yields at an effect, at a fork, or at a park. The evaluator wraps values in more `IO` nodes than emitted code, so this rule is what keeps the deterministic schedule the same on both engines. Under simulation one step runs at most 1000000 structural nodes; more fails the probe like a zero-delay loop does.
- **Views are data at `Value`.** A `View.*` call evaluates to a description (`VView(kind, args)`) with its closures and signals inside. `Signal.*` calls are native signals. `Ui.run` hands the description to the host `Ref` the embedding program installs (`Eval.withHost`) and fails loud without one. The host walks the description and builds the native view, so the compiler package links without the UI runtime and a non-UI package (`scuzz`, `examples/codegen`) evaluates UI code up to the mount. Only a `[ui]` package (Docs) mounts.
- **One kit table.** `Kits.scuzz` is the one list of builtins. The evaluator dispatches by kit name. A kit without an evaluator case fails the compiler's own verification. Kits are native runtime calls in both engines.
- **Checked input only.** The evaluator runs after `check` passes. Values carry runtime tags. Generics need no instantiation. Traits dispatch on the receiver tag.
- **Erasure matches live builds.** `.require`, `where`, and `Property.sometimes` erase in `eval` and `run`. They stay active under `fuzz`.
- **Tail calls.** A self tail call runs in constant evaluator stack, as emit does.
- **Fail loud.** An unsupported construct or kit stops evaluation with a Scuzz file and line. The evaluator does not guess.

### GC (v0)

libc `malloc`/`free` through `sz_alloc` / `sz_free`. No collector. Heap values are reference-counted. Constructor patterns borrow fields while an arm runs. An arm retains a borrowed heap result before it returns. A match releases its owned temporary after the arm produces its result. A tail-recursive match retains the next arguments before it releases the temporary and starts the next iteration. A callback retains a borrowed field that it returns. Immutable data forms no cycles.

### Skia

No vendored Skia tree. Thin `sk_capi` (measure + draw). **Default UI backend** is the pinned Skia CPU prebuilt. `SCUZZ_SKIA=sk_sw` is the explicit opt-out. `SCUZZ_SKIA=gpu` paints with `sk_sw` and presents through OpenGL. Impeller / Skia GPU raster stay deferred. Callers depend only on `sk_capi.h`. Editor glyphs use an embedded DejaVu Sans Mono face. `View.text` stays the proportional embedded sans.

### IO and impurity

One failure channel: `SzError` on `IO[T]`. Typed `E` on `IO` without environment `R`. Do not add `ZIO[R, E, A]`. Blessed kits only. No app-level `IO.delay`. No user FFI, `extern`, or plugins. Determinism and effect capture are not settled. Cooperative single-threaded fibers are the scheduler. `IO.race`, `IO.timeout`, and `IO.both` cancel the other child and resume after its finalizers run. Simulation is hermetic. No live sockets under sim. Persistent HTTP servers wait for new requests until cancellation in live and simulation runtimes. One Net API uses shared request checks, timeline events, and simulation dispatch. CLI and server HTTP use the HTTP/1.0 transport with OpenSSL. iOS and macOS GUI HTTP use URLSession and platform certificate trust. GUI clients verify loopback certificates. Requests park fibers and cancel through IO finalizers. GUI transport preserves status responses and does not follow redirects. A response is `(Int, Map[String, String], String)`. Serve binds `0.0.0.0` and `::`. `Net.serveTls` and `Net.serveOnceTls` terminate TLS with a process cert. The CLI and server loopback `https://` client does not verify that cert. Do not expose POSIX sockets. Do not add an app transport API. `Clock.iso8601` formats UTC from epoch milliseconds. No general time parser. No time zone kit. `Str.matches` is POSIX ERE full-string match on UTF-8 bytes. `Str.capture` returns the first match as a list: the full match, then each group. An empty list means no match or a bad pattern. `Str.replaceMatch` replaces the first match with a literal string. It does not expand backreferences. An empty pattern copies the text. `Hash.sha256` returns lowercase hex of the SHA-256 of UTF-8 bytes. Software SHA-256. No OpenSSL. Hash.hmacSha256 computes HMAC-SHA-256. Hash.constantTimeEqual compares equal-length byte strings without an early exit. Length is public. No other digests. `Hex.encode` returns lowercase hex of UTF-8 bytes. `Hex.decode` reverses that encoding. Odd length or a bad digit yields the empty string. `Base64.encode` returns RFC 4648 of UTF-8 bytes. `Base64.decode` reverses that encoding. Bad length, digit, or pad yields the empty string. No URL-safe alphabet. `Uuid.v4` returns an RFC 4122 version-4 UUID as lowercase hex with hyphens. It uses the blessed Random stream. No parse. No other versions. `Bytes.fromStr` copies UTF-8 bytes. `Bytes.len` is the byte count. No Fs or Net Bytes. Kits: run `scuzz docs kits`. A panic must print a Scuzz file and line.

`Fs.write` replaces a regular file in one operation. The live runtime writes a temporary file in the same directory, checks write and close errors, then renames it over the destination. An error before replacement preserves the destination. New files use mode 0600. Replacement keeps the existing access permission bits. It does not preserve other inode metadata. The destination cannot be a symbolic link or a special file. This is atomic visibility, not a power-loss durability guarantee. Simulation applies the same complete-content replacement.

Live and simulated HTTP clients share one URL parser. A URL with no path uses `/`. The request target keeps the query and excludes the fragment. Raw spaces and control bytes fail before dispatch. Each HTTP client call takes a request header map after the URL. Body methods take the body last. A server request is `(String, String, Map[String, String], String)`: path, method, headers, and body. Request header names use lowercase in handlers. The runtime owns Host, Content-Length, Connection, and Transfer-Encoding. Client maps cannot set these fields. Invalid names, control bytes other than tab, duplicate names with different case, and maps over 16 KiB fail before network dispatch. Simulation carries app headers through the virtual server. Header values do not enter the effect log.

`Stream` is one finite pull interpreter. Bind a Stream with `=`. `<-` needs `IO`. Do not add backpressure, publishers, or a second stream kit.

### `Ui` vs `View`

| Layer | Role | Purity |
| --- | --- | --- |
| **`View`** | Widget tree | Sync/pure `build` |
| **`Ui` / `UiSession`** | `mount` / `pump` / `inject` / `snapshot` | Effectful (`UiRuntime`) |

Headless is a **peer** of Desktop/Mobile. Frame boundary is `pump`. A live loop paints when the session is dirty. It waits when nothing changes. Each pump still drains OS events, so a static frame receives clicks and close. World effects stay blessed `IO`. No UI feature without a Headless path. Nested declarative construction only. `Ui.run(_ => view)` is the session. Dump and inject ops: run `scuzz docs commands`.

**The tree owns views.** A `View` is not a reference-counted value. Its parent frees it. A `List[View]` holds views for `View.each`, which mounts the list at layout and frees the views it replaces. A `Signal[List[View]]` that drops a list before any `View.each` mounts it frees the views in that list, so two writes between layouts do not leak the middle list. A list another holder still reads keeps its views. Do not mount a view pulled out of a list signal by hand.

A tap closure runs its synchronous part in the tap. Its `IO` runs on the scheduler after the injected script. Claims see the synchronous part at the tap state and the `IO` result at the last state.

`scuzz run` carries the session channel on every runtime: the session watches `build/inject.json` and rewrites `build/debug.json`. `run --exec` plays a finite ops program after the first pump, then quiesces and exits. Without `--exec` a `[ui]` run stays live until a `quit` op or signal. `scuzz exec` writes ops to a live session. `scuzz package` strips the channel.

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
- A `for` `=` bind can be a constructor, tuple, or cons pattern. The bind stays pure. A pattern that does not match stops the program with `for binding does not match`.
- Interpolated strings use the same escape rules as ordinary strings. Decode escapes in literal segments once. Parse expressions inside interpolation braces as source. Live code and verification use the same rules.
- Optional `package`; top-level `def` / `private def` / `oracle` / `import`; `@main def …: IO[Unit]`
- Payload enums + `record` sugar + thin traits/`impl` (static dispatch) + monomorphized generics
- A generic def pins its own type parameters for every check in its body. `A` does not match `Int` or `String` there. Call sites still instantiate parameters. Kit argument checks pin the caller type after substitution. A bare list pins its element type when `List.at` on that list is checked against a concrete type. `List.cons`, `::`, `List.concat`, and `List.getOrElse` pin it from a concrete element or list argument. Later bindings in that for-comprehension use the pin. Other unbound kit parameters still match through `Type.eq`.
- Record field lookup substitutes the receiver type arguments into the declared field type. The same rule applies inside callbacks.
- Constructor patterns compare direct String, Int, and Bool literals before an arm runs. Named fields use their declared positions. A failed literal comparison tries the next arm. Constructor, tuple, cons, as, and `[]` patterns nest in constructor fields and tuple components.
- A constructor call builds that enum case when the case exists. A kit call with the same prefix stays a kit when the name is not a case.
- Literal alternatives support chains of String, Int, or Bool values. Test each alternative before the arm guard. String contents can include the alternative separator.
- Constructor alternatives check tags and direct literal fields. Alternatives with the same binding names and types share the selected payload values. Field positions can differ. `check` rejects alternatives whose binding names or types differ. Guards and IO assertions use these bindings.
- Closure captures keep their declared types. Generated closure names cannot collide with source bindings. A local binding of `self` does not add an implicit parameter.
- An unannotated lambda bound with `=` pins its parameter type at the first apply of that binding whose argument type is concrete, in the rest of the same for-comprehension. An alias bound with `=` to that binding, or to such an alias, uses the same pin. The body checks under the pin and gives the return type. Later applies must match the pin. Applies with unresolved arguments do not pin. A nested binding or lambda parameter with the same name stops the pin scan. Without a concrete apply the binding stays unresolved and a later concrete apply asks for an annotation. Generic def bodies pin their own type parameters, so the rule does not apply there.
- File-stem modules; enums namespaced by stem
- Blessed kits + `Signal` / `View` / `Ui` / `Property.*` / `.require`
- No macros, no implicits, no HKT beyond `IO`, no null

## Language direction

Expression-only dialect. **`for` is the kernel binder**. `=` aliases a pure value. `<-` sequences an effect. No `val`. No `var`. No statement blocks. `{ case … }` is a lambda, not a block. Surface sugar elaborates to a small core.

Scala **nouns**, Rust/Cargo **verbs**. No JVM packages. Direction: payload **enums** / **`record`** + thin **traits**. Monomorphize generics early. No classes. Path deps remain the unit of reuse. Do not add a package registry. Details: run `scuzz docs language`. Keep/cut: [`compatibility.md`](compatibility.md).

## Verification posture

App correctness is not classical unit tests. Prefer mutation, fuzzing, properties, simulation, and determinism. All are first-class in the language and in `scuzz`.

- **Claims** live in `*.scuzz_verify`. The author surface is `Timeline => Verdict`. Dump slot ids stay behind the algebra. `Timeline.fileTextIs` compares a file with expected text at a recorded state. It observes the hermetic filesystem. `Timeline.fileSame` compares one file at two recorded states. It compares all bytes. A file missing at both states is the same. A file missing at exactly one state is different. It does not assert that intermediate states are equal. Missing files and directories return false. `Timeline.fold(t, model, (model, i) => model)` walks every state in order and returns the final model. A model claim is a pure reference model in a record: the step function reads one state, compares it with the model, and records the first state index that leaves it. The record lives in a live module. A verify file holds defs and oracles only. The claim turns that record into a `Verdict`. `lastHitHas` and `driveHas` read the last hit and the last drive as levels at a state. `Timeline.hit` is true only on the state that recorded that hit. A later state keeps the level and does not record the hit again. Do not add a temporal-operator calculus. `check` validates `Timeline.driveHas` names against the driver registry. A `driveHas` antecedent must fire once per campaign with search iterations. `a11yHas` and `signalStrHas` names are dynamic. A never-claim whose antecedent stays false is a sanctioned idiom. An unknown signal slot fails the probe.
- **`Property.force`** returns the declared IO payload type in verification code. It supports scalar and heap values.
- **`.require`** and `Property.sometimes` stay in live function bodies. They erase from live builds. An IO assertion checks the completed payload. Match arms keep their payload types during assertion rewriting. IO continuations capture those payload fields and keep separate indices across arms and nested matches.
- **Drivers** live in one `*.scuzz_scenario`. They are impure, parameterized, and oracle-free. `check` rejects `Property.*` and `.require` in scenario files. The scenario declares its fault surface in a `faults` def: `fs`, `net`, or `queue` entries with an optional `:fail`, `:drop`, or `:corrupt` mode. A scenario without `faults` gets no injected faults. A claim must not pass on `faulted` alone.
- **Simulation is hermetic.** Fuzz, mutation, and TestRuntime keep impurity inside fakes. No live sockets. Scheduler ownership, not address, is the determinism boundary.
- **Live loopback replay.** `scuzz fuzz --live` replays the corpus (`--iterations 0`) or one `--replay` file on the compiled binary. Clock, files, random, and sys stay simulated. Net uses host loopback sockets. The OpenSSL HTTP/1.0 client serves `http` and `https`. A non-loopback host fails. Search and mutation stay on the simulated network. URLSession and Skia pixels have no fuzz home. `--differential` compares structural dumps.
- **ASan replay.** `scuzz fuzz --asan` replays the corpus (`--iterations 0`) or one `--replay` file on the compiled binary under AddressSanitizer. Leak and use-after-free reports fail the probe. Search and mutation stay off. A `[ui]` package is rejected. Linux ASan probes skip the 512 MiB virtual memory limit because AddressSanitizer needs a large virtual address space.
- **Probe limits.** Each probe has a 20-second deadline. Linux probes also have a 512 MiB virtual memory limit. Darwin has no `RLIMIT_AS`. `--asan` skips the Linux virtual memory limit. Simulation stops after 1000000 scheduler steps per IO run. A limit failure fails the probe. Process cancellation kills the shell and its process group.
- **One `scuzz fuzz`, two engines.** Search, mutation, and coverage run on the evaluator when the evaluator covers the package: `scuzz fuzz` spawns one `scuzz eval --probe DIR` server per prepared file set, the server checks the package once and forks a child per probe, and a mutant is a file set, not a link. Corpus replay, `--replay`, and `--relate` run the compiled binary. `scuzz fuzz --asan` relinks that compiled probe with AddressSanitizer. A search failure found on the evaluator replays compiled before the campaign ends; a difference fails the campaign. The idle probe is the gate: `scuzz fuzz` runs it on both engines first, and a timeline difference, a deadline, or a crash on the evaluator runs every probe compiled and prints why. A `[ui]` package runs the compiled path for every phase until the browser slice lands. `SCUZZ_FUZZ_ENGINE=compiled` forces the compiled path; it is the parity control, not an author knob. Search feedback runs on the evaluator only: every Int comparison under coverage reports `|a - b|` at its site through the coverage hit channel (`dist:<site>:<d>`), the probe keeps the smallest distance per site, and the search keeps the script that lowers one and nudges one Int driver argument inside its `where` by a power of two sized to that distance. The compiled control has no feedback, so a guided evaluator search can reach what the compiled search does not (`examples/reach`). `--iterations N` allocates five eighths of N to search, rounded down. Mutation uses the remaining allocation, up to the number of sites. Initial probes and corpus replay do not use this allocation. Small packages obey the same limit. `--iterations 0` is corpus-only. Mutation is a phase of that command. Mutation results persist per site in `.scuzz/mutate.results`, keyed by compiler SHA-256. A def whose body hash changed re-mutates first. A campaign without `[fuzz].score_floor` uses the floor 1.000. Search and corpus failures fail the campaign. Summaries count completed search iterations and keep corpus failures separate. --no-fail-fast cannot turn a corpus failure into a passing campaign. Catalog: run `scuzz docs verify`.
- **Generation.** Generated drive arguments cover boundary Ints: zero, one, neg one, the i64 edges, and the `where` bound edges. Size grows with the iteration. The string alphabet covers empty, quotes, newlines, delimiters, and non-ASCII. A string argument that needs it is a quoted token in the drive script. `where` parses as an expression: `&&` conjuncts of comparisons against Int literals give the bounds. A `where` outside that shape keeps the drive out of the workload.
- **Facts.** A zero-argument oracle is a fact. Facts seed the campaign. Search generates arguments only for oracles that take parameters. Generated-program round-trip and engine parity are the primary oracles for the compiler, the formatter, and the emitter. Facts stay seeds.

```text
src/
  Todo.scuzz              # live module: defs + where + .require + sometimes
todo.scuzz_scenario        # one world: setup, replacements, drivers
count.scuzz_verify        # Timeline => Verdict session claims and `oracle` drive oracles
```

One `*.scuzz_scenario` file per project that uses scenarios. Multiple named scenarios and generated setup stay later. Live `scuzz run` loads `*.scuzz` only. No free-floating `tests/` package roots. Direction beyond this: [`optimization.md`](optimization.md). Ranked gaps: [`gaps.md`](gaps.md).

## UI design language

Scuzz Style is the default UI design language. Use warm paper, dark text, square controls, and clear borders. Use yellow for primary actions. Use dark rust for accent text. Headless, Desktop, and Mobile use the same paint path. Color ratios do not prove full accessibility conformance.

`View.indexBook` groups named pages around a persistent index. Index Book stays a kit. The Docs walkthrough does not use it. The walkthrough uses `View.tabs` as a progress strip. Check and Search nest a second `View.tabs` for `Main.scuzz` and `count.scuzz_verify`. Nested local tabs do not change `#stage=id`. The editor uses unnumbered landmarks. It does not paint Index Book chapter numbers.

**Flutter-style constraints** (constraints down, sizes up). Nested constructors only. Do not drift into CSS-ish ad-hoc rules. Diagnose through structural dumps + `*.scuzz_verify` + `.require`. Widget catalog: run `scuzz docs kits`. GUI catalog: run `scuzz docs gui`.

GUI apps also target WebAssembly. Scuzz Docs is the first browser app. Full web accessibility stays later. Browser limits: [`compatibility.md`](compatibility.md#browser-target).
