# Gaps and unknowns

What is unproven or missing, ranked by how much it threatens the thesis in [`vision.md`](vision.md). Two categories:

- **Unknowns** — bets we have not yet demonstrated are possible within our constraints. Each carries design risk; a bad outcome invalidates downstream work.
- **Known gaps** — pure work. The design is settled; nobody has done it yet.

When a gap closes or its assessment changes, update this file and (if direction changes) `vision.md`.

## Unknowns

### 1. Real text rendering through `sk_capi`

**Current state.** All text is an 8×8 ASCII bitmap font baked into `crates/ffi-skia/src/sk_sw.c` (`FONT8`, printable 32–126). Layout measures text as `strlen * font_px` (`text_width` in `crates/runtime/src/view.c`); theme `font_px` defaults to 8. `scripts/fetch_skia.sh` can drop prebuilts under `third_party/skia/prebuilt/` when `SCUZZ_SKIA_URL` is set, but nothing wires them into a build. `sk_capi.h` exposes draw-only text (`sk_canvas_draw_string`); there is no measure API, no Makefile dual-backend switch, and no hosted prebuilt tarball or ABI packaging contract under `third_party/skia/`.

**Unproven.** Whether the thin `sk_capi` ABI can absorb real Skia — glyph shaping, font metrics, anti-aliasing, non-monospace widths — without breaking the layout model or the structural-golden discipline. Everything about the UI face (layout calibration, theme tokens, goldens, future GPU presenters) is currently calibrated against a fake monospace font, so this is the load-bearing unknown for a Flutter-shaped product.

**Blocked (in-repo exhausted).** Proof requires a fetched Skia prebuilt that implements `sk_capi` (plus a documented tarball layout and link path). Vision forbids vendoring a Skia tree; no `SCUZZ_SKIA_URL` artifact exists; CI and linkers always build `sk_sw`. In-repo prep that does **not** close the proof: additive measure API + `text_width` wiring, Makefile prebuilt switch, tarball contract in `third_party/skia/README.md`. Do not attempt GPU presenters (unknown 6) until this is unblocked.

**Proof (when unblocked).** A `[ui]` app links a fetched Skia prebuilt behind `sk_capi.h`, text measures via real font metrics, structural (a11y) goldens stay logical-only and stable, and `sk_sw` remains the zero-dependency CI fallback.

### 2. True concurrency without losing determinism

**Current state.** `IO` runs on a **single-threaded cooperative fiber scheduler** (`run_io` in `crates/runtime/src/runtime.c`): FIFO ready queue, left-then-right fork for `race` / `both`, timer-heap parking for `sleep`, and park/wake for `Queue.take` / `Deferred.get`. Live idle waits with `nanosleep` to the next deadline; TestRuntime jumps virtual time to the soonest wakeup when all fibers are blocked on timers. No OS threads for IO concurrency.

**Proven (vertical slice).** Under TestRuntime: `race(sleep(100), sleep(1))` wins the 1ms path with clock += 1; `race(sleep, println)` wins println without advancing sleep; `both(println)` dumps are byte-stable across runs; `both(take, offer)` / `both(get, complete)` park and wake. Covered by `crates/runtime/tests/test_io.c`.

**Residual.** Multi-core / OS-thread parallelism is explicitly out of scope (would fight replay). Interruptible cancel mid-`nanosleep`, supervision trees, and Scuzz-language bindings for Queue/Deferred remain future work — not required for the determinism thesis.

### 3. Memory strategy under long-lived apps

**Current state.** libc `malloc`/`free` via `sz_alloc` / `sz_free`, manual ownership, `Resource` brackets; panic may leak (see GC decision in `vision.md`). No refcounting, arenas, or collector. Alloc accounting exists: size header before the user pointer, `sz_alloc_stats` / `sz_alloc_reset_stats`, optional `SCUZZ_ALLOC_TRACE=1` peak/live samples every N pumps in `sz_ui_pump_sync`. Runtime unit tests cover free-to-baseline, **2000-pump** static-label flatness, and **2000-pump counter-shaped** UI (`Signal.map` + `bindText` + periodic taps) with flat `live_count`/`live_bytes`. `Signal.map` frees the mapper's `SzString` after caching. Optional `make -C crates/runtime test-asan` runs in Linux CI (best-effort skip if unsupported).

**Proven (vertical slice).** Idle and light-interaction Headless pumps on counter-shaped UI stay flat under alloc accounting. That answers the core unknown for the v0 GC posture on the golden UI path.

**Residual knowns (not unknowns).** `Signal.list` / list cons cells remain arena-ish (no collection of orphaned lists); golden example binaries intentionally do not free signals at process exit (GC-v0); LSan-clean example binaries are out of scope until exit ownership is designed. A collector stays deferred until list-churn or larger graphs demand it.

### 4. Language growth vs the self-host ratchet

**Current state.** Stage 0 and self-host (`compiler-scuzz/`) support payload ADTs with a single `Int` or `String` field (`EnumCase` / `Case` + fields, `sz_adt_payload`, match binders) — proven by Stage-0 unit tests and `examples/adt` (`Opt.Some(42)`). Compiler sources stay on List+string-tag IR (multi-field cases would be needed for a full AST rewrite). `List` is monomorphic; the only type constructor is builtin `IO[T]`. No records, traits, or user generics in either compiler, though `compatibility.md` keeps "generics (monomorphize early)" as direction.

**Unproven.** Whether the Stage 0 → 1 → 2 ratchet stays tractable once features are structurally big. Every feature lands in Stage 0 first, then gets ported into the kernel dialect, then must survive the fixpoint gate (`scripts/selfhost.sh`, byte-identical Stage-2/3 IR). Payload ADTs for **user programs** are in (Stage 0 + self-host). Rewriting the compiler's own IR/AST onto payloads remains blocked on multi-field cases. Generics + early monomorphization is the next large stress test.

**Proof.** Payload ADTs land in Stage 0 (done), `compiler-scuzz/` compiles unary payload ADTs for user programs through the dual-boot gate (done for `examples/adt`), and a later multi-field / compiler-IR rewrite still has to keep the fixpoint intact.

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

### Diagnostics source locations

Stage 0 and self-host (`compiler-scuzz/`) thread `Span { file, start, end }` (byte offsets) from lexer → parser → expr → typer; `check` / `--message-format=json` emit `file` / `line` / `column`. Self-host exprs carry a trailing span on every expr node (`[tag, …children, span]`) so fixed-index `exprTag` / `nodeStr` / `nodeExpr` accessors stay valid.

### Dependency forms beyond `path`

`scuzz.toml` supports path deps only (enforced in `crates/compiler/src/manifest.rs` and `compiler-scuzz/src/Toml.scuzz`; see [`schemas/scuzz-toml.md`](schemas/scuzz-toml.md)). Git and versioned/hosted artifacts are direction, not implementation. No registry exists. Deliberately last: no ecosystem theater before there is an ecosystem.

### Deferred by decision (not blocked)

- **Flutter-style constraint layout** — today a single recursive stacker (`layout_node` in `view.c`): column/row/list/scroll with pad/gap. Locked direction: constraints down, sizes up, when the widget set grows. Real text metrics (unknown 1) should land first, since they change every intrinsic size.
- **Windows desktop embedder** — same session protocol as X11/Cocoa; secondary platform.
- **LSP / editor tooling** — `fmt`, `check --message-format=json` (with line/column in Stage 0 and self-host), and `watch` exist; a language server does not.
- **macOS in default CI** — the macOS job is `workflow_dispatch`-only for Actions cost; Darwin regressions surface late.
