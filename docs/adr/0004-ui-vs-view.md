# ADR 0004 — `Ui` vs `View` boundary

## Status

Accepted

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
- `View` is an element tree with layout frames + hit testing; **signals** hold UI state; theme tokens style widgets.
- IO → UI bridge: completed `IO` may `su_ui_bridge_post_*` signal writes; `pump` flushes the queue (UI-thread hop).
- Mobile uses the same session protocol. Input expands with pointer phases, scroll, soft-keyboard visibility, and lifecycle — all injectable under Headless. OS shells (`crates/embedder-mobile`) only present + map events.
- Animation ticks (`SuAnimFloat`) advance on `pump` using monotonic Clock dt; accessibility is View metadata (`role`/`label`) dumpable under Headless — no OS assistive-tech bridge yet. Theme gains `accent`/`disabled`/`radius` without forcing golden churn (default `radius = 0`).
- Counter/Todo/nav examples build View trees in ScalUI via blessed `Signal` / `View` / `Todo` / `Ui.run` builtins (wrapping the C API). Kernel demos (`Ui.runCounter` / `Ui.runTodo` / `Ui.runLive`) remain for kits that are not yet language-authored.
- **`Ui.run` session lifetime**: Headless is mount → optional scripted tap → snapshot → unmount. Window (when an embedder is present) stays open and pumps until quit (q/Esc) or `SCALUI_LIVE_FRAMES`; same entry point as Headless. `Ui.runLive` remains the kernel stay-open demo with its own tree.
- **`View.showWhen(sig, value, child)`**: declarative visibility (layout/paint/hit skip when `Signal.get(sig) != value`). Not a lambda workaround — remains valid after first-class taps exist.

### Tap API: first-class lambdas

`View.button(label, onTap)` takes a lambda literal (`_ => expr` / `name => expr`) as its tap closure — no separate builtin per signal-write shape. The former closed family (`buttonInc` / `buttonSet`) is deleted (forwards-only); callers write the signal update directly, e.g. `View.button("+1", _ => Signal.set(count, Signal.get(count) + 1))`.

Closure representation (both compilers): a lambda lowers to a function pointer matching `SuViewTapFn` (`void (*)(SuView *self, void *env)`) plus a captured-locals environment, packed the same way as `flatMap` continuations (a boxed `SuList`, ints boxed via `su_box_i64`). `su_lang_view_button` receives the unpacked `(fn, env)` pair and forwards to `su_view_button`.

Todo Add/Save remain controller-owned C taps until list mutation is language-facing.

Rules:

1. **Do not** add further specialized tap builtins now that lambdas exist — express new tap behavior as `View.button(label, _ => ...)`. No more Todo-shaped C controllers for one-off apps; delete the Todo C controller once `List` literals make it language-facing.
2. Missing list-literal support must **not** block Todo's controller-owned taps; those are independent of the tap calling convention.

## Consequences

- No UI feature may land Window- or Mobile-only without a Headless path.
- `scalui test` always Headless; `scalui run --headless` is first-class.
- Language-authored Views use opaque handles + first-class `_ => ...` lambda taps for `SuViewTapFn`.
- `scalui package` emits packaging shells; device toolchains are outside the Headless CI default.
