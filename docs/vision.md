# ScalUI vision

ScalUI is a **Flutter-shaped product** with a **Scala-inspired language**, not a Scala 3 / Scala Native / Maven citizen.

One doc for product intent and design locks. Next work lives in [`plan.md`](plan.md). Keep/cut tables live in [`compatibility.md`](compatibility.md). Manifest schema: [`schemas/scalui-toml.md`](schemas/scalui-toml.md).

Edit this file when a decision changes. No separate “ADR” process — just update the section.

## Thesis

- **Language**: purposeful Scala-inspired subset for UI apps and native codegen, with **built-in effect/IO** (Cats Effect spirit, not a cats port). Aim: denser expr dialect (`for` as primary binder) — see [Language direction](#language-direction) below.
- **Runtime**: custom native (LLVM). No JVM, no Java interop, no classpath/Maven.
- **UI**: one design language + Skia, as a **`Ui` effect** with Headless/Window/Mobile interpreters.
- **Tooling**: one CLI (`scalui`) for compile, link, assets, hot reload, packaging; later deterministic `scalui fuzz`.
- **Bootstrap**: self-host is a hard goal. Stage-0 (Rust) exists only to get there.

Upstream Scala Native is a *reference*, not a dependency. Divergence is intentional.

## Defaults

| Topic | Choice |
| --- | --- |
| UI testing / CI | `UiRuntime.Headless` first; Window/Mobile are alternate interpreters |
| Codegen | LLVM IR |
| Renderer (v0) | Skia via thin C ABI; Impeller deferred |
| Build tool | DIY Mill/Cargo-like: `scalui` (not sbt/Maven) |
| Effects | Language + runtime builtins |
| Impurity | All nondeterminism / external I/O through blessed `IO`; no app-level `IO.delay` escape hatch |
| Tests | TestRuntime fakes (clock/random/FS/net) for deterministic replay |
| Self-host | Stage 0 → 1 → 2 on the critical path |
| UI model | Pure `View` + effectful `Ui` session (`mount` / `pump` / `inject` / `snapshot`) |

## What ScalUI is not

- Not Scala 3, not the JVM, not Scala.js
- Not a cats / cats-effect / Typelevel port
- Not SwiftUI / UIKit / WinUI wrappers
- Not “every widget rebuild is an `IO`” (`View` build stays sync/pure)

## Success bars

**v0** — Install CLI → `scalui new` → Counter/Todo as `View` + builtin `IO` → `scalui test` (Headless) and `scalui run --headless` → `scalui run` opens a window when available.

**v1** — Stage-2 self-host; release builds do not need Rust Stage-0 except as CI canary.

## Decisions

### GC (v0)

libc `malloc`/`free` via `su_alloc` / `su_free`. No moving collector yet. Clear ownership frees strings/IO/`Resource`/Views; panic may leak. Revisit when long-lived interactive graphs demand it.

### Skia

No vendored Skia tree. Thin `sk_capi` + CPU `sk_sw` for Headless CI; optional prebuilts via `SCALUI_SKIA_URL`. Impeller deferred (GPU presenters later; must not change `Ui` session or logical goldens). Callers depend only on `sk_capi.h`.

### IO errors

One failure channel: `SuError` (message + optional code) on `IO`. Ops: `flatMap`, `delay`, `fail`, `handleErrorWith`, `attempt`, plus blessed kit (`Resource`, `Ref`, `Deferred`, `Queue`, `sleep`, `race`, `both`). Impurity codes: Fs **2**, Sys exec **3**, Clock **4**, Random **5**, Net **6**. TestRuntime (`SCALUI_TESTRT=1`) fakes the same surface. No checked exception hierarchy; panics abort via `su_panic`.

### `Ui` vs `View`

| Layer | Role | Purity |
| --- | --- | --- |
| **`View`** | Widget tree | Sync/pure `build` |
| **`Ui` / `UiSession`** | `mount` / `pump` / `inject` / `snapshot` | Effectful (`UiRuntime`) |

Headless is a **peer** of Window/Mobile, not a test-only shim. Frame boundary is `pump`. World effects stay blessed `IO`; bridge into signals via `su_ui_bridge_post_*`. No UI feature without a Headless path. Taps: `View.button(label, _ => …)` first-class lambdas (no specialized tap builtins). Prefer List + Signal over one-off C controllers.

### Kernel dialect (current)

Subset used by compiler sources and bootstrap examples. New features land in Stage 0 **before** `compiler-scalui/` depends on them. Dual-boot gate: `scripts/selfhost.sh`.

- Optional `package`; top-level `def` / `@main def …: IO[Unit]`; enums (Stage 1 sources avoid enums)
- Local `val` in blocks (today); `if` / `match`; literals incl. list `[a,b,c]` and `s"…"`
- Types: `Unit`, `Int`, `String`, `Bool`, `List`, `IO[T]`, nominal enums
- Builtins: `Str.*`, `List.*`, Fs/Sys/Clock/Random/Net, `Signal.*`, `View.*`, `Ui.run`, Theme/Color
- `IO` kit + `.flatMap` / `.handleErrorWith` / `.attempt`; lambdas `_ =>` / `name =>` for taps
- No macros, no implicits, no HKT beyond `IO`, no null

## Language direction

Aim (forwards-only when implemented): diverge further from “almost Scala” toward an expression-only, effect-sequenced dialect — dense, deterministic, verification-friendly without becoming a proof assistant.

### Expr dialect

- **`for` as primary binder**: `x = e` (pure alias), `x <- e` (effect), optional guards
- **No statement blocks**, no `var`, drop the `val` keyword once `=` binders exist
- Branch arms stay expressions; nested `for` when an arm needs names — don’t grow a second block grammar
- Surface sugar elaborates to a small core (bindings, `match`, ADTs, `IO`); self-host and checkers target the core

Illustrative (not today’s kernel):

```scala
@main def main: IO[Unit] =
  for {
    count = Signal.int(0)
    root  = View.column(
              View.text("Counter"),
              View.textSignal(count, "count = "),
              View.row(View.button("+1", _ => Signal.update(count, _ + 1)))
            )
    _    <- Ui.run(root)
  } yield ()
```

### Density

Clear-dense, not cryptic-dense: nested declarative `View`s, inference, single-expr forms, short update verbs, enums + match. Avoid implicits, deep HKT, and “everything is `IO`.” Counter/Todo should shrink once Views are nested expressions.

### Verification posture

Keep purity checkable (pure `A` vs `IO` vs session), total expr core, signals as an explicit store, immutable data by default, errors as values. Spec **signal store + View/a11y dump**, not Skia pixels. Defer dependent types and runtime-heap proofs.

### `scalui fuzz` (aspirational)

Deterministic TestRuntime + Headless event scripts:

```text
(program, seed/config, event script) → trace + signals + view dump
```

```bash
scalui fuzz                       # typed random scripts until fail / timeout
scalui fuzz --exhaust --depth N   # bounded systematic search
scalui fuzz --replay repro.toml
```

Oracles: panic/`SuError` → invariants → structural dumps (PNG last). Exhaustion is **bounded**, not infinite-state. Requires stable tap ids/labels, `pump` as time, no hidden nondeterminism.

## Roadmap

Phases 0–6 landed. Ordered next steps: [`plan.md`](plan.md). Vertical slices over breadth; no Window-only UI features.

## Risks

| Risk | Mitigation |
| --- | --- |
| Language + UI + tooling is huge | Ruthless subset; vertical slices; Counter before generality |
| Self-host / dialect drift | Kernel section above; port compiler early; Stage 0/1/2 CI |
| Effects too weak or too heavy | Builtin IO; pure `View`; `Ui` at session boundary |
| Hidden nondeterminism | Closed impurity + TestRuntime |
| “Almost Scala” confusion | Explicit non-goals; language direction above |
| Skia weight | `sk_sw` + optional prebuilts |
| Window-only features | Headless peer rule |
| GC vs frame budget | `pump` boundary; measure |
| Mobile packaging | Host Mobile peer first; device toolchains later |
