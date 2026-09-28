# Gaps and unknowns

Missing behavior and unproven claims, ranked by risk to [`philosophy.md`](philosophy.md). Implementation order: [`vision.md`](vision.md). Current slice: [`plans.md`](plans.md).

Remove closed gaps. Keep current measurements and their limits. Do not keep a history of completed work.

## Unknowns

### 1. Useful development through decisions

**Unproven.** Blind behavioral choices improve a real application toward an objective. A short witness gives enough context for a sound choice. Useful generation can sustain a bounded stream. Preferences remain useful after later edits. A human decides more efficiently from the card than from source alone.

**Proof.** Complete the session gates in `vision.md`. Run a human session on one small app and one objective. Measure retained acceptances per review minute, abstention reasons, Undo, and readiness latency. Record workload scope and the generator used. Controlled generator output proves the transport, not usefulness. Automated choices do not count as human evidence.

### 2. Review latency and evaluator parity

**Unproven.** Full corpus replay plus bounded search fits a frequent decision loop. Evaluator timelines remain equal to compiled timelines on new generated proposals. Index-based state alignment remains clear when a proposal adds or removes steps.

**Measurement.** Counter review uses four required workloads: idle and three corpus files. It has no seeds. It runs 32 search iterations with seed 1. A first card in a fresh target takes 24.6 s from editor launch to the visible question. The evaluator host is cached. A repeated card takes 3.3 s. Frozen-card preparation takes 2.97 s and 3.03 s. Cached workload navigation takes 62 ms. Cached step navigation takes 62 ms. The 250 ms navigation target is met. Each value is one sample. Both runs start a new editor process. The first sequence also edits an unsaved buffer. The cold value includes startup and input preparation. It is not a cold host-build measurement.

The host runs Linux 7.0.0-34-generic on an Intel Core i7-10875H at 2.30 GHz with about 31 GiB of memory. The CLI SHA-256 is `cea67a67b6aa45a31aa676e21802d115ff6e2e5403572e3ce0fc01e00ab1fd2a`. The evaluator host SHA-256 is `a23522ed0f470b9ab996d6b7d480f2bb765f11f75302f4cc585a00c81ed87bc7`. The source limit is 65,536 bytes across at most three proposed files. The existing probe limits apply.

**Known cost.** A zero-delay retry reaches the scheduler step cap in about 13 s on the evaluator and 0.7 s compiled. A mutated page limit reaches the 20 s probe deadline on both engines. The type-checker, code-generation, and CLI idle probes exceed their evaluator deadlines during concurrent validation. Their campaigns run compiled. They do not prove evaluator parity. Docs can differ on the representation of a signal that holds views. Do not hide these differences with a successful card.

**Proof.** Replay selected UI and IO witnesses compiled. Keep the existing campaign parity checks. Measure cold readiness, warm readiness, and cached navigation separately. Target cached navigation below 250 ms. Report an unmet target. Optimize demonstrated costs without reducing evidence. Semantic timeline alignment stays later.

### 3. Mobile on real devices

**Unproven.** JNI/ObjC embedding, touch, soft-keyboard text input, and Android OpenSSL on hardware. Hardware runs need provisioning. Simulator proofs do not close this gap.

**Proof.** Run Counter on one device with `scuzz package` and the platform toolchain. Physical-device work is outside the review arc.

The local iOS loop targets arm64 simulators on iOS 16 or later. Physical-device signing and release distribution remain open. iOS supports Net clients with platform certificate trust. Net HTTP servers remain host-only. Android packages reject Net calls because they do not link OpenSSL.

## Known gaps

### Queue and preference feedback

Review prepares a card on demand. There is no ready successor cache or complete job cancellation policy for the stream. Full-file siblings can replace accepted work from an older baseline. Can't decide has no reason. Local model requests and exported producer requests do not consume preferences. There is no retained-acceptance measure or local session summary.

**Proof:** Gates 4 and 5. Exercise late responses, baseline changes, repeated actions, pause, and session exit. Measure the result from local records.

### Rule review

There is no explicit claim suggestion or installation path from a preference. A finite witness cannot establish a universal rule. Rule installation needs protection against weakening existing claims and against stale source.

**Proof:** Gate 6. Show a separately approved rule that constrains a later proposal. Preserve existing claims. Reject an untriggered rule as unverified.

### Supporting compiler and editor work

- **Compile time.** `scuzz check examples/compiler` takes 5.46 s in one current sample. The CLI SHA-256 is `cea67a67b6aa45a31aa676e21802d115ff6e2e5403572e3ce0fc01e00ab1fd2a`. The editor campaign and PR checks run at the same time. `scuzz build --full examples/tyck` is recorded at about 16 s. Refresh these and the editor measurements after compile-time changes. Reduce demonstrated checker or LLVM emission costs only when they block this arc.
- **IDE subprocesses.** Run, Fuzz, and Diff use the CLI. Completion, formatting, code actions, semantic tokens, inlay hints, and folding use `scuzz lsp`. Removing these calls is not a gate for the stream.
- **Check scope.** The editor Check button does not perform all format and verify-file checks of `scuzz check`. Candidate gating must use the required shared checks even if the general button stays separate.
- **Standard kits.** OS threads remain missing. Add kit work only when it blocks an ordinary program or a required review proof.

### Local model evidence

macOS and Linux ARM64 backend execution remain unverified. Peak memory and useful Scuzz generation quality remain unknown. Larger source scopes can exceed the fixed context. Controlled replies do not prove usefulness.

**Proof.** Run the pinned backend on each supported host. Measure peak memory and useful output for the fixed catalog. Use real weights and the shared request, parser, and behavioral review. Report unavailable hosts separately. Do not expand the catalog or claim quality from model size.

**Measurement.** The host uses the pinned Q4_K_M artifacts and llama.cpp b11146 with CPU inference. Context is 4096 tokens. Output is at most 1024 tokens. Each row shows one measured finite command. Weights are cached. Compiler work can run at the same time. These samples do not establish stable latency or generation quality.

| Model | Command elapsed | Prompt | Prompt tokens | Generation | Output tokens | Generation tokens/s | Observed RSS at completion |
| --- | --- | --- | --- | --- | --- | --- | --- |
| SmolLM3 3B | 54.0 s | 12.9 s | 639 | 20.3 s | 124 | 6.06 | 3,738,292,224 bytes |
| Qwen2.5-Coder 7B Instruct | 112.8 s | 32.2 s | 695 | 51.6 s | 190 | 3.66 | 8,279,842,816 bytes |

Sampling uses seed 1 and temperature 0.2. The pinned revisions and artifact digests are in the [generation catalog](../examples/editor/generation/src/Models.scuzz). Prompt processing is 49.40 tokens/s for 3B and 21.56 tokens/s for 7B. The default sample uses CLI SHA-256 `cea67a67b6aa45a31aa676e21802d115ff6e2e5403572e3ce0fc01e00ab1fd2a`. The 7B sample does not retain its compiler identity.

The 7B model load has one observed sample of 3.9 s. The current 3B command does not measure load time separately. The process budgets are 4 GiB and 8 GiB. Linux enforces their virtual memory limits. The default finite proposal changes `greeting.txt` from `Hello` to `Hello Scuzz` under simulation. Its target has no registered claims. Its review completes all required workloads and 32 search cases. Finite Headless review retains a zero-event witness and exits normally. Frozen-card preparation takes 1.736 s in one sample. This interval excludes generation, editor startup, and input collection before the card freezes. The target has one required idle workload and no seeds or corpus files. It changes no source before a choice. A console-only proposal has no recorded difference. These cases do not measure human preference or application quality.

## Cuts and later work

Do not add user FFI, `extern`, plugins, library publishing, git or registry dependencies, or `scuzz add`. Do not start a provider framework, remote generator service, telemetry, or model training. Limits and exclusions for the current arc live in `vision.md`.

Generated setup inputs. Multiple named scenarios and campaign selection. Session event journal and live time ops. Stable scroll keys. Windows desktop. OS IME candidate windows. macOS release packaging in default CI. Developer ID signing and notarization. Full web accessibility. Real phone and screen-reader checks. Hot reload on web. Multiple UI factories in host hot reload. General model browsing. GPU inference variants. Gated-model login. Model conversion. Automatic oracle mining. Semantic timeline alignment. Divergence attribution to source defs. Pixel previews. Emit scalar fallbacks. Dogfood IDE: native file dialogs, menus, multi-window, multi-cursor, minimap, Git UI, debugger, plugin host, custom canvas kit.
