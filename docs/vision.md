# Scuzz Lang vision

Scuzz Lang is a **Flutter-shaped product** with a **Scala-inspired language**, not a Scala 3 / Scala Native / Maven citizen.

One doc for product intent, design locks, language direction, and open work. Keep/cut tables: [`compatibility.md`](compatibility.md). Manifest schema: [`schemas/scuzz-toml.md`](schemas/scuzz-toml.md). App path: [`guide.md`](guide.md).

Edit this file when a decision or next-step ordering changes.

## Thesis

- **Language**: purposeful Scala-inspired subset for UI apps, native CLI/server-shaped `IO` programs, and native codegen, with **built-in effect/IO** (Cats Effect spirit, not a cats port). Aim: denser expr dialect (`for` as primary binder) — see [Language direction](#language-direction) below.
- **Runtime**: custom native (LLVM). No JVM, no Java interop, no classpath/Maven.
- **UI**: primary product face — one design language + Skia, as a **`Ui` effect** with Headless/Window/Mobile interpreters.
- **Tooling**: one CLI (`scuzz`) for compile, link, assets, hot reload, packaging, deterministic `scuzz fuzz` over module **laws** + **sim** overlays.
- **Bootstrap**: self-host is a hard goal. Stage-0 (Rust) exists only to get there.
- **AI-Friendly**: Scuzz is meant to be read and written by LLMs.

Upstream Scala Native is a *reference*, not a dependency. Divergence is intentional.

## Defaults

| Topic | Choice |
| --- | --- |
| UI testing / CI | Laws via `scuzz fuzz` (primary); structural goldens as regression face; PNG optional via `--pixels` |
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
- Not example-based unit-test culture (`src/test`, Mockito, assert-equal fixtures) for apps — **laws + fuzz + sim** instead

## Success bars

**v0** — Install CLI → `scuzz new --ui` → Counter/Todo as `View` + builtin `IO` → `scuzz test` (Headless) and `scuzz run --headless` → `scuzz run` opens a window when available. IO-only path: `scuzz new` (no `--ui`) → `scuzz test` (TESTRT smoke) → `scuzz run`.

**v1** — Stage-2 self-host as the shipped `scuzz` (`package_release.sh` / `install.sh`); Rust Stage-0 is CI/bootstrap only. Dual-boot gate: `scripts/selfhost.sh`.

## Decisions

### Product name

Brand in prose: **Scuzz Lang** (short form **Scuzz**). CLI / cargo package `scuzz`; Stage-0 crate `scuzz-compiler`; self-host tree `compiler-scuzz/`; manifest `scuzz.toml`; sources `*.scuzz` (plus stem-paired `*.scuzz_sim` / `*.scuzz_laws`); C ABI `sz_` / `Sz*` / `SZ_*`. No dual names or legacy aliases.

### GC (v0)

libc `malloc`/`free` via `sz_alloc` / `sz_free`. No moving collector yet. Clear ownership frees strings/IO/`Resource`/Views; panic may leak. Revisit when long-lived interactive graphs demand it.

### Skia

No vendored Skia tree. Thin `sk_capi` (measure + draw). **Default UI backend** is the pinned Skia CPU prebuilt (`third_party/skia/PIN` → `scripts/fetch_skia.sh` / `SCUZZ_SKIA_URL`), linked when `third_party/skia/prebuilt/<triple>/libsk_capi.a` is present — fetch is fail-closed for supported triples. In-tree `sk_sw` remains an explicit opt-out (`SCUZZ_SKIA=sk_sw`) for offline/exotic hosts. As-needed rebuilds of the pin: `.github/workflows/skia-cpu.yml` + `scripts/build_skia_prebuilt.sh` (Linux x86_64 + macOS arm64). `PIN` uses a `{triple}` URL template so `package_release.sh` fetches the host-matching asset. Impeller deferred (GPU presenters later; must not change `Ui` session or logical goldens). Callers depend only on `sk_capi.h`.

### IO errors

One failure channel: `SzError` (message + optional code) on `IO`. Ops: `flatMap`, `delay`, `fail`, `handleErrorWith`, `attempt`, plus blessed kit (`Resource`, `Ref`, `Deferred`, `Queue`, `sleep`, `race`, `both`). **Concurrency:** cooperative single-threaded fibers (FIFO ready queue, left-before-right fork); `sleep` / empty `Queue.take` / incomplete `Deferred.get` park; TestRuntime advances virtual time to the next wakeup when idle. No OS threads for IO. Impurity codes: Fs **2**, Sys (**3**; exec + `readLine`), Clock **4**, Random **5**, Net **6**. TestRuntime (`SCUZZ_TESTRT=1`) fakes clock/random/FS/net plus console (scripted stdin, optional argv override, println capture+echo). No checked exception hierarchy; panics abort via `sz_panic`.

### `Ui` vs `View`

| Layer | Role | Purity |
| --- | --- | --- |
| **`View`** | Widget tree | Sync/pure `build` |
| **`Ui` / `UiSession`** | `mount` / `pump` / `inject` / `snapshot` | Effectful (`UiRuntime`) |

Headless is a **peer** of Window/Mobile, not a test-only shim. Frame boundary is `pump`. World effects stay blessed `IO`; bridge into signals via `sz_ui_bridge_post_*`. No UI feature without a Headless path. Taps: `View.button(label, _ => …)` first-class lambdas. Prefer `Signal.list` + `View.each` (framework-owned list reconciliation at layout). Derived display: `Signal.map` + `View.bindText`.

### IO apps vs Headless

| Path | Meaning |
| --- | --- |
| **Headless** | `UiRuntime` peer — still `View` / Skia / structural dumps; CI path for UI |
| **IO-only** | No `[ui]`, no `Ui.run`; `scuzz run` just execs the `@main: IO[Unit]` binary |

IO-only is **not** a fourth runtime peer. Package contract: missing `[ui]` ⇒ Skia omitted from the app link, `scuzz test` runs `SCUZZ_TESTRT=1` exit-0 smoke (not a11y goldens), and `scuzz run` is plain exec. Console kit: `Sys.args`, `Sys.readLine`, `IO.println` (TestRuntime fakes stdin / optional argv / println capture). See `examples/hello`, `examples/cli`, impurity kits.

### Kernel dialect

Subset used by compiler sources and bootstrap examples. New features land in Stage 0 **before** `compiler-scuzz/` depends on them. Dual-boot gate: `scripts/selfhost.sh` — each stage smokes `examples/hello` + `examples/adt` + `examples/modules` + `examples/record` + `examples/trait` + `examples/generic`, passes the counter/todo/nav Headless goldens, smokes `fuzz` on `examples/todo` and `fuzz --exhaust --depth 1` on `examples/counter`, and agrees with Stage 0 on `fmt --check` for the compiler sources; Stage 2 must re-emit byte-identical compiler IR (Stage-3 fixpoint).

- Optional `package`; top-level `def` / `private def` / `import Module.name` / `@main def …: IO[Unit]`; enums with N-field `Int`/`String`/`List`/ADT payloads (Stage 0 and self-host); **`record Name(f1: T1, …)`** as single-case enum sugar (`Name(args)` construct, `case Name(binds)` match, **`p.x` field access**); thin **traits** / `impl` with static-dispatch methods (`examples/trait`); one type parameter on defs (`def id[T](…)` → monomorphize; `examples/generic`); compiler sources use payload ADTs end-to-end (`Tok` … `Expr`, `Type`/`TyRes`, codegen `Emit`/`EnvBind`); multi-binder `match` — `examples/adt` exercises nullary/unary/multi-field/List payloads plus enum-typed def signatures; multi-field packs as `List` in the ADT payload; file-stem modules with `private def` module-local and `import` for bare disambiguation (`examples/modules`); enums are namespaced by file stem (same bare enum name allowed in two modules; `import Module.EnumName` brings type + cases into bare `Enum.*` scope)
- **`for { binders } yield e`** as primary binder: `x = e` (pure), `x <- e` (effect; yield wraps with `IO.pure` when any `<-` is present). Nested `for` in `if` / lambda arms when multi-bind is needed.
- No `val` / statement blocks / `var` — expression dialect only.
- `if` / `match`; literals incl. list `[a,b,c]` and `s"…"`
- Types: `Unit`, `Int`, `String`, `Bool`, `List`, `IO[T]`, nominal enums
- Builtins: `Str.*`, `List.*`, Fs (`read` / `write` / `list` / `mkdirs` / `canonicalize`)/Sys (`args` / `readLine` / `exec` / `getenv`)/Clock/Random/Net, `Ref.*` / `Queue.*` / `Deferred.*` (String payloads), `Signal.*` (incl. `Signal.map`), `View.*` (incl. nested `View.column`/`row` children, `View.each`, `View.bindText`, Column-only `View.expanded`), `Ui.run`, Theme/Color, `Law.signalInt` / `Law.a11yHas` / `Law.assert` (residual under TestRuntime)
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
    root  = View.column(
              View.text("Counter"),
              View.bindText(label),
              View.row(View.button("+1", _ => Signal.set(count, Signal.get(count) + 1)))
            )
    _    <- Ui.run(root)
  } yield ()
```

### Density

Clear-dense, not cryptic-dense: nested declarative `View`s (`View.column(child, …)` / `View.row(…)`), inference, single-expr forms, short update verbs, enums + match. Avoid implicits, deep HKT, and “everything is `IO`.”

### Modules and source shape

Scala **nouns**, Rust/Cargo **verbs** — without JVM packages or Rust `struct`/`impl` as the primary story.

| Layer | Meaning |
| --- | --- |
| Package (`scuzz.toml`) | Dependency / link boundary (crate) |
| File module (`Todo.scuzz`) | Namespacing + visibility |
| Optional deeper `mod` tree | Only when a single file gets heavy |

Direction for data/interfaces (see also [`compatibility.md`](compatibility.md)): payload **enums** / **`record`** product types (single-case enum sugar) + thin **traits**-as-interfaces; monomorphize generics early. Stage 0 and self-host (`compiler-scuzz/`) emit N-field `Int`/`String`/`List`/ADT payload enums for user programs (multi-field as `List` in `sz_adt_payload`); `record Name(…)` is the named-field surface (`examples/record`). Thin traits (`trait Show: def show(): String` / `impl Show for Point: …` with `self` in bodies; call `p.show()`) monomorphize to mangled defs — no vtables (`examples/trait`). Stage 0 and self-host monomorphize one type parameter on defs (`def id[T](x: T): T`; `examples/generic`). Compiler sources use payload ADTs end-to-end (`Tok` … `Expr`, typed `Type`/`Emit` channels). No classes (mutable identity), no `var`, no classpath/`com.foo.bar` directories. Path deps remain the unit of reuse. Stage 0 and self-host namespace defs **and enums** by file stem (`examples/modules`); surface stays two-level (`Enum.Case` / type `Enum`). `private def` is module-local (default public); `import Module.name` brings a public def or enum into bare scope for the importing module (disambiguates colliding names; unique bare still works without import). No `pub` yet.

### Laws, simulation, and verification

App correctness is **laws** searched by `scuzz fuzz`, not example-based unit tests. Authors declare properties the program must obey under interaction; the toolchain typechecks them and residualizes checks for TestRuntime / fuzz. Compilation and testing share one declaration surface:

| Phase | Role |
| --- | --- |
| `scuzz check` | Laws and sim overlays typecheck; laws are pure; sim bindings match live types/purity |
| `scuzz build` (sim graph) | Layer `*.scuzz_sim` over live defs; emit residual law checks (armed under TestRuntime / fuzz only) |
| `scuzz fuzz` | Search event scripts / IO schedules for law violations → `repro.toml` |
| Later (optional) | Prove trivial law fragments statically; leave the rest as search |

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

Deterministic TestRuntime + Headless event scripts (plus sim overlays when present):

```text
(program + sim, seed/config, event script) → exit code + signal store + a11y view dump + law results
```

```bash
scuzz fuzz [--iters N] [--seed S]   # typed random scripts until fail / N scripts
scuzz fuzz --exhaust --depth N      # bounded systematic search (all scripts of length 1..N)
scuzz fuzz --replay repro.toml      # deterministic replay of a recorded failure
```

Scripts are a line protocol — `tap <n>` / `text <s>` / `pump <k>` — played by the runtime (`SCUZZ_UI_SCRIPT`) across `pump` boundaries; on exit it writes the signal store + a11y view dump (`SCUZZ_FUZZ_DUMP`). The CLI probes the a11y dump for the typed event surface (buttons in scan order, text fields), generates seeded scripts (Lehmer/MINSTD LCG — the kernel dialect has no bitwise ops) or enumerates a finite alphabet under `--exhaust` (`tap <i>` for each button, `text` / `text a` when a field exists, `pump 1`), and writes `repro.toml` (seed + events) on failure. Exhaustive mode walks lengths `1..N` in stable order so shorter counterexamples win. `fuzz` lives in the Stage-1/2 CLI (not Stage 0); replay plays recorded events verbatim, so it is independent of the generator. **Oracles:** module **laws** first (residual `Law.assert` under `SCUZZ_TESTRT=1`); panic/`SzError` exit still fails; structural dumps aid diagnosis (PNG last). Requires stable tap order, `pump` as time, no hidden nondeterminism.

### Layout model

When the widget set grows beyond column/row: **Flutter-style constraints** (constraints down, sizes up). Column-only `View.expanded(child)` is the first flex bite (leftover height after non-flex siblings). Do not drift into CSS-ish ad-hoc layout rules.

### UI testing

**Laws + `scuzz fuzz`** are the primary correctness story for `[ui]` apps. Structural goldens (signal store + a11y dump) remain a **regression face** for silent shape drift — few, Headless-only, live graph — not a substitute for laws. PNG pixels stay optional (`scuzz test --pixels`). IO packages (no `[ui]`): laws + sim overlays under TestRuntime when present; otherwise `scuzz test` stays compile + `SCUZZ_TESTRT=1` exit-0 smoke. Tooling: `scuzz check` (typecheck live + sim + laws) and `--message-format=json`.

## Open work

Unknowns and known gaps, ranked by risk: [`gaps.md`](gaps.md). Open unknowns: device Mobile (blocked on NDK/Xcode), GPU presenters. IME can follow real text metrics.

App authors: [`guide.md`](guide.md). Vertical slices over breadth; no Window-only UI features.

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
| Skia weight | pinned CPU prebuilt default; `sk_sw` opt-out (`SCUZZ_SKIA=sk_sw`) |
| Window-only features | Headless peer rule |
| Diluting Flutter-shaped focus | UI stays the v0 bar; IO is substrate + first-class packaging without a fourth peer |
| GC vs frame budget | `pump` boundary; measure |
| Mobile packaging | Host Mobile peer first; device toolchains later |
