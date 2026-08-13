# Scuzz Lang vision

Scuzz Lang is a **Flutter-shaped product** with a **Scala-inspired language**, not a Scala 3 / Scala Native / Maven citizen.

One doc for product intent, design locks, language direction, and open work. Keep/cut tables: [`compatibility.md`](compatibility.md). Manifest schema: [`schemas/scuzz-toml.md`](schemas/scuzz-toml.md). App path: [`guide.md`](guide.md). Future empirical pre-optimization: [`optimization.md`](optimization.md).

Edit this file when a decision or next-step ordering changes.

## Thesis

- **Language**: purposeful Scala-inspired subset for native UI, CLI, server, and mobile apps, with **built-in effect/IO/Streaming** (Cats Effect and FS2 spirit, not a cats/fs2 port). Aim: denser expr dialect (`for` as primary binder) — see [Language direction](#language-direction) below.
- **Runtime**: custom native (LLVM). Native binaries, not a VM. No JVM, no Java interop, no classpath/Maven.
- **UI**: a primary product path, not the only one (Dart-shaped: GUI is first-class; so are CLI and server). One design language + Skia, as a **`Ui` effect** with Headless/Window/Mobile interpreters. Headless is a product runtime (agents, CI), not a test-only shim.
- **Tooling**: one CLI (`scuzz`) for compile, link, assets, watch, packaging, deterministic `scuzz fuzz` over module **laws** + **sim** overlays. Static hygiene is `scuzz check` (format-verify + typecheck; further lints emit here, no `lint` subcommand). `scuzz fmt` rewrites.
- **Bootstrap**: self-host is a hard goal. Stage-0 (Rust) exists only to get there.
- **AI-Friendly**: Scuzz is meant to be read and written by LLMs. Headless, hot reload, and debugging tools are meant to aid agents; Headless is a peer runtime, `watch` only rebuilds, `[ui] run --watch` stamp-reloads Views, writes `build/debug.dump`, and plays `build/inject.script`.

Upstream Scala Native is a *reference*, not a dependency. Divergence is intentional.

## Defaults

| Topic | Choice |
| --- | --- |
| UI testing / CI | Laws via `scuzz fuzz` (primary); structural goldens as regression face; PNG optional via `--pixels` |
| Static hygiene | `scuzz check` (format-verify + typecheck; lints on this command; no `lint` subcommand). `scuzz fmt` rewrites |
| Codegen | LLVM IR |
| Renderer (v0) | Skia via thin C ABI; Impeller deferred |
| Build tool | DIY Mill/Cargo-like: `scuzz` (not sbt/Maven) |
| Effects | Language + runtime builtins |
| Impurity | All nondeterminism / external I/O through blessed `IO`; no app-level `IO.delay` escape hatch |
| Tests | Module **laws** + **sim** overlays; TestRuntime fakes for blessed kits; no app-level unit-test culture |
| Modules | `scuzz.toml` package = crate; `Foo.scuzz` = module (not JVM packages) |
| Self-host | Stage 0 → 1 → 2 on the critical path; **release ships Stage 2** |
| UI model | Pure `View` + effectful `Ui` session (`mount` / `pump` / `inject` / `snapshot`) |

## What Scuzz Lang is not

- Not Scala 3, not the JVM, not Scala.js
- Not a cats / cats-effect / Typelevel port
- Not SwiftUI / UIKit / WinUI wrappers
- Not “every widget rebuild is an `IO`” (`View` build stays sync/pure)
- Not imperative View trees (`View.addChild`); nested constructors only
- Not example-based unit-test culture (`src/test`, Mockito, assert-equal fixtures) for apps — **laws + fuzz + sim** instead
- Not Flutter DevTools / VM patching. In-process reload, a live structural dump, and stamp-driven inject are in; `watch` only rebuilds.
- Not an sbt / Gradle / `pubspec` plugin DSL (`scuzz.toml` is data)
- Not Flutter platform channels

## Success bars

**v0** — Install CLI → `scuzz new` (IO) or `scuzz new --ui` (Counter/Todo as `View` + builtin `IO`) → `scuzz test` and `scuzz run` (`--headless` for UI). Window when available. Language `Resource.make` / `use`, `Stream`, and `Net.serve` / `serveOnce` ship (`examples/resource`, `examples/stream`, `examples/server`).

**v1** — Stage-2 self-host as the shipped `scuzz` (`package_release.sh` / `install.sh`); Rust Stage-0 is CI/bootstrap only. Dual-boot gate: `scripts/selfhost.sh`.

## Decisions

### Product name

Brand in prose: **Scuzz Lang** (short form **Scuzz**). CLI / cargo package `scuzz`; Stage-0 crate `scuzz-compiler`; self-host tree `compiler-scuzz/`; manifest `scuzz.toml`; sources `*.scuzz` (plus stem-paired `*.scuzz_sim` / `*.scuzz_laws`); C ABI `sz_` / `Sz*` / `SZ_*`. No dual names or legacy aliases.

### Tooling

One CLI. One typer. No second analyze frontend, no `*.g.scuzz` codegen, no `src/test` runner.

- **Watch** rebuilds when sources or `scuzz.toml` change (path deps included). It does not patch running machine code. Session stamp-watch swaps the View tree without resetting Signals. `[ui]` `run --watch` keeps the process, writes `build/reload.stamp`, rewrites `build/debug.dump` (signals + a11y) on dirty pumps, and plays `build/inject.script` (`tap` / `text` / `pump`). Do not document `watch` as hot reload.
- **Static hygiene** is `scuzz check`: format-verify `src/` + typecheck (live + sim + laws). `scuzz fmt` rewrites; `fmt --check` remains the dry-run. Further lints emit on `check` (same JSON diagnostics). No `lint` subcommand.
- **JSON diagnostics** (`scuzz check --message-format=json`) are the editor protocol: `[{severity, message, file?, line?, column?}]`. `check` emits them; other commands stay human until they use this same type. LSP, when added, wraps `scuzz check --message-format=json` — do not grow a second typer or a parallel schema.
- **`scuzz.toml` is data** — package, path deps, `[ui]`. No plugin DSL, no `build.scuzz` hooks, no sbt-shaped settings. Unknown keys and extra top-level tables are rejected; do not add `[plugins]`.
- **Fingerprint** (Stage 0 incremental): miss → rebuild. No `scuzz clean` ritual. Cache stays fail-closed (compiler/runtime identity belongs in the key when Stage 2 grows incremental). Stage 2 rebuilds every compile today.
- **Missing tools:** fail on the first missing tool with one install line. No `flutter doctor` mega-checklist.
- **`scuzz package` shells** are copy-patched templates (`shells/android`, `shells/ios`), not a Gradle/CocoaPods API.

### GC (v0)

libc `malloc`/`free` via `sz_alloc` / `sz_free`. No moving collector yet. Clear ownership frees strings/IO/`Resource`/Views, unshared `Signal.list` cons spines, and list string heads on signal free; panic may leak. Revisit when long-lived interactive graphs demand it.

### Skia

No vendored Skia tree. Thin `sk_capi` (measure + draw). **Default UI backend** is the pinned Skia CPU prebuilt (`third_party/skia/PIN` → `scripts/fetch_skia.sh` / `SCUZZ_SKIA_URL`), linked when `third_party/skia/prebuilt/<triple>/libsk_capi.a` is present — fetch is fail-closed for supported triples. In-tree `sk_sw` remains an explicit opt-out (`SCUZZ_SKIA=sk_sw`) for offline/exotic hosts. As-needed rebuilds of the pin: `.github/workflows/skia-cpu.yml` + `scripts/build_skia_prebuilt.sh` (Linux x86_64 + macOS arm64). `PIN` uses a `{triple}` URL template so `package_release.sh` fetches the host-matching asset. Impeller deferred (GPU presenters later; must not change `Ui` session or logical goldens). Callers depend only on `sk_capi.h`.

### IO errors

One failure channel: `SzError` (message + optional code) on `IO`. Ops: `flatMap`, `delay`, `fail`, `handleErrorWith`, `attempt`, plus blessed kit (`Ref`, `Deferred`, `Queue`, `sleep`, `race`, `both`). **`Resource.make` / `use`** (String payload) brackets acquire/release as `IO` — cleanup on success and on `IO` failure (`examples/resource`; Stage 0 and self-host). **`Stream`** is a finite pull (FS2 spirit, not a port): `emit` / `emits` / `eval` / `concat` / `evalMap` / `compileToList` / `drain` (String payload; `examples/stream`; Stage 0 and self-host). **Net** is client `httpGet` plus `serveOnce` (one HTTP/1.0 GET) and `serve` (keep the listen socket; String body; `examples/server`; Stage 0 and self-host). Live listen, connection read/write, and `httpGet` park on `poll`. DNS `getaddrinfo` still blocks. TestRuntime queues injected paths. **Concurrency:** cooperative single-threaded fibers (live / default: FIFO ready queue, left-before-right fork; under `scuzz fuzz --iters`, `SCUZZ_SCHED_SEED` makes ready-fiber pick among n>1 seed-driven); `sleep` / empty `Queue.take` / incomplete `Deferred.get` / readable-fd poll park; TestRuntime advances virtual time to the next wakeup when idle; live idle wait uses `poll` (and interruptible `nanosleep` when only timers remain) so a cancelled sleeper or a ready listen socket cannot hold the run loop. No OS threads for IO. Impurity codes: Fs **2**, Sys (**3**; exec + `readLine`), Clock **4**, Random **5**, Net **6**. TestRuntime (`SCUZZ_TESTRT=1`) fakes clock/random/FS/net plus console (scripted stdin, optional argv override, println capture+echo). No checked exception hierarchy; panics abort via `sz_panic`.

### `Ui` vs `View`

| Layer | Role | Purity |
| --- | --- | --- |
| **`View`** | Widget tree | Sync/pure `build` |
| **`Ui` / `UiSession`** | `mount` / `pump` / `inject` / `snapshot` | Effectful (`UiRuntime`) |

Headless is a **peer** of Window/Mobile, not a test-only shim — product runtime for agents and CI. Frame boundary is `pump`. World effects stay blessed `IO`; bridge into signals via `sz_ui_bridge_post_*`. No UI feature without a Headless path. Taps: `View.button(label, _ => …)` first-class lambdas. Prefer `Signal.list` + `View.each` (framework rebuilds children at layout). Derived display: `Signal.map` + `View.bindText`. **View construction is nested and declarative** (`View.column(child, …)` / `View.row(…)` / `View.stack(…)`). Session `replace_root` swaps that tree without resetting Signals. `Ui.run(_ => view)` re-runs that construction (Stage 0 and self-host); `Ui.run(view)` stays.

### IO apps vs Headless

| Path | Meaning |
| --- | --- |
| **Headless** | `UiRuntime` peer — still `View` / Skia / structural dumps; product path for agents and CI |
| **IO-only** | No `[ui]`, no `Ui.run`; `scuzz run` just execs the `@main: IO[Unit]` binary |

IO-only is **not** a fourth runtime peer. Package contract: missing `[ui]` ⇒ Skia omitted from the app link, `scuzz test` runs `SCUZZ_TESTRT=1` exit-0 smoke (not a11y goldens), and `scuzz run` is plain exec. Console kit: `Sys.args`, `Sys.readLine`, `IO.println` (TestRuntime fakes stdin / optional argv / println capture). See `examples/hello`, `examples/cli`, impurity kits.

### Kernel dialect

Subset used by compiler sources and bootstrap examples. New features land in Stage 0 **before** `compiler-scuzz/` depends on them. Dual-boot gate: `scripts/selfhost.sh` — each stage smokes `examples/hello` + `examples/adt` + `examples/modules` + `examples/record` + `examples/trait` + `examples/generic` + `examples/genum` + `examples/resource` + `examples/stream` + `examples/server`, passes the counter/todo/nav Headless goldens, smokes `fuzz` on `examples/todo`, `fuzz --exhaust --depth 1` on `examples/counter`, and IO-only `fuzz` on `examples/concurrency`, and agrees with Stage 0 on `fmt --check` for the compiler sources; Stage 2 must re-emit byte-identical compiler IR (Stage-3 fixpoint).

- Optional `package`; top-level `def` / `private def` / `import Module.name` / `@main def …: IO[Unit]`; enums with N-field `Int`/`String`/`List`/ADT payloads (Stage 0 and self-host); **`record Name(f1: T1, …)`** as single-case enum sugar (`Name(args)` construct, `case Name(binds)` match, **`p.x` field access**); thin **traits** / `impl` with static-dispatch methods (`examples/trait`); monomorphized generics on defs (N type params; `examples/generic`) and on **enums/records** (`enum Opt[T]:` / `record Box[T](x: T)`, instantiation via ctor args or expected type; `examples/genum`); compiler sources use payload ADTs end-to-end (`Tok` … `Expr`, `Type`/`TyRes`, codegen `Emit`/`EnvBind`); multi-binder `match` — `examples/adt` exercises nullary/unary/multi-field/List payloads plus enum-typed def signatures; multi-field packs as `List` in the ADT payload; file-stem modules with `private def` module-local and `import` for bare disambiguation (`examples/modules`); enums are namespaced by file stem (same bare enum name allowed in two modules; `import Module.EnumName` brings type + cases into bare `Enum.*` scope)
- **`for { binders } yield e`** as primary binder: `x = e` (pure), `x <- e` (effect; yield wraps with `IO.pure` when any `<-` is present). Nested `for` in `if` / lambda arms when multi-bind is needed.
- No `val` / statement blocks / `var` — expression dialect only.
- `if` / `match`; literals incl. list `[a,b,c]` and `s"…"`
- Types: `Unit`, `Int`, `String`, `Bool`, `List`, `IO[T]`, nominal enums
- Builtins: `Str.*`, `List.*`, Fs (`read` / `write` / `list` / `mkdirs` / `canonicalize`)/Sys (`args` / `readLine` / `exec` / `spawn` / `alive` / `getenv`)/Clock/Random/Net (`httpGet` / `serveOnce` / `serve`), `Ref.*` / `Queue.*` / `Deferred.*` (String payloads), `Resource.make` / `Resource.use` (String payload), `Stream.emit` / `emits` / `eval` / `concat` / `evalMap` / `compileToList` / `drain` (String payload), `Signal.*` (incl. `Signal.map`), `View.*` (nested `View.column`/`row`/`stack` children only; `View.each`, `View.bindText`, Column/Row `View.expanded`, `View.center`, `View.align`, `View.positioned`, `View.padding`, `View.sized`, `View.minSize`, `View.background`, `View.aspectRatio`, `View.fraction`), `Ui.run` (View or `_ => View` factory), Theme/Color, `Law.signalInt` / `Law.a11yHas` / `Law.assert` (residual under TestRuntime).
- `IO` kit + `.flatMap` / `.handleErrorWith` / `.attempt`; lambdas `_ =>` / `name =>` for taps
- No macros, no implicits, no HKT beyond `IO`, no null

## Language direction

Expression-only, effect-sequenced dialect — dense, deterministic, verification-friendly without becoming a proof assistant. **`for` is the kernel binder**; no `val`/statement blocks.

### Expr dialect

- **`for` as primary binder**: `x = e` (pure alias), `x <- e` (effect)
- **No statement blocks**, no `var`, no `val`
- Branch arms stay expressions; nested `for` when an arm needs names — don’t grow a second block grammar
- Surface sugar elaborates to a small core (`Let`, `FlatMap`, `match`, ADTs, `IO`); self-host and checkers target the core

App-shaped Counter:

```scala
@main def main: IO[Unit] =
  for {
    count = Signal.int(0)
    label = Signal.map(count, n => s"count = $n")
    _    <- Ui.run(_ => View.column(
              View.text("Counter"),
              View.bindText(label),
              View.row(View.button("+1", _ => Signal.set(count, Signal.get(count) + 1)))
            ))
  } yield ()
```

### Density

Clear-dense, not cryptic-dense: nested declarative `View`s (`View.column(child, …)` / `View.row(…)`), inference, single-expr forms, short update verbs, enums + match. Avoid implicits, deep HKT, “everything is `IO`,” and imperative `View.addChild`.

### Modules and source shape

Scala **nouns**, Rust/Cargo **verbs** — without JVM packages or Rust `struct`/`impl` as the primary story.

| Layer | Meaning |
| --- | --- |
| Package (`scuzz.toml`) | Dependency / link boundary (crate) |
| File module (`Todo.scuzz`) | Namespacing + visibility |
| Optional deeper `mod` tree | Only when a single file gets heavy |

Direction for data/interfaces (see also [`compatibility.md`](compatibility.md)): payload **enums** / **`record`** product types (single-case enum sugar) + thin **traits**-as-interfaces; monomorphize generics early. Stage 0 and self-host (`compiler-scuzz/`) emit N-field `Int`/`String`/`List`/ADT payload enums for user programs (multi-field as `List` in `sz_adt_payload`); `record Name(…)` is the named-field surface (`examples/record`). Thin traits (`trait Show: def show(): String` / `impl Show for Point: …` with `self` in bodies; call `p.show()`) monomorphize to mangled defs — no vtables (`examples/trait`). Stage 0 and self-host monomorphize generics on defs (N type params; `examples/generic`) and generic enums/records — `Type.App` through typecheck, elaboration writes instantiations onto construct/pattern nodes (ctor args first, else expected type at def-ret/if/match-arm/call-arg), mono clones `__gen_Name_T…` EnumDefs and erases `App` before codegen (`examples/genum`). Compiler sources use payload ADTs end-to-end (`Tok` … `Expr`, typed `Type`/`Emit` channels). No classes (mutable identity), no `var`, no classpath/`com.foo.bar` directories. Path deps remain the unit of reuse. Stage 0 and self-host namespace defs **and enums** by file stem (`examples/modules`); surface stays two-level (`Enum.Case` / type `Enum`). `private def` is module-local (default public); `import Module.name` brings a public def or enum into bare scope for the importing module (disambiguates colliding names; unique bare still works without import). No `pub` yet.

### Laws, simulation, and verification

App correctness is **laws** searched by `scuzz fuzz`, not example-based unit tests. Authors declare properties the program must obey under interaction; the toolchain typechecks them and residualizes checks for TestRuntime / fuzz. Compilation and testing share one declaration surface:

| Phase | Role |
| --- | --- |
| `scuzz check` | Format-verify `src/`; laws and sim overlays typecheck; laws are pure; sim bindings match live types/purity |
| `scuzz build` (sim graph) | Layer `*.scuzz_sim` over live defs; emit residual law checks (armed under TestRuntime / fuzz only) |
| `scuzz fuzz` | Search event scripts / IO schedules for law violations → `repro.toml` |
| Later (optional) | Prove trivial law fragments statically; leave the rest as search |

Direction beyond this (not current work): fuzz-verified `*.scuzz_tune` machine manifests — [`optimization.md`](optimization.md).

**File convention (stem-paired, no attribute tags):**

```text
src/
  Todo.scuzz              # live module
  Todo.scuzz_sim          # same names replace live defs under sim / fuzz
  Todo.scuzz_laws         # pure laws over signals / a11y dump / module vals
```

- Live/`scuzz run` loads `*.scuzz` only.
- Fuzz/test loads `Foo.scuzz`, then replaces same-named top-level defs from `Foo.scuzz_sim` (exactly one live and at most one sim binding per name; same type and purity), then arms `Foo.scuzz_laws`.
- No free-floating `tests/` package roots — only stem-paired overlays next to the module they constrain.
- Rename drift is an error: sim names without a live twin, or laws that reference unknown names, fail `check`.

**Simulation** is first-class at the **app policy / impurity edge**, not Mockito for every `def`:

- Prefer swapping values you own (API base URL, a `Backend` ADT, scripted fixtures) via `*.scuzz_sim`.
- Blessed kits (`Net`, `Fs`, `Clock`, …) keep one implementation; TestRuntime still fakes the wire under `SCUZZ_TESTRT=1`.
- Do not stub pure helpers, `View` builders, or `Signal` cells. Sim bodies must stay deterministic.

**Observation surface for laws:** signal store + View/a11y dump (and pure reads of module state) — not Skia pixels. Kernel/runtime (Stage 0 Rust, C runtime) keep ordinary example tests; the laws story is for **Scuzz apps**.

### Verification posture

Keep purity checkable (pure `A` vs `IO` vs session), total expr core, signals as an explicit store, immutable data by default, errors as values. Spec **laws over signal store + View/a11y dump**, not Skia pixels. Defer dependent types and runtime-heap proofs — residual laws + fuzz first; static proof only where cheap.

### `scuzz fuzz`

Deterministic TestRuntime + (for `[ui]`) Headless event scripts (plus sim overlays when present):

```text
(program + sim, seed/config, event script, schedule seed) → exit code + signal store + a11y view dump + law results
```

```bash
scuzz fuzz [--iters N] [--seed S]   # [ui]: scripts × schedules; IO-only: schedules
scuzz fuzz --exhaust --depth N      # [ui] bounded event search (FIFO schedule)
scuzz fuzz --replay repro.toml      # deterministic replay (events + optional schedule_seed)
```

Scripts are a line protocol — `tap <n>` / `text <s>` / `pump <k>` — played by the runtime (`SCUZZ_UI_SCRIPT`) across `pump` boundaries; on exit it writes the signal store + a11y view dump (`SCUZZ_FUZZ_DUMP`). The CLI probes the a11y dump for the typed event surface (buttons in scan order, text fields), generates seeded scripts (Lehmer/MINSTD LCG — the kernel dialect has no bitwise ops) or enumerates a finite alphabet under `--exhaust` (`tap <i>` for each button, `text` / `text a` when a field exists, `pump 1`), and writes `repro.toml` on failure. `--iters` also sets `SCUZZ_SCHED_SEED` (recorded as `schedule_seed` in `repro.toml`); live runs and `--exhaust` stay FIFO. IO-only packages search schedule seeds only (no event scripts); `--exhaust` requires `[ui]`. Exhaustive mode walks lengths `1..N` in stable order so shorter counterexamples win. `fuzz` lives in the Stage-1/2 CLI (not Stage 0); replay plays recorded events verbatim and restores `schedule_seed` when present. **Oracles:** module **laws** first (residual `Law.assert` under `SCUZZ_TESTRT=1`); panic/`SzError` exit still fails; structural dumps aid diagnosis (PNG last). Requires stable tap order, `pump` as time, no hidden nondeterminism.

### Layout model

When the widget set grows beyond column/row: **Flutter-style constraints** (constraints down, sizes up). `View.expanded(child)` takes leftover height in a Column or leftover width in a Row; `View.center(child)` fills the max slot and centers the child; `View.align(ax, ay, child)` places the child (`0` start / `1` center / `2` end on each axis); `View.stack(…)` overlays children (loose size to largest); `View.positioned(x, y, child)` offsets a Stack child from the stack origin; `View.padding(n, child)` deflates max (and min) constraints by a uniform inset; `View.sized(w, h, child)` is a tight w×h slot (clamped to incoming max); `View.minSize(w, h, child)` raises min width/height (`0` = no floor on that axis; clamped to incoming max); `View.background(color, child)` paints `color` and sizes to the child (constraints pass through); `View.aspectRatio(rw, rh, child)` is the largest `rw:rh` box that fits incoming max (child laid out in that tight slot); `View.fraction(wpct, hpct, child)` takes that percent of incoming max (`0` = size to child on that axis). Do not drift into CSS-ish ad-hoc layout rules. Do not grow Flutter-style constraint-overflow dumps; diagnose via structural dumps + laws.

### UI testing

**Laws + `scuzz fuzz`** are the primary correctness story for `[ui]` apps. **Structural goldens** are checked-in Headless dumps (signal store + a11y tree) compared byte-for-byte by `scuzz test` — a regression face for silent shape drift, few, live graph, not a substitute for laws. PNG pixels stay optional (`scuzz test --pixels`). IO packages (no `[ui]`): laws + sim overlays under TestRuntime when present; `scuzz fuzz --iters` searches schedule seeds; otherwise `scuzz test` stays compile + `SCUZZ_TESTRT=1` exit-0 smoke. Tooling: `scuzz check` (format-verify `src/` + typecheck live + sim + laws); `--message-format=json` on `check` is the editor protocol (LSP wraps it). Further lints emit on `check`; `scuzz fmt` rewrites. No `lint` subcommand.

## Open work

Unknowns and known gaps, ranked by risk: [`gaps.md`](gaps.md). Open unknowns: device Mobile (blocked on NDK/Xcode), GPU presenters. Near-term residual: poll Sys.readLine (DNS `getaddrinfo` still blocks).

App authors: [`guide.md`](guide.md). Vertical slices over breadth; no Window-only UI features. UI is a primary path among CLI/server/desktop/mobile — not the only v0 bar.

## Risks

| Risk | Mitigation |
| --- | --- |
| Language + UI + tooling is huge | Ruthless subset; vertical slices; Counter before generality |
| Self-host / dialect drift | Kernel section above; port compiler early; Stage 0/1/2 CI |
| Effects too weak or too heavy | Builtin IO; pure `View`; `Ui` at session boundary |
| Hidden nondeterminism | Closed impurity + TestRuntime + deterministic `*.scuzz_sim` |
| Laws become brittle dump goldens | Laws talk to named module/signal surface; strict sim/live pairing in `check` |
| Sim becomes Mockito | Only top-level same-name overlays; no stubbing pure `View`/`Signal`; kits stay TestRuntime |
| “Almost Scala” confusion | Explicit non-goals; language direction above; [guide.md](guide.md) |
| Watch sold as hot reload | `watch` rebuilds; `[ui]` `run --watch` stamp-reloads Views, writes `build/debug.dump`, plays `build/inject.script`; no new machine code |
| IDE typer ≠ batch typer | One JSON schema; LSP wraps `scuzz check` |
| Skia weight | pinned CPU prebuilt default; `sk_sw` opt-out (`SCUZZ_SKIA=sk_sw`) |
| Window-only features | Headless peer rule |
| Treating UI as the only product | UI is a primary path (Dart-shaped); CLI/server/desktop/mobile are peers; native binaries, not a VM |
| GC vs frame budget | `pump` boundary; measure |
| Mobile packaging | Host Mobile peer first; device toolchains later |
