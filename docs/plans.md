# Short-term plan: Scuzz bindings for Ref / Queue / Deferred

**Goal.** Expose the existing C concurrency kit as Stage-0 + self-host builtins so app `IO` can use `Ref` / `Queue` / `Deferred` (with `IO.race` / `IO.both` / `IO.sleep` already in language). Prove under TestRuntime virtual time — no OS-thread work.

## Status

Pending.

## Steps

1. **Surface** — Pick a minimal opaque API matching runtime ABI (String payloads for v0):
   - `Ref.of(s)` / `Ref.get` / `Ref.set`
   - `Queue.unbounded` / `Queue.offer` / `Queue.take`
   - `Deferred.empty` / `Deferred.complete` / `Deferred.get`
2. **Stage 0** — Lexer/parser/typ/codegen (or call-table builtins) for the names above; format round-trip.
3. **Example** — Extend `examples/effects` (or add a small IO example) that parks on `Queue.take` / `Deferred.get`, races a producer via `IO.race`/`IO.both`, and passes under `SCUZZ_TESTRT=1`.
4. **Self-host** — Mirror builtins in `compiler-scuzz`; `scripts/selfhost.sh` green. Do not use the new kit inside compiler sources this slice.
5. **Docs** — Update `vision.md` kernel builtins + `gaps.md` concurrency residual; remove this file when done.

## Non-goals

- OS threads, interruptible cancel mid-`nanosleep`, supervision trees
- Generics over Ref/Queue element types (String-only / opaque ptr is enough for v0)
- `import`, records/traits, constraint layout
