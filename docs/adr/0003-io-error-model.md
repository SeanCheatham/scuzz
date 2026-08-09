# ADR 0003 — IO error model

## Status

Accepted (Phase 0)

## Context

Builtin `IO` needs one clear failure story. We will not port cats-effect's full machinery.

## Decision

**Single failure channel: `SuError` (message string + optional code), carried by `IO`.**

Phase 0 surface:

- Success values are untyped `void*` at the C ABI; typed at the language level.
- `IO` can complete as ok(value) or err(SuError).
- Language/runtime ops: `handleErrorWith`, `attempt` (Phase 3 completes the blessed kit); Phase 0 ships `flatMap`, `delay`, and fail/panic boundaries.
- **No checked exception hierarchy.** Panics (invariant violations) abort via `su_panic`.
- Prefer string messages for human diagnostics; structured errors can layer later without changing the ok/err split.

## Consequences

- Simple interpreter and LLVM lowering.
- Not source-compatible with cats-effect `Throwable` channels — documented non-goal.
- UI session failures (Phase 1) use the same `IO` error channel.
