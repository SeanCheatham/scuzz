# Gaps and unknowns

What is unproven or missing, ranked by how much it threatens the thesis in [`vision.md`](vision.md). Two categories:

- **Unknowns** — bets we have not yet demonstrated are possible within our constraints. Each carries design risk; a bad outcome invalidates downstream work.
- **Known gaps** — pure work. The design is settled; nobody has done it yet.

When a gap closes or its assessment changes, update this file and (if direction changes) `vision.md`.

## Unknowns

### 1. Real text rendering through `sk_capi`

**Current state.** All text is an 8×8 ASCII bitmap font baked into `crates/ffi-skia/src/sk_sw.c` (`FONT8`, printable 32–126). Layout measures text as `strlen * font_px` (`text_width` in `crates/runtime/src/view.c`); theme `font_px` defaults to 8. `scripts/fetch_skia.sh` can drop prebuilts under `third_party/skia/prebuilt/` when `SCUZZ_SKIA_URL` is set, but nothing wires them into a build.

**Unproven.** Whether the thin `sk_capi` ABI can absorb real Skia — glyph shaping, font metrics, anti-aliasing, non-monospace widths — without breaking the layout model or the structural-golden discipline. Everything about the UI face (layout calibration, theme tokens, goldens, future GPU presenters) is currently calibrated against a fake monospace font, so this is the load-bearing unknown for a Flutter-shaped product.

**Proof.** A `[ui]` app links a fetched Skia prebuilt behind unchanged `sk_capi.h`, text measures via real font metrics, structural (a11y) goldens stay logical-only and stable, and `sk_sw` remains the zero-dependency CI fallback.

### 2. True concurrency without losing determinism

**Current state.** `IO` is a single-threaded trampoline (`run_io` / `sz_io_unsafe_run` in `crates/runtime/src/runtime.c`). `race` runs both sides sequentially (with a swap when one side is a pure sleep); `both` is sequential; `Queue.take` fails when empty and `Deferred.get` fails when incomplete instead of parking a fiber. Live `sleep` is `nanosleep`.

**Unproven.** Whether the runtime can gain genuine concurrency — an event loop or threads, parked/woken fibers, a `race` that actually races — while preserving the determinism that fuzz, laws, TestRuntime, and goldens depend on. Determinism is the crown jewel; naive concurrency destroys replayability. The open design question is what the deterministic semantics of concurrent `IO` even are (virtual-time scheduling under TestRuntime, stable interleaving order, `pump` as the only observable clock for UI).

**Proof.** `race` / `both` / `Queue` / `Deferred` behave concurrently in live mode, TestRuntime replays the same program to identical dumps across runs, and `scuzz fuzz --replay` stays byte-stable (including residual law checks once laws land).

### 3. Memory strategy under long-lived apps

**Current state.** libc `malloc`/`free` via `sz_alloc` / `sz_free`, manual ownership, `Resource` brackets; panic may leak (see GC decision in `vision.md`). No refcounting, arenas, or collector. Alloc accounting exists: size header before the user pointer, `sz_alloc_stats` / `sz_alloc_reset_stats`, optional `SCUZZ_ALLOC_TRACE=1` peak/live samples every N pumps in `sz_ui_pump_sync`. Runtime unit tests cover free-to-baseline and flat-ish live count across pumps. Optional `make -C crates/runtime test-asan` (ASan; best-effort, not default CI).

**Unproven.** Whether manual ownership holds up in a long-running interactive app: RSS growth over thousands of pumps, fragmentation, leaks from signal/view graphs that outlive a frame. The vision defers a collector "when long-lived interactive graphs demand it" — but without a long-lived RSS proof we do not know whether that point is near or far.

**Proof.** A stay-open app (`examples/live`-shaped) driven for thousands of pumps shows flat RSS, and a leak check (e.g. ASan/LSan in CI on the runtime tests) is clean on the golden examples. Partially addressed: accounting + unit tests + optional `test-asan`; full long-lived RSS proof remains open.

### 4. Language growth vs the self-host ratchet

**Current state.** Stage 0 supports payload ADTs with a single `Int` or `String` field (`EnumCase { name, fields }`, `sz_adt_payload`, match binders) — proven by Stage-0 unit tests. `examples/adt` and `compiler-scuzz/` stay nullary-only so the dual-boot gate does not need the new syntax yet. `List` is monomorphic; the only type constructor is builtin `IO[T]`. No records, traits, or user generics in either compiler, though `compatibility.md` keeps "generics (monomorphize early)" as direction. The self-hosted compiler (~3,100 lines under `compiler-scuzz/src/`) still contorts around nullary/string-tag encodings.

**Unproven.** Whether the Stage 0 → 1 → 2 ratchet stays tractable once features are structurally big. Every feature lands in Stage 0 first, then gets ported into the kernel dialect, then must survive the fixpoint gate (`scripts/selfhost.sh`, byte-identical Stage-2/3 IR). Payload ADTs are the first stress test: Stage 0 is in; **self-host adoption** (`compiler-scuzz/` parse/type/codegen + rewriting compiler IR/AST to use payloads) is the remaining proof. Generics + early monomorphization is the second, larger one.

**Proof.** Payload ADTs land in Stage 0 (done), `compiler-scuzz/` adopts them for its own IR/AST types, and the dual-boot gate still passes with the fixpoint intact.

### 5. Mobile on real devices

**Current state.** `crates/embedder-mobile/` is a host shell (`SCUZZ_MOBILE_SHELL=1`) plus explicitly compile-shaped packaging stubs — Android `shells/android/` (manifest, stub gradle, JNI hooks) and iOS `shells/ios/` (plist, ObjC delegate). No NDK or Xcode build has ever run.

**Unproven.** The entire device chain: cross-compiling the LLVM-emitted app + C runtime + `sk_sw` for arm64, JNI/ObjC embedding, surface/present, touch and soft-keyboard input on hardware. Consciously deferred (host Mobile peer first), but zero of it is demonstrated.

**Proof.** One example (counter) runs on one real device or simulator via `scuzz package` plus the platform toolchain.

### 6. GPU presenters (Impeller / Skia GPU)

**Current state.** Deferred by decision, with the constraint that presenters "must not change `Ui` session or logical goldens" — asserted, never demonstrated. Only the CPU raster path exists.

**Unproven.** That the presenter seam actually holds: a GPU backend behind `sk_capi` with identical structural dumps and (tolerance-bounded) pixels. Downstream of unknown 1; do not attempt before real Skia is wired.

**Proof.** A second presenter renders the golden examples with unchanged structural goldens.

## Known gaps

### File-as-module (vs merge-all `src/`)

Vision direction: `scuzz.toml` package = crate, `Foo.scuzz` = module ([vision.md](vision.md#modules-and-source-shape)). Stem pairing for `*.scuzz_sim` / `*.scuzz_laws` is implemented; Stage 0/1/2 still merge all live `src/**/*.scuzz` into one program with a single `@main` (no per-file visibility / namespaces yet).

### Diagnostics source locations (Stage 0)

Stage 0 threads `Span { file, start, end }` from lexer → parser → `Expr { kind, span }` → typer; `check` / `--message-format=json` emit line/column. Remaining: port the same span model into `compiler-scuzz` (self-host).

### Dependency forms beyond `path`

`scuzz.toml` supports path deps only (enforced in `crates/compiler/src/manifest.rs` and `compiler-scuzz/src/Toml.scuzz`; see [`schemas/scuzz-toml.md`](schemas/scuzz-toml.md)). Git and versioned/hosted artifacts are direction, not implementation. No registry exists. Deliberately last: no ecosystem theater before there is an ecosystem.

### Deferred by decision (not blocked)

- **Flutter-style constraint layout** — today a single recursive stacker (`layout_node` in `view.c`): column/row/list/scroll with pad/gap. Locked direction: constraints down, sizes up, when the widget set grows. Real text metrics (unknown 1) should land first, since they change every intrinsic size.
- **Windows desktop embedder** — same session protocol as X11/Cocoa; secondary platform.
- **LSP / editor tooling** — `fmt`, `check --message-format=json` (with line/column in Stage 0), and `watch` exist; a language server does not. Self-host span parity is the remaining prerequisite for useful LSP diagnostics from Stage 1/2.
- **macOS in default CI** — the macOS job is `workflow_dispatch`-only for Actions cost; Darwin regressions surface late.
