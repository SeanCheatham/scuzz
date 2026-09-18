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

**Unproven.** An evaluator written in Scuzz produces the same observable output as the emitted binary on every example. An evaluator `scuzz fuzz` campaign on an app-sized package finishes in less wall-clock time than the compiled campaign, with identical summaries. Interpreted steps are slower; rebuilds and process spawns are gone. The balance is not measured.

**Proof.** CI diffs `scuzz eval` against `scuzz run` on `examples/hello`, `examples/kernel`, and `examples/io`. `scuzz fuzz` on `examples/counter`, `examples/webhook`, and `examples/api-report` reports the same kill, coverage, and reach results on both engines and completes faster on the evaluator. Arc and slices: [`vision.md`](vision.md#evaluator-arc).

## Known gaps

Next work improves general language usability. Prioritize compiler correctness, memory ownership, type composition, standard kits, and tooling. Examples prove these capabilities. Locks: [`philosophy.md`](philosophy.md).

### Cuts

Do not add user FFI, `extern`, or plugins. Determinism and effect capture are not settled.

Do not add library publishing, git or registry deps, or `scuzz add`. Path deps stay. A hosted registry may never ship.

### Thesis-critical

Resolve these gaps when they prevent ordinary language use.

1. **Checker and emit residuals** — A Queue or Deferred payload pins at the first offer or complete in the same for-comprehension, including nested expressions and payloads seen through a lambda parameter over a collection. Applying an env-bound lambda with an unresolved parameter letter to a concrete argument fails check. An unannotated lambda in argument or def-body position binds its parameter from the expected function type, so a loose lambda can no longer escape through an expected function type. A generic def pins its own type parameters for every check in its body. Kit argument checks pin the caller type after substitution. Unbound kit parameters still match through `Type.eq`. Parse Param/Fun stay strings. A path-dep file over 40k keeps def heads with a stub body so Check can resolve a qualified call. Tuple components and constructor fields compare String, Int, and Bool literals. Constructor, tuple, cons, as, and `[]` patterns nest in those positions.

2. **Compile-time performance** — `scuzz check examples/compiler` is 17 s. A cold `scuzz build examples/tyck` is 34 s. Emitted string literals intern to pinned allocations. Remaining cost: RC retain/release churn and `sz_list_concat` in string building. Coverage still parses a compiled graph that differs from live.

### Table-stakes

Required for CLI, server, and desktop applications.

Filesystem symbolic links, extended metadata preservation, and power-loss durability remain open.

`Map` / `Set` keys beyond `Int` or `String`. `scuzz eval` UI kits, the live signal readers (`Property.signal*`, `Property.a11yHas`), and `Fuzz.*`: the prefixes in `Eval.excludedKits()` (evaluator arc, [`vision.md`](vision.md#evaluator-arc)). `scuzz fuzz` on the evaluator: step 2 of the fuzz engine slice. Time parse and zones. Generators. Drive `==` wrap on UI. OS threads. HTTPS serve with app cert and key files.

### Later

Do not start FFI, plugins, or a package registry. Other later items stay parked.

Generated setup inputs. Multiple named scenarios and campaign selection. Stable scroll keys. Simulation faults. Semantic mutants. Schedule replay: `schedule_seed` replays a PRNG walk over fiber creation order and contention steps, not recorded decisions. A code change elsewhere in the program can shift the interleaving under the same seed and turn a pinned concurrency failure green. Fix: record the fiber picked at each contention step in the corpus entry and report drift on replay. Do it in the evaluator scheduler first (evaluator arc). Windows desktop. OS IME candidate windows. macOS release packaging in default CI. Developer ID signing and notarization. Full web accessibility. Real phone and screen-reader checks. Hot reload on web. Multiple UI factories in host hot reload. Oracle mining. Emit scalar fallbacks. Dogfood IDE: native file dialogs, menus, multi-window, multi-cursor, minimap, Git UI, debugger, plugin host, custom canvas kit.
