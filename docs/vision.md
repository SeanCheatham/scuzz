# ScalUI vision

ScalUI is a **Flutter-shaped product** with a **Scala-inspired language**, not a Scala 3 / Scala Native / Maven citizen.

One doc for product intent, design locks, language direction, and open work. Keep/cut tables: [`compatibility.md`](compatibility.md). Manifest schema: [`schemas/scalui-toml.md`](schemas/scalui-toml.md). App path: [`guide.md`](guide.md).

Edit this file when a decision or next-step ordering changes.

## Thesis

- **Language**: purposeful Scala-inspired subset for UI apps, native CLI/server-shaped `IO` programs, and native codegen, with **built-in effect/IO** (Cats Effect spirit, not a cats port). Aim: denser expr dialect (`for` as primary binder) — see [Language direction](#language-direction) below.
- **Runtime**: custom native (LLVM). No JVM, no Java interop, no classpath/Maven.
- **UI**: primary product face — one design language + Skia, as a **`Ui` effect** with Headless/Window/Mobile interpreters.
- **Tooling**: one CLI (`scalui`) for compile, link, assets, hot reload, packaging, deterministic `scalui fuzz`.
- **Bootstrap**: self-host is a hard goal. Stage-0 (Rust) exists only to get there.

Upstream Scala Native is a *reference*, not a dependency. Divergence is intentional.

## Defaults

| Topic | Choice |
| --- | --- |
| UI testing / CI | Structural dumps (signal + a11y) first; PNG optional via `--pixels` |
| Codegen | LLVM IR |
| Renderer (v0) | Skia via thin C ABI; Impeller deferred |
| Build tool | DIY Mill/Cargo-like: `scalui` (not sbt/Maven) |
| Effects | Language + runtime builtins |
| Impurity | All nondeterminism / external I/O through blessed `IO`; no app-level `IO.delay` escape hatch |
| Tests | TestRuntime fakes (clock/random/FS/net/console) for deterministic replay |
| Self-host | Stage 0 → 1 → 2 on the critical path |
| UI model | Pure `View` + effectful `Ui` session (`mount` / `pump` / `inject` / `snapshot`) |

## What ScalUI is not

- Not Scala 3, not the JVM, not Scala.js
- Not a cats / cats-effect / Typelevel port
- Not SwiftUI / UIKit / WinUI wrappers
- Not “every widget rebuild is an `IO`” (`View` build stays sync/pure)

## Success bars

**v0** — Install CLI → `scalui new --ui` → Counter/Todo as `View` + builtin `IO` → `scalui test` (Headless) and `scalui run --headless` → `scalui run` opens a window when available. IO-only path: `scalui new` (no `--ui`) → `scalui test` (TESTRT smoke) → `scalui run`.

**v1** — Stage-2 self-host; release builds do not need Rust Stage-0 except as CI bootstrap.

## Decisions

### GC (v0)

libc `malloc`/`free` via `su_alloc` / `su_free`. No moving collector yet. Clear ownership frees strings/IO/`Resource`/Views; panic may leak. Revisit when long-lived interactive graphs demand it.

### Skia

No vendored Skia tree. Thin `sk_capi` + CPU `sk_sw` for Headless CI; optional prebuilts via `SCALUI_SKIA_URL`. Impeller deferred (GPU presenters later; must not change `Ui` session or logical goldens). Callers depend only on `sk_capi.h`.

### IO errors

One failure channel: `SuError` (message + optional code) on `IO`. Ops: `flatMap`, `delay`, `fail`, `handleErrorWith`, `attempt`, plus blessed kit (`Resource`, `Ref`, `Deferred`, `Queue`, `sleep`, `race`, `both`). Impurity codes: Fs **2**, Sys (**3**; exec + `readLine`), Clock **4**, Random **5**, Net **6**. TestRuntime (`SCALUI_TESTRT=1`) fakes clock/random/FS/net plus console (scripted stdin, optional argv override, println capture+echo). No checked exception hierarchy; panics abort via `su_panic`.

### `Ui` vs `View`

| Layer | Role | Purity |
| --- | --- | --- |
| **`View`** | Widget tree | Sync/pure `build` |
| **`Ui` / `UiSession`** | `mount` / `pump` / `inject` / `snapshot` | Effectful (`UiRuntime`) |

Headless is a **peer** of Window/Mobile, not a test-only shim. Frame boundary is `pump`. World effects stay blessed `IO`; bridge into signals via `su_ui_bridge_post_*`. No UI feature without a Headless path. Taps: `View.button(label, _ => …)` first-class lambdas. Prefer `Signal.list` + `View.each` (framework-owned list reconciliation at layout). Derived display: `Signal.map` + `View.bindText`.

### IO apps vs Headless

| Path | Meaning |
| --- | --- |
| **Headless** | `UiRuntime` peer — still `View` / Skia / structural dumps; CI path for UI |
| **IO-only** | No `[ui]`, no `Ui.run`; `scalui run` just execs the `@main: IO[Unit]` binary |

IO-only is **not** a fourth runtime peer. Package contract: missing `[ui]` ⇒ Skia omitted from the app link, `scalui test` runs `SCALUI_TESTRT=1` exit-0 smoke (not a11y goldens), and `scalui run` is plain exec. Console kit: `Sys.args`, `Sys.readLine`, `IO.println` (TestRuntime fakes stdin / optional argv / println capture). See `examples/hello`, `examples/cli`, impurity kits.

### Kernel dialect

Subset used by compiler sources and bootstrap examples. New features land in Stage 0 **before** `compiler-scalui/` depends on them. Dual-boot gate: `scripts/selfhost.sh` — each stage smokes `examples/hello` + `examples/adt`, passes the counter/todo/nav Headless goldens, smokes `fuzz` on `examples/todo` and `fuzz --exhaust --depth 1` on `examples/counter`, and agrees with Stage 0 on `fmt --check` for the compiler sources; Stage 2 must re-emit byte-identical compiler IR (Stage-3 fixpoint).

- Optional `package`; top-level `def` / `@main def …: IO[Unit]`; nullary enums (Stage 1 sources avoid enums; Stage 1/2 emit `su_adt_new` / `su_adt_tag` + `match` `switch`)
- **`for { binders } yield e`** as primary binder: `x = e` (pure), `x <- e` (effect; yield wraps with `IO.pure` when any `<-` is present). Nested `for` in `if` / lambda arms when multi-bind is needed.
- No `val` / statement blocks / `var` — expression dialect only.
- `if` / `match`; literals incl. list `[a,b,c]` and `s"…"`
- Types: `Unit`, `Int`, `String`, `Bool`, `List`, `IO[T]`, nominal enums
- Builtins: `Str.*`, `List.*`, Fs/Sys (`args` / `readLine` / `exec` / `getenv`)/Clock/Random/Net, `Signal.*` (incl. `Signal.map`), `View.*` (incl. nested `View.column`/`row` children, `View.each`, `View.bindText`), `Ui.run`, Theme/Color
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

### Verification posture

Keep purity checkable (pure `A` vs `IO` vs session), total expr core, signals as an explicit store, immutable data by default, errors as values. Spec **signal store + View/a11y dump**, not Skia pixels. Defer dependent types and runtime-heap proofs.

### `scalui fuzz`

Deterministic TestRuntime + Headless event scripts:

```text
(program, seed/config, event script) → exit code + signal store + a11y view dump
```

```bash
scalui fuzz [--iters N] [--seed S]   # typed random scripts until fail / N scripts
scalui fuzz --exhaust --depth N      # bounded systematic search (all scripts of length 1..N)
scalui fuzz --replay repro.toml      # deterministic replay of a recorded failure
```

Scripts are a line protocol — `tap <n>` / `text <s>` / `pump <k>` — played by the runtime (`SCALUI_UI_SCRIPT`) across `pump` boundaries; on exit it writes the signal store + a11y view dump (`SCALUI_FUZZ_DUMP`). The CLI probes the a11y dump for the typed event surface (buttons in scan order, text fields), generates seeded scripts (Lehmer/MINSTD LCG — the kernel dialect has no bitwise ops) or enumerates a finite alphabet under `--exhaust` (`tap <i>` for each button, `text` / `text a` when a field exists, `pump 1`), and writes `repro.toml` (seed + events) on failure. Exhaustive mode walks lengths `1..N` in stable order so shorter counterexamples win. `fuzz` lives in the Stage-1 CLI; replay plays recorded events verbatim, so it is independent of the generator. Oracles: panic/`SuError` exit → structural dumps (PNG last). Requires stable tap order, `pump` as time, no hidden nondeterminism.

### Layout model

When the widget set grows beyond column/row: **Flutter-style constraints** (constraints down, sizes up). Do not drift into CSS-ish ad-hoc layout rules.

### UI testing

Primary goldens are **structural** (signal store + a11y view dump). PNG pixels are optional (`scalui test --pixels`). IO packages (no `[ui]`): `scalui test` is compile + `SCALUI_TESTRT=1` exit-0 smoke. Agent-facing: `scalui check` (typecheck only) and `--message-format=json`.

## Open work

App authors: [`guide.md`](guide.md). Vertical slices over breadth; no Window-only UI features.

## Risks

| Risk | Mitigation |
| --- | --- |
| Language + UI + tooling is huge | Ruthless subset; vertical slices; Counter before generality |
| Self-host / dialect drift | Kernel section above; port compiler early; Stage 0/1/2 CI |
| Effects too weak or too heavy | Builtin IO; pure `View`; `Ui` at session boundary |
| Hidden nondeterminism | Closed impurity + TestRuntime |
| “Almost Scala” confusion | Explicit non-goals; language direction above; [guide.md](guide.md) |
| Skia weight | `sk_sw` + optional prebuilts |
| Window-only features | Headless peer rule |
| Diluting Flutter-shaped focus | UI stays the v0 bar; IO is substrate + first-class packaging without a fourth peer |
| GC vs frame budget | `pump` boundary; measure |
| Mobile packaging | Host Mobile peer first; device toolchains later |
