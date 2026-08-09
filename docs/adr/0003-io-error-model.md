# ADR 0003 — IO error model

## Status

Accepted (Phase 0; kit completed Phase 3; FS/Sys live interpreters Phase 4; Clock/Random/Net + TestRuntime Phase 6)

## Context

Builtin `IO` needs one clear failure story. We will not port cats-effect's full machinery.

## Decision

**Single failure channel: `SuError` (message string + optional code), carried by `IO`.**

Surface:

- Success values are untyped `void*` at the C ABI; typed at the language level.
- `IO` can complete as ok(value) or err(SuError).
- Language/runtime ops: `flatMap`, `delay`, `fail`, `handleErrorWith`, `attempt` (`SuEither`), plus the Phase 3 blessed kit (`Resource` with release-on-failure, `Ref`, `Deferred`, `Queue`, `sleep`, `race`, `both`).
- Blessed impurity interpreters (live + TestRuntime fakes):
  - `Fs.read`/`write`/`list`/`mkdirs` — error code **2**
  - `Sys.args`/`exec`/`getenv` — error code **3** (exec)
  - `Clock.realTime`/`monotonic` (+ `IO.sleep` via Clock) — code **4** reserved
  - `Random.nextInt` — code **5** reserved
  - `Net.httpGet` — error code **6**
  - Console out: `IO.println` (no failure channel beyond process IO errors)
- **TestRuntime** (`su_testrt_install` / `SCALUI_TESTRT=1`): fake clock (sleep advances virtual ms), seeded RNG, in-memory FS, stubbed `Net.httpGet` map — same `SuError` codes as live.
- **No checked exception hierarchy.** Panics (invariant violations) abort via `su_panic`.
- Prefer string messages for human diagnostics; structured errors can layer later without changing the ok/err split.
- App code must not use `IO.delay` thunks that reach outside this blessed surface; runtime-private delay for interpreters is allowed.

## Consequences

- Simple interpreter and LLVM lowering.
- Not source-compatible with cats-effect `Throwable` channels — documented non-goal.
- UI session failures (Phase 1) use the same `IO` error channel.
- Deterministic `scalui test` / unit tests can install TestRuntime without wall time or the real OS.
