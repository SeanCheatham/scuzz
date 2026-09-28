# Gaps and unknowns

Missing behavior and unproven claims, ranked by risk to [`philosophy.md`](philosophy.md). Implementation order: [`vision.md`](vision.md). Current slice: [`plans.md`](plans.md).

Remove closed gaps. Keep current measurements and their limits. Do not keep a history of completed work.

## Unknowns

### 1. Useful development through decisions

**Unproven.** Blind behavioral choices improve a real application toward an objective. A short witness gives enough context for a sound choice. Useful generation can sustain a bounded stream. Preferences remain useful after later edits. A human decides more efficiently from the card than from source alone.

**Proof.** Complete the session gates in `vision.md`. Run a human session on one small app and one objective. Measure retained acceptances per review minute, abstention reasons, Undo, and readiness latency. Record workload scope and the generator used. Controlled generator output proves the transport, not usefulness. Automated choices do not count as human evidence.

### 2. Review latency and evaluator parity

**Unproven.** Full corpus replay plus bounded search fits a frequent decision loop. Evaluator timelines remain equal to compiled timelines on new generated proposals. Index-based state alignment remains clear when a proposal adds or removes steps.

**Measurement to refresh.** A warm Counter comparison is recorded at about 8.5 s from tap to lanes with idle and seed workloads. This is not a measurement of the expanded review. Editor check is recorded at about 19 s cold. Its fuzz probe build is recorded at 55 s to 75 s. Record host, compiler identity, workload count, search budget, and cold or warm state when measuring again.

**Known cost.** A zero-delay retry reaches the scheduler step cap in about 13 s on the evaluator and 0.7 s compiled. A mutated page limit reaches the 20 s probe deadline on both engines. Docs can differ on the representation of a signal that holds views. Do not hide these differences with a successful card.

**Proof.** Replay selected UI and IO witnesses compiled. Keep the existing campaign parity checks. Measure cold readiness, warm readiness, and cached navigation separately. Target cached navigation below 250 ms. Report an unmet target. Optimize demonstrated costs without reducing evidence. Semantic timeline alignment stays later.

### 3. Mobile on real devices

**Unproven.** JNI/ObjC embedding, touch, soft-keyboard text input, and Android OpenSSL on hardware. Hardware runs need provisioning. Simulator proofs do not close this gap.

**Proof.** Run Counter on one device with `scuzz package` and the platform toolchain. Physical-device work is outside the review arc.

The local iOS loop targets arm64 simulators on iOS 16 or later. Physical-device signing and release distribution remain open. iOS supports Net clients with platform certificate trust. Net HTTP servers remain host-only. Android packages reject Net calls because they do not link OpenSSL.

## Known gaps

### Behavioral questions

IDE review runs idle and seeds. It does not replay the corpus or run differential search. It does not shrink a review witness. Playback starts automatically. The card does not summarize complete evidence or claim trigger reach. An accessibility tile view does not reproduce screen layout. File sets with no observed difference can remain pending.

**Proof:** Gate 2. Keep recorded structure distinct from a pixel preview. Show finite evidence limits.

### Objective and generation

There is no managed local model path, objective-driven request contract, session work budget, or scoped source set. Generate creates mutation proposals. It does not provide direction toward a product objective. The importer validates complete publication metadata, source hashes, baselines, and canonical paths. Requests do not bind to an objective or session. Source and outcome deduplication remain missing.

**Proof:** Gate 3. Prove model setup and lifecycle with fake effects, a controlled local child, private loopback, and external directory publication. Test the real downloader, pinned backend, and one small local model separately. Record model revision, backend identity, memory and context limits, readiness time, and Scuzz output quality. External import must need no model resources.

### Queue and preference feedback

Review prepares a card on demand. There is no ready successor cache, request budget, or complete job cancellation policy for the stream. Full-file siblings can replace accepted work from an older baseline. Can't decide has no reason. Local model requests and exported producer requests do not consume preferences. There is no retained-acceptance measure or local session summary.

**Proof:** Gates 4 and 5. Exercise late responses, baseline changes, repeated actions, pause, and session exit. Measure the result from local records.

### Rule review

There is no explicit claim suggestion or installation path from a preference. A finite witness cannot establish a universal rule. Rule installation needs protection against weakening existing claims and against stale source.

**Proof:** Gate 6. Show a separately approved rule that constrains a later proposal. Preserve existing claims. Reject an untriggered rule as unverified.

### Supporting compiler and editor work

- **Compile time.** `scuzz check examples/compiler` is recorded at about 4.5 s. `scuzz build --full examples/tyck` is recorded at about 16 s. Refresh these and the editor measurements after compile-time changes. Reduce demonstrated checker or LLVM emission costs only when they block this arc.
- **IDE subprocesses.** Run, Fuzz, and Diff use the CLI. Completion, formatting, code actions, semantic tokens, inlay hints, and folding use `scuzz lsp`. Removing these calls is not a gate for the stream.
- **Check scope.** The editor Check button does not perform all format and verify-file checks of `scuzz check`. Candidate gating must use the required shared checks even if the general button stays separate.
- **Standard kits.** OS threads remain missing. Add kit work only when it blocks an ordinary program or a required review proof.

### Finite CLI generation

`scuzz ide` launches the editor. It has no finite suggestion command. Testing generation needs a shared operation that runs without UI input and stops after one result.

**Proof:** Gate 3. Implement `scuzz ide generate-suggestion` with the same catalog, request, parser, and process lifecycle as the IDE. Prove structured output, nonzero failure status, no source writes, complete publication, and child cleanup. Run the real CPU generation proof through it. Behavioral approval still needs the shared review path.

### Managed local models

The cache, download, and inference lifecycle are missing. Compatibility with shared cache locks, immutable revisions, interrupted downloads, and offline reuse needs proof. CPU runtime acquisition and model startup need a bounded implementation. The fixed 3B and 7B catalog needs download, template, resource, and cache-state proofs. CLI model listing and download are missing. CPU latency, peak memory, and useful Scuzz output are unmeasured for both options. Local model context may be too small for the selected source set. Generated Scuzz may be invalid or directionless. Useful quality on an ordinary host is unproven.

**Proof:** Gate 3. Use the published Hugging Face cache through its downloader. Use one pinned llama.cpp CPU backend and single-file GGUF. Prove lifecycle failures without weights in ordinary CI. Prove offline `scuzz models list` and bounded `scuzz models download <model-id>` through the same catalog and downloader. Run one opt-in real CPU session with the default 3B option. Check the 7B option on a host with enough memory. Keep an unavailable larger-model check explicit. Record prompt and generation time, tokens per second, observed memory, and review latency. Do not expand the catalog or claim throughput from model size. A grammar-valid JSON response is not evidence of a useful code change.

**Current host limit:** The downloader is available on the inspected host. `llama-server` is not on PATH. Real inference still needs backend setup and a selected artifact. Do not treat mocked inference as that proof.

## Cuts and later work

Do not add user FFI, `extern`, plugins, library publishing, git or registry dependencies, or `scuzz add`. Do not start a provider framework, remote generator service, telemetry, or model training. Limits and exclusions for the current arc live in `vision.md`.

Generated setup inputs. Multiple named scenarios and campaign selection. Session event journal and live time ops. Stable scroll keys. Windows desktop. OS IME candidate windows. macOS release packaging in default CI. Developer ID signing and notarization. Full web accessibility. Real phone and screen-reader checks. Hot reload on web. Multiple UI factories in host hot reload. General model browsing. GPU inference variants. Gated-model login. Model conversion. Automatic oracle mining. Semantic timeline alignment. Divergence attribution to source defs. Pixel previews. Emit scalar fallbacks. Dogfood IDE: native file dialogs, menus, multi-window, multi-cursor, minimap, Git UI, debugger, plugin host, custom canvas kit.
