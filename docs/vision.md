# Scuzz Lang vision

Scuzz Lang is a **Flutter-shaped product** with a **Scala-inspired language**, not a Scala 3 / Scala Native / Maven citizen.

One doc for product intent, design locks, language direction, and open work. Keep/cut tables: [`compatibility.md`](compatibility.md). Manifest schema: [`schemas/scuzz-toml.md`](schemas/scuzz-toml.md). App path / surface catalogs: [`guide.md`](guide.md). Future empirical pre-optimization: [`optimization.md`](optimization.md).

Edit this file when a decision or next-step ordering changes.

## Thesis

- **Language**: purposeful Scala-inspired subset for native UI, CLI, server, and mobile apps, with **built-in effect/IO/Streaming** (ZIO-inspired, not a ZIO or cats/fs2 port). Aim: denser expr dialect (`for` as primary binder) — see [Language direction](#language-direction).
- **Runtime**: custom native (LLVM). Native binaries, not a VM. No JVM, no Java interop, no classpath/Maven.
- **UI**: a primary product path, not the only one (Dart-shaped: GUI is first-class; so are CLI and server). One design language + Skia, as a **`Ui` effect** with Headless/Window/Mobile interpreters. Headless is a product runtime (agents, CI), not a test-only shim.
- **Tooling**: one opinionated CLI (`scuzz`) — compile, link, assets, watch, packaging, format, check, and the whole verification stack. One formatter, one check surface, one testing strategy. Batteries-included: mutation, fuzzing, property/laws, simulation, and determinism are **first-class in the language and `scuzz`**, not a third-party harness sprawl. Static hygiene is `scuzz check` (format-verify + typecheck; further lints emit here, no `lint` subcommand). `scuzz fmt` rewrites.
- **Bootstrap**: self-host is a hard goal. Stage-0 (Rust) exists only to get there.
- **AI-Friendly**: Headless, hot reload, and debugging tools aid agents. Headless is a peer runtime; `watch` only rebuilds; `[ui] run --watch` stamp-reloads Views, writes `build/debug.dump` (including `[taps]` / `[fields]` live strings / `[scrolls]` and live `View.bindText`), and plays `build/inject.script`. `[ui]` build emits `build/reload.dylib`; stamp-watch `dlopen`s it so a source View-label change appears live (Signals stay).

Upstream Scala Native is a *reference*, not a dependency. Divergence is intentional.

## Defaults

| Topic | Choice |
| --- | --- |
| UI testing / CI | In-source laws + composed drivers via `scuzz fuzz` (primary); mutation as law-strength gate; structural goldens as regression face; PNG optional via `--pixels` |
| Static hygiene | `scuzz check` (format-verify + typecheck; lints on this command; no `lint` subcommand). `scuzz fmt` rewrites |
| Codegen | LLVM IR |
| Renderer (v0) | Skia via thin C ABI; Impeller deferred |
| Build tool | DIY Mill/Cargo-like: `scuzz` (not sbt/Maven) |
| Effects | Language + runtime builtins |
| Impurity | All nondeterminism / external I/O through blessed `IO`; no app-level `IO.delay` escape hatch |
| Tests | One built-in strategy: **mutation + fuzz + laws (property) + sim + determinism** (TestRuntime). Oracles live **in source** (laws, inline checks, refinements); **drivers** are oracle-free workloads the fuzzer composes. No classical unit-test culture, no external test frameworks |
| Modules | `scuzz.toml` package = crate; `Foo.scuzz` = module (not JVM packages) |
| Self-host | Stage 0 → 1 → 2 on the critical path; **release ships Stage 2** |
| UI model | Pure `View` + effectful `Ui` session (`mount` / `pump` / `inject` / `snapshot`) |

## What Scuzz Lang is not

- Not Scala 3, not the JVM, not Scala.js
- Not a ZIO library port
- Not a cats / cats-effect / Typelevel port
- Not SwiftUI / UIKit / WinUI wrappers
- Not “every widget rebuild is an `IO`” (`View` build stays sync/pure)
- Not imperative View trees (`View.addChild`); nested constructors only
- Not classical / example-based unit-test culture (`src/test`, Mockito, assert-equal fixtures, third-party test runners) for apps — **mutation + fuzz + laws + sim + determinism**, all in `scuzz`, instead. Examples survive only as oracle-free **drivers**; the objection is example-based *oracles*, not examples-as-workloads
- Not Flutter DevTools / VM patching. In-process reload, a live structural dump, and stamp-driven inject are in; `watch` only rebuilds.
- Not an sbt / Gradle / `pubspec` plugin DSL (`scuzz.toml` is data)
- Not Flutter platform channels

## Success bars

**v0** — Install CLI (`curl …/install.sh | sh`, or checkout `./scripts/install.sh`) → `scuzz new` (IO) or `scuzz new --ui` (Counter/Todo as `View` + builtin `IO`) → `scuzz test` and `scuzz run` (`--headless` for UI). Window when available. Language `Resource` / `Stream` / `Net.serve` ship (`examples/resource`, `examples/stream`, `examples/server`).

**v1** — Stage-2 self-host as the shipped `scuzz` (GitHub Releases; `package_release.sh` / `install.sh`); Rust Stage-0 is CI/bootstrap only. Dual-boot gate: `scripts/selfhost.sh`.

## Decisions

### Product name

Brand in prose: **Scuzz Lang** (short form **Scuzz**). CLI / cargo package `scuzz`; Stage-0 crate `scuzz-compiler`; self-host tree `compiler-scuzz/`; manifest `scuzz.toml`; sources `*.scuzz` (plus stem-paired `*.scuzz_sim` / `*.scuzz_drivers`); C ABI `sz_` / `Sz*` / `SZ_*`. No dual names or legacy aliases.

### Tooling

One CLI. One typer. One formatter. One check surface. One testing strategy. No second analyze frontend, no `*.g.scuzz` codegen, no `src/test` runner, no bolted-on mutation/fuzz/property ecosystems.

- **Watch** rebuilds when sources or `scuzz.toml` change. It does not patch running machine code. `[ui]` `run --watch` recompiles `build/reload.dylib`, stamps, and swaps the View tree without resetting Signals (see [`guide.md`](guide.md)). Do not document `watch` as hot reload.
- **Static hygiene** is `scuzz check`; `scuzz fmt` rewrites. No `lint` subcommand.
- **Verification** is batteries-included in `scuzz` and the language (laws, sim overlays, deterministic TestRuntime, fuzz search, mutation) — not optional crates or Maven/npm test plugins.
- **JSON diagnostics** (`scuzz check --message-format=json`) are the editor protocol. LSP, when added, wraps that — do not grow a second typer or schema.
- **`scuzz.toml` is data** — package, path deps, `[ui]`. No plugin DSL, no `build.scuzz` hooks. Unknown keys rejected; do not add `[plugins]`.
- **Fingerprint** (Stage 0 incremental): miss → rebuild. No `scuzz clean` ritual. Stage 2 rebuilds every compile today.
- **Missing tools:** fail on the first missing tool with one install line. No `flutter doctor` mega-checklist.
- **`scuzz package` shells** are copy-patched templates (`shells/android`, `shells/ios`), not a Gradle/CocoaPods API.

### GC (v0)

libc `malloc`/`free` via `sz_alloc` / `sz_free`. No moving collector yet. Clear ownership frees strings/IO/`Resource`/Views, unshared `Signal.list` cons spines, and list string heads on signal free; panic may leak. Revisit when long-lived interactive graphs demand it.

### Skia

No vendored Skia tree. Thin `sk_capi` (measure + draw). **Default UI backend** is the pinned Skia CPU prebuilt (`third_party/skia/PIN` → `scripts/fetch_skia.sh`); in-tree `sk_sw` is the explicit opt-out (`SCUZZ_SKIA=sk_sw`). Impeller deferred (GPU presenters later; must not change `Ui` session or logical goldens). Callers depend only on `sk_capi.h`.

### IO and impurity

One failure channel: `SzError` on `IO`. Blessed kits only — no app-level `IO.delay`. Cooperative single-threaded fibers (no OS threads for IO); `sleep` / empty `Queue.take` / incomplete `Deferred.get` / fd poll park. Cancel (race loser / `IO.timeout` / `Fiber.interrupt`) runs `IO.ensure` / `Resource` finalizers. `Fiber.fork` starts a supervised child (`join` parks; unjoined children cancel when the root completes). `IO.timeout(ms, inner)` is a blessed race of sleep-fail vs inner and keeps inner's `IO[T]`. `IO.forever(inner)` reruns until failure or cancel (`IO[Unit]`, never succeeds). `IO.repeatN(n, inner)` runs inner once plus `n` extra times (last success). `IO.retryN(n, inner)` retries on failure up to `n` extra times. TestRuntime (`SCUZZ_TESTRT=1`) fakes clock/random/FS/net/console. Surface catalogs: [`guide.md`](guide.md). Panics abort via `sz_panic`.

### `Ui` vs `View`

| Layer | Role | Purity |
| --- | --- | --- |
| **`View`** | Widget tree | Sync/pure `build` |
| **`Ui` / `UiSession`** | `mount` / `pump` / `inject` / `snapshot` | Effectful (`UiRuntime`) |

Headless is a **peer** of Window/Mobile — product runtime for agents and CI. Frame boundary is `pump`. World effects stay blessed `IO`; bridge into signals via `sz_ui_bridge_post_*`. No UI feature without a Headless path. Prefer `Signal.list` + `View.each`; derived display via `Signal.map` + `View.bindText`. Nested declarative construction only. `Ui.run(_ => view)` re-runs construction on stamp-watch; `Ui.run(view)` stays.

### IO apps vs Headless

| Path | Meaning |
| --- | --- |
| **Headless** | `UiRuntime` peer — still `View` / Skia / structural dumps |
| **IO-only** | No `[ui]`, no `Ui.run`; plain `@main: IO[Unit]` exec |

Missing `[ui]` ⇒ Skia omitted from the link; `scuzz test` is TESTRT exit-0 smoke (not a11y goldens). IO-only is **not** a fourth runtime peer.

### Kernel dialect

Subset used by compiler sources and bootstrap examples. New features land in Stage 0 **before** `compiler-scuzz/` depends on them. Dual-boot gate: `scripts/selfhost.sh` (smoke examples, Headless goldens, fuzz, fmt parity, Stage-2 IR fixpoint).

Locks (not an API catalog — see [`guide.md`](guide.md)):

- Expression dialect only: `for` primary binder (`=` pure, `<-` effect); no `val` / statement blocks / `var`
- Optional `package`; top-level `def` / `private def` / `import`; `@main def …: IO[Unit]`
- Payload enums + `record` sugar (methods on generic records and enums) + thin traits/`impl` (static dispatch, including generic records and enums, with impl methods that mention `T`) + monomorphized generics on defs/enums/records
- File-stem modules; enums namespaced by stem; `import Module.name` for bare disambiguation
- Types: `Unit`, `Int`, `String`, `Bool`, `List`, `IO[T]`, nominal enums
- Blessed kits + `Signal` / `View` / `Ui` / `Law.*` / `.require` as documented in the guide
- No macros, no implicits, no HKT beyond `IO`, no null

## Language direction

Expression-only, effect-sequenced dialect — dense, deterministic, verification-friendly without becoming a proof assistant. **`for` is the kernel binder**; no `val`/statement blocks.

### Expr dialect

- **`for` as primary binder**: `x = e` (pure alias), `x <- e` (effect)
- **No statement blocks**, no `var`, no `val`
- Branch arms stay expressions; nested `for` when an arm needs names
- Surface sugar elaborates to a small core (`Let`, `FlatMap`, `match`, ADTs, `IO`); self-host and checkers target the core

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

Clear-dense, not cryptic-dense: nested declarative `View`s, inference, single-expr forms, short update verbs, enums + match. Avoid implicits, deep HKT, “everything is `IO`,” and imperative `View.addChild`.

### Modules and source shape

Scala **nouns**, Rust/Cargo **verbs** — without JVM packages or Rust `struct`/`impl` as the primary story.

| Layer | Meaning |
| --- | --- |
| Package (`scuzz.toml`) | Dependency / link boundary (crate) |
| File module (`Todo.scuzz`) | Namespacing + visibility |
| Optional deeper `mod` tree | Only when a single file gets heavy |

Direction: payload **enums** / **`record`** + thin **traits**-as-interfaces; monomorphize generics early. No classes (mutable identity), no `var`, no classpath packages. Path deps remain the unit of reuse. `private def` is module-local (default public). No `pub` yet. Details and examples: [`guide.md`](guide.md), keep/cut: [`compatibility.md`](compatibility.md).

### Laws, simulation, mutation, and verification

App correctness is **not** classical unit tests. Prefer **mutation, fuzzing, property-oriented laws, simulation, and determinism** — all first-class in the language and `scuzz` tooling. The split is **oracles in source, drivers as the test surface**:

- **Oracles live in the live module.** Top-level `law` declarations (pure, nullary `Bool` predicates), explicit `.require(pred)` on values / `IO` (type-preserving; residual `Law.check` / sequenced `Law.assert` under verify), reachability `Law.sometimes(name)`, and `where` refinements on `def` params and `record` fields. All erase from live builds; armed under TestRuntime / fuzz / mutation. Application is at the call site via `.require`.
- **Drivers (`*.scuzz_drivers`) do things.** Impure, parameterized, oracle-free steps; `scuzz fuzz` composes them (generated args, random order / interleaving) alongside the UI event alphabet. `check` rejects `Law.*` and `.require` in driver files — an assert inside a driver is a unit test in a costume.
- **`Law.sometimes` keeps composition honest.** Reachability accumulates across a fuzz *campaign*; declared-but-never-reached states fail the campaign, so oracle-free drivers cannot pass vacuously. It is a coverage/fitness signal for corpus guidance, alongside Headless dump novelty. It is a path marker (`Unit`), not a value method.
- **Mutation pressures the oracles.** Surviving mutants mean weak laws/refinements.

| Phase | Role |
| --- | --- |
| `scuzz check` | Format-verify `src/`; typecheck live + laws/refinements + sim + drivers; laws pure, drivers `IO`, no oracles in drivers; every `law` must appear in a `.require`; sim bindings match live types/purity |
| `scuzz build` (verify graph) | Layer `*.scuzz_sim` over live defs; compile drivers; residualize `.require` / `where` checks (armed under TestRuntime / fuzz / mutation only) |
| `scuzz fuzz` | Compose drivers + event scripts / IO schedules. Per-run oracles: `.require` residuals, refinements, panic/`SzError`. Per-campaign oracle: `Law.sometimes` reachability → `repro.toml` |
| `scuzz mutate` | Negate residual `Law.check` / `Law.assert` / `.require`; flip relational/boolean ops, swap `+`/`-`, `*`/`/`, and `%`→`*`, drop `&&` conjuncts, and `0`↔`1` inside those predicates; idle probe plus `--iters` fuzz; surviving mutants mean weak or unreached oracles |
| Later (optional) | Discharge trivial law/refinement fragments statically; leave the rest as search |

Direction beyond this (not current work): fuzz-verified `*.scuzz_tune` — [`optimization.md`](optimization.md). Pivot slices and status: [`gaps.md`](gaps.md).

**File convention (stem-paired, no attribute tags):**

```text
src/
  Todo.scuzz              # live module: defs + laws + refinements (all oracles)
  Todo.scuzz_sim          # same names replace live defs under sim / fuzz / mutation
  Todo.scuzz_drivers      # oracle-free workloads composed by scuzz fuzz
```

- Live/`scuzz run` loads `*.scuzz` only; laws, `.require`, inline checks, and refinements erase.
- No separate laws file: oracles belong next to the code they constrain, in `*.scuzz`.
- Fuzz / mutation / test layers sim, then arms oracles. Rename drift fails `check`.
- No free-floating `tests/` package roots; no third-party test or mutation frameworks.
- Prefer swapping values you own via `*.scuzz_sim`; blessed kits stay one implementation + TestRuntime fakes. Do not stub pure helpers, `View` builders, or `Signal` cells.
- Driver params stay generator-friendly (`Int` / `String` / `Bool`); drivers may call live defs but never assert correctness.
- Observation surface: signal store + View/a11y dump — not Skia pixels. Kernel/runtime keep ordinary example tests; this strategy is for **Scuzz apps**.

### Verification posture

Keep purity checkable (pure `A` vs `IO` vs session), total expr core, signals as an explicit store, immutable data by default, errors as values. Refinements attach to **positions** (`def` params, `record` fields), not a refined-type algebra: checked dynamically at call / construction under the verify graph, erased live. `.require(pred)` attaches an oracle to a **value** (or `IO[A]` sequenced after the effect) while preserving the receiver type — even when `pred` is `IO[Bool]`. They are deliberately the fragment that migrates to static discharge later with no author rewrite. Defer dependent types and runtime-heap proofs — residual oracles + fuzz + mutation first; static proof only where cheap.

### `scuzz fuzz`

Deterministic TestRuntime + (for `[ui]`) Headless event scripts (plus sim overlays when present). The fuzz alphabet is the typed event surface (buttons, text fields) **plus declared drivers** (`drive <name> [args]` extends the script line protocol; the verify build publishes the driver table alongside the a11y dump). Oracles: in-source **laws/refinements** first; panic/`SzError` still fails; `Law.sometimes` reachability judges the campaign; structural dumps aid diagnosis (PNG last). `repro.toml` records events + driver invocations verbatim, so replay is generator-independent. Requires stable tap order, `pump` as time, no hidden nondeterminism. Determinism makes any failing prefix replayable. Seeded `--iters` keeps `[ui]` prefixes that hit new `Law.sometimes` names or a new Headless `dump.txt`, and IO-only schedule seeds that hit new sometimes names, then extends/perturbs them (CLI-only; no runtime machinery). Flags, script verbs, and schedule seeds: [`guide.md`](guide.md). `fuzz` lives in the Stage-1/2 CLI (not Stage 0).

### Layout model

**Flutter-style constraints** (constraints down, sizes up). Do not drift into CSS-ish ad-hoc rules. Do not grow Flutter-style constraint-overflow dumps; diagnose via structural dumps + laws. Shipped widgets (`column` / `row` / `stack` / `expanded` / `center` / `align` / `positioned` / `padding` / `sized` / `minSize` / `background` / `aspectRatio` / `fraction`, …): [`guide.md`](guide.md).

### UI testing

**In-source laws + composed drivers via `scuzz fuzz` + built-in mutation** are primary for `[ui]` apps. **Structural goldens** (Headless signal + a11y dumps) are a regression face — few, live graph, not a substitute for laws. PNG optional (`scuzz test --pixels`). IO packages: laws + drivers + sim under TestRuntime when present; otherwise compile + TESTRT exit-0 smoke.

## Open work

Unknowns and known gaps: [`gaps.md`](gaps.md). Next slices: generic traits — [`plans.md`](plans.md). Open unknowns: device Mobile (blocked on NDK/Xcode), GPU presenters.

App authors: [`guide.md`](guide.md). Vertical slices over breadth; no Window-only UI features. UI is a primary path among CLI/server/desktop/mobile — not the only v0 bar.

## Risks

| Risk | Mitigation |
| --- | --- |
| Language + UI + tooling is huge | Ruthless subset; vertical slices; Counter before generality |
| Self-host / dialect drift | Kernel section above; port compiler early; Stage 0/1/2 CI |
| Effects too weak or too heavy | Builtin IO; pure `View`; `Ui` at session boundary |
| Hidden nondeterminism | Closed impurity + TestRuntime + deterministic `*.scuzz_sim` |
| Laws become brittle dump goldens | Laws talk to named module/signal surface; strict sim/live pairing in `check`; mutation kills weak oracles |
| Sim becomes Mockito | Only top-level same-name overlays; no stubbing pure `View`/`Signal`; kits stay TestRuntime |
| Drivers become integration tests | `check` rejects `Law.*` in driver files; correctness lives only in live-module oracles |
| Drivers pass vacuously | `Law.sometimes` reachability fails the campaign when declared states are never reached |
| Verification tool sprawl | One `scuzz` strategy — mutation/fuzz/laws/sim/determinism in-tree; no external test frameworks |
| “Almost Scala” confusion | Explicit non-goals; language direction above; [guide.md](guide.md) |
| Watch sold as hot reload | `watch` rebuilds; `[ui]` `run --watch` recompiles `build/reload.dylib` then stamp-reloads Views |
| IDE typer ≠ batch typer | One JSON schema; LSP wraps `scuzz check` |
| Skia weight | pinned CPU prebuilt default; `sk_sw` opt-out |
| Window-only features | Headless peer rule |
| Treating UI as the only product | UI is a primary path (Dart-shaped); CLI/server/desktop/mobile are peers |
| GC vs frame budget | `pump` boundary; measure |
| Mobile packaging | Host Mobile peer first; device toolchains later |
