# Scuzz Lang vision

Long-term planning arcs: open work and risks. Product intent, design locks, and language direction: [`philosophy.md`](philosophy.md). Keep/cut: [`compatibility.md`](compatibility.md). Ranked gaps: [`gaps.md`](gaps.md). Later tune work: [`optimization.md`](optimization.md).

Edit this file when the next-step order changes.

## Open work

Next: make the language usable for general application development. Prioritize compiler correctness, memory ownership, type composition, standard kits, and tooling. Use examples to prove these capabilities through the built-in verification strategy. Harden that strategy on the [verification arc](#verification-arc). Specific application workflows do not define the scope.

### Evaluator arc

The evaluator arc is in the tree. Locks: [`philosophy.md`](philosophy.md#evaluator).

The evaluator runs checked programs without emit or link. It gives `scuzz fuzz` an in-process engine: no rebuild per mutant, no process spawn per probe, cheap state forks for branching, and coverage with comparison operand feedback. It gives Docs a live engine through the existing WebAssembly target: a page evaluates a snippet and mounts the result, and a guided tutorial renders reduction steps, schedules, coverage, mutants, and a drive-oracle search from the same engine. It gives `scuzz eval` on the host. The compiled binary stays the deploy artifact and the corpus replay engine.

Slices, in order. Each slice closes with a proof in `examples/`.

1. **Core.** In the tree. `examples/compiler/src/Eval.scuzz` evaluates expressions, `match`, `for`, closures, records, enums, traits, and module calls. `IO.println`, `IO.pure`, `map`, and `flatMap` map to native `IO`. A self tail call runs in constant stack. `scuzz eval PATH` runs an IO-only package. Proof: `examples/codegen` `ev*` oracles call evaluated defs and print `eval-ok`; `scripts/ci.sh hello` diffs `scuzz eval` against `scuzz run` on `examples/hello`.
2. **Kits.** In the tree. Evaluator cases for `Str`, `List`, `Map`, `Set`, `Json`, `Float`, `Builder`, `Hash`, `Hex`, `Base64`, and `IO.both`, `IO.fail`, `handleErrorWith` with typed errors. Record `copy`, implicit `self` defs, `for` guards, and bare `_` callbacks evaluate. `Eval.excludedKits()` lists the namespace prefixes later slices own. Proof: `examples/codegen` `evKitsCovered` probes every non-excluded row in `Kits.scuzz`; `scripts/ci-kernel.sh` diffs `scuzz eval` against `scuzz run` on `examples/kernel`.
3. **Effects.** In the tree. `IO` combinators, `Fs`, `Sys`, `Clock`, `Random`, `Uuid`, `Bytes`, `Ref`, `Queue`, `Deferred`, `Fiber`, `Resource`, `Stream`, and `Net` map to native `IO` at `Value`. Native `IO[A]` failures lift to `VStr`; typed failures stay `Value`. `Property.sometimes` is a no-op outside `scuzz fuzz`. Proof: `scripts/ci-kernel.sh` diffs `scuzz eval` against `scuzz run` on `examples/io` with clock and random lines removed; `evKitsCovered` probes every row outside `Eval.excludedKits()`.
4. **Fuzz engine.** In the tree. `Property`, `Scenario`, `Timeline`, and `Verdict` cases at `Value`. The runtime accepts closure setup, drivers, and claims and runs one probe in process (`sz_fuzz_*`). `Fuzz.*` kits lower to those hooks. `Eval.probe` registers the prepared fuzz files through them and reports def-entry and branch-arm hits with the keys `Emit` interns; the probe silences the evaluator binary's own coverage. `scuzz eval --probe DIR` is the process entry; `scuzz fuzz` spawns it for search, shrink, and mutants on a package without `[ui]`, with the probe env under the `SCUZZ_EV_` prefix. A mutant is a file set under `build/fuzz/mutate/<site>/ev/`; a mutant that fails `check` counts as invalid. A promoted search failure replays compiled. Corpus replay, `--replay`, and `--relate` run compiled. Proof: `scripts/ci-fuzz.sh` runs `examples/webhook` and `examples/api-report` on both engines and diffs `summary.json`; `examples/codegen` `probe-ok`; `crates/runtime/tests/test_io.c` covers the hooks; `examples/counter` stays compiled.
5. **Branching and coverage.** In the tree. One `scuzz eval --probe` server per file set forks each probe. A scheduler step is one effect on both engines. The evaluator idle probe on `examples/kernel` and `examples/fmt` runs under the probe deadline. Every Int comparison under coverage reports its operand distance; the search keeps the script that lowers a distance and nudges one Int driver argument by a power of two sized to that distance. Proof: `examples/reach` hides a `Property.sometimes` behind `code == 4242`; the evaluator search reaches it in 32 iterations and the compiled control does not (`scripts/ci-fuzz.sh`). Not taken: snapshot and fork at scheduler steps, and expression coverage. A campaign still interprets setup per probe ([`gaps.md`](gaps.md)). Take them when a proof needs them.
6. **Browser.** In the tree. `View.*` calls evaluate to a `VView` description; `Signal.*` calls are native signals; `Ui.run` hands the description to the host `Ref`. Docs depends on the compiler package, and `Mount.scuzz` walks the description into a native `View` with closures as taps. The Run stage holds an editor, a Run button, diagnostics, and the mounted view; `Eval.tryNow` runs inside the Run tap and evaluator signals pool per stage. The web build links the whole compiler package to wasm32 (the linker drops unreached defs); the HTTP client and servers fail loud at the call. Proof: Headless claims tap `+1` and Run on the page (`scuzz fuzz --iterations 0 examples/docs`); `crates/embedder-web/test.cjs` taps `+1`, edits the source, runs it, and reads a check error in Chromium, Firefox, and WebKit. Not taken: `View.each` in `Mount`, and a `View` as a reference-counted value ([`gaps.md`](gaps.md)).
7. **Guided tutorial.** In the tree. `TryOut` carries a reduction trace: one row per step with the expression span, the binding that changed, and the effect performed, capped, with self tail calls collapsed. `Block.Viz(kind, src)` is a manual block. The walkthrough paints schedule and coverage on Cover, and a live/mutant diff on Mutation. Proof: `examples/codegen` prints `eval-trace-ok` and `eval-sched-ok`; Headless claims read pass on one seed and fail on the other (`scuzz fuzz --iterations 0 examples/docs`); `crates/embedder-web/test.cjs` reads the same labels in Chromium, Firefox, and WebKit.
8. **Live campaign.** In the tree. Docs searches an `oracle` on the evaluator (`Eval.campSearch`) and shows the failing argument. A `def` that returns Bool is not an oracle. The user edits the snippet and presses Fuzz. The search does not call `Fuzz.probe`, so it does not nest inside a Docs campaign. Proof: `examples/codegen` prints `eval-camp-ok`; Headless `afterHit` reads `fail hidden 3`; Chromium, Firefox, and WebKit tap Fuzz and read the same line.
9. **Tutorial path.** In the tree. Search shows the live campaign with fail and pass chips. State keeps count across stages. Proof: Headless claims read the stage headings (`scuzz fuzz --iterations 0 examples/docs`); Chromium, Firefox, and WebKit read the same labels.
10. **Schedule branches.** In the tree. Cover runs one `IO.both` snippet under seeds 0 and 128 and prints `leftFirst` with the first winner and the verdict on each branch. The search does not call `Fuzz.probe`. Proof: Headless reads `seed 0 first=R fail` and `seed 128 first=L pass` (`scuzz fuzz --iterations 0 examples/docs`); Chromium, Firefox, and WebKit read the same lines.
11. **World pair.** In the tree. Cover paints the two scheduler worlds as a `View.row` of cards (`semantics:seed 0` and `semantics:seed 128`). Each card shows `first=` and trace rows. Proof: Headless reads both semantics and `first=R fail` / `first=L pass` (`scuzz fuzz --iterations 0 examples/docs`); Chromium, Firefox, and WebKit read the same labels.
12. **Walkthrough shell.** In the tree. The Docs app is a gated walkthrough. It does not paint the technical manual. Continue stays off until the stage gate holds. Off-stage viz does not construct. Hash is `#stage=id`. Proof: Headless claims (`scuzz fuzz --iterations 0 examples/docs`); Chromium, Firefox, and WebKit walk the stages.
13. **Growing Counter.** In the tree. One program grows across Intro, Run, View, Check, State, Search, Cover, and Mutation. Intro is a short language and tooling overview. Run's `@main` binds `inc(0)`. View mounts a `View`. `+1` does not change the label. Check opens `count.scuzz_verify` and runs `oracle incAdds`. Check and Search show `Main.scuzz` and `count.scuzz_verify`. State mounts the Counter `Signal`. Continue waits for `+1`. Search finds `hidden 3` in the verify file. Cover shows the two scheduler worlds and paints reached lines of `inc`. Mutation shows live source, mutant source, and the diff. `incAdds` rejects the mutant. Continue sits above the stage body with `n/8` in the app bar. Continue writes the next starter when the editor still holds the prior starter. Proof: Headless claims (`scuzz fuzz --iterations 0 examples/docs`); Chromium, Firefox, and WebKit walk the same stages.

### Session control arc

`scuzz run` carries the session control channel on every runtime. The channel is file-based: an inject document drives the session and a debug dump reports it. `scuzz exec` sends ops to a live session. `--exec` plays a finite ops program at boot, then exits. The same op vocabulary serves batch and attached modes. Headless, Desktop, and Mobile share the channel. Web needs a second transport and waits for the web hot-reload work.

Deferred on this arc, unproven value: a session event journal with prefix-replay rewind, and time ops (`pause_time`, `resume_time`, clock advance). Time ops reuse the TestRuntime clock fakes on a live session. They make no hermetic claim. Simulation stays the only hermetic tier.

### Verification arc

Closed impurity makes a probe a function of program, seed, script, and schedule seed. Search, mutation, and coverage already run inside `scuzz fuzz`. These slices close the gaps that make authors leave that path. Ranked list: [`gaps.md`](gaps.md#verification).

Slices, in order. Each slice closes with a proof in `examples/`.

1. **Shrink.** Delta-debug script lines. Nudge Int arguments toward a `where` bound or zero. Store the minimal script. Proof: a known failing search on `examples/reach` or `examples/api-report` stores a shorter corpus entry that still fails.
2. **Claim reachability.** `check` rejects unknown `driveHas` / `a11yHas` / `signalStrHas` names. A campaign fails when a claim antecedent never fires. Proof: `examples/bad-*` for a renamed driver; `examples/webhook` claims still pass.
3. **Generators.** Boundary Ints. Size that grows with the iteration. A string alphabet that includes empty, quotes, newlines, delimiters, and non-ASCII. Parse `where` as an expression. Proof: generated oracles on `examples/kernel` or `examples/tyck` reach those shapes; a compound `where` keeps its bounds.
4. **Fault surface.** The scenario declares the faults it injects. Claims drop `faulted` as a pass. Fault storms and partial writes follow. Proof: `examples/io` at `--iterations 16` is green or fails on a real invariant; `examples/webhook` claims no longer pass on `faulted` alone.
5. **Schedule replay.** Record the fiber picked at each contention step in the corpus entry. Report drift on replay. Do it in the evaluator scheduler first. Proof: a pinned `examples/bad-sched` or `examples/webhook` concurrent entry stays red after an unrelated edit, or the campaign reports drift.
6. **Mutation gate.** Mutate only defs that changed since the last fingerprint. Persist per-site kill results keyed by compiler SHA-256. Default `[fuzz].score_floor` on. Proof: a small edit in `examples/counter` mutates that def; a second campaign reuses prior kills; a surviving mutant fails the floor.
7. **Model claims.** Add `Timeline.fold`. Document a pure reference model over the timeline. Do not add a temporal-operator calculus. Proof: `examples/counter` or `examples/studio` states the model in one claim.
8. **Live transport.** A sanctioned `--live` corpus replay against the live loopback transport, or an explicit statement that OpenSSL, URLSession, TLS, and Skia pixels have no test home. Simulation stays hermetic. Proof: `examples/webhook` or `examples/api-report` replays one corpus entry on the live client, or `philosophy.md` states the cut.
9. **ASan replay.** Corpus replay on the compiled engine runs under ASan. Proof: `scripts/ci-fuzz.sh` or a runtime ASan slice replays `examples/io` corpus without a leak report.
10. **Destructuring binds.** A `for` bind unpacks a constructor. Proof: a compiler helper chain in `examples/compiler` becomes one bind; `scuzz fuzz --iterations 0 examples/tyck` stays green. Update the kernel lock in `philosophy.md`.
11. **Facts tier.** Name goldens as a facts tier in `philosophy.md`. Keep them as campaign seeds. Make generated-program round-trip and engine parity the compiler's primary oracles. Proof: `examples/tyck`, `examples/codegen`, and `examples/fmt` keep generated oracles as the search workload; goldens stay zero-argument seeds.

### Success bars

**v0** — Install CLI (`curl …/install.sh | sh`, or checkout `./scripts/install.sh`) → `scuzz new` (IO) or `scuzz new --ui` (Counter as `View` + builtin `IO`) → `scuzz fuzz --iterations 0`, and `scuzz run` (`--target headless` for UI). Desktop when available. Language `Resource` / `Stream` / `Net.serve` ship (`examples/io`).

**v1** — Shipped `scuzz` is the Scuzz CLI (GitHub Releases; `package_release.sh` / `install.sh`). Cut a release with the GitHub `release` workflow. Kernel surface is proven by examples. `fuzz` lives on that CLI.

### Current arcs

The webhook receiver authenticates GitHub HMAC-SHA-256 signatures before JSON parsing. It stores the latest received JSON object after authentication. HTTP 200 follows a complete file replacement. A failed write returns 503. Invalid signatures and requests preserve the prior file. Its scenarios send concurrent valid and invalid deliveries through the persistent server. Claims require a complete authenticated payload after the batch. A final delivery checks that the server continues to accept requests. Network and filesystem faults can preserve the prior payload or a complete accepted payload. It uses HTTP behind an HTTPS reverse proxy. It has no delivery queue or deduplication.

The API report fetches authenticated JSON records and writes an open-record report. It follows Link headers with the next relation. `Net.nextLink(currentUrl, header)` resolves the next link without effects. Relative links resolve against the current page. Next links must keep the scheme, host, and port. Link headers take priority over numeric X-Next-Page headers. A Link header without a next relation ends the report. Without Link, it follows numeric X-Next-Page headers on the configured URL. Numeric page numbers must increase and cannot exceed 100. A numeric next-page URL replaces existing page query fields, including percent-encoded names. It keeps other query values and excludes the fragment. Link pagination rejects repeated URLs and stops before page 101. It writes only after all pages succeed. It retries HTTP 429, 502, 503, and 504 at most twice per page. Retry-After accepts seconds or an HTTP date. `Net.retryAfterMillis(value, nowMs)` parses the delay without reading a clock. It accepts IMF-fixdate, RFC 850, and asctime dates. Expired dates yield zero. Invalid values and delay overflow yield -1. The report reads the current time through `Clock.realTime`. Without that header, HTTP 429 waits 1000 ms and gateway errors wait 100 ms. Invalid or ambiguous Retry-After values stop the request. One timeout covers all pages, attempts, and delays. Its corpus covers cursor links, cross-origin links, page cycles, the page limit, pagination, invalid page progression, recovery, retry exhaustion, rejected credentials, invalid JSON, failed status, and timeout. File claims check report contents. State comparisons verify that failed requests preserve arbitrary prior report text. Under filesystem faults, report claims require the prior contents or a complete new report.

The network UI fetches JSON through the shared Net API. It shows loading, failure, and success. Input continues during a request. Retry preserves the tap count. Native UI loops yield to IO fibers. Session exit cancels IO tap handlers. iOS and macOS GUI requests use URLSession with platform certificate trust. CLI and server requests keep the OpenSSL transport. Simulation uses the shared hermetic dispatch. Host and iOS simulator reload check captures before they use retained state. Source edits in the simulator preserve Signals. Manifest changes and the r command restart the app. Failed builds and incompatible reloads preserve the app. Code remains available to active IO handlers until the session ends. Host and simulator watch sessions accept r to rebuild and restart. They accept q to stop. A host app stops when its CLI session ends. Physical iPhone proof remains open.

The Docs app grows one Counter across Intro, Run, View, Check, State, Search, Cover, and Mutation. It does not expose the technical manual as an Index Book. `scuzz docs` remains the STE reference. Stage links use stable ids in `#stage=`. Check and Search nest file tabs for live source and `*.scuzz_verify`. Check opens the verify tab. State Continue waits for the mounted `+1`. Cover paints reached lines of `inc`. Mutation shows live source, mutant source, and the diff. `incAdds` rejects the mutant. Continue sits above the stage body. The app bar shows `n/8`. Headless claims check stages, Continue gates, the growing snippet, and the in-page Fuzz search. Corpus taps keep the full control label.

Ranked list: [`gaps.md`](gaps.md).

## Risks

| Risk | Mitigation |
| --- | --- |
| Language + UI + tooling is huge | Small subset. Vertical slices. Counter before generality |
| Dialect unexercised by apps | Kernel examples. `check` / `fuzz` on passing `examples/`. `examples/bad-*` are expected-fail |
| Effects too weak or too heavy | Builtin IO. Pure `View`. `Ui` at session boundary |
| Hidden nondeterminism | Closed impurity + hermetic TestRuntime. No live sockets under sim |
| Properties become brittle dump goldens | Named observations. Mutation kills weak oracles. Live-graph paint is not a dump fixture. Goldens are a facts tier, not the compiler's primary oracles ([verification arc](#verification-arc)) |
| `String` as bytes mangles UI text | `Str.*` indexes Unicode code points; `Str.byteLen` / `Str.byteSlice` keep bytes for framing. Case maps stay ASCII |
| LSP span misses the token | One JSON schema. Check diagnostics and LSP use recorded spans. The dogfood IDE consumes that schema |
| Sim becomes Mockito | Only qualified IO replacements. No stubbing pure `View`/`Signal`. Kits stay TestRuntime |
| Drivers become integration tests | `check` rejects `Property.*` in scenario files. Correctness lives in `*.scuzz_verify` and live-module `.require` |
| Drivers pass vacuously | `Property.sometimes` reachability fails the campaign when declared states are never reached. Claim antecedents must fire. `check` validates `driveHas` names |
| Verification tool sprawl | One `scuzz` strategy — mutation/fuzz/properties/sim/determinism in-tree. No external test frameworks |
| Slow fuzz inner loop pushes authors back to ad-hoc testing | Corpus-only replay tier answers in seconds. Full campaigns stay in CI. Shrink stores a readable failure |
| Search fails large and unread | Delta-debug the script. Nudge Ints toward bounds. Store the minimal corpus entry |
| Generators miss empty, overflow, and delimiters | Boundary Ints. Growing size. A real string alphabet. `where` is an expression |
| Faulted claims pass without an invariant | Scenario declares its fault surface. Claims drop `faulted` as a pass |
| Schedule seed replay turns a concurrency failure green | Record the fiber at each contention step. Report drift |
| Mutation samples too few sites to gate | Diff-scoped mutation. Persisted per-site kills. Default `score_floor` |
| Transport and Skia bugs have no test home | Sanctioned `--live` loopback replay, or an explicit cut in `philosophy.md` |
| Wall-clock probe deadlines flake on a slow host | Prefer the 1000000-step bound. Size the deadline from the idle probe |
| RC misses Signal/Ref cycles and view-list leaks | ASan corpus replay on the compiled engine |
| No `val` / no destructure multiplies helper defs | `for` constructor binds. Update the kernel lock when that lands |
| `Property.sometimes` verdicts vary with iteration budget | Checked-in corpus keeps reaching prefixes. Summary separates never-reached from not-reached-in-budget |
| Concrete business facts have no home without unit tests | Concrete-fact `.require` checks are sanctioned oracles. Zero-argument verify oracles seed the campaign. Generated-program properties stay the compiler search workload |
| “Almost Scala” confusion | Explicit non-goals. Language direction: [`philosophy.md`](philosophy.md). Run `scuzz docs language`. |
| Watch confused with hot reload | `scuzz watch` rebuilds. `[ui]` `run --watch` is hot reload (stamp-reload Views). IO-only `run --watch` kills and reruns |
| IDE typer ≠ batch typer | One JSON schema. LSP wraps `scuzz check`. No second typer |
| Evaluator ≠ emitted binary | One meaning. `fuzz` replays the corpus compiled after an evaluator campaign and fails on a difference. CI diffs `eval` against `run` on examples. The toolchain, editor, and Docs run under both engines before end users do |
| Evaluator grows a second runtime | `IO` maps to native `IO`. No evaluator scheduler, fakes, or clock. Kits are native calls through one `Kits.scuzz` table |
| Dogfood IDE before editor primitives | `scuzz ide` launches the bundled editor (`examples/editor`). Headless stays a peer. Do not add a `scuzz-ide` binary |
| Skia weight | pinned CPU prebuilt default. `sk_sw` opt-out |
| Desktop-only features | Headless peer rule. One input alphabet for editor keys, caret, selection, clipboard, compose, and inject |
| Treating UI as the only product | UI is a primary path (Flutter-shaped). CLI/server/desktop/mobile are peers |
| GC vs frame budget | `pump` boundary. Measure |
| Mobile packaging | Host Mobile peer first. `scuzz package` drives Android/iOS shells. Hardware USB later |
| Self-hosting stalls or forks the toolchain | Bootstrap fetches the newest GitHub `v*` release. The Scuzz CLI is the product. No dual toolchains |
