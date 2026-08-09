# Guidance for coding agents

## Where planning and vision live

Read these before large changes. Prefer updating them over inventing parallel docs.

| Doc | Purpose |
| --- | --- |
| [`docs/vision.md`](docs/vision.md) | Product thesis, locked defaults, **phased roadmap (0–6)**, risks |
| [`docs/compatibility.md`](docs/compatibility.md) | What we keep / cut vs Scala & Typelevel; platform matrix; self-host stages |
| [`docs/adr/`](docs/adr/) | Accepted decisions (GC, Skia, IO errors, `Ui` vs `View`, kernel dialect) |
| [`docs/schemas/scalui-toml.md`](docs/schemas/scalui-toml.md) | Package manifest schema draft |
| [`README.md`](README.md) | Quick start and current status only |

Roadmap detail belongs in `docs/vision.md`. Design locks belong in ADRs. Do not restate the full plan in READMEs, issue templates, or new markdown files unless something genuinely does not fit those homes.

## Keep the codebase lightweight

ScalUI is already a language + runtime + UI + tooling bet. **Ruthless subset and vertical slices win.**

- **No docs sprawl.** Do not add README/GUIDE/ARCHITECTURE files per crate “for completeness.” Stub crates may have a short README pointing at the phase; otherwise document in `docs/` or in code comments where a future reader will actually look. Update existing docs instead of creating siblings.
- **No premature abstraction.** One clear implementation beats traits/factories/indirection for a single caller. Introduce seams when a second backend or Stage-1 port needs them (e.g. Headless vs Window), not before.
- **No ecosystem theater.** No Maven/JVM/cats compatibility layers, no “just in case” dependency graphs, no copying Scala Native structure for familiarity.
- **Forwards-only.** Do not maintain backwards compatibility, migration shims, or legacy code paths—especially during the prototype / pre-v1 phase. Prefer deleting and rewriting call sites over keeping dual APIs “for now.”
- **Vertical slice over scaffolding.** Prefer a working hello / Counter path to empty module trees and placeholder APIs.
- **Headless-first for UI.** Never land Window-only UI behavior; Headless is a peer runtime (see ADR 0004).
- **Kernel dialect discipline.** Compiler and bootstrap sources stay inside what Stage 0 can emit until Stage 1 catches up (ADR 0005).
- **No agent- or tool-specific references in the repo.** Keep CI, docs, and code project-owned—not tied to a particular coding agent, branch naming scheme, or vendor workflow.

## Default workflow

1. Check `docs/vision.md` for which phase the work belongs to.
2. Check relevant ADRs before changing GC, effects, UI boundaries, or Skia strategy.
3. Implement the smallest slice that proves the behavior (test or `examples/` when applicable).
4. Update the one doc that owns the decision—not a new parallel note.
