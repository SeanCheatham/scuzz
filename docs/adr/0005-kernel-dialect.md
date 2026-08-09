# ADR 0005 — Kernel dialect for self-host

## Status

Accepted (Phase 0; expanded Phase 3; expanded Phase 4; impurity builtins Phase 6; View/Signal language surface post–Phase 6)

## Context

Self-host requires the compiler sources to stay inside what Stage 0 can emit until Stage 1 catches up. Phase 4 lands Stage 1/2 in `compiler-scalui/`.

## Decision

Document a **kernel dialect**: the subset used by compiler sources and bootstrap examples.

### Kernel (Stage 0 → Phase 6)

- Optional `package a.b.c`
- Top-level nullary `enum Name:` / `enum Name { case A, case B }` ADTs (Stage 0; Stage 1 sources avoid enums)
- Top-level `def name(params): Type = body` (Phase 4) and `@main def name: IO[Unit] = expr`
- Local `val` bindings at block starts (not inside bare `if` branches)
- `if (cond) then else else` (Phase 4); `match { case Enum.Case => expr; case _ => expr }`
- Literals: strings, ints, unit `()`
- Types: `Unit`, `Int`, `String`, `Bool`, `List`, `IO[T]`, nominal enums
- Ops: int arithmetic/compare, `&&`/`||`, string `+`
- Builtins: `Str.*`, `List.*`, `Fs.read`/`write`/`list`/`mkdirs`, `Sys.args`/`exec`/`getenv`,
  `Clock.realTime`/`monotonic`, `Random.nextInt`, `Net.httpGet`,
  `Signal.*`, `View.*`, `Todo.*`, `Ui.run` / `Ui.runWithTodo`
- Calls: `IO.println`/`delay`/`sleep`/`fail`/`pure`/`race`/`both`, `.flatMap(x => …)` (bound or `_`), `.handleErrorWith`, `.attempt`
- Phase 1–6 demos (Stage 0): `Ui.runHeadless` / `runCounter` / `runLive` / `runTodo`, `Effects.runKit`, `Impurity.runKit`, `Lexer.classify`
- Multi-file `src/**/*.scala` units merged per package
- No macros, no implicits, no HKT beyond `IO`, no null

### Expansion rules

1. New compiler features must land in Stage 0 **before** compiler sources depend on them.
2. Prefer AST shapes and golden LLVM tests that port cleanly to ScalUI Stage 1.
3. Runtime GC / fibers / Skia remain C/Rust; not part of the kernel dialect.
4. Dual-boot CI (`scripts/selfhost.sh`) is the dialect-drift gate: Stage 1 must rebuild itself and run `examples/hello`.

## Consequences

- Parser/codegen stay small and portability-oriented.
- `compiler-scalui/` is the Stage 1/2 compiler + CLI; Stage 0 Rust remains a canary host.
- Blessed Fs/Sys/Clock/Random/Net keep ScalUI app + compiler code off raw libc impurity.
