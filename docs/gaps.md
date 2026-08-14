# Gaps and unknowns

What is unproven or missing, ranked by how much it threatens the thesis in [`vision.md`](vision.md).

- **Unknowns** — bets not yet demonstrated within our constraints. A bad outcome invalidates downstream work.
- **Known gaps** — settled design; unfinished or deliberately deferred work.

When a gap closes or its assessment changes, update this file and (if direction changes) `vision.md`.

## Unknowns

### 1. Mobile on real devices

**Status.** Blocked on Android NDK and/or Xcode. `crates/embedder-mobile/` is a host shell (`SCUZZ_MOBILE_SHELL=1`) plus packaging stubs (`shells/android/`, `shells/ios/`). No NDK/Xcode build has run; no toolchain invocation in Makefiles or CI.

**Unproven.** Cross-compiling LLVM app + C runtime + `sk_sw` for arm64, JNI/ObjC embedding, surface/present, and touch/soft-keyboard on hardware.

**Proof.** One example (counter) runs on one device or simulator via `scuzz package` plus the platform toolchain. Further stubs do not close the proof.

### 2. GPU presenters (Impeller / Skia GPU)

**Status.** Unblocked on text/metrics. Only the CPU raster path exists today (`sk_sw` or fetched Skia CPU prebuilt). Presenters must not change `Ui` session or logical goldens — asserted, not demonstrated.

**Unproven.** That a GPU backend behind `sk_capi` keeps identical structural dumps and tolerance-bounded pixels.

**Proof.** A second presenter renders the golden examples with unchanged structural goldens. Unused GPU stubs do not close the proof.

## Known gaps

### Near-term (in-source oracles + drivers)

Vision locks **oracles in source, drivers as the test surface** ([`vision.md`](vision.md#laws-simulation-mutation-and-verification)). Stage 0 and `compiler-scuzz/` parse top-level `law name: Bool = …`, provide `.require` (type-preserving application; residual `Law.check` / sequenced `Law.assert`), `Law.check` / `Law.sometimes`, load stem-paired `*.scuzz_drivers`, and attach `where` refinements on `def` params and `record` fields. Unused laws fail `check`.

Open questions parked here: driver argument generation beyond `Int`/`String`; corpus-guided prefix extension (CLI-only).

### Near-term (AI-friendly tooling)

- **Hot reload and debugging tools** — Headless + in-process reload + debug for agents. Headless is a peer; `scuzz watch` only rebuilds; `[ui]` `run --watch` stamp-reloads the View tree (Signals stay), writes `build/debug.dump`, and plays `build/inject.script`. Still missing: new machine code in-process and deeper agent debug UX. Do not document `watch` as hot reload.

### Residuals

- **Net** — IPv6 and CNAME-follow are out.
- **Concurrency** — cooperative fibers only; `IO.ensure` / `Resource` release on cancel (including `IO.timeout` / `Fiber.interrupt`). Language `Fiber.fork` / `join` / `interrupt` and `IO.forever` / `repeatN` / `retryN` are in. Later: OS threads, supervision trees.
- **Memory** — counter-shaped Headless pumps stay flat under alloc accounting; `Signal.list` frees unshared cons spines. Later: a collector if list-churn still demands it.
- **Language surface** — richer generics beyond monomorphized defs/enums/records ([`compatibility.md`](compatibility.md)).
- **Richer mutation** — `scuzz mutate` (Stage 1/2) negates residual `Law.check` / `Law.assert` predicates and probes each mutant once under TestRuntime. Survivors are weak or unreached oracles. Still missing: more IR operators, and per-mutant fuzz scripts/schedules. No external mutators.

### Dependency forms beyond `path`

Path deps only (`manifest.rs`, `Toml.scuzz`). Git / versioned / hosted artifacts are direction; no registry.

**Deferred (deliberately last).** No ecosystem theater before there is an ecosystem. Revisit after path deps + file-as-module are the proven reuse story.

### Deferred by decision

- **Flutter-style constraint layout** — recursive stacker today (`layout_node` in `view.c`). Locked direction: constraints down, sizes up. Widget set is in [`vision.md`](vision.md#layout-model); do not grow CSS-ish rules or Flutter-style constraint-overflow dumps.
- **Windows desktop embedder** — same session protocol as X11/Cocoa; secondary platform.
- **OS IME candidate windows** — focused TextField caret uses measured advance (`sz_view_caret_rect`); embedders do not yet place OS IME UI from it.
- **LSP / editor tooling** — `fmt`, `check --message-format=json`, and `watch` exist; no language server. JSON diagnostics are the single editor protocol; LSP must wrap `scuzz check --message-format=json`.
- **macOS in default CI** — macOS job is `workflow_dispatch`-only; Darwin regressions surface late.
