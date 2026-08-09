# ADR 0003 — IO error model

## Status

Accepted (Phase 0; kit completed Phase 3; FS/Sys live interpreters Phase 4)

## Context

Builtin `IO` needs one clear failure story. We will not port cats-effect's full machinery.

## Decision

**Single failure channel: `SuError` (message string + optional code), carried by `IO`.**

Surface:

- Success values are untyped `void*` at the C ABI; typed at the language level.
- `IO` can complete as ok(value) or err(SuError).
- Language/runtime ops: `flatMap`, `delay`, `fail`, `handleErrorWith`, `attempt` (`SuEither`), plus the Phase 3 blessed kit (`Resource` with release-on-failure, `Ref`, `Deferred`, `Queue`, `sleep`, `race`, `both`).
- Phase 4 live interpreters: `Fs.read`/`write`/`list`/`mkdirs`, `Sys.args`/`exec`/`getenv` — failures use the same `SuError` channel (fake FS/TestRuntime in Phase 6).
- **No checked exception hierarchy.** Panics (invariant violations) abort via `su_panic`.
- Prefer string messages for human diagnostics; structured errors can layer later without changing the ok/err split.

## Consequences

- Simple interpreter and LLVM lowering.
- Not source-compatible with cats-effect `Throwable` channels — documented non-goal.
- UI session failures (Phase 1) use the same `IO` error channel.
