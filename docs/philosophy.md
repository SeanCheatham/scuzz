# Scuzz Lang philosophy

Scuzz Lang uses a language inspired by Scala and a UI design inspired by Flutter.
This file defines product intent, fixed design decisions, and language direction.
Edit it when a decision changes.

Open work and order: [`vision.md`](vision.md).
Supported features and exclusions: [`compatibility.md`](compatibility.md).
Host setup: [`developer-environment.md`](developer-environment.md).
Future performance work: [`optimization.md`](optimization.md).
App instructions, manifests, and kits: run `scuzz docs start`, `scuzz docs manifest`, and `scuzz docs kits`.

## Thesis

Use a small functional language for native CLI, server, desktop, and mobile applications.
Keep syntax concise. Use `for` as the primary binder.
Effects use built-in `IO`, inspired by ZIO.
The language and standard kits cover common application needs.
Scala Native supplies a reference. It is not a dependency.

Compile applications through LLVM to native binaries.
GUI applications can also target WebAssembly.
Scuzz Docs is the first browser application.
One evaluator supports verification, `scuzz eval`, the browser playground, and IDE review.
Deployment uses compiled applications.

Use one UI design language and Skia through the `Ui` effect.
Headless, Desktop, and Mobile are peer runtimes.
Headless, hot reload, and debugging tools support automated development and CI.
Examples in `examples/` check language behavior.
The compiler and CLI are Scuzz source.

The target development loop uses proposal review.
A developer sets an objective. A generator proposes one small change.
Scuzz checks its observable effects. The IDE shows a blind comparison.
The developer chooses a lane. Each choice guides the next proposal.
See [Proposal review](#proposal-review) for the design and `vision.md` for remaining gates.

## Defaults

| Topic | Choice |
| --- | --- |
| Verification | One strategy: mutation, fuzzing, properties, simulation, and determinism. Use `scuzz fuzz --iterations` |
| Claims and drivers | Claims use `Timeline => Verdict` in `*.scuzz_verify`. Drivers have no oracle. Put `.require` in live bodies |
| UI verification | Headless is required. `--differential` compares live dumps across Skia renderers |
| Source checks | `scuzz check` checks formatting and types. `scuzz fmt` formats live sources, scenarios, and claims |
| Code generation | LLVM IR. Emit reads checked types |
| Renderer | Thin Skia C interface. Impeller remains deferred |
| Effects | Built-in language and runtime operations. All external IO and nondeterminism use `IO`. No application-level `IO.delay`. Simulation is hermetic |
| Modules | A `scuzz.toml` package is a crate. A `Foo.scuzz` file is a module |
| Toolchain | One `scuzz` CLI, compiler, evaluator, formatter, and checker. `VERSION` defines the product version. Bootstrap uses the newest GitHub `v*` release |
| UI model | Pure `View` construction and effectful `Ui` sessions |
| Watch | `scuzz watch` rebuilds. IO-only `run --watch` restarts. UI `run --watch` reloads Views |

## Decisions

### Product name

Brand in prose: **Scuzz Lang** (short form **Scuzz**). CLI `scuzz`; compiler package `examples/compiler`; manifest `scuzz.toml`; sources `*.scuzz` (plus `*.scuzz_scenario` and `*.scuzz_verify`); C ABI `sz_` / `Sz*` / `SZ_*`. No dual names or legacy aliases.

### Tooling

A replay report covers one recorded input. Its verdict agrees with the command result. Retain its script, seeds, declared scheduler choices, and executed choices. Report author rule execution separately. A replay does not establish a complete campaign result. Replace previous success before checking or building starts.

One CLI. One typer. One formatter. One linter. One compiler. One evaluator. One testing strategy. No second analyze frontend. No `*.g.scuzz` codegen. No `src/test` runner. No bolted-on mutation/fuzz/property ecosystems.

- **Watch** rebuilds when sources or `scuzz.toml` change. `[ui]` `run --watch` is hot reload. See `scuzz docs gui`. IO-only `run --watch` kills and reruns.
- **Static hygiene** is `scuzz check` (the linter). An expression that ends before its required body or operand is a parse error. A reported parser error cannot pass the shared project check. CLI and IDE checks use this rejection. `scuzz fmt` preserves a file with a parser error. Language server formatting returns no edits for that source. No `lint` subcommand.
- **Verification** is built into `scuzz` and the language. The terminal and JSON summary use the same coverage and reachability results. They report reached functions, branch arms, sometimes labels, and triggers. Write `oracle name` for a drive oracle. An oracle returns Bool. `check` rejects an oracle that does not. A public `def` that returns Bool is not an oracle. `private oracle` is a parse error. Boolean drive oracles assert the value of the complete expression. A `for` with only `=` bindings keeps the result type of its body. An equality oracle can report both operands when it fails. A search failure fails `scuzz fuzz`. A mutation survivor does not. The driver registry grows with the package. Drive names must be unique. Catalog: run `scuzz docs verify`.
- **JSON diagnostics** (`scuzz check --message-format=json`) are the editor protocol. `scuzz lsp` wraps `check`. Panic, goto-def, and rename must use Scuzz source spans. Do not grow a second typer or schema.
- **Project IDE.** `scuzz ide` launches a Scuzz `[ui]` package. Headless stays a peer. Editor landmarks stay unnumbered. The Docs walkthrough does not use Index Book. Index Book stays a kit. The IDE depends on `examples/compiler` by path, as Docs does. The SDK ships `compiler/`, `syntax/`, and `ui-host/` next to `ide/`. These paths supply the compiler modules and the UI review host. The installed IDE uses `SCUZZ_HOME/bin/scuzz` for compiler identity. Check and source tools call compiler modules in-process. Review uses the shared compiler review path. The Check button calls `Check.checkFilesOwn` on the package and its path dependencies. Info, Go to def, and Rename call `Lsp` module functions in-process. Native builds, `git`, and evaluator probe servers use subprocesses. Model downloads use the native Net client. Local inference uses an owned subprocess. External producers publish proposal files independently. The compiler modules are the only typer. The IDE does not add a second one. Do not add Desktop-only editor behavior. Do not ship a second `scuzz-ide` binary.
- **`scuzz.toml` is data** - package, path deps, `[ui]`, optional `[fuzz].score_floor`. No plugin DSL. Unknown keys rejected. `run --target` and `ide --target` take an explicit platform (`linux` / `macos` / `headless` / `android` / `ios`) and override `[ui].default_runtime`. A package without `[ui]` accepts only the host platform. No `scuzz add`. No git or registry deps. No library publishing. A hosted registry may never ship.
- **Docs.** `scuzz docs` prints the technical manual from `examples/manual`. Kit rows come from `examples/compiler/src/Kits.scuzz`. App instructions stay in the manual. Do not add `guide.md`. `examples/docs` supplies the [Counter walkthrough](#counter-walkthrough). It uses live code and verification artifacts.
- **Fingerprint** (incremental): miss -> rebuild. The compiler reads source modules in name order within each package. This order keeps seeded mutation sites stable across filesystems. Cache keys include the SHA-256 of the executing compiler. A compiler change invalidates live and verification artifacts. Each output directory owns its live and verification cache stamps. The runtime supplies this identity through the reserved SCUZZ_EXECUTABLE_SHA256 key in Sys.getenv. A host environment value cannot replace it. Simulation reads this key from its fake environment only. Native make stays quiet on success. Fail on the first missing tool with one install line.
- **`scuzz package`:** `--target` is linux, macos, android, ios, web, or all. linux and macos must match the host. Hardware device runs stay open ([`gaps.md`](gaps.md)).
- **iOS local loop.** `scuzz devices` lists available iOS simulators. `scuzz run --target ios` selects or boots a simulator, builds and installs the app, and streams app output. `--device` selects an exact name or ID. `--watch` reloads Views after source changes. It preserves Signals. Manifest changes and the r command rebuild and restart. A build error or an incompatible capture preserves the running app. Restart resets app state. Host and simulator reload use the same capture checks. Native UI loops yield to the IO scheduler. IO tap handlers run as session-owned fibers. Session exit cancels their work. Native object caches shorten source rebuilds. The iOS viewport excludes safe areas and the docked keyboard. UIKit layout changes send shared resize events. Live records include viewport, keyboard, and lifecycle changes. Headless replays these events. Run `scuzz docs ios`.

### Counter walkthrough

The walkthrough grows one Counter through eight stages.
Each stage has one prompt and one live artifact.
Continue stays above the stage body and remains disabled until its gate passes.
The app bar shows the stage name and `n/8`.

| Stage | Required behavior |
| --- | --- |
| Intro | Introduce the language and tools. No gate |
| Run | Bind `inc(0)` in `@main` |
| View | Mount a View. Selecting `+1` does not change the label |
| Check | Show `leftFirst` source with seed 0 and seed 128. The rule requires L to win. Seed 0 fails because R wins. Seed 128 passes because L wins. Open `count.scuzz_verify` and run `oracle incAdds` |
| State | Mount the Counter Signal. Continue waits for `+1` |
| Search | Fuzz `oracle hidden` in the verify file |
| Cover | Show reached lines of `inc`. No gate |
| Mutation | Show live source, mutant source, and the diff. `incAdds` rejects the mutant |

A claim names a rule for a recorded run.
Scuzz selects seeds and records each schedule as a Timeline.
The claim reads the Timeline after the run and returns a Verdict.
See [Verification posture](#verification-posture) for the claim contract.

Use `View.tabs` for progress. Keep Index Book as a separate kit.
Check and Search have local tabs for `Main.scuzz` and `count.scuzz_verify`.
Local tabs do not change `#stage=id`.
Stage IDs remain stable when titles change.
Continue copies the next starter into each editor only when its text still matches the previous starter.
Do not use an empty `@main` in walkthrough snippets.

Check, Cover, and Mutation construct their visualizations when the stage opens.
Keep a small evaluator warmup of the Counter snippets at startup.
Search calls an oracle at `Value`. It does not call `Fuzz.probe`.
Headless claims check the Views before the browser proof.
The technical manual and the walkthrough use separate presentation code.
Docs and the native evaluator host share `examples/ui-host/mount`.
This package converts evaluator View values into native Views.

### Proposal review

These are the target design locks. Missing behavior stays in [`gaps.md`](gaps.md). Implementation order and completion gates stay in [`vision.md`](vision.md). The current slice stays in `plans.md`.

- **One objective.** A session has one short objective, a set of allowed source paths, and explicit work limits. The objective stays above the card. An objective change invalidates pending work. The developer can pause generation and end the session. The stream continues only within its work limits. Show request count, deadlines, and queue status.

  Count limits do not guarantee a monetary spend limit for an external provider.

- **One change against the working tree.** A behavior proposal adds or replaces at most three `src/*.scuzz` files in the selected package. It does not delete files or change the manifest, dependencies, scenarios, or verification files. Every path must resolve inside the allowed source set. Reject path traversal, duplicate module stems, and symbolic links that leave that set. Broader edits stay later.

- **One compiler and evaluator.** Review calls the compiler review modules. Target probes use the existing evaluator server and its forked children. Review does not need `git` or a native build of each proposal. Do not add a second checker, evaluator, scheduler, or probe protocol. Native replay is an explicit parity check outside card navigation.

- **Frozen inputs.** A card owns the exact baseline, proposed files, and evidence that the developer sees. Its identity includes the objective, compiler identity, manifest, dependencies, scenario, verification sources, workloads, seeds, and review limits. Hash canonical path-and-content records. A changed input invalidates the card and its cached evidence.

- **Choose a lane.** The card randomizes left and right once. Both behavior and source use that order. Choose left and Choose right select the displayed state. Choosing the proposal accepts it. Choosing the baseline rejects it. Reveal the mapping after the choice. Do not show candidate names, generator explanations, or side-specific verdict labels that reveal the mapping before the choice.

- **Abstention.** Can't decide is an abstention. It skips the card without a preference label or a source write. Optional reasons are No visible difference, Need another scenario, and Outside my goal. A reason can guide the next request. It does not weaken a claim or count as rejection. Randomize changes the selected card, not the lane order of a displayed card.

- **Decision identity.** A decision names the displayed card and selected lane. Disable choices until its evidence is ready. Ignore duplicate or late actions for that card. Do not queue a choice against the next card. A failed review clears the previous evidence and disables its choices.

- **Safe acceptance.** Before a write, check that the full reviewed input set and proposal still match the card. Refuse acceptance if a touched file has a dirty buffer. Never substitute another pending proposal for the displayed card. Write only its frozen bytes. Serialize acceptance, undo, and recovery. Pause source-changing editor actions during this operation. Recheck each touched file before its replacement.

  An external writer can still race this check; detect unexpected bytes during recovery and preserve them. Do not promise a transaction across independent writers.

- **Recovery and undo.** Store source snapshots and an operation journal before the first source write. A failed or interrupted multi-file operation remains recoverable. `Fs.write` is atomic for one file, not for a file set. Recovery runs before a new review. If recovery finds unrelated edits, preserve them and show the conflict.

  Undo reverses the last accepted change only when the affected files still match its result. Undo is a new recorded operation. It invalidates pending work. Recovery and Undo validate operation identity, decision, paths, and write fields in live code. Recovery rejects a journal that fails these checks before it changes source or Undo state.

- **Durable local records.** Store review records, snapshots, and recovery data under `.scuzz/ide/`. They survive deletion of `build/`. Records include card and proposal identities, both hashes, lane mapping, choice or abstention, objective, region, generator identity, witness, review limits, evidence status, and timing. Durable evidence includes the witness script and both recorded timelines.

  A proposal without its frozen request archive leaves the queue as a stale publication. Preparation continues with other proposals. Do not reconstruct the missing request. A build-path reference alone is insufficient. Keep records local. Build reports and caches stay under `build/ide/`. Do not keep a second decision log or a migration reader.

- **Claims gate preference.** A candidate must pass static checks and every assertion, oracle, and claim executed by the required review workloads. Compare absolute candidate verdicts, not only regression counts. A shared failure on both sides still blocks the candidate. An incomplete check, crash, timeout, or replay drift cannot count as a pass.

  A broken baseline can produce a repair card only when the candidate passes. Existing claims and scenarios stay fixed for behavior proposals.

- **Evidence has a limit.** Required workloads include idle, all seeds, and the package corpus. Replay them on the shared evaluator. Use bounded differential search to find more differences. Shrink a witness without losing its difference. Show completed and planned workloads, claim results, reached claim triggers, search budget, and any evaluator parity limit. Zero registered claims means `unguarded: no claims`.

  A verify file alone is not evidence of protection. Finite review does not prove all executions correct.

- **One observable question.** A behavior card needs a recorded difference. A new card opens the compact question view. Open the shortest available witness at its first useful difference, paused. Show the trigger and changed signals, accessibility entries, effects, or files. Keep other workloads, the timeline rail, and the source available on expansion. Play is optional. A workload or step tap does no IO.

  Source-only changes with no observed difference leave the behavior deck with a recorded No observed difference result. Do not label them equivalent.

- **Honest presentation.** The app bar contains Develop, Verify, and Run. Develop shows one objective and two comparison lanes. Each lane shows its check result and changed rows with the recorded structure. Each lane has its choice button below it. Code tools stay in Code. Session setup stays in Session. Model metadata stays out of the comparison.

  Other review controls and full evidence stay in More evidence. Recorded accessibility tiles describe structure. They are not pixel previews. Use the existing Headless paint and input path. Keep index-based timeline alignment for this arc. State when added or removed steps can affect the comparison. Do not build a new renderer or alignment engine for a card.

- **A bounded ready queue.** Prepare cards before navigation. Cache baseline work and evidence by their full input identity. Keep the displayed card and at most one ready successor. Run at most one generation request and one review job. Local generation waits for review of the previous generated candidate. A changed baseline cancels stale work.

  Cancel owned fibers and process groups on pause, objective change, and session exit. A generated candidate that fails static or workload checks leaves the queue with an automatic failure record. Local generation requests another suggestion within the eight-request limit. An explicit New session action stops owned work, changes the baseline epoch, and resets the request count. It preserves the objective, scope, and selected model.

  The next request includes up to three recent failed or excluded candidates in time order. It includes source and diagnostics. A failure requests a correction. A duplicate requests a different useful addition. Duplicate failed source stays excluded for the same inputs. Setup, transport, and incomplete baseline failures show their diagnostic next to the retry action. They need an explicit retry.

  The Generate mutations control creates diagnostic proposals. Generate suggestion uses the selected local model.

- **Managed local generation.** Scuzz manages model selection, download, cache reuse, and the inference process. Use llama.cpp as the first backend and single-file GGUF text models as the first format. Pin backend archives and verify their sizes and SHA-256 digests. Prefer Metal on Apple Silicon and Vulkan on Linux when a supported GPU is available.

  Use the pinned CPU archive when Linux has no usable Vulkan device. Use CPU inference when the backend reports no supported GPU. Offer a fixed model list with a Qwen3.5 4B default and a Qwen3.5 9B option. The exact catalog and profiles live in the [generation catalog](../examples/editor/generation/src/Models.scuzz). Do not accept arbitrary model names, repositories, files, or paths. Do not switch models automatically.

  Linux and macOS host support use the same process contract. Record platform proof limits. Do not embed an inference engine in the compiler. There is no configurable remote inference endpoint or OpenAI API key.

- **Resource use.** Expect generation to take minutes on CPU. Keep progress, elapsed time, cancellation, and review navigation available. Bound CPU threads, context, output, requests, and memory. Let llama.cpp fit GPU use to available device memory. Available host memory below the model budget plus 2 GiB gives a warning. It does not block setup or generation. Measure actual latency and quality on each supported backend.

  Slow generation never weakens behavioral checks. Model sizes do not prove useful Scuzz output.

- **Model cache.** Download pinned files from HTTPS with `Net.httpGetToFile`. Stream response bytes to a temporary file. Verify the exact size and SHA-256 before rename. Store files under `SCUZZ_MODEL_CACHE`, or under `~/.cache/scuzz/models` when that variable is empty. Keep model files out of packages and the workspace. Do not prune the Scuzz model cache or update a selected revision silently.

  Record repository, resolved commit, file, content identity, and backend identity for a session.

- **Explicit local setup.** Show the model identity, license link, download size, cache location, and configured resource limits before Download. Show cache state for each catalog option. Reuse only its pinned artifact. Fetch only the explicitly selected artifact. Cache backend binaries separately from model weights. Check download completion before loading.

  A missing tool, unsupported model, failed download, or insufficient resource budget gives a diagnostic and preserves the review session. Never load model bytes through source-string APIs. Gated model login and new model conversion stay later.

- **Owned inference.** Start one local inference process for the selected model. Bind private transport to loopback only. A protocol-compatible HTTP route can be an internal detail; it is not a user-configured endpoint. Stop owned inference on pause, model change, and session exit. Restart on resume from the same cached snapshot. Do not run a system service or terminate unrelated model processes.

  No source or preference data goes to a remote inference service.

- **Generation input and output.** Supply the objective, frozen sources, allowed paths, fixed verification and scenario files, corpus inputs, recent feedback, and limits to the local model. Fit the prompt and output within the model context. Reject an oversized request instead of silently omitting source or constraints. Use the objective as a long-term direction. Request one small useful addition per candidate.

  Supply valid Scuzz examples and relevant UI signatures. The local model states one small change and returns at most three edits. Each edit names an allowed path, one unique source block, and its replacement. New additions use blocks of one to three lines. A correction can replace a complete file. Scuzz applies exact matches. A correction uses the failed candidate as its source.

  The frozen baseline remains the review reference. It supplies complete replacement files, publication identities, and model provenance. Validate reply JSON locally, even when backend grammar constraints produce it. Check generated source before formatting. Format valid generated source with the shared formatter before publication. Preserve invalid syntax for compiler diagnostics. Review and acceptance use the formatted publication bytes. The model never writes the working tree.

  Save the request, raw model reply, and generation measurements under `build/ide/generation/`. Save replies before validation. Record each sampling seed and profile. Use temperature 0.7, top_p 0.8, top_k 20, and min_p 0.0 for local generation.

- **CLI model management.** `scuzz models list` shows the fixed catalog and local cache state without network access. `scuzz models download <model-id>` streams one pinned catalog artifact from Hugging Face to the Scuzz model cache and exits. Both commands share model data and cache operations with the IDE. Offer structured output for scripts.

  Download does not select a session model, start inference, or fetch backend binaries. Do not add arbitrary model inputs or model-cache deletion.

- **Finite CLI generation.** `scuzz ide export-request` freezes package inputs and an objective. `scuzz ide generate-suggestion` generates one candidate from that request and exits. `scuzz ide review-suggestion` checks the publication and runs the same bounded review as the IDE. It writes `build/ide/report.json`. Reject unchanged source before behavioral review. It shares catalog selection, setup, generation, and validation with the IDE.

  Use cache by default; downloads need an explicit command flag. Publish a complete candidate directory and report a structured result. Do not write package source, launch the UI, or leave owned inference running. Publication is not behavioral approval or acceptance. Keep acceptance and rule installation behind explicit review actions.

- **External proposals.** External tools publish complete candidate directories under `build/proposals/`. The IDE does not launch or authenticate these tools. Export a bounded generation request with its baseline identity for a producer to read. Require complete-publication metadata and validate every candidate through the same importer and review path. Source scope, stale-input checks, claim gates, and human choices apply to both inputs.

  Directory import works without a local model or inference backend.

- **Proposal identity and renewal.** Give each request and proposal a unique identity and a baseline hash. Publish imported proposals under `build/proposals/` only when their complete contents and metadata are ready. Recheck imported files under the same limits as generated files. Deduplicate source results and observed outcomes within the objective and baseline. After acceptance, generate against the new baseline.

  Do not apply old full-file siblings to it. Region focus uses canonical paths. Mutation generation stays an explicit diagnostic control. It does not refill the useful stream. `scuzz fuzz` mutation operators and budgets stay unchanged.

- **Preferences guide requests.** Use recent choices, abstentions, and undo operations as bounded request context. Do not train a model or silently turn a choice into a rule. Measure retained acceptances per review minute, readiness latency, decision time, abstention reasons, duplicate results, and undo rate. Keep rate alone is not the success criterion.

- **Rule suggestions are separate.** A developer can request a claim draft from a recorded preference. Show the source and supporting witness in an explicit rule review. A rule proposal adds claims through `*.scuzz_verify`; it cannot remove or weaken existing claims. Check and replay the new rule before offering installation. Installing it requires a separate human action and uses the same snapshot and recovery checks.

  It invalidates pending behavior cards. Do not infer a universal rule from one finite witness.

- **Existing tools stay.** Compare buffers remains an explicit disk-to-buffer comparison. `scuzz diff` remains the git-revision comparison. Share delta, search, shrink, and timeline code with them. Do not change their exit semantics to fit the deck. Update app-author instructions in `scuzz docs ide` when each behavior ships.

### Self-hosting

The product CLI is Scuzz (`examples/cli`). `scripts/bootstrap.sh` fetches the newest GitHub `v*` release. It builds a temporary compiler from `examples/compiler` with one build entry point. That compiler builds the full product CLI with the current emission rules. The script removes the temporary compiler. Do not ship two toolchains. Product version lives in `VERSION`.

`examples/syntax` is the lexer and parser. `examples/compiler` is the checker, evaluator, emit, and compile pipeline. `examples/fmt`, `examples/tyck`, and `examples/codegen` prove printer, checker, evaluator, and emitter. The temporary compiler excludes `Eval.scuzz`. Evaluation entry points live in that module. The temporary build path only calls builtins that the newest `v*` bootstrap emits. Its sources use only forms that release emits. The full CLI and evaluator can call builtins that the checkout compiler emits. A `for` pattern bind stays out of compiler sources until a release emits it. Parse `Param` and `Fun` stay strings. A path-dependent file over 40k keeps def heads and a stub body, so Check can resolve a qualified call.

### Evaluator

`Eval.scuzz` in `examples/compiler` evaluates a checked program. It is Scuzz. It is one module of the one compiler, not a second toolchain.

- **Scope.** `scuzz eval` runs a package on the host. `scuzz fuzz` searches, mutates, and measures coverage on the evaluator. The [Counter walkthrough](#counter-walkthrough) compiles the evaluator to WebAssembly. IDE review uses the shared evaluator server and forked probes. `scuzz run` and `scuzz package` use compiled applications.
- **Same meaning.** A program has one meaning. A difference between evaluator output and emitted output, other than speed, is a compiler bug or an evaluator bug. Hide internal signal storage from IO and UI probe timelines. Internal storage writes do not request a UI frame. Keep user signals visible. Build each UI row in the render scope of `View.each`. `scuzz fuzz` replays the corpus on the compiled binary after an evaluator campaign. A difference fails the campaign.
- **Child failures.** Preserve a child's error value through `Fiber.join`. The evaluator stores the child result before it crosses the native fiber interface. A join returns the child value or propagates its error. The shared native scheduler supplies cancellation errors. Check String failures, file faults, and cancellation with exact native replay.
- **Tap timing.** A tap closure on the evaluator host runs its synchronous part at the tap and submits the IO tail to the probe scheduler, as compiled emission does. A pure tap submits nothing. A callback error fails the probe even when the callback returns no IO. Under simulation `Sys.args` returns the fake argument list. It is empty without an override.
- **Probe readiness.** A server reports readiness after it loads the program. The client allows 30 seconds for startup. Each response has a separate 30-second wait. A failed source check reports its error before a request. The client keeps invalid-source status `3`. A failed startup clears the server. The 20-second probe deadline remains in the runtime.
- **One scheduler.** The evaluator maps `IO` to native `IO`. It does not own a scheduler, fibers, fakes, faults, clocks, or timelines. TestRuntime, hermetic simulation, schedule seeds, and `Timeline` are shared with compiled programs. One probe path: the runtime registers setup, drivers, and claims as function pointers from emitted code or as closures from the evaluator (`Fuzz.*` kits) and runs both the same way. A corpus entry records the fiber picked at each contention step in `schedule_picks`. Retain every recorded choice. Keep replay storage separate from the schedule sampling bound. Error conversion does not add a scheduler choice. Replay follows the recorded picks. A recorded fiber that is not ready, or a run that outlasts its recorded picks, fails the probe as schedule drift. A probe env reaches the evaluator process under the `SCUZZ_EV_` prefix, so the toolchain itself runs live until `Fuzz.probe` starts.
- **A scheduler step is one effect.** `pure`, `flatMap`, `handleError`, `attempt`, `ensure`, and loop entry run in the same step as the effect that follows them. A fiber yields at an effect, at a fork, or at a park. The evaluator wraps values in more `IO` nodes than emitted code, so this rule is what keeps the deterministic schedule the same on both engines. Under simulation one step runs at most 1000000 structural nodes; more fails the probe like a zero-delay loop does.
- **Views are data at `Value`.** A `View.*` call evaluates to a description (`VView(kind, args)`) with its closures and signals inside. `Signal.*` calls are native signals. `Ui.run` hands the description to the host `Ref` the embedding program installs (`Eval.withHost`) and fails without one. The host converts the description to a native View. The compiler package links without the UI runtime. An IO package can evaluate UI code until mounting requires a UI host. Only a `[ui]` package can mount these values. Docs and the native probe host use the same mount package.
- **One kit table.** `Kits.scuzz` is the one list of builtins. The evaluator dispatches by kit name. A kit without an evaluator case fails the compiler's own verification. Kits are native runtime calls in both engines.
- **Checked input only.** The evaluator runs after `check` passes. Values carry runtime tags. Generics need no instantiation. Traits dispatch on the receiver tag.
- **Erasure matches live builds.** `.require`, `where`, and `Property.sometimes` erase in `eval` and `run`. They stay active under `fuzz`.
- **Tail calls.** A self tail call runs in constant evaluator stack, as emit does. The lexer and the reserved-name scan use direct self tail calls.
- **Unsupported operations.** An unsupported construct or kit stops evaluation with a Scuzz file and line. The evaluator does not guess.

### Memory ownership

Use libc `malloc` and `free` through `sz_alloc` and `sz_free`.
Heap values use reference counts. There is no collector. Constructor patterns borrow fields while an arm runs. An arm retains a borrowed heap result before it returns. A match releases its owned temporary after the arm produces its result. A tail-recursive match retains the next arguments before it releases the temporary and starts the next iteration. A callback retains a borrowed field that it returns. A field read retains a heap field before it releases an owned temporary tuple. Immutable data forms no cycles.

### Skia

No vendored Skia tree. Thin `sk_capi` (measure + draw). **Default UI backend** is the pinned Skia CPU prebuilt. `SCUZZ_SKIA=sk_sw` is the explicit opt-out. `SCUZZ_SKIA=gpu` rasterizes on OpenGL. It does not paint on the CPU. Structural dumps stay the same. The channel delta against `sk_sw` stays within 2. Impeller stays deferred. Callers depend only on `sk_capi.h`. Editor glyphs use an embedded DejaVu Sans Mono face. `View.text` stays the proportional embedded sans.

### IO and impurity

Use one failure channel: `SzError` on `IO[T]`.
Typed `IO[E, A]` does not add an environment parameter.
Use built-in kits. Do not add application-level `IO.delay`, user FFI, `extern`, plugins, or `ZIO[R, E, A]`.
Determinism and effect capture still have open limits.
A panic reports a Scuzz file and line.

Cooperative fibers run on one thread.
`IO.race`, `IO.timeout`, and `IO.both` cancel the other child and wait for its finalizers.
Simulation is hermetic and has no live sockets.
Persistent HTTP servers wait for requests until cancellation in both runtimes.

Use one Net API with shared request checks, timeline events, and simulation dispatch.
CLI and server HTTP use HTTP/1.0 with OpenSSL.
iOS and macOS GUI clients use URLSession and platform certificate trust.
GUI clients verify loopback certificates, preserve status responses, and do not follow redirects.
Requests park fibers. IO finalizers cancel them.
Responses contain status, headers, and body: `(Int, Map[String, String], String)`.
Servers bind `0.0.0.0` and `::`.
`Net.serveTls` and `Net.serveOnceTls` use a process certificate.
The CLI and server loopback HTTPS client does not verify that certificate.
Do not expose POSIX sockets or add an application transport API.

Keep pure kits small. Signatures come from `scuzz docs kits`.

| Kit | Behavior and limits |
| --- | --- |
| Clock | `iso8601` formats UTC from epoch milliseconds. `parse` returns `Option[Int]` for an ISO-8601 instant with seconds, an optional fraction of one to three digits, and `Z` or a fixed offset. Invalid text returns None. `zone` formats an offset in minutes east of UTC. Offsets range from -1080 to 1080. An invalid offset returns an empty string. Named time zones remain unsupported |
| String patterns | Use POSIX ERE on UTF-8 bytes. `matches` checks the complete string. `capture` returns the first complete match and its groups. An empty list means no match or an invalid pattern. `replaceMatch` replaces the first match with literal text. It does not expand backreferences. An empty pattern copies the input |
| Hash | Software SHA-256 and HMAC-SHA-256 return lowercase hex of UTF-8 bytes. `constantTimeEqual` compares equal-length byte strings without an early exit. Length is public. Do not add other digests or an OpenSSL dependency |
| Hex | Encode UTF-8 bytes as lowercase hex. Decode reverses this. An odd length or invalid digit returns an empty string |
| Base64 | Use RFC 4648 on UTF-8 bytes. Decode rejects invalid length, digits, or padding with an empty string. No URL-safe alphabet |
| UUID | `v4` returns an RFC 4122 UUID with lowercase hex and hyphens. Use built-in Random. No parser or other UUID version |
| Bytes | `fromStr` copies UTF-8 bytes. `len` returns the byte count. Fs and Net do not take Bytes |

SIGINT and SIGTERM cancel the live program through its IO scheduler. The scheduler runs finalizers before exit. Owned process groups stop through their existing cancellation finalizers. The exit status is 130 for SIGINT and 143 for SIGTERM. Simulation keeps its scheduler limits and signal behavior. Forced termination cannot run finalizers.

`Fs.write` replaces a regular file in one operation. The live runtime writes a temporary file in the same directory. It checks write and close errors. It syncs the file. It renames the file over the destination. It syncs the destination directory. Power-loss behavior remains unproven. An error before replacement preserves the destination. New files use mode 0600. Replacement keeps the access permission bits. It keeps the owner, the group, and the extended attributes when the process can set them. Replacement updates the timestamps. On the live runtime, a symbolic link destination replaces the regular file that the link names. The link stays. A special file destination fails. An error after replacement can retain the new file. Read the file state before recovery. Simulation applies the same complete-content replacement. A partial-write fault builds the new bytes, discards them, and keeps the previous file. A fault after replacement retains the complete new bytes and returns an error.

Live and simulated HTTP clients share one URL parser. A URL with no path uses `/`. The request target keeps the query and excludes the fragment. Raw spaces and control bytes fail before dispatch. Each HTTP client call takes a request header map after the URL. Body methods take the body last. `Net.httpGetToFile` streams a response body to a caller path with bounded memory. It returns status, headers, and byte count. It supports Content-Length, chunked transfer encoding, and close-delimited bodies. It removes an incomplete file after an error or cancellation. The caller uses a temporary path and renames it after validation. `Net.httpGet` keeps its 1 MiB body limit. A server request is `(String, String, Map[String, String], String)`: path, method, headers, and body. Request header names use lowercase in handlers. The runtime owns Host, Content-Length, Connection, and Transfer-Encoding. Client maps cannot set these fields. Invalid names, control bytes other than tab, duplicate names with different case, and maps over 16 KiB fail before network dispatch. Simulation carries app headers through the virtual server. Header values do not enter the effect log. A request with `Prefer: wait=N` can wait N seconds for its response. The supported range is 1 through 900 seconds. Other values keep the transport default. `IO.timeout` can set a shorter bound. DNS, connection, and write limits stay fixed.

`Stream` is one finite pull interpreter. Bind a Stream with `=`. `<-` needs `IO`. Do not add backpressure, publishers, or a second stream kit.

### `Ui` vs `View`

| Layer | Role | Purity |
| --- | --- | --- |
| **`View`** | Widget tree | Sync/pure `build` |
| **`Ui` / `UiSession`** | `mount` / `pump` / `inject` / `snapshot` | Effectful (`UiRuntime`) |

Headless is a **peer** of Desktop/Mobile. Frame boundary is `pump`. A live loop paints when the session is dirty. It waits when nothing changes. Each pump still drains OS events, so a static frame receives clicks and close. World effects stay built-in `IO`. No UI feature without a Headless path. Nested declarative construction only. `Ui.run(_ => view)` is the session. Dump and inject ops: run `scuzz docs commands`.

Desktop windows are resizable. A window resize sends the shared resize event. The session lays out and paints at the new size. The live record includes the resize. Headless replays it.

A `View.button` press, a button that becomes shown, a `View.tabs` change, and a `View.each` insert each paint for four pumps. The new tab panel uses the same appear step as a button that becomes shown. The next pump paints the settled tree. A static tree paints one frame and waits. The clock is `pump`. Do not read wall-clock time. Headless and Desktop paint the same frames. The structural dump records the step. The timeline records the hit on the pump that took the input. The session dump keeps that hit. Do not add a spring, a motion language, blur, or a gradient.

Use `View.animatedOpacity(value, pumps, child)` for an opacity transition.
The value is a `Signal[Int]`. Display clamps it to 0 through 100.
The constructor does not write the Signal. Initial display uses its current value.
The pump count is positive and fits a signed 32-bit integer.
Each painted pump advances one linear step. Layout and input do not advance it.
Structural timeline text records `opacity:label=value`, `target:opacity:label=value`, and `pumps:opacity:label=remaining`.
Frame labels use a descendant name. They stop at a complete UTF-8 character within 64 bytes.
A changed target starts from the displayed value. It does not restart from an endpoint.
Hidden content snaps to its target. Removed content releases its transition state.
Pause preserves the displayed value. Resume advances one pump without time catch-up.
Opacity preserves layout, focus, input, and accessibility. Use visibility to remove input.
Both static and animated opacity composite the child as one transparent layer.
Overlapping child paint stays opaque inside that layer. Nested opacity multiplies.
Software and OpenGL layers use one surface-sized buffer or target for each active layer.
The default Skia renderer manages its own layer storage.

Use `View.reducedMotion(reduced, child)` to reduce motion in a subtree.
A nonzero Signal snaps explicit transitions and removes built-in motion steps.
An ancestor or session preference stays effective inside nested wrappers.
Headless and native sessions accept `SCUZZ_UI_REDUCED_MOTION=1`.
The browser follows `prefers-reduced-motion` and listens for changes.
Native operating-system preference detection remains open.
The shared `reduceMotion` input records preference changes.

Script input paints one pump while an explicit transition has pending steps.
Built-in four-pump motion finishes before the next scripted input.
Live journals set `manualPumps` to true and record painted pumps explicitly.
Consecutive painted pumps share one count. Multiple inputs can precede one pump.
Journal replay does not add implicit pumps. Script input does not enter a live journal.
The existing quiescence bound stays effective. A long unfinished transition trips that bound.

**The tree owns views.** A `View` is reference counted. Its parent releases it. A `View` parameter transfers to its parent without an extra retain. A `List[View]` holds views for `View.each`, which mounts the list at layout and frees the views it replaces. Releasing an unmounted `Signal[List[View]]` list releases its View references. Two writes between layouts release the intermediate list. A list another holder still reads keeps its views. Do not mount a view pulled out of a list signal by hand.

A tap closure runs its synchronous part in the tap. Its `IO` runs on the scheduler after the injected script. Claims see the synchronous part at the tap state and the `IO` result at the last state.

`scuzz run` carries the session channel on every runtime: the session watches `build/inject.json` and rewrites `build/debug.json`. `run --exec` plays a finite ops program after the first pump, then quiesces and exits. Without `--exec` a `[ui]` run stays live until a `quit` op or signal. `scuzz exec` writes ops to a live session. `scuzz package` strips the channel.

### IO apps vs Headless

| Path | Meaning |
| --- | --- |
| **Headless** | `UiRuntime` peer - still `View` / Skia / structural dumps |
| **IO-only** | No `[ui]`, no `Ui.run`; plain `@main: IO[Unit]` exec |

Missing `[ui]` => Skia omitted from the link. IO-only is **not** a fourth runtime peer.

### Kernel dialect

The language `scuzz` implements. Proof is examples (`examples/hello`, `kernel`, `io`, `counter`, `studio`, `scale`).

Locks (not an API catalog - run `scuzz docs language` and `scuzz docs kits`):

- Expression dialect only: `for` primary binder (`=` pure, `<-` effect); no `val` / statement blocks / `var`
- Integer division rounds toward zero. The remainder is zero or has the dividend sign. A zero divisor fails with a Scuzz source error in live and verification runs. Minimum Int divided by -1 returns minimum Int. Its remainder is zero. Check expected invalid divisors in live code and return Result.
- A left-arrow binding requires IO. IO aliases and typed errors use the same check. A pure value or an unresolved generic value is a type error at this boundary.
- A Unit IO binding, `() <- effect`, executes the effect and propagates its error. The bound payload is Unit. Native emission and evaluation use the same behavior.
- A `for` binding pattern covers every possible value of its type. The checker uses the match coverage rules for pure and IO bindings. Constructor and tuple patterns can bind fields when all values match. Use `match` for a partial constructor, list, or literal pattern. A `=` binding stays pure. A field access before a tuple binding stays a field access. It does not consume that binding as a method call.
- Interpolated strings use the same escape rules as ordinary strings. Decode escapes in literal segments once. Parse expressions inside interpolation braces as source. Check each interpolation hole as an expression. Live code and verification use the same rules.
- Optional `package`; top-level `def` / `private def` / `oracle` / `import`; `@main def ...: IO[Unit]`
- Payload enums + `record` sugar + thin traits/`impl` (static dispatch) + monomorphized generics
- Named types in declarations, lambda annotations, and expression ascriptions must resolve to a built-in type, an enum, a record, an alias, or a declared type parameter. A single capital letter can be an open type. An `impl` names a declared trait and supplies its declared number of type arguments.
- A type application in a declaration or expression annotation supplies the declared number of type arguments. `IO` takes one or two type arguments. A bare generic type name stays valid as an open type. A type parameter cannot take type arguments. An unannotated lambda leaves its parameter type open for inference.
- An `impl` supplies each trait method once. Method parameter types and the return type match after trait type arguments and aliases resolve. Parameter names can differ. The one-argument and two-argument `IO` forms use the same `String` error type.
- A generic def pins its own type parameters for every check in its body. A pinned parameter does not match a concrete type. Call sites instantiate parameters. Kit argument checks pin the caller type after substitution. A bare list, map, set, or stream pins an open type argument from a concrete use in that check. Later bindings in that for-comprehension use the pin. Do not add a pin for another kit call. One check binds an unbound value to one concrete type. A second concrete type for that value is an error. `eqPinned` stays the rule inside a generic def.
- **Private record constructors.** Write `record Amount private(value: Int)`. Only the declaring module constructs or copies the record. Other modules read fields and match values. Type aliases and generic arguments retain this access rule. A live Result constructor validates external input. Private access does not prove a domain predicate. Generated verification inputs cannot construct these records. Drivers accept raw input and use the constructor.
- **Record copy checks.** Check each replacement against the declared field type after generic substitution. Reject unknown fields, duplicate fields, excess arguments, and positional arguments after named arguments. A public wrapper cannot replace a private record field with an unrelated value.
- Record field lookup substitutes the receiver type arguments into the declared field type. The same rule applies inside callbacks.
- Constructor patterns compare direct String, Int, and Bool literals before an arm runs. An unqualified case uses the enum type of the matched value. A bare case must belong to that type. The checker rejects an unknown qualified enum case. A qualified enum case must belong to the type of the matched value, including a value in a nested field. A module-qualified record pattern uses the declaring file stem. A `Json` case needs a `Json` value. A `Json.parse` result needs a `Result.Ok` pattern around the `Json` case. Named fields use positions from that enum case. A failed literal comparison tries the next arm. Constructor, tuple, cons, as, and `[]` patterns nest in constructor fields and tuple components.
- A match on a closed type covers every possible value. Closed types include enums, records, Bool, Unit, Json, tuples, lists, Option, and Result. Resolve aliases in constructor field types. Check nested payload patterns and alternatives. A guarded arm does not establish coverage. The checker rejects an empty match, an unknown pattern field, a repeated field, and excess positional fields. A tuple pattern has the declared number of components.
- Enum cases require identifiers. Parsing stops when a case has no identifier or does not advance past its case keyword.
- A constructor call builds that enum case when the case exists. A module-qualified record constructor uses the declaring file stem. A kit call with the same prefix stays a kit when the name is not a case.
- Literal alternatives support chains of String, Int, or Bool values. Test each alternative before the arm guard. String contents can include the alternative separator.
- Constructor alternatives check tags and direct literal fields. Alternatives with the same binding names and types share the selected payload values. Field positions can differ. `check` rejects alternatives whose binding names or types differ. Guards and IO assertions use these bindings.
- Closure captures keep their declared types. Generated closure names cannot collide with source bindings. A local binding of `self` does not add an implicit parameter.
- An unannotated lambda bound with `=` pins its parameter type at the first apply of that binding whose argument type is concrete, in the rest of the same for-comprehension. A placeholder lambda such as `_ + 1` uses the same rule. An alias bound with `=` to that binding, or to such an alias, uses the same pin. The body checks under the pin and gives the return type. Later applies must match the pin. Applies with unresolved arguments do not pin. A nested binding or lambda parameter with the same name stops the pin scan. Without a concrete apply the binding stays unresolved and a later concrete apply asks for an annotation. Generic def bodies pin their own type parameters, so the rule does not apply there.
- A file stem names a module. Enums, records, aliases, and traits use the declaring module in their type identity. A local type takes precedence over an import. An imported type takes precedence over a unique project declaration. An ambiguous short name is an error. Qualified types use `Module.Type`. Qualified enum cases use `Module.Type.Case`. Local value bindings keep their value meaning in checking and native emission. Bare `Some`, `None`, `Ok`, and `Err` use the built-in types. Names that start with `__Scuzz` are reserved.
- One resolution pass supplies type identities to checking, native emission, evaluation, and verification input schemas. Constructor fields and match coverage use the declared type. Native tags distinguish declared types and do not depend on file order. Signal dumps use the emitted tags. Evaluator equality compares the declaring type and case. Verification input decoding uses the parameter type for nested constructors.
- Indent trait and `impl` methods. A top-level `def` starts in column 1. Formatting preserves this boundary.
- A `for` binding of a `Result` must retain and use the result. The rule also applies to a `Result` payload from an IO binding. Reject wildcard discard, an unused result binding, and a pattern that covers only one outcome. Use an exhaustive match to select a failure policy. Return the result or pass it to another function when that function owns the policy. A reference in a scope that shadows the binding does not use it. The rule also checks each Result field selected by a tuple, list-cons, or constructor binding pattern. An omitted Result field in a constructor pattern is a discard. Use distinct names in each for binding pattern. Pattern alternatives must bind the same names with the same types. Retain and use each result on its own. A bound list of Results must also be retained and used. Nested Result lists and IO payloads use the same rule. Other whole container bindings remain outside this check. The check does not establish that every receiver handles an error.
- External integer input uses `Str.parseInt`: strict decimal input returns `Result[String, Int]`. Required JSON fields use `Json.field`: the object and field checks return `Result[String, Json]`. A present null is a value. Match the JSON case to check its type. Use ordinary app code to check domain ranges before constructing records. These checks stay active in native runs and evaluation. Explicit fallback APIs express an app policy. Verification assumptions and erased assertions do not validate live input.
- Built-in kits + `Signal` / `View` / `Ui` / `Property.*` / `.require`
- `List.sort`, `List.max`, and `List.min` order `Int`, `Float`, and `String`. Float order puts NaN after numbers. Equal values keep their input order in `List.sort`.
- No macros, no implicits, no HKT beyond `IO`, no null

## Language direction

Expression-only dialect. **`for` is the kernel binder**. `=` aliases a pure value. `<-` sequences an effect. No `val`. No `var`. No statement blocks. `{ case ... }` is a lambda, not a block. Surface sugar elaborates to a small core.

Scala **nouns**, Rust/Cargo **verbs**. No JVM packages. Direction: payload **enums** / **`record`** + thin **traits**. Monomorphize generics early. No classes. Path deps remain the unit of reuse. Do not add a package registry. Details: run `scuzz docs language`. Keep/cut: [`compatibility.md`](compatibility.md).

## Verification posture

App correctness uses rules about observable behavior. Prefer mutation, fuzzing, properties, simulation, and determinism. All are first-class in the language and in `scuzz`. Claims can constrain state changes, effects, cancellation, duplicate requests, and recovery. A relation claim can check two executions. Reference models are optional. They are not the default app verification path.

C interface checks cover memory ownership, input limits, and host behavior.
Keep their assertions active with release build flags.
Check that deliberate defects fail the relevant checks.
Keep sanitizer builds separate from normal builds.
A library build alone does not check runtime behavior.

Resource rules check release counts and later effects after cancellation or timeout. Synchronize a cancellation workload with entry into the resource body. Keep expected effect errors explicit through Result. Check bounded progress as well as preserved state. Repeated request workloads check the resulting content and status. Fault workloads check interrupted writes and recovery. Reject deliberate changes to these behaviors with the same rules. Finite simulation does not establish every live scheduler or platform behavior.

Formal methods remain optional. `examples/proof` uses the checked syntax tree for signed 64-bit bit-vector queries. It uses the existing runtime for finite schedule exploration. Keep its query translator in the example package. Keep solver dependencies out of the default toolchain. Use the same author rules for fuzzing and formal checks. Separate symbolic proofs, exhaustive bounded checks, and sampled results. Record assumptions, trigger reach, input identities, trust, limits, and cost. Unsupported operations, unknown results, and unmet limits cannot produce a proof result. Do not add a general proof language or a mandatory app model.

An exhaustive application check must retain every scheduler choice for exact replay. A truncated scheduler record leaves the affected scope incomplete. Keep a completed sub-scope separate from the complete application result. Ordinary search and mutation have separate evidence. A search defect does not make an incomplete mutation campaign complete.

- **Claims** live in `*.scuzz_verify`. The author surface is `Timeline => Verdict`. Dump slot ids stay behind the algebra. `Timeline.fileTextIs` compares a file with expected text at a recorded state. It observes the hermetic filesystem. `Timeline.fileSame` compares one file at two recorded states. It compares all bytes. A file missing at both states is the same. A file missing at exactly one state is different.

  It does not assert that intermediate states are equal. Missing files and directories return false. `Timeline.fold(t, model, (model, i) => model)` walks every state in order and returns the final model. A model claim keeps its reference state in a record. Its step function compares each Timeline state with the model. It records the first state index that does not match.

  The record lives in a live module. A verify file holds defs and oracles only. The claim turns that record into a `Verdict`. `lastHitHas` and `driveHas` read the last hit and the last drive as levels at a state. `Timeline.hit` is true only on the state that recorded that hit. A later state keeps the level and does not record the hit again.

  Do not add a temporal-operator calculus. `check` validates `Timeline.driveHas` names against the driver registry. A `driveHas` antecedent must fire once per campaign with search iterations. `a11yHas` and `signalStrHas` names are dynamic. A never-claim whose antecedent stays false is a sanctioned idiom. An unknown signal slot fails the probe.

- **`Property.force`** returns the declared IO payload type in verification code. It supports scalar and heap values. Native emission keeps the callback indices of the surrounding expression when it forces an IO value. Inline bindings keep those indices for both the argument and the body.

- **Live Result use.** A Result binding needs a use that remains in live code. The receiver of `.require` remains. Its label and predicate erase. A use only in those arguments does not satisfy the Result use check. Apply the same rule to Result lists and selected Result fields. A function call does not prove that its receiver handles the error.

- **`.require`** and `Property.sometimes` stay in live function bodies. They erase from live builds. An IO assertion checks the completed payload. Match arms keep their payload types during assertion rewriting. IO continuations capture those payload fields and keep separate indices across arms and nested matches.

- **Drivers** live in one `*.scuzz_scenario`. They are impure, parameterized, and oracle-free. `check` rejects `Property.*` and `.require` in scenario files. The scenario declares its fault surface in a `faults` def: `fs`, `net`, or `queue` entries with an optional `:fail`, `:drop`, or `:corrupt` mode. A scenario without `faults` gets no injected faults. A claim must not pass on `faulted` alone. A plan faults one call.

  The call index runs from 1 through 16 for each declared entry. The next pass faults every later call of that kind. The pass after that discards an `Fs.write` and keeps the previous file. The following pass does both. The next two passes report a write error after replacement, once or on every later call. Other filesystem calls retain their failure behavior.

  A fault seed below 145 stays one call. A recorded fault_seed can select fs:fail:N, fs:partial:N, or fs:after:N. N is a positive filesystem call index. These explicit plans can reach later calls. Setup does not consume the index.

- **Simulation is hermetic.** Fuzz, mutation, and TestRuntime keep impurity inside fakes. No live sockets. Scheduler ownership, not address, is the determinism boundary.

- **Live loopback replay.** `scuzz fuzz --live` replays the corpus (`--iterations 0`) or one `--replay` file on the compiled binary. Clock, files, random, and sys stay simulated. Net uses host loopback sockets. The OpenSSL HTTP/1.0 client serves `http` and `https`. A non-loopback host fails. Search and mutation stay on the simulated network. URLSession and Skia pixels have no fuzz home. `--differential` compares structural dumps.

  GPU pixels stay within a channel tolerance of `sk_sw`.

- **ASan replay.** `scuzz fuzz --asan` replays the corpus (`--iterations 0`) or one `--replay` file on the compiled binary under AddressSanitizer. Leak and use-after-free reports fail the probe. Search and mutation stay off. A `[ui]` package is rejected. Linux ASan probes skip the 512 MiB virtual memory limit because AddressSanitizer needs a large virtual address space.

- **Behavioral diff.** `scuzz diff [REV] [PATH]` compares a git revision with the working tree. `REV` defaults to `HEAD`. The CLI is the only caller of `git`. It checks out `REV` in a detached worktree under `build/delta/`. The same compiler builds both sides into `build/delta/a` and `build/delta/b`. Both sides run hermetically under TestRuntime.

  Workloads are the idle probe, the seeds, and the union of both `corpus/` directories with duplicate contents removed. Each workload is `same`, `diverged`, `fixed`, `regressed`, or `unrunnable`. The delta names the first diverging timeline state and the changed `signals`, `a11y`, `effects`, `files`, and other sections. States align by index. The report also compares controls, drivers, oracles, claim verdicts, `Property.sometimes` names, and classify counts.

  `--iterations N` searches the shared alphabet for a timeline or verdict difference. The shrinker keeps that difference. Witnesses go to `build/delta/witness-<hash>.toml`. `scuzz fuzz --replay` reads them. Each workload that differs writes both timelines to `build/delta/tl-<hash>-a.txt` and `tl-<hash>-b.txt`. The report names them in `timelines`. `scuzz diff` writes only under `build/delta/`. It never writes `corpus/`. There is no expected-delta file.

  The exit code is 1 only for a regression.

- **Probe limits.** Each probe has a 20-second deadline. Linux probes also have a 512 MiB virtual memory limit. Darwin has no `RLIMIT_AS`. `--asan` skips the Linux virtual memory limit. Simulation stops after 1000000 scheduler steps per IO run. A limit failure fails the probe. Process cancellation kills the shell and its process group. The iOS console runs as the owned child.

  Its cleanup trap stops the simulator app. `Sys.kill` discards unread child output. The runtime keeps the child slot until it collects the exit status.

- **One `scuzz fuzz`, two engines.** Search, mutation, and coverage run on the evaluator. A package with no `[ui]` uses one `scuzz eval --probe DIR` server per file set. A `[ui]` package uses one headless host. The server checks the package once and forks a child per probe. A mutant is a file set, not a link.

  Corpus replay, `--replay`, `--relate`, `--asan`, and `--live` run compiled. A search failure found on the evaluator replays compiled before the campaign ends. A difference fails the campaign. The idle probe runs on both engines first. A timeline difference, a deadline, or a crash on the evaluator runs every probe compiled and prints why. A search deadline replays the same workload compiled.

  The remaining search and mutation probes use compiled output. `SCUZZ_FUZZ_ENGINE=compiled` forces the compiled path. It is the parity control. Search feedback runs on the evaluator only. Every covered `Int` comparison reports `|a - b|`. The search keeps one drive line that lowers the farthest distance at or below 2^16. The distance file keeps the last drive in the script.

  It nudges one `Int` argument on that line inside its `where` by a power of two sized to that distance. A nearer comparison does not replace that climb. `examples/reach` is the proof. `examples/api-report` uses the same climb on the page cap. `--iterations N` gives five eighths of N to search, rounded down. Mutation uses the rest, up to the site count.

  Initial probes and corpus replay stay outside that budget. `--iterations 0` is corpus-only. Mutation results persist per site in `.scuzz/mutate.results`, keyed by the complete recorded mutation input identity. A def whose body hash changed re-mutates first. A campaign without `[fuzz].score_floor` uses the floor 1.000. Search and corpus failures fail the campaign. `--no-fail-fast` cannot turn a corpus failure into a pass. Catalog: run `scuzz docs verify`.

- **Verification assumptions.** `Property.assume(name, condition)` returns Unit in live source. A false condition under TestRuntime ends the input as excluded. It does not recover as an IO error. Scenario and verify files reject this operation. It has no effect in a live run. It does not validate production input. `where`, `.require`, oracle, and claim failures remain failures.

  A search exclusion consumes its iteration and does not count as a pass. A required idle, seed, or corpus exclusion fails the campaign. A failure shrink cannot replace a failure with an exclusion. A mutant exclusion does not count as a kill. It stays in the score denominator. Confirm each evaluator search exclusion compiled with the same reason. A different result fails the campaign.

- **Mutation rejection.** A detected defect has terminal property or claim rejection evidence. A caught assertion does not supply terminal evidence. A deadline, crash, or tool error without rejection evidence is incomplete. An incomplete mutation stays in the score denominator and fails the campaign. Do not reuse incomplete or unknown results. A source check error is an invalid mutation.

  A link or file error is incomplete. Keep rule predicates fixed. Checked application values remain mutation sites. Baseline and mutant probes retain recorded scheduler choices. Each probe keeps its workload in replay.toml. Reports retain probe logs and name the source site and rejected rule.

- **Mutation input identity.** Reuse site results only for an equal recorded campaign input. The input record includes prepared source, claims, drivers, package manifests, workloads, seeds, recorded choices, engine, limits, compiler identity, native artifacts, and a host environment hash. Keep mutation-input.json beside the report. A changed input hash clears all stored site results. Keep definition hashes to select changed definitions first.

  Compiled live and verification build identities include runtime archives. The input record identifies both programs. The native JSON encoder uses direct tail recursion. Its large input proof checks control bytes and UTF-8 with an 8 MiB stack, a 512 MiB Linux memory limit, and a 20-second deadline.

- **Coverage identity.** Compiled sites use stable source pointers. Evaluator and author rule records use text keys. Both paths record each text once. Keep a text index for shared identity. Reset both indexes with the probe. Coverage recording must retain the required workload within the probe limit.

- **Author rule execution.** The report lists author properties and claims that execute. An assumption does not count as a guard. An IO assertion counts when its IO runs. A campaign is unguarded when no author rule executes. Guarded means at least one author rule executes. It does not establish relevant trigger reach or rule strength.

  Keep this evidence separate from execution success and source coverage.

- **Optional bounded experiments.** Keep the application checker optional. Reuse one evaluator server for each source graph and isolate each request in a child. Count a preemption when a scheduler selects another fiber while the preceding fiber remains runnable. Record each declared preemption bound as a separate scope. Preserve the wider search. Preserve every permitted choice and exact replay.

  Record each rule with its method, verdict, bounds, completed scope, and open obligations. Sampled success does not establish complete scope.

- **Checked scope.** The campaign report separates passed, failed, excluded, incomplete, and unrun workloads. It records inputs, schedules, faults, recorded picks, engines, and exclusion reasons. It lists driver argument schemas and bounds. It shows declared and executed timeline claims. Claim execution does not establish trigger reach. Keep trigger reach separate. Clear claim and classification records at campaign start. Deadlines and step limits stay incomplete.

  Terminal property and claim rejection stays failed. Preserve probe exit codes. Start each campaign with an incomplete report. After campaign start, a check or build error leaves an incomplete report. Finite input records do not prove all executions correct.

- **Workload fields.** Supported TOML workload fields accept optional spaces around equals. Multiline arrays retain their events and scheduler picks. Array comments do not add events. Field names in comments or event text do not select a field. Reports retain the declared script, seeds, and picks.

- **Shared project checks.** The CLI, language server, and editor use one project check path. Manifest and dependency errors produce project diagnostics. Do not replace load errors with an empty diagnostic list or an incomplete source graph. Keep source, format, verification structure, and verification type errors visible. Check the prepared verification program before running it. Canonical package paths identify dependency cycles.

  Open dependency files supply their current text to the checked graph. Update diagnostics for all open files after an edit.

- **Conditional and absence rules.** A campaign checks the author idle workload on the selected engine. Idle UI workloads contain no implicit tap. A failed idle workload blocks the required pass. A corpus-only pass covers its recorded workloads. Inspect claim execution and trigger reach separately. A claim can execute when its trigger never occurs. A positive search budget requires declared triggers to occur.

  An absence rule needs workloads that can produce the forbidden behavior. Check that the rule rejects such a workload.

- **Generation.** Generated drive arguments cover boundary Ints: zero, one, negative one, the i64 edges, and the `where` bound edges. Size grows with the iteration. The string alphabet covers empty, quotes, newlines, delimiters, and non-ASCII. A string argument that needs it is a quoted token in the drive script. `where` parses as an expression: `&&` conjuncts of comparisons against Int literals give the bounds.

  A driver `where` outside that shape fails the source check. Bounds with no valid Int also fail the source check.

- **Generated compiler checks.** `examples/compiler-cases` supplies one bounded source generator to the type-checker and emitter proofs. Cases vary nested matches, closures, pure `for` bindings, generic records, heap values, and IO continuations. Each case has a model result from its seed. Coverage cases select subsets of Boolean payload patterns. They vary tuple, Option, enum, and Result owners and guards.

  Module cases check separate declarations with repeated names in both file orders. They check qualified generic types, imports, traits, and nested verification inputs. Regular CI compares evaluator and native output with an independent host model. It checks invalid types, formatter stability, search, mutation, and rejection of a shared wrong result. Native subprocesses stay in CI. Simulation stays hermetic.

  These finite checks do not prove every program correct.

- **Runtime undefined behavior checks.** The native CI slice runs the C IO and UI checks with UBSan. Compile and link with the undefined behavior check group. Stop at the first detected error. Reject a known signed overflow before the runtime checks. Missing support or disabled instrumentation fails the requested check. Keep binaries separate from the normal build.

  Run the checks from their own fixture directory. The Linux host proof passes. Other hosts need their own proof. The Skia wrapper, prebuilt Skia, and emitted LLVM code stay outside this instrumentation.

- **Facts.** A zero-argument oracle is a fact. Facts seed the campaign. Search generates arguments only for oracles that take parameters. Generated-program round-trip and engine parity are the primary oracles for the compiler, the formatter, and the emitter. Facts stay seeds.


```text
src/
  Todo.scuzz              # live module: defs + where + .require + sometimes
todo.scuzz_scenario        # one world: setup, replacements, drivers
count.scuzz_verify        # Timeline => Verdict session claims and `oracle` drive oracles
```

One `*.scuzz_scenario` file per project that uses scenarios. Multiple named scenarios and generated setup stay later. Live `scuzz run` loads `*.scuzz` only. No free-floating `tests/` package roots. Direction beyond this: [`optimization.md`](optimization.md). Ranked gaps: [`gaps.md`](gaps.md).

## UI design language

Scuzz Style is the default UI design language. Use warm paper, dark text, square controls, and clear borders. Use yellow for primary actions. Use dark rust for accent text. Headless, Desktop, and Mobile use the same paint path. Color ratios do not prove full accessibility conformance.

Construct immutable themes with `Theme.light()`, `Theme.dark()`, or `Theme.colors`.
`Theme.colors` takes background, surface, foreground, primary, on-primary, border, muted, accent, and selection colors.
Use ARGB integers or `Color.rgb` and `Color.rgba` values.
Apply a theme with `View.theme(theme, child)`.
The wrapper paints its background inside its frame.
The nearest wrapper supplies the colors for controls, text, and tooltips.
Nested themes do not change sibling colors.
The color constructors use the default font size, spacing, and control height.
Use `Theme.metrics(theme, fontSize, lineGap, padding, gap, controlHeight)` to change these values.
This function returns a new Theme. It preserves the source Theme and its colors.
Use logical pixels. Font size must be positive. Spacing values must be nonnegative.
Control height must fit the font size plus the line gap. Invalid metrics stop the program.
Nested themes supply their own metrics. Device scale stays separate.
Layout, paint, carets, editor input, and scrolling use the same metrics.
Measured lists invalidate row heights when these metrics change.
Browser caret, focus, and selection colors follow the same palette.
Theme values do not follow operating-system appearance changes.
The zero-argument Theme color functions return default palette values.

Build immutable styled text with `Text.span(string)` and `Text.concat(list)`.
Use `Text.color(color, text)` and `Text.size(size, text)` to change span attributes.
Use `Text.bold`, `Text.italic`, `Text.underline`, and `Text.monospace` for other styles.
Each function returns a new value. It preserves its source.
Apply a style to a concatenated value to change all its spans.
Use `Text.plain(text)` to read the complete source string.
Use `View.richText(text)` to display one paragraph.
Words wrap across span boundaries. Mixed sizes use one baseline per line.
Line boxes contain font descenders and underlines.
Default sizes and colors follow the nearest View and Theme wrappers.
Explicit span sizes use logical pixels. Explicit span colors override the inherited color.
Use `View.maxLines` and `View.ellipsis` to limit display.
The ellipsis uses the final visible span style.
Browser copying keeps source spaces and explicit line breaks across styles.
Synthetic ellipsis marks do not add source characters to copied text.

The default native Skia renderer shapes proportional text with HarfBuzz.
SheenBidi supplies direction and script runs within each displayed line.
Measurement and paint use the same positioned glyphs.
The embedded face supplies kerning, ligatures, combining marks, and Arabic forms.
Wrapping and ellipsis keep shaped source clusters together.
Plain text measurement and paint use complete lines.
Field carets, pointer input, and selection use shaped source positions.
Left and Right move a field caret in the visual direction.
Composition paints the complete preview without changing the committed Signal.
Source strings keep their original UTF-8 bytes.
Editor cells and native monospace spans keep their cell layout.
The browser uses its font shaper and source positions from DOM text ranges.
Pinned Noto fonts supply fallback for Devanagari, Thai, and Simplified Chinese.
Font selection keeps source clusters together. Text line boxes contain fallback marks.
Web packages include the same fonts. Font loading updates layout and keeps input state.
Other script coverage, shaping across styled spans, wrapped paragraph direction, and full bidi caret affinity remain open.
The software and OpenGL native renderers retain their code-point cells.
They do not supply these proportional shaping guarantees.
See the [HarfBuzz shaping model](https://harfbuzz.github.io/getting-started.html).

Use `View.tooltip(message, child)` for short help.
Hover or keyboard focus opens the help. Focus stays on the child control.
The tooltip has no Tab stop. The nearest nested tooltip supplies the description.
The pointer can move from the trigger onto the popup without closing it.
Escape clears a field composition first. It then dismisses visible tooltips.
A later Escape closes an open dialog.
Dismissal stays effective until both hover and focus leave the trigger.
The popup wraps inside the viewport and uses the nearest Theme.
Empty messages have no popup or browser description.
Browser controls reference the help through `aria-describedby`.
The help remains a description when the popup is closed.
Follow the [W3C hover and focus guidance](https://www.w3.org/WAI/WCAG22/Understanding/content-on-hover-or-focus.html).
Real screen-reader and physical-device checks remain open.

`View.indexBook` groups named pages around a persistent index.
The [Counter walkthrough](#counter-walkthrough) uses tabs.
The editor uses unnumbered landmarks and updates source marks while its source editor is hidden.

Container constructors take one `List[View]` for their children.
Use list literals for fixed children. Use `List.map` for data-driven children.
Native and evaluator construction use the same flat list.
Evaluate list elements from left to right.
They have no fixed child count limit.
The container retains each child before it releases a temporary list.
Do not mount one View in two parents.

`View.autoGrid` selects equal column slots from the parent width.
It uses the shared grid path on all runtimes.
Its minimum cell width, gaps, and padding use the theme scale.
Resize changes the column count.
Children excluded by `View.showWhen` use no slot.
An unbounded width uses one column.

`View.adaptive(minWidth, compact, wide)` selects from the parent width.
The width limit uses logical pixels and the current theme scale.
Use compact below the limit. Use wide at the limit and with unbounded width.
Only the selected branch has layout, paint, input, and accessibility.
Resize clears focus in the hidden branch.
Construct separate Views for both branches. Keep shared Signals outside them.

Tab and Shift+Tab move focus through visible controls in tree order.
Use `View.overlay(open, label, child)` for a named modal dialog.
The label supplies its accessible name. An empty label uses Dialog.
Focus moves to the first control when the dialog opens.
Layout focus changes update the soft keyboard request.
A keyboard-dismiss event stays effective while focus stays on the same control.
A dialog with no control takes focus itself.
Focus stays inside the top dialog. Escape closes that dialog.
Closing returns focus to its opener. A removed opener uses the first visible control.
Use `View.card` for the dialog surface.
Card text and empty card space keep the dialog open.
An interactive wrapper around a card keeps its action.
Backdrop taps outside the card close the dialog.
Wheel, touch scroll, hover, and secondary input stay inside the top dialog.
A dialog scope change cancels the current pointer gesture.
Browser dialogs expose their name and modal state. Background content stays outside the browser accessibility tree.
Enter and Space activate the focused control.
Use `View.slider(value, label, minimum, maximum, step)` for a named integer slider.
The bounds use signed Int values. Minimum must not exceed maximum. Step must be positive.
Allowed values start at minimum and advance by step without exceeding maximum.
The last allowed step supplies the upper endpoint. Equal bounds give one value.
Display clamps and snaps the Signal value. Construction does not write the Signal.
Input writes an allowed value. Half-step ties select the higher value.
Slider arrows change the value by one step. Home and End select the endpoints.
PageUp and PageDown change the value by ten steps.
The browser uses the shared runtime for keys and pointer input.
Its accessible value uses decimal text across the full Int range.
Keyboard behavior uses the [W3C slider pattern](https://www.w3.org/WAI/ARIA/apg/patterns/slider/).
Use `View.split(fraction, label, start, end)` for two resizable panes.
The label names the primary pane and its vertical separator. An empty label uses Primary pane.
The fraction is a percent from 0 through 100. Display clamps the value without writing it.
Tab visits the primary pane, separator, and secondary pane in that order.
Left and Right change the primary pane by one percent. Home and End select the endpoints.
Enter collapses the primary pane or restores its previous nonzero size.
A collapsed pane retains state. It has no paint, input, focus, or accessibility content.
Pointer drag starts on the handle. Pane controls keep their input.
The browser exposes the separator name, value, orientation, and controlled primary region.
Keyboard behavior uses the [W3C window splitter pattern](https://www.w3.org/WAI/ARIA/apg/patterns/windowsplitter/).
Use `Text.action(key, text)` to mark an underlined action in styled text.
The key uses the full signed Int range. An outer action replaces actions inside its text.
Styles and concatenation preserve action keys. Each concatenated occurrence keeps a separate identity.
Use `View.actionText(text, onAction)` to receive the key.
Handlers support lambdas, named functions, and captured function values.
A handler can update Signals or return IO through the existing UI callback path.
`View.richText` displays the same text without action targets.
Each visible action has one button and one Tab stop across all its wrapped fragments.
Enter and Space activate it. Pointer input uses its source fragments.
Gaps between fragments and synthetic ellipsis dots do not activate actions.
Empty and completely elided actions have no input or accessibility target.
Text selection and copying preserve the source across style and action boundaries.
Browser action buttons remain in source order inside their text block.
Keyboard behavior uses the [W3C button pattern](https://www.w3.org/WAI/ARIA/apg/patterns/button/).
Use `View.expansionTile(open, label, child)` for collapsible content.
Only the header toggles the open Signal. Enter and Space activate the focused header.
A nonzero Signal shows the body. An empty label uses Details.
The body keeps its own visibility conditions. Passive body input does not toggle the header.
Nested controls keep their input. Body text remains selectable in the browser.
Closing preserves committed field state. A focused body returns focus to its header.
Closing does not move focus from an outside control.
The browser exposes a button with its expanded state and controlled content region.
Keyboard behavior uses the [W3C disclosure pattern](https://www.w3.org/WAI/ARIA/apg/patterns/disclosure/).
Use `View.select(selected, label, options)` for compact single selection.
Options use `Signal[List[String]]`. Give the control and each option a nonempty label.
The selected Signal holds a zero-based index. Construction preserves the Signal.
An invalid index displays no selected text. An empty list has no input or Tab stop.
The control has one focus stop. Popup options use stable index identities within one list.
A new list cancels pending input and closes the popup. It resets type-ahead and scroll.
The control keeps its identity, focus, and selected Signal. The application owns index changes.
A new list uses new option identities. An old pointer press cannot select a replacement option.
The popup keeps at most nine row Views, including partially visible rows.
Arrows open the popup and move a pending index. Home and End use the endpoints.
PageUp and PageDown move ten options. Navigation stops at each endpoint.
Enter, Space, Tab, and explicit focus transfer commit the pending index.
Escape, a second control activation, and outside pointer input cancel it.
Outside pointer input closes the popup and consumes that input.
A newer external Signal value replaces pending input. Stale focus transfer cannot overwrite it.
Type-ahead matches option prefixes. Repeated characters cycle through matching options.
ASCII matching ignores case. Other UTF-8 bytes match exactly.
The prefix expires after one second. Input journals record the time for exact replay.
Search keeps input time monotonic when it changes event order. Explicit replay rejects backwards time.
Wheel and touch input scroll the popup. Scrolling keeps the pending option visible.
Popup input stays inside the active dialog. Escape closes the popup before the dialog.
Browser semantics use a named combobox, a linked listbox, and indexed options.
The browser keeps focus on the combobox and reports the pending active option.
Editable selection and full Unicode case folding remain open.

Use `View.segmented(selected, label, choices)` for equal-width choices.
Choices use `List[String]`. The selected Signal holds an index that starts at zero.
Each choice uses the shared radio input and selection paint.
The label names the group. An empty label uses Choices.
Construction preserves the Signal. An index outside the list selects no choice.
An empty list has no input target. A single choice uses one target.
The group has one Tab stop. Use the selected choice, or the first choice when none is selected.
Arrows select the next or previous choice and wrap at the ends.
Home and End select the first and last choices. Enter and Space select the focused choice.
Repeated labels keep separate identities. Keys stay inside the named group.
Text input on a focused radio does not edit a background field.
Browser semantics use a named radio group and a checked state for each choice.
Use the [W3C radio group pattern](https://www.w3.org/WAI/ARIA/apg/patterns/radio/).
Use `View.radioGroup(label, child)` to name a radio group.
Its radios share one Signal. Arrows select radios inside that group.
Each group has one Tab stop: the focused, selected, or first visible radio.
A tab list has one current Tab stop. Left and Right move tab focus.
Enter or Space selects its panel. Tab moves into the panel.
A text-only panel is a Tab stop. A panel that starts with a control skips this stop.
Focus groups preserve View identity when labels repeat.
Focus traversal has no fixed control limit.
Plain Tab inserts two spaces in an editor. Ctrl+Tab moves focus out.
Headless and native hosts use the shared focus path.
The browser uses DOM focus with shared Tab, editor, radio, and modal key handling.
Tab can leave the browser application when no next control exists.

`View.measuredList(estimate, items, key, render)` measures mounted row heights.
Give it a bounded height. The estimate uses logical pixels.
The runtime uses that estimate until it measures a row at the available width.
Each row uses at least one logical pixel of height.
Keep a height index for prefix sums and scroll lookup.
Do not construct offscreen rows to measure the complete list.
The scroll extent changes as the runtime measures rows.
Preserve the visible item by key when its measurements or list position change.
Resize invalidates cached heights. Mounted rows keep their Views and Signals.
Follow the focused row when data or width changes.
Fill the last viewport when row heights are less than their estimates.
Compute row positions relative to the viewport to preserve pixel precision.
Fixed and measured lists use the same row ownership and input paths.

`View.virtualList(rowHeight, items, key, render)` uses fixed row heights.
Give it a bounded height. Row height uses logical pixels and theme scale.
Use `Signal[List[A]]` for items and `A => Int` for unique keys.
The renderer receives a `Signal[A]` with the current item.
Use `Signal.map` and `View.bindText` to show item changes.
Keep mounted rows and their Signals when their keys stay in the viewport.
Follow the focused row when a data change moves its key.
Construct one extra row on each side of the viewport.
Release row Views and Signals when rows leave that window.
Track row Signals directly. Include Signals created by later row layout callbacks.
Detach derived Signals through direct links when their source is released.
Use the derived Signal owner for callbacks that run later.
Index constructor names by Signal kind and name.
The last name assignment wins within each Signal kind.
Keep persistent application state outside the renderer.
Do not keep a row input Signal after its row leaves the viewport.
Reject duplicate keys before the row window changes.
Use the shared scroll, input, and paint paths.
View IDs increase at construction. Do not reuse an ID after a View is released.
Ignore browser events and pointer targets with a released ID.

`View.expanded(weight, child)` divides remaining row width or column height.
Subtract fixed children, padding, and gaps before this division.
Use the ratio of each weight to the sum of the shown child weights.
Values below one use a weight of one.
`View.stretch` preserves an expanded child's weight.
Use intrinsic layout when the main axis has no bound or fixed children exceed its space.

Decode image bytes with `Image.decode`.
Return `Result[String, Image]` for invalid data and decode limits.
Keep image file and network reads in IO.
Keep decoded image pixels immutable.
The IO compiler decodes images without a UI renderer.
`View.image` retains the image and shares its paint cache across Views.
Use the image label for accessibility. Use a separate Text View for a caption.
Preserve the image aspect ratio and center it in a tight slot.
Use linear sampling and source-over alpha on each renderer.

Pass layout constraints from parents to children.
Return measured sizes to parents.
Use nested constructors and one layout model.
Diagnose behavior through structural dumps, `*.scuzz_verify`, and `.require`.
Widget and GUI instructions: run `scuzz docs kits` and `scuzz docs gui`.
Full web accessibility remains open.
Browser limits: [`compatibility.md`](compatibility.md#browser-target).
