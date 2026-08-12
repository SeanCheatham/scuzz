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

### Residuals (closed bets)

- **Compiler IR onto payload ADTs** — self-host ratchet intact; compiler-source AST/IR families are payload ADTs (`Tok`, `InterpPart`, `ForBinder`, `Pattern`, `MatchArm`, `Span`, `EnumField`/`EnumCase`/`EnumDef`, `Param`/`Def`/`Prog`, `Expr` with `span` on every case), plus enum-typed def signatures (`normalizeTy`) and `Int`/`String`/`List`/ADT payload fields. Match arm join blocks keep nested if/match PHI-valid. Optional string-encoding follow-ons: [`ast_todo.md`](ast_todo.md). Records/traits/generics: [`compatibility.md`](compatibility.md).
- **Real text through `sk_capi`** — measure/text-size APIs on `sk_capi.h`; layout uses `sk_font_measure_string`; dual-backend Makefile defaults to `third_party/skia/prebuilt/<triple>/libsk_capi.a` (fail-closed fetch via `PIN` / `fetch_skia.sh`); `SCUZZ_SKIA=sk_sw` opts into the in-tree software backend; as-needed `.github/workflows/skia-cpu.yml` + `scripts/build_skia_prebuilt.sh` produce a fat `sk_capi` archive. Structural goldens remain logical-only and stay green against real Skia.
- **Concurrency** — cooperative fibers + TestRuntime virtual-time jumps cover the determinism thesis (`test_io.c`). Out of scope / later: OS threads, interruptible cancel mid-`nanosleep`, supervision trees, Scuzz bindings for Queue/Deferred.
- **Memory** — counter-shaped Headless pumps stay flat under alloc accounting (`test_ui.c`, optional `test-asan`). Later: `Signal.list` collection, exit-time signal ownership / LSan-clean examples, a collector if list-churn demands it.
- **File-as-module** — stem namespaces on Stage 0 and self-host (`examples/modules`). Later: `private`/`pub`, `import`, enum-per-module.
- **Diagnostics spans** — Stage 0 and self-host emit `file:line:column` (and JSON). Done.

### Dependency forms beyond `path`

Path deps only (`manifest.rs`, `Toml.scuzz`). Git / versioned / hosted artifacts are direction; no registry.

**Deferred (deliberately last).** No ecosystem theater before there is an ecosystem. Revisit after path deps + file-as-module are the proven reuse story.

### Deferred by decision

- **Flutter-style constraint layout** — recursive stacker today (`layout_node` in `view.c`). Locked direction: constraints down, sizes up. Real text metrics exist; constraint layout not started.
- **Windows desktop embedder** — same session protocol as X11/Cocoa; secondary platform.
- **LSP / editor tooling** — `fmt`, `check --message-format=json`, and `watch` exist; no language server.
- **macOS in default CI** — macOS job is `workflow_dispatch`-only; Darwin regressions surface late.
