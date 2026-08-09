# ADR 0005 — Kernel dialect for self-host

## Status

Accepted (Phase 0; expanded Phase 3)

## Context

Self-host requires the compiler sources to stay inside what Stage 0 can emit until Stage 1 catches up.

## Decision

Document a **kernel dialect**: the subset used by compiler sources and bootstrap examples.

### Kernel (Stage 0 → Phase 3)

- Optional `package a.b.c`
- Top-level nullary `enum Name:` / `enum Name { case A, case B }` ADTs
- Top-level `@main def name: IO[Unit] = expr`
- Local `val` bindings; sync expressions (`Enum.Case`, `Lexer.classify`, `match`)
- `match { case Enum.Case => expr; case _ => expr }`
- String literals, int literals (for `IO.sleep`), unit `()`
- Type syntax: `Unit`, `IO[Unit]`, nominal enum types
- Calls: `IO.println`, `IO.delay`, `IO.sleep`, `IO.fail`, `IO.race`, `IO.both`, `.flatMap`, `.handleErrorWith`, `.attempt`
- Phase 1 UI: `Ui.runHeadless(str)` → `IO[Unit]`
- Phase 2 UI: `Ui.runCounter` / `Ui.runTodo` → `IO[Unit]`
- Phase 3 effects: `Effects.runKit` → `IO[Unit]`
- Phase 3 parser bootstrap: `Lexer.classify(str)` → `Tok` ADT (`su_lexer_classify` tag order)
- Multi-file `src/**/*.scala` units merged per package
- No macros, no implicits, no HKT beyond `IO`, no null

### Expansion rules

1. New compiler features must land in Stage 0 **before** compiler sources depend on them.
2. Prefer AST shapes and golden LLVM tests that port cleanly to ScalUI Stage 1.
3. Runtime GC / fibers / Skia remain C/Rust; not part of the kernel dialect.

## Consequences

- Parser/codegen stay small and portability-oriented.
- Drift is a CI failure mode (dual-boot later), not a doc-only aspiration.
- `compiler-scalui/` begins the Stage 1 port (parser ADTs + classify smoke) under Stage 0.
