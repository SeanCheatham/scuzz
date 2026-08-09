# ADR 0004 — `Ui` vs `View` boundary

## Status

Accepted — Phase 1 session API + Phase 2 declarative tree landed (`crates/runtime`)

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
- World effects (HTTP, files) stay plain `IO`; compose with `Ui` at the edges.
- Phase 2: `View` is an element tree with layout frames + hit testing; **signals** hold UI state; theme tokens style widgets.
- IO → UI bridge: completed `IO` may `su_ui_bridge_post_*` signal writes; `pump` flushes the queue (UI-thread hop).

## Consequences

- No UI feature may land Window-only without a Headless path.
- `scalui test` always Headless; `scalui run --headless` is first-class.
- Stage-0 demos (`Ui.runCounter` / `Ui.runTodo`) build Views in C until the kernel dialect can express widget trees.
