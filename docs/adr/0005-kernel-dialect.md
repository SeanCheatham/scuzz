# ADR 0005 — Kernel dialect for self-host

## Status

Accepted (Phase 0)

## Context

Self-host requires the compiler sources to stay inside what Stage 0 can emit until Stage 1 catches up.

## Decision

Document a **kernel dialect**: the subset used by compiler sources and bootstrap examples.

### Kernel (Stage 0 → Phase 0/1)

- Top-level `@main def name: IO[Unit] = expr`
- `def` / `val` bindings (local vals in later slices)
- String literals, unit `()`
- Type syntax: `Unit`, `IO[Unit]`, later `IO[A]` for monomorphic A
- Calls: `IO.println(str)`, `IO.delay`, `.flatMap(cont)` with simple `_ => expr` or named params as added
- Phase 1 UI: `Ui.runHeadless(str)` → `IO[Unit]` (runtime drives mount/pump/snapshot; size via env / `scalui.toml` `[ui]`)
- No macros, no implicits, no HKT beyond `IO`, no null

### Expansion rules

1. New compiler features must land in Stage 0 **before** compiler sources depend on them.
2. Prefer AST shapes and golden LLVM tests that port cleanly to ScalUI Stage 1.
3. Runtime GC / fibers / Skia remain C/Rust; not part of the kernel dialect.

## Consequences

- Parser/codegen stay small and portability-oriented.
- Drift is a CI failure mode (dual-boot later), not a doc-only aspiration.
