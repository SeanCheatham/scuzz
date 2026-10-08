# Gaps and unknowns

Missing behavior and unproven claims, ranked by risk to [`philosophy.md`](philosophy.md). Implementation order: [`vision.md`](vision.md). Current slice: [`plans.md`](plans.md).

Remove closed gaps. Keep current measurements and their limits. Do not keep a history of completed work.

## Unknowns

### 1. Useful development through decisions

**Unproven.** Blind behavioral choices improve a real application toward an objective. A short witness gives enough context for a sound choice. Useful generation can sustain a bounded stream. Preferences remain useful after later edits. A human decides more efficiently from the card than from source alone.

**Proof.** Complete the session gates in `vision.md`. Run a human session on one small app and one objective. Measure retained acceptances per review minute, abstention reasons, Undo, and readiness latency. Record workload scope and the generator used. Controlled generator output proves the transport, not usefulness. Automated choices do not count as human evidence.

### 2. Review latency and evaluator parity

**Unproven.** Full corpus replay plus bounded search fits a frequent decision loop. Evaluator timelines remain equal to compiled timelines on new generated proposals. Index-based state alignment remains clear when a proposal adds or removes steps.

**Measurement.** Counter review uses four required workloads: idle and three corpus files. It has no seeds. It runs 32 search iterations with seed 1. A first card in a fresh target takes 27.40 s from editor launch to the visible question. The evaluator host is cached. Frozen card preparation takes 3.98 s. Restart restores the saved cards in 743 ms. Cached card navigation takes 61 ms. Cached workload navigation takes 67 ms. Cached step navigation takes 61 ms. The 250 ms navigation target is met. Each value is one Headless sample. The first sequence also edits an unsaved buffer. The cold value includes startup and input preparation. It does not measure a cold host build. The warm run starts a new editor process and reuses the saved card identities and evidence. The editor mutation campaign runs at the same time.

The host runs Linux 7.0.0-34-generic on an Intel Core i7-10875H at 2.30 GHz with about 31 GiB of memory. The CLI SHA-256 is `4bf1aeddc4148d4db8c674bffc2503bb70cd5405227483fcf1d896ba77d55c73`. The editor SHA-256 is `8b98ef68457917acb0d9498ffd5a1046a80f12ce679c8a3a73f2d5a635fcda0f`. The evaluator host SHA-256 is `cb8d264b1dcabee9995f75e290fed230feae5c5dd97fb4cbb8a50f931c2f5a23`. The source limit is 65,536 bytes across at most three proposed files. The existing probe limits apply.

**Finite parity.** CI compares exact timelines and claims for generated type-checker and code-generation drives. An IO proof keeps user signals visible and hides internal storage. Editor idle, queue-cache, and protocol replay have equal timelines and claims. The editor corpus has 227 passing entries. Its 21 claim drivers run. Three sometimes labels and four UI triggers remain unreached. These finite checks do not prove all programs equal. One sequential editor comparison takes 13.08 s for idle, 17.30 s for queue-cache, and 16.52 s for protocol replay on the evaluator. These intervals include host startup. The samples use Linux 7.0.0-38-generic. Compiled runs take 0.11 s, 0.22 s, and 0.21 s. The 20 s probe deadline and Linux memory limit apply.

**Constructor and View parity.** A constructor proof checks both declaration orders, defaults, named arguments, records, and match fields. Its evaluator and native timelines and claims are equal. A Headless proof checks direct, list, and nested View signals with a button action. Its timelines and claims are equal. The Docs corpus has 10 passing entries. Its evaluator idle check does not select compiled fallback.

**Compiler scope proof.** Inline bindings use names from their expression scopes. Renaming includes local function calls. Nested matches use the checked scalar result type. Generated compiler proofs check repeated names in separate match arms, nested expressions, closure captures, aliases, and heap results. The generated proofs also check generic record access and wildcard fields with heap values. These finite checks do not prove all binding scopes correct.

**Function lookup cost.** The evaluator uses an index by module and function name. A fixed source snapshot runs `irGenerated 7` and `evGenerated 7`. Three warm probes take 7.71 to 7.82 s with the index. The same source takes 8.17 to 8.26 s when the resolver filters a list of function names. The median decreases by about 4 percent. Timelines and claims are equal. These samples run in sequence on the same Linux host. They do not establish a general speed bound. The CLI SHA-256 for the index is `5f926ad12f7487c2715456bb30ea1a7ce47e0efac01f357d3dd88ca0a75aa3c5`.

**Map cost.** Evaluator maps use an immutable balanced tree. A fixed workload inserts 5,000 keys in ascending order and reads each key. Three warm probes take 0.201 to 0.210 s. The same source takes 7.724 to 7.850 s with a sorted list. The median decreases by about 97 percent. Timelines and claims are equal. The implementations run in sequence on the same Linux host. These samples do not establish a general speed bound. The CLI SHA-256 for the tree is `a946738eaa3423673593d0e3ef061631f0db29e6495353cbce0f8ba96749689b`. CI compares three sequences of 192 operations against native maps. It checks balance, key order, lookup, deletion, and prior values. An API proof checks callback order, integer, float, and Boolean values, compound keys, NaN order, and positive and negative zero. The selected map timelines and claims are equal.

**Record field cost.** The compiler reads only selected record fields. It skips wildcard payload fields. Source-offset lookup returns an integer without a tuple allocation. A fixed source snapshot runs `irGenerated 7` and `evGenerated 7`. Three warm probes take 7.147 to 7.187 s. Nine baseline probes take 8.538 to 8.680 s. Baseline groups run before and after the changed compiler. The median decreases by about 16.5 percent. Timelines and claims are equal in all three comparisons. The probes run in sequence on the same Linux host. It uses Linux 7.0.0-38-generic on an Intel Core i7-10875H at 2.30 GHz with about 31 GiB of memory. Each server has a separate startup wait. These samples do not establish a general speed bound. The CLI SHA-256 is `9b669e84f10cec6b46295df28e0768ae366c9ae8d3278c1d5087eb3b1924eb6d`.

CPU samples from two type-checker idle probes place about 21 percent of probe CPU time in list indexing. Stack samples show these reads in kit lookup and trace checks. Generated LLVM for a kit lookup has one payload read instead of 18. These samples do not establish a general CPU cost bound.

**Known cost.** A zero-delay retry reaches the scheduler step cap in about 13 s on the evaluator and 0.7 s compiled. A mutated page limit reaches the 20 s probe deadline on both engines. The type-checker, code-generation, and CLI idle probes exceed their evaluator deadlines in sequential validation. Each server reports readiness before two probe requests. Type-checker startup takes 8.093 s. Its probes take 20.736 s and 20.731 s. Code-generation startup takes 7.754 s. Its two probes take 20.730 s and 20.737 s. CLI startup takes 8.102 s. Its two probes take 20.743 s and 20.705 s. Each completed request reports deadline status `124`. Startup has a separate 30 s wait. Each response has its own 30 s wait. A failed source check reports its error before a request. These are sequential samples on one Linux host. Comparison distances use a hash lookup. Function lookup uses an index by module and name. Maps use a balanced tree. Record access reads only selected fields. Source-offset lookup avoids a tuple allocation. These changes do not bring the idle probes below the deadline. Their campaigns run compiled. They do not prove evaluator parity. View signals use the native handle form in dumps. This form does not describe pixels. Do not hide other parity failures with a successful card.

**Proof.** Replay selected UI and IO witnesses compiled. Keep the existing campaign parity checks. Measure cold readiness, warm readiness, and cached navigation separately. Target cached navigation below 250 ms. Report an unmet target. Optimize demonstrated costs without reducing evidence. Semantic timeline alignment stays later.

### 3. Mobile on real devices

**Unproven.** JNI/ObjC embedding, touch, soft-keyboard text input, and Android OpenSSL on hardware. Hardware runs need provisioning. Simulator proofs do not close this gap.

**Proof.** Run Counter on one device with `scuzz package` and the platform toolchain. Physical-device work is outside the review arc.

The local iOS loop targets arm64 simulators on iOS 16 or later. Physical-device signing and release distribution remain open. iOS supports Net clients with platform certificate trust. iOS `Net.httpGetToFile` compiles, but the current Net proof app does not launch with the SDK scene-lifecycle requirement. Its runtime proof stays open. Net HTTP servers remain host-only. Android packages reject Net calls because they do not link OpenSSL.

## Known gaps

### Local choice feedback

Can't decide has no reason. Local model requests and exported producer requests do not consume preferences. There is no retained-acceptance measure or local session summary. Region focus needs controls to retain or select a region after a result.

**Proof:** Gate 5. Exercise bounded feedback, abstention reasons, region focus, isolated reveals, Undo, later replacement, pause, and restart. Reproduce summary totals from primary local records. Measure retained acceptances per active review minute. Keep human usefulness unproven.

### Rule review

There is no explicit claim suggestion or installation path from a preference. A finite witness cannot establish a universal rule. Rule installation needs protection against weakening existing claims and against stale source.

**Proof:** Gate 6. Show a separately approved rule that constrains a later proposal. Preserve existing claims. Reject an untriggered rule as unverified.

### Supporting compiler and editor work

- **Type names across files.** Constructor checking, field lookup, and native tags select the first matching declaration in file order. Fields from repeated short names stay separate. CI checks two declaration orders against fixed results. Short type names do not give repeated declarations separate module identities. Use distinct type names for separate layouts. Module type identity remains open.

- **Compile time.** `scuzz check examples/compiler` takes 4.63 s in one current sample. The CLI SHA-256 is `4bf1aeddc4148d4db8c674bffc2503bb70cd5405227483fcf1d896ba77d55c73`. The editor campaign, CLI corpus replay, and PR checks run at the same time. `scuzz build --full examples/tyck` is recorded at about 16 s. Refresh these and the editor measurements after compile-time changes. Reduce demonstrated checker or LLVM emission costs only when they block this arc.
- **IDE subprocesses.** Run, Fuzz, and Diff use the CLI. Completion, formatting, code actions, semantic tokens, inlay hints, and folding use `scuzz lsp`. Removing these calls is not a gate for the stream.
- **Check scope.** The editor Check button does not perform all format and verify-file checks of `scuzz check`. Candidate gating must use the required shared checks even if the general button stays separate.
- **Standard kits.** OS threads remain missing. Add kit work only when it blocks an ordinary program or a required review proof.

### Local model evidence

The pinned Qwen3.5 9B model runs on macOS ARM64 Metal. It produces a message input and a Send action that retains messages in a visible list. Each candidate passes all three required workloads and 32 search cases with no regression. A native interaction proof enters a message, selects Send, and checks the retained message and cleared input. These finite cases do not prove sustained useful generation. Qwen3.5 4B execution and Linux x86-64 and ARM64 Vulkan execution remain unverified. GPU detection and CPU fallback need further host proof. Peak memory remains unknown. Larger source scopes can exceed the fixed context. Controlled replies do not prove usefulness.

**Proof.** Run the pinned backend on each supported host. Measure peak memory and useful output for the fixed catalog. Use real weights and the shared request, parser, and behavioral review. Report unavailable hosts separately. Do not expand the catalog or claim quality from model size.

**Measurement.** One successful macOS Metal 9B correction takes 41.2 s from request start to publication. It uses 2,025 prompt tokens and 463 output tokens. This sample does not establish a latency bound. Peak memory for the current catalog remains unverified. The pinned revisions and artifact digests are in the [generation catalog](../examples/editor/generation/src/Models.scuzz). The process budgets are 4 GiB and 8 GiB. Linux enforces their virtual memory limits. The default finite proposal changes `greeting.txt` from `Hello` to `Hello Scuzz` under simulation. Its target has no registered claims. Its review completes all required workloads and 32 search cases. Finite Headless review retains a zero-event witness and exits normally. Frozen card preparation takes 1.736 s in one sample. This interval excludes generation, editor startup, and input collection before the card freezes. The target has one required idle workload and no seeds or corpus files. It changes no source before a choice. A console-only proposal has no recorded difference. These cases do not measure human preference or application quality.

## Cuts and later work

Do not add user FFI, `extern`, plugins, library publishing, git or registry dependencies, or `scuzz add`. Do not start a provider framework, remote generator service, telemetry, or model training. Limits and exclusions for the current arc live in `vision.md`.

Generated setup inputs. Multiple named scenarios and campaign selection. Session event journal and live time ops. Stable scroll keys. Windows desktop. OS IME candidate windows. macOS release packaging in default CI. Developer ID signing and notarization. Full web accessibility. Real phone and screen-reader checks. Hot reload on web. Multiple UI factories in host hot reload. General model browsing. Metal and Vulkan performance measurements. Gated-model login. Model conversion. Automatic oracle mining. Semantic timeline alignment. Divergence attribution to source defs. Pixel previews. Emit scalar fallbacks. Dogfood IDE: native file dialogs, menus, multi-window, multi-cursor, minimap, Git UI, debugger, plugin host, custom canvas kit.
