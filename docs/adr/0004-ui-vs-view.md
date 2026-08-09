# ADR 0004 — `Ui` vs `View` boundary

## Status

Accepted — Phase 1 session API + Phase 2 declarative tree + Phase 5 Mobile peer + Phase 6 anim/a11y hooks + post–Phase 6 language View surface + interaction slice (`buttonSet` / `showWhen` / stay-open `Ui.run`)

## Context

“UI as an effect” must not destroy frame budgets or conflate declarative structure with session I/O.

## Decision

Two layers:

| Layer | Role | Purity |
| --- | --- | --- |
| **`View`** | Declarative widget tree | Sync/pure `build` |
| **`Ui` / `UiSession`** | `mount` / `pump` / `inject` / `snapshot` | Effectful; needs `UiRuntime` |

- `UiRuntime.Headless` is a **peer** of `Window` / `Mobile`, not a test-only shim.
- Widget rebuild is **not** an `IO`. Frame boundary is `UiSession.pump`.
- World effects (HTTP, files, clock, random) stay plain blessed `IO`; compose with `Ui` at the edges.
- Phase 2: `View` is an element tree with layout frames + hit testing; **signals** hold UI state; theme tokens style widgets.
- IO → UI bridge: completed `IO` may `su_ui_bridge_post_*` signal writes; `pump` flushes the queue (UI-thread hop).
- Phase 5: Mobile uses the same session protocol. Input expands with pointer phases, scroll, soft-keyboard visibility, and lifecycle — all injectable under Headless. OS shells (`crates/embedder-mobile`) only present + map events.
- Phase 6: animation ticks (`SuAnimFloat`) advance on `pump` using monotonic Clock dt; accessibility is View metadata (`role`/`label`) dumpable under Headless — no OS assistive-tech bridge yet. Theme gains `accent`/`disabled`/`radius` without forcing golden churn (default `radius = 0`).
- Post–Phase 6: Counter/Todo/nav examples build View trees in ScalUI via blessed `Signal` / `View` / `Todo` / `Ui.run` builtins (wrapping the C API). Kernel demos (`Ui.runCounter` / `Ui.runTodo` / `Ui.runLive`) remain for kits that are not yet language-authored.
- **`Ui.run` session lifetime**: Headless is mount → optional scripted tap → snapshot → unmount. Window (when an embedder is present) stays open and pumps until quit (q/Esc) or `SCALUI_LIVE_FRAMES`; same entry point as Headless. `Ui.runLive` remains the kernel stay-open demo with its own tree.
- **`View.showWhen(sig, value, child)`**: declarative visibility (layout/paint/hit skip when `Signal.get(sig) != value`). Not a lambda workaround — remains valid after first-class taps exist.

### Interim tap API / lambda migration

Language-facing taps are a **small closed family** that write int signals — not an open-ended zoo of app-specific builtins:

| Builtin | Meaning | Future lambda shape |
| --- | --- | --- |
| `View.buttonInc(label, sig)` | `sig += 1` | `_ => Signal.set(sig, Signal.get(sig) + 1)` |
| `View.buttonSet(label, sig, value)` | `sig = value` | `_ => Signal.set(sig, value)` |
| Todo Add/Save | controller-owned until lists are general | replace when list mutation is language-facing |

Rules:

1. **Do not** add further specialized tap builtins unless they reduce to int-signal writes (or a similarly tiny closed set). No more Todo-shaped C controllers for one-off apps.
2. First-class `SuViewTapFn` lambdas (closures + codegen) are the intended replacement for `buttonInc` / `buttonSet`; those builtins remain until then and may later become thin sugar or be deleted (forwards-only).
3. Missing lambdas must **not** block stay-open session or declarative `showWhen` work — those are independent of the tap calling convention.

## Consequences

- No UI feature may land Window- or Mobile-only without a Headless path.
- `scalui test` always Headless; `scalui run --headless` is first-class.
- Language-authored Views use opaque handles + the interim tap family above until first-class lambdas exist for `SuViewTapFn`.
- `scalui package` emits packaging shells; device toolchains are outside the Headless CI default.
