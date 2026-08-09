# ADR 0004 — `Ui` vs `View` boundary

## Status

Accepted (Phase 0 design lock; code in Phase 1)

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

## Consequences

- No UI feature may land Window-only without a Headless path.
- `scalui test` always Headless; `scalui run --headless` is first-class.
