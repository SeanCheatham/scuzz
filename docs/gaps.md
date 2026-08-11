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

**Current state.** libc `malloc`/`free` via `sz_alloc` / `sz_free`, manual ownership, `Resource` brackets; panic may leak (see GC decision in `vision.md`). No refcounting, arenas, or collector. Nothing measures allocation behavior across `pump` boundaries.

**Unproven.** Whether manual ownership holds up in a long-running interactive app: memory growth per pump, fragmentation, leaks from signal/view graphs that outlive a frame. The vision defers a collector "when long-lived interactive graphs demand it" — but without measurement we do not know whether that point is near or far.

**Proof.** A stay-open app (`examples/live`-shaped) driven for thousands of pumps shows flat RSS, and a leak check (e.g. ASan/LSan in CI on the runtime tests) is clean on the golden examples.

### 4. Language growth vs the self-host ratchet

**Current state.** Enums are nullary-only (`EnumDef { name, cases: Vec<String> }` in `crates/compiler/src/ast.rs`; `parseEnum*` in `compiler-scuzz/src/Parser.scuzz`). `List` is monomorphic; the only type constructor is builtin `IO[T]`. No payload ADTs, records, traits, or user generics in either compiler, though `compatibility.md` keeps "generics (monomorphize early)" as direction. The self-hosted compiler (~3,100 lines under `compiler-scuzz/src/`) contorts around this with string-tag encodings.

**Unproven.** Whether the Stage 0 → 1 → 2 ratchet stays tractable once features are structurally big. Every feature lands in Rust Stage 0 first, then gets ported into the kernel dialect, then must survive the fixpoint gate (`scripts/selfhost.sh`, byte-identical Stage-2/3 IR). Payload ADTs (`case Some(x: Int)`) are the first real stress test — and the self-hosted compiler is their first customer. Generics + early monomorphization is the second, larger one.

**Proof.** Payload ADTs land in Stage 0, `compiler-scuzz/` adopts them for its own IR/AST types, and the dual-boot gate still passes with the fixpoint intact.

### 5. Mobile on real devices

**Current state.** `crates/embedder-mobile/` is a host shell (`SCUZZ_MOBILE_SHELL=1`) plus explicitly compile-shaped packaging stubs — Android `shells/android/` (manifest, stub gradle, JNI hooks) and iOS `shells/ios/` (plist, ObjC delegate). No NDK or Xcode build has ever run.

**Unproven.** The entire device chain: cross-compiling the LLVM-emitted app + C runtime + `sk_sw` for arm64, JNI/ObjC embedding, surface/present, touch and soft-keyboard input on hardware. Consciously deferred (host Mobile peer first), but zero of it is demonstrated.

**Proof.** One example (counter) runs on one real device or simulator via `scuzz package` plus the platform toolchain.

### 6. GPU presenters (Impeller / Skia GPU)

**Current state.** Deferred by decision, with the constraint that presenters "must not change `Ui` session or logical goldens" — asserted, never demonstrated. Only the CPU raster path exists.

**Unproven.** That the presenter seam actually holds: a GPU backend behind `sk_capi` with identical structural dumps and (tolerance-bounded) pixels. Downstream of unknown 1; do not attempt before real Skia is wired.

**Proof.** A second presenter renders the golden examples with unchanged structural goldens.

## Known gaps

### Module laws + sim overlays

Vision locks app verification as stem-paired `Foo.scuzz` / `Foo.scuzz_sim` / `Foo.scuzz_laws`: same-name sim replacements under fuzz/TestRuntime, pure laws as the fuzz oracle, no app-level unit-test trees ([vision.md](vision.md#laws-simulation-and-verification)). Nothing of that toolchain exists yet — `scuzz fuzz` still fails only on panic/`SzError`, and `scuzz test` is goldens (UI) or exit-0 smoke (IO). Smallest slice: one example (`examples/counter` or `todo`) with a `*.scuzz_laws` predicate over the signal/a11y dump, residualized under `SCUZZ_TESTRT=1`, failing fuzz with `repro.toml` when broken; then a `*.scuzz_sim` that swaps one app-owned value (e.g. a URL string).

### File-as-module (vs merge-all `src/`)

Vision direction: `scuzz.toml` package = crate, `Foo.scuzz` = module ([vision.md](vision.md#modules-and-source-shape)). Today Stage 0/1/2 still merge all `src/**/*.scuzz` into one program with a single `@main`. Pure work once laws/sim need per-module overlay resolution — start with stem pairing for sim/laws without full visibility/namespaces if that unblocks the verification slice sooner.

### Desktop keyboard input into `TextField`

`View.textField` works: focus-on-tap, `sz_view_handle_text`, `SZ_INPUT_TEXT` events, Headless script `text <s>`, used by `examples/todo`. But the desktop embedders (`x11_present.c`, `macos_present.m`) only map q / Escape to quit — no keystroke ever reaches a `TextField`, so you cannot type into a Scuzz app in a window. Smallest high-value slice: translate X11/Cocoa key events into `SZ_INPUT_TEXT` (plus backspace), keeping Headless the behavioral reference. IME is out of scope until real text rendering (unknown 1) lands.

### Diagnostics carry no source locations

`Diagnostic { severity, message, file, line, column }` exists in both compilers with `--message-format=json` (`crates/compiler/src/check.rs`), but `line`/`column` are always `None` and `file` is a heuristic — the typer only produces unlocated strings (`TypeError::Msg` in `crates/compiler/src/typ.rs`). Pure work: thread spans from lexer through typer in Stage 0, then port. Hurts every user of the language, including compiler development itself.

### Dependency forms beyond `path`

`scuzz.toml` supports path deps only (enforced in `crates/compiler/src/manifest.rs` and `compiler-scuzz/src/Toml.scuzz`; see [`schemas/scuzz-toml.md`](schemas/scuzz-toml.md)). Git and versioned/hosted artifacts are direction, not implementation. No registry exists. Deliberately last: no ecosystem theater before there is an ecosystem.

### Deferred by decision (not blocked)

- **Flutter-style constraint layout** — today a single recursive stacker (`layout_node` in `view.c`): column/row/list/scroll with pad/gap. Locked direction: constraints down, sizes up, when the widget set grows. Real text metrics (unknown 1) should land first, since they change every intrinsic size.
- **Windows desktop embedder** — same session protocol as X11/Cocoa; secondary platform.
- **LSP / editor tooling** — `fmt`, `check --message-format=json`, and `watch` exist; a language server does not. Spans (above) are a prerequisite for a useful one.
- **macOS in default CI** — the macOS job is `workflow_dispatch`-only for Actions cost; Darwin regressions surface late.
