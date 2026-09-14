# Gaps and unknowns

What is unproven or missing, ranked by how much it threatens the thesis in [`vision.md`](vision.md).

- **Unknowns** — claims not yet shown within our constraints. A bad outcome invalidates later work.
- **Known gaps** — settled design. Work is unfinished or deferred on purpose.

State what is missing. Do not record what landed. When a gap closes or its assessment changes, update this file. If direction changes, also update `vision.md`.

## Unknowns

### 1. Mobile on real devices

**Unproven.** JNI/ObjC embedding on hardware. Touch and soft-keyboard text input on hardware. OpenSSL on the Android NDK or iOS (do not vendor OpenSSL-for-iOS). Hardware runs need provisioning.

**Proof.** One example (counter) runs on one device or simulator with `scuzz package` plus the platform toolchain. That bar stays host-gated. Hardware device runs stay open.

### 2. GPU presenters (Impeller / Skia GPU)

**Unproven.** A GPU rasterizer (Impeller or Skia GPU) behind `sk_capi` keeps identical structural dumps and tolerance-bounded pixels without a CPU paint pass.

**Proof.** `SCUZZ_SKIA=gpu` renders `examples/counter` with unchanged live structural dumps. `scuzz fuzz --differential --iterations 0` on counter is the host proof. Unused GPU stubs do not close the proof.

## Known gaps

Next work is app-shaped stdlib and real tooling. Checker Fun-string residuals are not the next slice. Locks: [`vision.md`](vision.md).

### Cuts

Do not add user FFI, `extern`, or plugins. Determinism and effect capture are not settled.

Do not add library publishing, git or registry deps, or `scuzz add`. Path deps stay. A hosted registry may never ship.

### Thesis-critical

Not the next slice.

1. **Checker and emit residuals** — `Map.empty`, `Set.empty`, and `List.empty` still use a bare constructor. A Queue or Deferred handle has no payload until the first offer or complete. Param letters (`A`/`E`) still unify. Parse Param/Fun stay strings. A path-dep file over 40k keeps def heads with a stub body so Check can resolve a qualified call.

2. **Compile-time performance** — `scuzz check examples/compiler` is 16 s. A cold `scuzz build examples/tyck` is 1 m 10 s. Remaining cost: RC retain/release churn and `sz_list_concat` in string building. Coverage still parses a compiled graph that differs from live.

### Table-stakes

Needed before a real CLI, server, or desktop app stays. Next work lives here.

Time parse and zones. Regex capture and replace. Hex/base64 codecs. UUID. HMAC. `Map` / `Set` keys beyond `Int` or `String`. `scuzz eval`. Generators. Drive `==` wrap on UI. OS threads. HTTPS serve with app cert and key files.

### Later

Do not start FFI, plugins, or a package registry. Other later items stay parked.

Generated setup inputs. Multiple named scenarios and campaign selection. Stable scroll keys. Simulation faults. Semantic mutants. Windows desktop. OS IME candidate windows. macOS full packaging in default CI. Full web accessibility. Real phone and screen-reader checks. Hot reload on web. Oracle mining. Emit scalar fallbacks. Dogfood IDE: native file dialogs, menus, multi-window, multi-cursor, minimap, Git UI, debugger, plugin host, custom canvas kit.
