# Guidance for coding agents

## Where things live

| Doc | Purpose |
| --- | --- |
| [`HUMANS.md`](HUMANS.md) | Human source of truth for product intent. Agents read it. Agents never edit it. |
| [`docs/vision.md`](docs/vision.md) | Product intent, locks, language direction, open work, risks |
| [`docs/gaps.md`](docs/gaps.md) | Unknowns and known gaps, ranked by risk |
| [`docs/plans.md`](docs/plans.md) | Next short-term slice only. Delete the slice when it is done. Do not keep history. |
| [`docs/optimization.md`](docs/optimization.md) | Later empirical pre-optimization (`*.scuzz_tune`). Not current work. |
| [`docs/compatibility.md`](docs/compatibility.md) | Keep or cut vs Scala and effect libraries. Platforms. Toolchain. |
| [`docs/guide.md`](docs/guide.md) | App author path from install to run |
| [`docs/developer-environment.md`](docs/developer-environment.md) | Checkout host setup |
| [`docs/schemas/scuzz-toml.md`](docs/schemas/scuzz-toml.md) | Package manifest schema |
| [`README.md`](README.md) | Pitch, install, one example |

If a decision or next-step order changes, edit `vision.md`. Keep `vision.md` aligned with `HUMANS.md`.

## Keep the codebase small

Scuzz Lang is a language, a runtime, UI, and tooling. Keep a small subset. Ship vertical slices.

- **No extra docs.** Do not add README, GUIDE, or ARCHITECTURE files per crate for completeness. A stub crate may have a short README that states its role. Put other docs in `docs/` or in code comments. Update existing files. Do not add sibling files.
- **No early abstraction.** Write one clear implementation for one caller. Add a seam only when a second backend needs it (Headless vs Desktop).
- **No ecosystem theater.** Do not add Maven, JVM, cats, or ZIO compatibility layers. Do not add unused dependency graphs. Do not copy Scala Native structure for familiarity.
- **Forwards only.** Do not keep backwards compatibility, migration shims, or legacy paths. Delete and rewrite call sites. Do not keep dual APIs.
- **Vertical slice over scaffolding.** Prefer a working hello or Counter path to empty module trees and placeholder APIs.
- **Headless first for UI.** Do not add Desktop-only UI behavior. Headless is a peer runtime. See `vision.md`.
- **One compiler.** The toolchain is Rust (`crates/cli`). Language proof is `examples/` that exercise the kernel. See `vision.md`.
- **No agent- or tool-specific references.** Keep CI, docs, and code owned by the project. Do not tie them to a coding agent, a branch naming scheme, or a vendor workflow.
- **No history in the tree.** Write docs and comments in present tense. Do not keep phase diaries, landed changelogs, or references to removed paths.

## Writing

Write all docs, README files, and comments in ASD-STE100 Simplified Technical English.

- Use short sentences. Put one idea in each sentence.
- Use simple present tense. Use active voice. Use imperative for instructions.
- Do not use slang, filler, or history.
- Do not add files for completeness.

## Default workflow

1. Read `HUMANS.md` for product intent. Do not edit it.
2. Read `docs/vision.md` for locks and current direction.
3. Add the smallest slice that proves the behavior (a test or `examples/` when it applies).
4. Update `vision.md`. If `compatibility.md` or `guide.md` owns the topic, update that file instead.
