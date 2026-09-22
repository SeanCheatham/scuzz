# Gaps and unknowns

What is unproven or missing, ranked by how much it threatens the thesis in [`philosophy.md`](philosophy.md).

- **Unknowns** — claims not yet shown within our constraints. A bad outcome invalidates later work.
- **Known gaps** — settled design. Work is unfinished or deferred on purpose.

State what is missing. Do not record what landed. When a gap closes or its assessment changes, update this file. If direction changes, also update `philosophy.md`.

## Unknowns

### 1. Mobile on real devices

**Unproven.** JNI/ObjC embedding on hardware. Touch and soft-keyboard text input on hardware. OpenSSL on the Android NDK. iOS clients use the Apple transport. Hardware runs need provisioning.

**Proof.** One example (counter) runs on one device or simulator with `scuzz package` plus the platform toolchain. That bar stays host-gated. Hardware device runs stay open.

The local iOS loop targets arm64 simulators on iOS 16 or later. Source edits reload the View and preserve app state. Manifest changes and the r command restart the app. Physical device signing and release distribution remain open. iOS supports Net clients with platform certificate trust. Net HTTP servers remain host-only. Android packages reject Net calls because they do not link OpenSSL.

### 2. GPU presenters (Impeller / Skia GPU)

**Unproven.** A GPU rasterizer (Impeller or Skia GPU) behind `sk_capi` keeps identical structural dumps and tolerance-bounded pixels without a CPU paint pass.

**Proof.** `SCUZZ_SKIA=gpu` renders `examples/counter` with unchanged live structural dumps. `scuzz fuzz --differential --iterations 0` on counter is the host proof. Unused GPU stubs do not close the proof.

### 3. Evaluator parity and speed

**Partly proven.** An evaluator written in Scuzz produces the same observable output as the emitted binary on every example. `scuzz fuzz` on `examples/webhook` and `examples/api-report` writes the same `summary.json` on both engines (`scripts/ci-fuzz.sh`). `examples/io` also matches on both engines: a scheduler step is one effect, so the extra `IO` wrapping in the evaluator does not move the interleaving. Speed is even, not better: one `scuzz eval --probe` server per file set checks the package once and forks a child per probe, and the evaluator campaign on `examples/api-report` takes the same wall clock as the compiled one. Every drive step still interprets: the evaluator idle probe on `examples/kernel` (`countdown(1000000)`) takes 12 s and on `examples/fmt` 18 s on the checkout host, under the 20-second deadline, so the `examples/kernel` campaign runs about five times longer than compiled. A slower host falls back to compiled probes at the idle gate.

**Proof.** CI diffs `scuzz eval` against `scuzz run` on `examples/hello`, `examples/kernel`, and `examples/io`. `scripts/ci-fuzz.sh` prints wall clock for both engines on `examples/webhook`, `examples/api-report`, and `examples/io` and diffs the summaries. Distance feedback does not move those summaries yet; a package where it does needs a looser check (mutation and corpus equal, evaluator reach a superset). The open half: the evaluator campaign completes faster. Arc and slices: [`vision.md`](vision.md#evaluator-arc).

## Known gaps

Next work improves general language usability. Prioritize compiler correctness, memory ownership, type composition, standard kits, and tooling. Examples prove these capabilities. Locks: [`philosophy.md`](philosophy.md).

### Cuts

Do not add user FFI, `extern`, or plugins. Determinism and effect capture are not settled.

Do not add library publishing, git or registry deps, or `scuzz add`. Path deps stay. A hosted registry may never ship.

### Thesis-critical

Resolve these gaps when they prevent ordinary language use.

1. **Checker and emit residuals** — A Queue or Deferred payload pins at the first offer or complete in the same for-comprehension, including nested expressions and payloads seen through a lambda parameter over a collection. Applying an env-bound lambda with an unresolved parameter letter to a concrete argument pins the parameter at the first concrete apply in the same for-comprehension. An apply outside that comprehension still asks for an annotation. An unannotated lambda in argument or def-body position binds its parameter from the expected function type, so a loose lambda can no longer escape through an expected function type. A generic def pins its own type parameters for every check in its body. Kit argument checks pin the caller type after substitution. Unbound kit parameters other than a pinned list, map, or set still match through `Type.eq`. Parse Param/Fun stay strings. A path-dep file over 40k keeps def heads with a stub body so Check can resolve a qualified call. Tuple components and constructor fields compare String, Int, and Bool literals. Constructor, tuple, cons, as, and `[]` patterns nest in those positions.

2. **Compile-time performance** — `scuzz check examples/compiler` is 20 s. A cold `scuzz build examples/tyck` is 47 s. Emitted string literals intern to pinned allocations. Remaining cost: RC retain/release churn and `sz_list_concat` in string building. Coverage still parses a compiled graph that differs from live.

### Table-stakes

Required for CLI, server, and desktop applications.

Filesystem symbolic links, extended metadata preservation, and power-loss durability remain open.

`Map` / `Set` keys beyond `Int` or `String`. The live signal readers (`Property.signal*`, `Property.a11yHas`) and `Fuzz.*` at `Value` (the probe entry calls them natively): the prefixes in `Eval.excludedKits()` (evaluator arc, [`vision.md`](vision.md#evaluator-arc)). `scuzz fuzz` on the evaluator for a `[ui]` package. `View` as a reference-counted value: the tree owns views, a list signal frees the lists `View.each` never mounted, and a view pulled out of a list by hand stays unsafe ([`philosophy.md`](philosophy.md), "The tree owns views"). Docs `Mount.scuzz` does not mount `View.each`. Time parse and zones. Drive `==` wrap on UI. OS threads. HTTPS serve with app cert and key files.

### Verification

The one testing strategy is mutation, fuzz, properties, simulation, coverage, and determinism. These gaps weaken that strategy. Rank is threat order. Arc: [`vision.md`](vision.md#verification-arc). Locks: [`philosophy.md`](philosophy.md#verification-posture).

1. **URLSession and Skia pixels.** Host loopback OpenSSL replay is `scuzz fuzz --live`. URLSession, TLS error cases, and Skia pixels have no fuzz home. `--differential` compares structural dumps.

### Later

Do not start FFI, plugins, or a package registry. Other later items stay parked.

Generated setup inputs. Multiple named scenarios and campaign selection. Fault storms and partial writes. Stable scroll keys. Windows desktop. OS IME candidate windows. macOS release packaging in default CI. Developer ID signing and notarization. Full web accessibility. Real phone and screen-reader checks. Hot reload on web. Multiple UI factories in host hot reload. Oracle mining. Emit scalar fallbacks. Dogfood IDE: native file dialogs, menus, multi-window, multi-cursor, minimap, Git UI, debugger, plugin host, custom canvas kit.
