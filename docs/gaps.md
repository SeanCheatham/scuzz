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

### Near-term (HUMANS alignment)

- **Hot reload and debugging tools** — HUMANS wants Headless + in-process reload + debug, especially for agents. Headless is a peer runtime; `scuzz watch` only rebuilds. Session stamp-watch rebuilds the View tree without resetting Signals. `Ui.run(_ => View)` re-runs construction. Stage 0 `run --watch` keeps a `[ui]` process and writes `build/reload.stamp` (`test_ui_run_rebuild_keepalive`). Stage 1/2 keep-alive and debug tools remain. Do not document `watch` as hot reload.

### Residuals

- **Net listen** — `Net.serveOnce` one HTTP/1.0 GET on Stage 0 and self-host (`examples/server`; TestRuntime injects the path). Live accept blocks the fiber like `httpGet` (code **6**). Later: persistent accept/poll.
- **Concurrency** — cooperative fibers + TestRuntime virtual-time jumps (`test_io.c`); Scuzz `Ref` / `Queue` / `Deferred` (String payloads) via Stage 0 + self-host (`examples/concurrency`). Live / default ready-queue pick is FIFO; `scuzz fuzz --iters` uses seed-driven pick among n>1 (`SCUZZ_SCHED_SEED`). Live `IO.race` of sleeps waits only for the soonest wake; idle `nanosleep` is EINTR-interruptible so a cancelled sleeper cannot hold the run loop. Later: OS threads, supervision trees.
- **Memory** — counter-shaped Headless pumps stay flat under alloc accounting (`test_ui.c`, optional `test-asan`). `View.each` with a stable `Signal.list` is pump-flat; `Signal.list` frees unshared cons spines on set and string heads on free (`test_signal_list_spine_collect`). Shared tails (cons onto the current list) stay. Later: a collector if list-churn still demands it.
- **File-as-module** — stem namespaces for defs and enums + `private def` + `import Module.name` on Stage 0 and self-host (`examples/modules`). **`record`** product types + `p.x` field access (`examples/record`). Thin **traits**/`impl` with static-dispatch method calls (`examples/trait`). Stage 0 and self-host **monomorphized** generics on defs (N type params; `examples/generic`) and on enums/records (`enum Opt[T]:` / `record Box[T](x: T)`; `examples/genum`). Later: richer generics — [`compatibility.md`](compatibility.md).

### Dependency forms beyond `path`

Path deps only (`manifest.rs`, `Toml.scuzz`). Git / versioned / hosted artifacts are direction; no registry.

**Deferred (deliberately last).** No ecosystem theater before there is an ecosystem. Revisit after path deps + file-as-module are the proven reuse story.

### Deferred by decision

- **Flutter-style constraint layout** — recursive stacker today (`layout_node` in `view.c`). Locked direction: constraints down, sizes up. `View.expanded` takes leftover Column height or Row width (`examples/todo`, `examples/nav`); `View.center` fills and centers; `View.align(ax, ay, child)` places start/center/end (`examples/nav` Other page); `View.stack` overlays; `View.positioned(x, y, child)` offsets a Stack child (`examples/counter`); `View.padding(n, child)` deflates max/min (`examples/nav` Home); `View.sized(w, h, child)` is a tight slot (`examples/counter`); `View.minSize(w, h, child)` raises min (`examples/counter`); `View.background(color, child)` paints behind the child (`examples/counter`); `View.aspectRatio(rw, rh, child)` fits an `rw:rh` box (`examples/nav` Home); `View.fraction(wpct, hpct, child)` takes that percent of max (`examples/nav` Home). Min is enforced after measure; padding deflates min for the child.
- **Windows desktop embedder** — same session protocol as X11/Cocoa; secondary platform.
- **OS IME candidate windows** — focused TextField caret uses measured advance (`sz_view_caret_rect`); embedders do not yet place OS IME UI from it.
- **LSP / editor tooling** — `fmt`, `check --message-format=json` (format-verify + typecheck), and `watch` exist; no language server. JSON diagnostics are the single editor protocol (`[{severity, message, file?, line?, column?}]`); LSP must wrap `scuzz check --message-format=json`. Do not add a second frontend or a parallel schema.
- **macOS in default CI** — macOS job is `workflow_dispatch`-only; Darwin regressions surface late.
