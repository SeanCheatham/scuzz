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

Close thesis-critical gaps before table-stakes kits. Close table-stakes before later items. Locks: [`vision.md`](vision.md).

### Thesis-critical

Close them in this order.

1. **Typed agent session schema** — One JSON schema for dump, inject, fuzz verdict, and coverage. The dump and inject surfaces landed (`v=1` through `.json` paths). The fuzz verdict surface landed (`build/fuzz/summary.json` covers fuzz, corpus, classify, mutate, and coverage). Open: typed a11y tree and typed list/value signal payloads. Text dump, script, and summary stay until the schema covers every surface. `--message-format=json` applies to `check` only until then.

2. **Source-region coverage** — Open: branch coverage.

3. **Scenario initialization and lifetime** — Residual: generated setup inputs, multiple named scenarios, and campaign selection.

4. **Checker and emit residuals** — Param letters (`A`/`E`) still unify. A bare kit return `IO` means some IO. The parser stores Fun/Param types as strings; Check parses them. A path-dep file over 40k keeps def heads with a stub body so Check can resolve a qualified call.

### Table-stakes

Needed before a real CLI, server, or desktop app stays.

- **HTTP as a server** — Status, headers, and `0.0.0.0` bind stay out. HTTPS `Net.serve` stays out. Expand `Net` on this HTTP/1.0 stack. Do not add a second client.
- **Missing kits** — No calendar time, regex, hash, hex/base64, or UUID. `Map` / `Set` keys are `Int` or `String`. Expand blessed kits. No user FFI.
- **`scuzz eval`** — No worksheet. A one-file eval helps humans and agents try one def.
- **Generators** — Argument reduction remains open. Direction: `Gen[T]` combinators and shrinking that keeps `where` bounds. Stateful model generators stay later.
- **Drive `==` wrap on UI** — A top-level `a == b` drive oracle wraps into a `for` that prints both sides. On a `[ui]` package that wrap can trip the unpaired-acquire session check. Write `if (a == b) true else false` until emit drops the extra retain.
- **Scheduler lock** — Cooperative fibers on one thread are the scheduler for CLI, server, and UI. OS threads and supervision trees stay later.

### Later

Do not start these before thesis-critical gaps close.

- **Stable inject keys** — `tap N` / `scroll N` follow a11y preorder. A refactor can miss a stored corpus entry. Named control keys for inject stay after named claim observations.
- **Simulation faults and multiple worlds** — Clock skew, partitions, and a model to relate against stay later.
- **Mutation depth** — Semantic mutants stay later.
- **Dependency forms beyond `path`** — Git, versioned, and hosted artifacts are direction. There is no registry. A lockfile identity can land before a registry.
- **Windows desktop embedder** — same session protocol as X11/Cocoa. Secondary platform.
- **OS IME candidate windows** — Embedders do not place OS IME candidate UI from the focused-field caret rect.
- **macOS full packaging in default CI** — `macos-smoke` runs on push/PR. Full packaging stays `workflow_dispatch`.
- **Web apps** — Full accessibility, real phone checks, and hot reload remain open.
- **Oracle idioms** — English grammar, Given rows, and intent thunks stay deferred with mining. They are not current work.
- **Emit fallbacks when Check returns an empty type** — Float tuple construction can still fail LLVM type checks. Some emitter helpers still use scalar fallbacks when the checker type is empty. Direction: keep types on the checked tree; do not guess from SSA names.

### Dogfood IDE

Open and deferred:

- OS IME candidate-window placement stays deferred.
- Do not add `Fs.watch` or an exec stub map. File change detection stays Clock plus Fs poll.
- In-app open-folder UI is enough. Native OS file dialogs, native menus, and multi-window stay later.
- Multi-cursor, minimap, Git UI, debugger, plugin host, custom canvas kit, and Windows desktop embedder stay later.
- Flutter DevTools / VM patching is an explicit non-goal.
