# Guidance for coding agents

## Where things live

| Doc | Purpose |
| --- | --- |
| [`docs/vision.md`](docs/vision.md) | Product thesis, decisions, language direction, open work, risks |
| [`docs/compatibility.md`](docs/compatibility.md) | Keep/cut vs Scala & Typelevel; platforms; self-host stages |
| [`docs/guide.md`](docs/guide.md) | App author happy path |
| [`docs/schemas/scuzz-toml.md`](docs/schemas/scuzz-toml.md) | Package manifest schema |
| [`README.md`](README.md) | Quick start and current status only |

If a decision or next-step ordering changes, edit `vision.md`.

## Keep the codebase lightweight

Scuzz Lang is already a language + runtime + UI + tooling bet. **Ruthless subset and vertical slices win.**

- **No docs sprawl.** Do not add README/GUIDE/ARCHITECTURE files per crate “for completeness.” Stub crates may have a short README pointing at role; otherwise document in `docs/` or in code comments. Update existing docs instead of creating siblings.
- **No premature abstraction.** One clear implementation beats traits/factories/indirection for a single caller. Introduce seams when a second backend or Stage-1 port needs them (e.g. Headless vs Window), not before.
- **No ecosystem theater.** No Maven/JVM/cats compatibility layers, no “just in case” dependency graphs, no copying Scala Native structure for familiarity.
- **Forwards-only.** Do not maintain backwards compatibility, migration shims, or legacy code paths—especially during the prototype / pre-v1 phase. Prefer deleting and rewriting call sites over keeping dual APIs.
- **Vertical slice over scaffolding.** Prefer a working hello / Counter path to empty module trees and placeholder APIs.
- **Headless-first for UI.** Never land Window-only UI behavior; Headless is a peer runtime (see `vision.md`).
- **Kernel dialect discipline.** Compiler and bootstrap sources stay inside what Stage 0 can emit until Stage 1 catches up (`vision.md` kernel section).
- **No agent- or tool-specific references in the repo.** Keep CI, docs, and code project-owned—not tied to a particular coding agent, branch naming scheme, or vendor workflow.
- **No historical record-keeping in tree.** Present-tense docs and comments; no phase diaries, “landed” changelogs, or dangling references to removed paths.

## Default workflow

1. Check `docs/vision.md` for intent, locks, and current direction.
2. Implement the smallest slice that proves the behavior (test or `examples/` when applicable).
3. Update `vision.md` (or `compatibility.md` / `guide.md` when that file owns the topic).
