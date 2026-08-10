# ADR 0005 — Kernel dialect for self-host

## Status

Accepted (expanded for self-host, impurity builtins, and the language View surface)

## Context

Self-host requires the compiler sources to stay inside what Stage 0 can emit until Stage 1 catches up. Stage 1/2 live in `compiler-scalui/`.

## Decision

Document a **kernel dialect**: the subset used by compiler sources and bootstrap examples.

### Kernel

- Optional `package a.b.c`
- Top-level nullary `enum Name:` / `enum Name { case A, case B }` ADTs (Stage 1 sources avoid enums)
- Top-level `def name(params): Type = body` and `@main def name: IO[Unit] = expr`
- Local `val` bindings in blocks (including after statement expressions)
- `if (cond) then else else`; `match { case Enum.Case => expr; case _ => expr }`
- Literals: strings, ints, unit `()`, interpolated strings `s"...$x..."`, list literals `[a, b, c]`
- Types: `Unit`, `Int`, `String`, `Bool`, `List`, `IO[T]`, nominal enums
- Ops: int arithmetic/compare, `&&`/`||`, string `+`
- Builtins: `Str.*` (incl. `lines`), `List.*` (incl. `append`), `Fs.read`/`write`/`list`/`mkdirs`, `Sys.args`/`exec`/`getenv`,
  `Clock.realTime`/`monotonic`, `Random.nextInt`, `Net.httpGet`,
  `Signal.*` (int/str/list get/set), `View.*` (incl. `button` / `showWhen` / `addTexts`), `Ui.run`,
  `Theme.accent`/`primary`/`muted`/`foreground`, `Color.rgb`
- Calls: `IO.println`/`delay`/`sleep`/`fail`/`pure`/`race`/`both`, `.flatMap(x => …)` (bound or `_`), `.handleErrorWith`, `.attempt`
- First-class lambda literals: `_ => expr` / `name => expr` (single param, untyped), used as `View.button` tap closures (`IO` bodies run via `su_io_unsafe_run`)
- Kernel demos: `Ui.runHeadless` / `runCounter` / `runLive` / `runTodo`, `Effects.runKit`, `Impurity.runKit`, `Lexer.classify`
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
