# Gaps and unknowns

Missing behavior and unproven claims, ranked by risk to [`philosophy.md`](philosophy.md). Implementation order: [`vision.md`](vision.md).

Remove closed gaps. Keep current measurements and their limits. Do not keep a history of completed work.

## Unknowns

### 1. Useful development through decisions

**Unproven.** Blind behavioral choices improve a real application toward an objective. A short witness gives enough context for a sound choice. Useful generation can sustain a bounded stream. Preferences remain useful after later edits. A human decides more efficiently from the card than from source alone.

**Proof.** Complete the session gates in `vision.md`. Run a human session on one small app and one objective. Measure retained acceptances per review minute, abstention reasons, Undo, and readiness latency. Record workload scope and the generator used. Controlled generator output proves the transport, not usefulness. Automated choices do not count as human evidence.

**Priority.** Keep current work on the language and verification guarantees in `vision.md`. Local choice feedback, rule review, and the complete session proof remain later work. General compiler idle cost work needs a demonstrated application or proof blocker.

### 2. Review latency and evaluator parity

**Unproven.** Full corpus replay plus bounded search fits a frequent decision loop. Evaluator timelines remain equal to compiled timelines on new generated proposals. Index-based state alignment remains clear when a proposal adds or removes steps.

**Measurement.** Counter review uses four required workloads: idle and three corpus files. It has no seeds. It runs 32 search iterations with seed 1. A first card in a fresh target takes 8.0594 s from editor launch to the visible question. The evaluator host is cached. Frozen card preparation takes 3.523 s. Restart restores the saved cards in 936.7 ms. Cached card navigation takes 163.5 ms. Cached workload navigation takes 106.7 ms. Cached step navigation takes 106.7 ms. The 250 ms navigation target is met. Each value is one Headless sample. The first sequence also edits an unsaved buffer. The cold value includes startup and input preparation. It does not measure a cold host build. The warm run starts a new editor process and reuses the saved card identities and evidence.

The host runs Linux 7.0.0-38-generic on an Intel Core i7-10875H at 2.30 GHz with about 31 GiB of memory. The CLI SHA-256 is `ad7ba6b36468b34f1c8eda053c0c05815246bfa57eab8de207580aa18cca1ebb`. The editor SHA-256 is `897924038d30e0f9f3595b34485711d2453b240d3c2b5badf5f12fb24b154f3f`. The evaluator host SHA-256 is `45be89f371850c247dfcecd546ab925f027c1ef067173f84c50f4b1248446042`. The source limit is 65,536 bytes across at most three proposed files. The existing probe limits apply.

**Finite parity.** CI compares exact timelines and claims for generated type-checker and code-generation drives. An IO proof keeps user signals visible and hides internal storage. Editor idle, queue-cache, and protocol replay have equal timelines and claims. The editor corpus has 228 passing entries. Its 22 claim drivers run. Three sometimes labels and four UI triggers remain unreached. These finite checks do not prove all programs equal. One sequential editor comparison takes 20.75 s for idle, 23.93 s for queue-cache, and 23.40 s for protocol replay on the evaluator. These intervals include host startup. The samples use Linux 7.0.0-38-generic. Compiled runs take 0.06 s, 0.21 s, and 0.21 s. The 20 s probe deadline and Linux memory limit apply.

**Constructor and View parity.** A constructor proof checks both declaration orders, defaults, named arguments, records, and match fields. Its evaluator and native timelines and claims are equal. A Headless proof checks direct, list, and nested View signals with a button action. Its timelines and claims are equal. The Docs corpus has 10 passing entries. Its idle native and evaluator timelines differ for View-valued list Signals. Native output uses handles. Evaluator output contains View graphs. The campaign uses compiled probes. Exact replay for these Signals remains open.

**Compiler scope proof.** Inline bindings use names from their expression scopes. Renaming includes local function calls. Nested matches use the checked scalar result type. Generated compiler proofs check repeated names in separate match arms, nested expressions, closure captures, aliases, and heap results. The generated proofs also check generic record access and wildcard fields with heap values. These finite checks do not prove all binding scopes correct.

**Implementation samples.** The comparisons run sequentially on one Linux host.
Recorded timelines and claims agree in each comparison.
Each row identifies the measured compiler. Refresh these measurements after changes to the relevant paths.
These finite samples do not establish a general speed bound.

| Implementation | Workload | Measured probes | Baseline probes | Median decrease | Compiler SHA-256 |
| --- | --- | --- | --- | --- | --- |
| Module and function index | `irGenerated 7` and `evGenerated 7` | 3 warm probes, 7.71–7.82 s | 8.17–8.26 s; function-name list | About 4% | `5f926ad12f7487c2715456bb30ea1a7ce47e0efac01f357d3dd88ca0a75aa3c5` |
| Immutable balanced map | Insert and read 5,000 ascending keys | 3 warm probes, 0.201–0.210 s | 7.724–7.850 s; sorted list | About 97% | `a946738eaa3423673593d0e3ef061631f0db29e6495353cbce0f8ba96749689b` |
| Selected record fields and integer source offsets | `irGenerated 7` and `evGenerated 7` | 3 warm probes, 7.147–7.187 s | 9 probes, 8.538–8.680 s | About 16.5% | `9b669e84f10cec6b46295df28e0768ae366c9ae8d3278c1d5087eb3b1924eb6d` |
| Completed evaluator values | `irGenerated 7` and `evGenerated 7` | 6 warm probes, 6.993–7.091 s | 6 probes, 7.202–7.259 s | About 2.5% | `179dfbb8b6c7b711151fc634078c793dec00b6005814a223d73f18ccf3ed7dd6` |

The record-field and completed-value samples use Linux 7.0.0-38-generic,
an Intel Core i7-10875H at 2.30 GHz, and about 31 GiB of memory.
Their baseline groups run before and after changed probes.
The completed-value baseline groups surround the first changed group.
Each record-field server has a separate startup wait.
Record access reads selected fields and skips wildcard payloads.
Source-offset lookup returns an integer without a tuple allocation.
Completed values avoid temporary expressions, environments, and variable lookups.
Native dispatch uses a tail-call loop.
A proof checks string and list results after 20,000 tail calls.

CI compares three sequences of 192 map operations with native maps.
It checks balance, key order, lookup, deletion, and prior values.
The API proof checks callback order, integer, float, and Boolean values, compound keys, NaN order, and both signs of zero.
Selected map timelines and claims agree.

Two type-checker idle CPU samples place about 11% of probe CPU time in list indexing.
Stack samples locate these reads in kit lookup and trace checks.
Generated LLVM for a kit lookup has one payload read instead of 18.
Allocation samples identify coverage-key construction and evaluator steps as large allocation sources.
Allocation counts do not measure CPU time. These samples do not establish a general CPU cost bound.

**Known cost.** The fixed-source type-checker, code-generation, and CLI idle probes exceed their evaluator deadlines in sequential validation. Each server reports readiness before two probe requests. Type-checker startup takes 8.710 s. Its probes take 20.740 s and 20.739 s. Code-generation startup takes 8.219 s. Its two probes take 20.738 s and 20.737 s. CLI startup takes 8.155 s. Its two probes take 20.716 s and 20.729 s. Each completed request reports deadline status `124`. Startup has a separate 30 s wait. Each response has its own 30 s wait. A failed source check reports its error before a request. These are sequential samples on one Linux host. Comparison distances use a hash lookup. Function lookup uses an index by module and name. Maps use a balanced tree. Record access reads only selected fields. Source-offset lookup avoids a tuple allocation. Completed values avoid temporary expressions and environments. These changes do not bring the idle probes below the deadline. Their campaigns run compiled. They do not prove evaluator parity. View signals use the native handle form in dumps. This form does not describe pixels. Do not hide other parity failures with a successful card.

**Proof.** Replay selected UI and IO witnesses compiled. Keep the existing campaign parity checks. Measure cold readiness, warm readiness, and cached navigation separately. Target cached navigation below 250 ms. Report an unmet target. Optimize demonstrated costs without reducing evidence. Semantic timeline alignment stays later.

### 3. Mobile on real devices

**Unproven.** JNI/ObjC embedding, touch, soft-keyboard text input, and Android OpenSSL on hardware. Hardware runs need provisioning. Simulator proofs do not close this gap.

**Proof.** Run Counter on one device with `scuzz package` and the platform toolchain. Physical-device work is outside the review arc.

The local iOS loop targets arm64 simulators on iOS 16 or later. Physical-device signing and release distribution remain open. iOS supports Net clients with platform certificate trust. Hosted simulator checks cover HTTP requests, streamed downloads, cancellation, certificate trust, and reactive network UI. Net HTTP servers remain host-only. Android packages reject Net calls because they do not link OpenSSL.

## Known gaps

### UI framework capability

Real device checks for responsive layouts remain open.
Real device checks for virtual lists remain open.
Real mobile device bitmap checks remain open.
Image color profiles, orientation metadata, and animated formats remain open.
Browser accessibility and real screen-reader checks remain incomplete.
Android exposes virtual controls to platform accessibility tools.
The shared runtime supplies owned semantic snapshots and validates actions against current visible controls.
Headless records and replays accessibility actions by semantic position.
A finite Android emulator proof checks 100 repeated-label controls, text editing, UTF-16 selection, range actions, modal exclusion, and stale actions after replacement.
Android service checks cover virtual input focus, cleared focus, and native Backspace over a selected emoji.
Android emulator checks cover initial focus events for new dialog controls.
Android emulator checks compare scaled and clipped semantic bounds with visible screenshot pixels.
Hover-event routing passes with Android exploration mode enabled.
Raw touchscreen conversion and real screen-reader checks remain open.
Android records accessibility actions, viewport changes, and lifecycle input.
A finite emulator proof replays that record in Headless.
Named Signals, field text, byte selection offsets, and the semantic tree agree.
iOS and Desktop screen-reader bridges remain open.
Keyboard checks cover a form, named radio groups, manual tabs, nested dialogs, named range sliders, and editors.
Range checks cover signed 64-bit bounds, stepped values, pointer input, and browser accessibility data.
Physical-device and real screen-reader checks for sliders remain open.
Named progress checks cover live values, range limits, hidden indicators, dialogs, and input exclusion.
Native checks cover signed 64-bit limits and escaped names.
Physical-device and real screen-reader checks for progress indicators remain open.
Indeterminate progress remains open.
Split-pane checks cover keys, collapse, restore, retained state, and named browser separators.
Physical-device and real screen-reader checks for split panes remain open.
Dialog checks cover initial focus, focus return, removed openers, and empty dialogs.
Browser checks cover named modal groups and background exclusion.
Pointer checks cover card surfaces, scroll boundaries, hover, secondary input, and cancelled gestures.
Shared runtime checks cover repeated labels and 100 focus targets.
Segmented choice checks cover names, equal widths, keys, repeated labels, and browser radio states.
Empty, single, and 100-choice groups have shared runtime checks.
Physical-device and real screen-reader checks for segmented choices remain open.
Select checks cover pending input, commit, cancel, type-ahead, dialogs, and bounded popup rows.
Native checks cover empty, invalid, repeated, and 1000-option lists.
Browser checks cover combobox relations, active options, keys, wheel input, touch input, and viewport changes.
Physical-device and real screen-reader checks for select controls remain open.
Reactive option checks cover replacement, empty lists, recovery, retained fields, and stale pointer input.
Editable options and full Unicode case folding remain open.
Compiled UI corpus replay under ASan remains open.
The runtime C sanitizer suite checks UI constructors and input.
Expansion checks cover header input, body conditions, nested fields, focus return, and linked browser regions.
Physical-device and real screen-reader checks for expansion controls remain open.
Tooltip checks cover focus, hover, popup input, Escape, composition, clipping, themes, and browser descriptions.
Native and evaluator checks compare exact visibility, timelines, and claims.
Physical-device and real screen-reader checks for tooltips remain open.
Keyboard behavior for other composite controls remains unproven.
Inline action checks cover wrapped fragments, Int keys, callbacks, focus, source copying, and browser button order.
Physical-device and real screen-reader checks for inline actions remain open.
Default native Skia checks cover kerning, ligatures, combining accents, Arabic joining, and mixed-direction lines.
Source cluster checks cover wrapping and ellipsis. Field checks cover shaped carets and pointer positions.
Same-font span checks cover kerning, ligatures, Arabic joining, combining marks, and fallback clusters.
Native paint checks compare 25 style and source-cluster cases with unsplit text.
24 native and browser cases check Arabic joining across font and size changes.
They include direction controls, isolates, transparent marks, line breaks, and ellipsis.
Empty isolates also block joining.
Browser checks cover joined source ranges, colored glyphs, copying, wrapped-action selection, and retained focus.
Pinned Noto fonts supply native and browser fallback for Arabic, Devanagari, Thai, and Simplified Chinese.
Finite checks cover source clusters, mark bounds, CJK field positions, and composition.
Browser checks cover delayed font loading, retained focus, source copying, and font advances.
The CJK font download uses 17.8 MB before compression. Smaller font delivery remains open.
Color emoji, author-supplied fonts, and locale-specific CJK forms remain open.
Other contextual scripts, mark placement across font changes, and full bidi caret affinity remain open.
Native software and OpenGL use the shared native shaper and clipped glyph coverage rows.
Their monospace paint uses pinned DejaVu Sans Mono glyph outlines.
Monospace metrics scale without pixel rounding.
Unicode editor checks cover pointer input, arrow movement, insertion, and source byte offsets at two scales.
They pass for software, OpenGL, Skia, ASan, and UBSan.
Monospace fallback faces remain open.
Paint checks cover proportional advances, source clusters, styles, paragraph ranges, and large zoom values.
Full runtime checks pass for all three native renderers. Android arm64 and x86_64 builds pass.
ASan and UBSan checks pass for the portable shaper and glyph raster path.
168 script, size, and style cases compare widths, carets, clusters, and glyph outlines with Skia.
They include proportional and monospace spans at fractional font sizes.
The glyph reference uses unhinted Skia with subpixel positions.
Text advance and glyph placement defects fail these checks.
Current UI checks pass for software, OpenGL, Skia, ASan, and UBSan.
Browser shaping checks pass in all five configurations.
Current text and control checks pass in all five configurations. Package CI passes.
Android API 36 emulator checks cover text changes, typing after a combining mark,
keyboard viewport changes, button input, and retained state after resume.
The emulator uses x86_64, four cores, and 4 GB of memory.
Monospace application checks also cover counter input, typing, keyboard viewport changes, and resume.
Software and OpenGL application snapshots have a maximum channel delta of 1.
Software and OpenGL replay produce exact native and evaluator timelines and claims.
The OpenGL campaign completes within the probe limits with Mesa worker threads disabled.
Default-renderer replay and clean Headless UI checks pass.
Package CI and the rebuilt CLI manual checks pass.
Hosted macOS and arm64 iOS simulator checks cover native startup, network UI, source reload, state retention, keyboard viewport changes, and shutdown.
Real-device styled text checks remain open.
Opacity transitions use the shared pump clock and retain input state.
Renderer checks cover overlap, nested layers, clips, transparency, and GPU orientation.
Runtime checks cover retargeting, hidden cancellation, pause, reduction, and recorded pumps.
Size transitions use the shared pump clock and retain child state.
Native and browser checks cover exact steps, retargeting, clipping, input, reduction, and zero size.
Native and evaluator replays compare exact timelines and claims.
Content-size transitions measure the child with incoming constraints and keep fractional dimensions.
Native checks cover wrapped text, constraints, exact steps, clipping, and retained input.
Browser checks cover pixels, following content, retargeting, hidden cancellation, reduction, and input state.
Native and evaluator replays compare exact content-size timelines and claims.
Position transitions use the shared pump clock and current child layout.
Native checks cover exact steps, both axes, clipped parents, source limits, and retained selection.
Browser checks cover moving control bounds, old and current pointer locations, hidden cancellation, and reduction.
Browser range checks keep signed 64-bit source values while display clamps the coordinates.
Native and evaluator replays compare exact position timelines, claims, and control bounds.
Scale transitions use the shared pump clock and keep layout and text wrapping unchanged.
Native and browser checks cover nested scales, clips, glyphs, source limits, and displayed input bounds.
Zero scale excludes paint, input, focus, and accessibility. Child state remains available.
Browser checks compare glyph coverage with a separate Canvas reference at six scales.
Native and evaluator replays compare exact scale timelines, claims, and control bounds.
Rotation transitions remain open.
Linux Desktop reads the Settings portal motion preference and receives live changes.
A private bus check covers initial values, invalid signals, foreign senders, stale replies, and service restart.
Recordings include the initial session preference before live input.
Shared runtime checks compare 400 Desktop and Mobile event recordings with Headless replay across five transition types.
They check retained input, field selection, and the forced native setting.
Additional checks cover pause, reduction, View replacement, and resume.
The shared runtime checks and private portal check pass under ASan and UBSan.
Full native and evaluator preference replay checks remain open.
Android reads the animation setting before application startup and observes live changes.
Android emulator checks cover initial preferences, live changes, visible input, resume, and restart.
The Android viewport excludes the system bars and display cutout.
Android emulator checks cover keyboard insets, typing, dismissal, reopening, and retained text.
Apple preference sources read the initial setting and observe native notifications.
Hosted SDK checks cover both sources with forced preference values and native notification delivery.
Real operating-system preference changes remain unverified on Apple platforms.
Real-device motion checks remain open.
Operating-system theme selection and real-device theme checks remain open.

Use the UI work order in `vision.md`.
Prove each capability with an application, shared input, and exact native and evaluator replay.
Keep these gaps open until the corresponding runtime behavior and platform checks pass.

### Local choice feedback

Can't decide has no reason. Local model requests and exported producer requests do not consume preferences. There is no retained-acceptance measure or local session summary. Region focus needs controls to retain or select a region after a result.

**Proof:** Gate 5. Exercise bounded feedback, abstention reasons, region focus, isolated reveals, Undo, later replacement, pause, and restart. Reproduce summary totals from primary local records. Measure retained acceptances per active review minute. Keep human usefulness unproven.

### Rule review

There is no explicit claim suggestion or installation path from a preference. A finite witness cannot establish a universal rule. Rule installation needs protection against weakening existing claims and against stale source.

**Proof:** Gate 6. Show a separately approved rule that constrains a later proposal. Preserve existing claims. Reject an untriggered rule as unverified.

### Verification scope and application correctness

- **Parser diagnostics.** A reported parser error cannot pass the shared checker. Formatting preserves the affected file. Language server formatting returns no edits. Tests cover declaration names, bodies, parameters, defaults, annotations, and match guards. Parser errors with no token span mark the start of the affected source file. Exact token spans for these declaration errors remain open.
- **Input boundaries.** Strict integers and required JSON fields return explicit errors. The kernel checks field types and domain ranges in live code. General typed JSON decoding and nested field error paths remain open. Extend the kit when an application needs them. Verification assumptions do not validate live input. Result checks reject uses confined to erased `.require` labels and predicates. Private record constructors restrict construction and copy to the declaring module. The editor validates relative paths in live Result constructors and retains canonical path checks. Private access does not prove the predicate. Other erased checks can still be mistaken for input validation. Diagnose clear uses before adding more input operations.
- **Failure handling.** Result bindings and Result fields in tuple, list-cons, and constructor binding patterns reject discard and partial patterns. Bound Result lists, including nested lists and IO payloads, reject unused values. Other whole containers can still hide unused results. A whole-container alias does not replace explicit use of a selected Result field. Extend these checks only for a demonstrated application need. Passing a result to a function does not prove that the receiver handles its error. Keep expected errors explicit through input and effect boundaries.
- **Behavior rules.** Make rules about state, effects, cancellation, duplicate requests, and recovery easy to express and diagnose. Check relationships between executions without a second app implementation. The recovery fixture compares file presence and contents for two sources, the journal, the operation record, and the Undo state. A conflict-write control passes the endpoint rules but fails this comparison. Repeated recovery preserves the same five files. A completed request rejects a duplicate. Duplicate rejection preserves these files and an unrelated file. Separate defects exercise the guard and file preservation rules. These checks use exact editor function bodies. They do not cover all editor operations or concurrent external writes.
- **Reference models.** Deferred. A model is optional. It is not the default verification path.

Undo validates the accepted record before journal creation.
The check covers record identity, decision, write field types, paths, and file presence.
With an empty journal, 19 malformed records preserve every operation file.
The live check also compares all workspace files.
Recovery uses the same operation shape check.
The operation identity must match its card and decision before recovery changes files.
The malformed journal scope compares all files in its separate workspace.

### Formal verification

**Unproven.** The supported subset and resource cost fit an ordinary application author workflow. Formal methods add useful defect detection beyond ordinary fuzzing. General solver translation and compilation correctness remain unproven.

**Evidence.** The optional [formal checks](vision.md#formal-verification-methods)
reuse author rules. Commands and report paths live in
[`developer-environment.md`](developer-environment.md#formal-verification-experiment).
The reports retain exact inputs, tool identities, bounds, trusted components,
and command costs. These costs exclude human writing time.

The pure expression check translates checked Int and Bool expressions.
It uses signed 64-bit comparisons and arithmetic modulo 2^64.
It rejects effects, recursion, implicit assumptions, and unsupported expressions.
One balance oracle supplies both fuzz checks and solver queries.
The check covers all signed 64-bit inputs for this invariant.
It checks assumption consistency and required trigger reach separately.
It compares 70 expression results with independent host arithmetic.
It checks live input validation and replays minimum Int and an overflow defect
at maximum Int through both engines. Ordinary fuzzing uses 16 iterations.

The finite protocol runs two requests in sequence.
The caller can cancel the first worker before the second request starts.
Each request releases its resource once and waits before its seven-byte write.
Errors before replacement preserve previous bytes.
Errors after replacement retain complete new bytes.
Cancellation preserves the first request file.
The checker rejects a release defect with an ordinary replay workload.

| Scope | Inputs and faults | Limits | One Linux sample |
| --- | --- | --- | --- |
| Protocol | Inputs 1 and 2; no fault or one fail, partial, or after-replacement fault at calls 1–4 | 16 contention choices; 128 steps; 1024 executions; 60 s after build | 676 executions; at most 4 choices and 36 steps; 12.80 s checking; 4.67 s cached build |
| Recovery | Four journal states and four cancellation inputs; fault calls 1–16 | 1000 steps and contention choices; 2048 executions; 60 s | 196 sequential paths pass; wider scope reaches the execution limit in 56.37 s; 117268 prefixes remain |
| Recovery with no preemption | Same recovery inputs and faults | Separate 2048-execution and 60 s limits | All 392 paths pass in 10.74 s; no interruption during recovery |
| Acceptance comparison | Inputs 0, 1, 4, 5; fault calls 1 and 9; all three fault modes | At most one preemption; 1000 steps; 2048 executions; 60 s per method | All 1286 exhaustive paths pass in 38.56 s; 2048 sampled runs take 11.89 s and reach 112 distinct paths |
| Prepared Undo comparison | Inputs 2, 3, 6, 7; fault calls 1 and 9; all three fault modes | Same comparison limits | All 1300 exhaustive paths pass in 43.25 s; 2048 sampled runs take 12.45 s and reach 112 distinct paths |
| Undo request comparison | Inputs 0–3; fault calls 1 and 25; all three fault modes | Same comparison limits | All 567 exhaustive paths pass in 21.27 s; 2048 sampled runs take 13.19 s and reach 107 distinct paths |

Setup does not consume fault bounds.
A preemption selects another fiber while the previous fiber remains runnable.
A switch after blocking does not consume this bound.
Each sampled path occurs in its completed exhaustive comparison set.
The sampled method uses the existing PCT scheduler with seed 42.
Each comparison uses the same source graph, rules, faults, and budgets.
The one- and two-preemption recovery scopes remain incomplete.
Their separate limits do not replace the wider required scope.
The protocol checks its bounded path sets against its complete schedule set.
Zero, one, and two preemptions produce 1, 3, and 5 distinct protocol paths.

Recovery uses exact editor function bodies with fixed fixture inputs.
Source selection remains trusted. The check does not run the complete IDE.
Two source files, the journal, the operation record, and the Undo state
supply five operation files for comparison.
Missing and empty files differ. Conflict and repeated recovery rules compare
presence and contents. Duplicate rejection also preserves an unrelated file.
Separate observation drivers keep reads out of existing fault paths.
Expected file faults retain a pending operation. Unexpected errors fail a rule.

Ordinary recovery fuzzing uses 64 iterations, seed 42, and a 60 s checking limit.
Its corpus covers acceptance, Undo, conflicts, cancellation, repeated recovery,
duplicate requests, and malformed records and journals.
One sample passes 67 required probes and 40 search probes in 30.88 s.
It detects 22 sampled mutations. Two are inert. None survives or stays incomplete.
These mutations affect the fixture and selected application functions.
The 63 fault-free rule checks and exact engine comparisons take 1.39 s.
Both methods reject missing-write and external-overwrite controls.
All 14 direct defect checks produce named author rule failures in both engines.
Ordinary native replay reproduces each rejection.
A conflict-write defect passes endpoint rules but fails the file comparison.
Other controls change guards, paths, fields, Undo state, journals, or unrelated files.
The duplicate workflow has 12 source lines and three author assertions.
Its sample costs are 6.75 s for the defect build, below 0.01 s for native checking,
0.19 s for evaluator comparison, and 0.40 s for ordinary native replay.

Live file checks cover directory sync errors and process termination
before and after replacement. Previous or complete new bytes remain present.
The live recovery check completes 23 acceptance and Undo recovery cases.
Restart completes recovery. Repeated recovery preserves all five operation files.
The live Undo request check completes 22 cases, including 19 malformed records.
A further 21 malformed journal cases preserve all workspace files and Undo state.
These checks retain source and native identities, logs, and file contents.

**Open scope.** Power loss, external edits during recovery, temporary file cleanup,
other fault calls, other application operations, and unbounded schedules remain open.
Cancellation paths can reach 144 contention choices.
The 16-point sampling policy does not limit stored replay choices.
Finite schedules do not prove eventual completion or inductive safety.
An executed rule does not establish specification completeness.
One host sample does not establish a general cost bound or method advantage.

**Next gate.** Measure human writing time, rule comprehension, and usefulness in one author session. Preserve wider incomplete results. Keep power loss, concurrent external edits, and other application operations open. Do not infer a general method advantage from one application scope.

**Limits.** Refinement types, static effect protocols, inductive proofs, and compilation validation remain proposals. Existing finite campaigns do not establish these guarantees. A proof establishes a stated property under its assumptions. It does not establish that the specification matches all product needs.

### Supporting compiler and editor work

- **Compile time.** `scuzz check examples/compiler` takes 4.63 s in one current sample. The CLI SHA-256 is `4bf1aeddc4148d4db8c674bffc2503bb70cd5405227483fcf1d896ba77d55c73`. The editor campaign, CLI corpus replay, and PR checks run at the same time. `scuzz build --full examples/tyck` is recorded at about 16 s. Refresh these and the editor measurements after compile-time changes. Reduce demonstrated checker or LLVM emission costs only when they block this arc.
- **IDE subprocesses.** Run, Fuzz, and Diff use the CLI. Completion, formatting, code actions, semantic tokens, inlay hints, and folding use `scuzz lsp`. Removing these calls is not a gate for the stream.
- **Standard kits.** OS threads remain missing. Add kit work only when it blocks an ordinary program or a required review proof.

### Local model evidence

The pinned Qwen3.5 9B model runs on macOS ARM64 Metal. It produces a message input and a Send action that retains messages in a visible list. Each candidate passes all three required workloads and 32 search cases with no regression. A native interaction proof enters a message, selects Send, and checks the retained message and cleared input. These finite cases do not prove sustained useful generation. Qwen3.5 4B execution and Linux x86-64 and ARM64 Vulkan execution remain unverified. GPU detection and CPU fallback need further host proof. Peak memory remains unknown. Larger source scopes can exceed the fixed context. Controlled replies do not prove usefulness.

**Proof.** Run the pinned backend on each supported host. Measure peak memory and useful output for the fixed catalog. Use real weights and the shared request, parser, and behavioral review. Report unavailable hosts separately. Do not expand the catalog or claim quality from model size.

**Measurement.** One successful macOS Metal 9B correction takes 41.2 s from request start to publication. It uses 2,025 prompt tokens and 463 output tokens. This sample does not establish a latency bound. Peak memory for the current catalog remains unverified. The pinned revisions and artifact digests are in the [generation catalog](../examples/editor/generation/src/Models.scuzz). The process budgets are 4 GiB and 8 GiB. Linux enforces their virtual memory limits. The default finite proposal changes `greeting.txt` from `Hello` to `Hello Scuzz` under simulation. Its target has no registered claims. Its review completes all required workloads and 32 search cases. Finite Headless review retains a zero-event witness and exits normally. Frozen card preparation takes 1.736 s in one sample. This interval excludes generation, editor startup, and input collection before the card freezes. The target has one required idle workload and no seeds or corpus files. It changes no source before a choice. A console-only proposal has no recorded difference. These cases do not measure human preference or application quality.

## Cuts and later work

Do not add user FFI, `extern`, plugins, library publishing, git or registry dependencies, or `scuzz add`. Do not start a provider framework, remote generator service, telemetry, or model training. Limits and exclusions for the current arc live in `vision.md`.

Generated setup inputs. Multiple named scenarios and campaign selection. Session event journal and live time ops. Stable scroll keys. Windows desktop. OS IME candidate windows. macOS release packaging in default CI. Developer ID signing and notarization. Full web accessibility. Real phone and screen-reader checks. Hot reload on web. Multiple UI factories in host hot reload. General model browsing. Metal and Vulkan performance measurements. Gated-model login. Model conversion. Automatic oracle mining. Semantic timeline alignment. Divergence attribution to source defs. Pixel previews. Emit scalar fallbacks. Project IDE: native file dialogs, menus, multi-window, multi-cursor, minimap, Git UI, debugger, plugin host, custom canvas kit.
