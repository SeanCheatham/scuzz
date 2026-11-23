# Gaps and unknowns

Missing behavior and unproven claims, ranked by risk to [`philosophy.md`](philosophy.md). Implementation order: [`vision.md`](vision.md).

Remove closed gaps. Keep current measurements and their limits. Do not keep a history of completed work.

## Unknowns

### 1. Useful development through decisions

**Unproven.** Blind behavioral choices improve a real application toward an objective. A short witness gives enough context for a sound choice. Useful generation can sustain a bounded stream. Preferences remain useful after later edits. A human decides more efficiently from the card than from source alone.

**Proof.** Hermetic and live finite Headless proofs cover the software session gates for UI (`examples/counter`) and IO (`examples/webhook`) objectives over local processes, private loopback HTTP, and filesystem storage. Real GGUF model generation without controlled replies and human usefulness remain unverified. Run a real developer session on one small app and one objective. Measure retained acceptances per active review minute, abstention reasons, Undo count, rule installations, and readiness latency. Record workload scope and the generator used. Controlled generator output proves transport and verification gates, not usefulness. Automated choices do not count as human evidence.

**Priority.** Run a measured human developer session with a real local or external generator. General compiler idle cost work needs a demonstrated application or proof blocker.

### 2. Review latency and evaluator parity

**Unproven.** Full corpus replay plus bounded search fits a frequent decision loop. Evaluator timelines remain equal to compiled timelines on new generated proposals. Index-based state alignment remains clear when a proposal adds or removes steps.

**Measurement.** Counter review uses four required workloads: idle and three corpus files. It has no seeds. It runs 32 search iterations with seed 1. A first card in a fresh target takes 5.3910 s from editor launch to the visible question. The evaluator host is cached. Frozen card preparation takes 2.171 s. Restart restores the saved cards in 680.3 ms. Cached card navigation takes 105.2 ms. Cached workload navigation takes 105.3 ms. Cached step navigation takes 105.9 ms. The 250 ms navigation target is met. Each value is one Headless sample. The first sequence also edits an unsaved buffer. The cold value includes startup and input preparation. It does not measure a cold host build. The warm run starts a new editor process and reuses the saved card identities and evidence.

The host runs Linux 6.12.113+ on an Intel Xeon processor (4 vCPUs) with about 16 GiB of memory. The CLI SHA-256 is `19b4621eb15113c997ddc6e355f499ff11cc889403b3525343c251c297675b5e`. The editor SHA-256 is `9595bacad569ce9f6446bf3606ffd010585dc24ac0c59613d404dee11a605f75`. The evaluator host SHA-256 is `077e135e57f91b52da20cc43c4b970baedd5d066c1edbe93f27afd65f49ad254`. The source limit is 65,536 bytes across at most three proposed files. The existing probe limits apply.

**Finite parity.** CI compares exact timelines and claims for generated type-checker and code-generation drives. An IO proof keeps user signals visible and hides internal storage. Editor idle, queue-cache, protocol, and complete-session corpus replays have equal timelines and claims on evaluator and compiled engines. The editor corpus has 261 passing entries. Its 25 claim drivers run. It reaches all 20 sometimes labels and fires all 17 UI triggers. These finite checks do not prove all programs equal. One sequential editor comparison takes 21.46 s for idle, 24.44 s for queue-cache, and 23.75 s for protocol replay on the evaluator. These intervals include host startup. The samples use Linux 6.12.113+. Compiled runs take 0.05 s, 0.18 s, and 0.18 s. The 20 s probe deadline and Linux memory limit apply.

**Constructor and View parity.** A constructor proof checks both declaration orders, defaults, named arguments, records, and match fields. Its evaluator and native timelines and claims are equal. A Headless proof checks direct, list, and nested View signals with a button action. Its timelines and claims are equal. The Docs corpus has 11 passing entries. Docs idle and corpus runs have equal native and evaluator timelines and claims. Campaigns run on the evaluator. These finite checks do not prove all View signal graphs equal.

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

**Unproven.** JNI/ObjC embedding, touch, soft-keyboard text input, and platform HTTP on hardware. Hardware runs need provisioning. Simulator and emulator proofs do not close this gap.

**Proof.** Run Counter on one device with `scuzz package` and the platform toolchain. Physical-device work is outside the review arc.

The local iOS loop targets arm64 simulators on iOS 16 or later. Physical-device signing and release distribution remain open. iOS and Android support Net HTTP clients with platform certificate trust and permit local-network HTTP. Hosted iOS simulator checks cover HTTP requests, streamed downloads, cancellation, certificate trust, and reactive network UI. Native JNI HttpURLConnection checks cover Android HTTP requests, streamed downloads, headers, redirect refusal, cancellation, certificate trust, and local-network manifest rules; physical-device HTTP checks remain open. Net HTTP servers remain host-only.

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
Linux Desktop exposes semantic controls over AT-SPI2 D-Bus.
Private D-Bus checks replay recorded Linux Desktop accessibility actions in Headless.
macOS, iOS, and real screen-reader checks remain open.
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
Type-ahead uses the Unicode full case fold. The fold uses common and full mappings and excludes Turkic mappings.
Native checks cover empty, invalid, repeated, and 1000-option lists.
Browser checks cover combobox relations, active options, keys, wheel input, touch input, and viewport changes.
Physical-device and real screen-reader checks for select controls remain open.
Reactive option checks cover replacement, empty lists, recovery, retained fields, and stale pointer input.
Editable options remain open.
Compiled UI corpus replay under ASan covers Headless with portable software paint.
Prebuilt Skia and GPU backends remain uninstrumented.
Expansion checks cover header input, body conditions, nested fields, focus return, and linked browser regions.
Physical-device and real screen-reader checks for expansion controls remain open.
Tooltip checks cover focus, hover, popup input, Escape, composition, clipping, themes, and browser descriptions.
Native and evaluator checks compare exact visibility, timelines, and claims.
Physical-device and real screen-reader checks for tooltips remain open.
Composite keyboard checks cover `View.tabs`, `View.indexBook`, `View.focusGroup`, modal `View.overlay` focus traps, and focus return.
Physical-device and real screen-reader checks for composite controls remain open.
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
Native and browser field checks cover leading and trailing bidi caret affinity for paint, pointer input, and visual arrow movement.
Other contextual scripts and mark placement across font changes remain open.
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
Rotation transitions use the shared pump clock and keep layout and text wrapping unchanged.
Native and browser checks cover nested rotations, clips, glyphs, source limits, and displayed input bounds.
Native and evaluator replays compare exact rotation timelines, claims, and control bounds.
Real-device rotation checks remain open.
Linux Desktop reads the Settings portal motion preference and receives live changes.
A private bus check covers initial values, invalid signals, foreign senders, stale replies, and service restart.
Recordings include the initial session preference before live input.
Shared runtime checks compare 400 Desktop and Mobile event recordings with Headless replay across five transition types.
They check retained input, field selection, and the forced native setting.
Additional checks cover pause, reduction, View replacement, and resume.
The shared runtime checks and private portal check pass under ASan and UBSan.
Native and evaluator replays compare session motion preferences and recordings across six transition types.
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

### Split laptop and mobile review

`scuzz ide serve` serves the workspace card feed, idempotent decisions, and steer queue over local Wi-Fi HTTP with a bearer pairing token. `scuzz ide crank` and `scuzz ide serve --generate` run unattended laptop proposal rounds against `.scuzz/ide/`. `--message-format=json` emits single-line JSON output on `scuzz check`, `scuzz fuzz`, `scuzz diff`, `scuzz package`, `scuzz models`, `scuzz ide serve`, and `scuzz ide crank`. Each crank round recovers interrupted `active` steer items and retires undecided cards from older baselines. Each round folds the `.scuzz/ide/session.json` objective, queued `.scuzz/ide/steer.json` prompts, and bounded `.scuzz/ide/records/*.json` choices into local `Qwen3.5` GGUF or inbox candidate rounds. Every candidate must pass `scuzz check`, `scuzz diff`, `scuzz fuzz`, and mutation checks before `scuzz ide crank` writes a ready card. `examples/ide-mobile` runs the shared review deck and persistent `.scuzz/ide-mobile/state.json` FIFO outbox on Headless, Desktop, iOS simulator, and Android APK. Hermetic `TestRuntime` claims, ASan corpus replay, live loopback sync checks, and CLI checks cover unauthorized request rejection, duplicate action IDs, stale-base `accept` refusal, ordered batch replay, offline outbox persistence across app restarts, steer and preference folding, unverified candidate rejection, dirty-buffer refusal, and crash recovery across `fs` and `net` faults. Physical phone Wi-Fi and device checks remain open.

### Local choice feedback

`examples/editor` records optional abstention reasons (`no-visible-difference`, `need-another-scenario`, and `outside-my-goal`) alongside `Can't decide` without turning abstention into a negative preference. Desktop IDE session requests, local model generation, and `scuzz ide export` fold bounded human choices, abstention reasons, Undo state, and automatic exclusions from `.scuzz/ide/records/`. Post-choice output shows an isolated lane reveal, a short behavior result, and region focus controls that retain or narrow `allowed` source paths. The local session summary reports accepted choices, conservative SHA-256 retained acceptances, baseline choices, abstentions by reason, automatic exclusions, duplicates, Undo, readiness wait, active review time, and retained acceptances per active review minute from primary records. Hermetic `TestRuntime` claims and corpus replay cover abstention reasons, bounded feedback, Undo, later replacement, isolated reveals, region focus, pause, and restart. Human usefulness and measured developer keep rates remain open.

### Rule review

`examples/editor` offers an explicit `Suggest rule` action for a recorded `accept` or `baseline` choice. External mode exports `.scuzz/ide/rule-request.json` and `build/ide/rule-request.json` with `"kind": "rule"` and imports matching rule publications from `build/proposals/`. Local model mode generates a rule draft under the session request limit. A rule draft can add one root `*.scuzz_verify` file or append `Timeline => Verdict` claims to one existing root `*.scuzz_verify` file without altering existing definitions or `*.scuzz_scenario`. A dedicated `Rule review` card shows the target verify file, claim source, supporting witness, replayed workload scope, and finite-evidence notice. Verification checks syntax, preserved claim definitions, typechecking, all required workloads, the supporting witness, and `Verdict.afterHit` and `Verdict.onHit` trigger reach before marking a draft verified. `Install rule` revalidates dirty buffers and frozen package and verify inputs, writes through `.scuzz/ide/journal.json`, supports restart recovery and `Undo`, and invalidates queued behavior cards. Hermetic `TestRuntime` claims, ASan corpus replay, and Headless UI checks cover preference selection, rule rejection, existing-claim preservation, untriggered-claim rejection, stale-input and dirty-buffer refusal, crash recovery, `Undo`, and later proposal enforcement. Human usefulness and measured rule adoption remain open.

### Verification scope and application correctness

- **Parser diagnostics.** A reported parser error cannot pass the shared checker. The checker and language server report the offending token span across declarations and expressions. After a syntax error, the parser synchronizes at the next top-level declaration at delimiter depth zero. The checker and language server report each independent top-level syntax error in one pass. Checking stops before type checking when a syntax error is present. Formatting preserves the affected file. Language server formatting returns no edits. Tests cover declarations, parameters, defaults, constraints, imports, annotations, match guards, unclosed delimiters, trailing tokens, and multi-error recovery. Inside a declaration, the parser synchronizes across `for` bindings, `match` arms, block statements, and `trait` and `impl` member headers. A syntax error names one expected token when the parser has one. The message stays `unexpected token` when the parser has no single expected token.
- **Input boundaries.** Strict integers and required JSON fields return explicit errors. The kernel checks field types and domain ranges in live code. Nested object-field paths name a missing field or a wrong JSON case. `Json.decode` reads one JSON object into one record and returns `Result[String, Record]`. Each field is read by its name. A missing required field or a wrong JSON case names the path. A present null is a value. A `Json` field keeps null. A scalar field or a nested record rejects a present null as the wrong case. Nested records use the same decoder. A `List[String]`, `List[Int]`, `List[Bool]`, `List[Float]`, or list of records reads a JSON array. A nested record in that list uses the same decoder. An error names the path and the array index. A present null in a scalar list element is the wrong case. A field of type `Option[String]`, `Option[Int]`, `Option[Bool]`, `Option[Float]`, or `Option` of a record accepts a missing field as None. It accepts a JSON null as None. A present value decodes as Some. That value uses the same case rules as a required field. A wrong JSON case names the path. A required field stays required. A field whose type is an enum decodes from a JSON string. The string matches a case name. Each case has no fields. A missing field is an error and names the path. A null, a non-string, or an unknown case name is an error. That error names the path and the bad value. An unknown case name reports unknown case. A null or a non-string reports wrong case. The application checks domain ranges. Maps, sets, nested lists, generic records, recursive record types, an enum case with fields, a generic enum, a list of enums, `Option` of an enum, `Option` of a list, `Option` of `Json`, and nested `Option` stay open. `Json.field` still omits the field name. Verification assumptions do not validate live input. Result checks reject uses confined to erased `.require` and `Property` labels and predicates. Live `for` checks also reject non-Result bindings used only in erased checks. External `Fs.read`, `Sys`, and `Net` `for` bindings reject erased guards without live `if`, `match`, or `Result` validation. Private record constructors restrict construction and copy to the declaring module. The editor validates relative paths in live Result constructors and retains canonical path checks. Private access does not prove the predicate. Indirect flows across separate functions or mutable state can still bypass local guard checks.
- **Failure handling.** Result bindings and Result fields in tuple, list-cons, and constructor binding patterns reject discard and partial patterns. Bound Result lists, options, tuples, records, enums, maps, references, sets, streams, queues, signals, deferred values, and fibers reject unused Result values across generic instantiations, recursive types, nested containers, type aliases, and IO payloads. Tuple and record field projections do not satisfy live use for uninspected Result paths. Match arms reject wildcard and unused Result bindings across Result containers. A whole-container alias does not replace explicit use of a selected Result field. A map, reference, set, stream, queue, signal, deferred, or fiber operation that does not return the Result does not satisfy live use. Passing a result to a function does not prove that the receiver handles its error. An inline call can still hide a Result. A Result whose type arguments are only the enclosing type does not make that deferred value or fiber a carrier. Keep expected errors explicit through input and effect boundaries.
- **Behavior rules.** Make rules about state, effects, cancellation, duplicate requests, and recovery easy to express and diagnose. Check relationships between executions without a second app implementation. The recovery fixture compares file presence and contents for two sources, the journal, the operation record, and the Undo state. A conflict-write control passes the endpoint rules but fails this comparison. Repeated recovery preserves the same five files. A completed request rejects a duplicate. Duplicate rejection preserves these files and an unrelated file. External-write scenarios check interrupted acceptance, interrupted Undo, and Undo requests under the stated ownership limits. Recovery and Undo detect unexpected external bytes on owned files before replacement or deletion. They preserve external edits and unrelated workspace files. They surface explicit conflict state. Schedule-relation checks (`scuzz fuzz --relate`) compare ownership-gated schedules without endpoint file assertions. Sampled fuzzing, bounded exhaustive simulation, and schedule-relation checks all reject overwrite-blind defect controls. Separate defects exercise the guard and file preservation rules. These checks use exact editor function bodies. They do not cover all editor operations or concurrent external-write race windows.
- **Mutation attribution.** Surviving mutants emit structured records in `summary.json` and `scuzz fuzz --message-format=json`. Each survivor record reports its site index, mutation kind, file, 1-based line and column span, diverged workloads, and passing claims. Human output reports the exact `file:line:column` span, enclosing symbol, mutation kind, diverged workloads, and passing claims. Reports list `incomplete_sites` and `excluded_sites` explicitly and preserve survivor attribution across cached `.scuzz/mutate.results` reuse and both execution engines. Equivalent mutants that change observable intermediates without violating user intent still need human or claim review.
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
| External ownership comparison | Inputs 0, 1, 2, 3, 6, 7; fault calls 1 and 5; all three fault modes | Same comparison limits | All 996 exhaustive paths pass in 34.39 s; 2048 sampled runs take 12.95 s and reach 110 distinct paths |

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

External-write checks cover interrupted acceptance, interrupted Undo, and Undo requests under the stated ownership limits.
Owned-file modifications and creations stop recovery or Undo before replacement or deletion.
Conflict paths retain the pending journal or accepted Undo state.
They preserve sibling and unrelated workspace files.
Unowned workspace writes allow recovery and Undo to settle owned files.
Recovery and Undo keep the external workspace file.
A step invariant checks that external edits on owned and unowned files remain intact across every timeline step.
An oracle-free driver pairs editor operations and external writes across schedules under an ownership gate.
`scuzz fuzz --relate` passes all 82 corpus entries in 5.19 s.
Single runs of the oracle-free driver pass on four overwrite-blind controls.
The schedule-relation claim, bounded simulation, and sampled fuzzing all reject each overwrite-blind control.

Ordinary recovery fuzzing uses 64 iterations, seed 42, and a 60 s checking limit.
Its corpus covers acceptance, Undo, conflicts, external writes, schedule relations,
cancellation, repeated recovery, duplicate requests, and malformed records and journals.
One sample passes 83 required probes and 40 search probes in 38.01 s.
It detects 22 sampled mutations. Two are inert. None survives or stays incomplete.
These mutations affect the fixture and selected application functions.
The 75 fault-free rule checks and exact engine comparisons take 1.83 s.
All methods reject missing-write and overwrite-blind controls.
All 17 direct defect checks produce named author rule failures in both engines.
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
Five live external-write cases check interrupted acceptance, interrupted Undo deletion, Undo requests, and unowned workspace edits.
These checks retain source and native identities, logs, and file contents.

**Open scope.** Power loss, concurrent external-write race windows, temporary file cleanup,
other fault calls, other application operations, and unbounded schedules remain open.
Cancellation paths can reach 144 contention choices.
The 16-point sampling policy does not limit stored replay choices.
Finite schedules do not prove eventual completion or inductive safety.
An executed rule does not establish specification completeness.
One host sample does not establish a general cost bound or method advantage.

**Next gate.** Measure human writing time, rule comprehension, and usefulness in one author session. Preserve wider incomplete results. Keep power loss, concurrent external-write race windows, and other application operations open. Do not infer a general method advantage from one application scope.

**Limits.** Refinement types, static effect protocols, inductive proofs, and compilation validation remain proposals. Existing finite campaigns do not establish these guarantees. A proof establishes a stated property under its assumptions. It does not establish that the specification matches all product needs.

### Supporting compiler and editor work

- **Compile time.** `scuzz check examples/compiler` takes 4.63 s in one current sample. The CLI SHA-256 is `4bf1aeddc4148d4db8c674bffc2503bb70cd5405227483fcf1d896ba77d55c73`. The editor campaign, CLI corpus replay, and PR checks run at the same time. `scuzz build --full examples/tyck` is recorded at about 16 s. Refresh these and the editor measurements after compile-time changes. Reduce demonstrated checker or LLVM emission costs only when they block this arc.
- **IDE subprocesses.** Editor capabilities run in-process through `Lsp`. Run, Fuzz, and Diff use the CLI. Removing these calls is not a gate for the stream.
- **Standard kits.** OS threads remain missing. Add kit work only when it blocks an ordinary program or a required review proof.

### Local model evidence

The pinned Qwen3.5 9B model runs on macOS ARM64 Metal. It produces a message input and a Send action that retains messages in a visible list. Each candidate passes all three required workloads and 32 search cases with no regression. A native interaction proof enters a message, selects Send, and checks the retained message and cleared input. These finite cases do not prove sustained useful generation. Qwen3.5 4B execution and Linux x86-64 and ARM64 Vulkan execution remain unverified. GPU detection and CPU fallback need further host proof. Peak memory remains unknown. Larger source scopes can exceed the fixed context. Controlled replies do not prove usefulness.

**Proof.** Run the pinned backend on each supported host. Measure peak memory and useful output for the fixed catalog. Use real weights and the shared request, parser, and behavioral review. Report unavailable hosts separately. Do not expand the catalog or claim quality from model size.

**Measurement.** One successful macOS Metal 9B correction takes 41.2 s from request start to publication. It uses 2,025 prompt tokens and 463 output tokens. This sample does not establish a latency bound. Peak memory for the current catalog remains unverified. The pinned revisions and artifact digests are in the [generation catalog](../examples/editor/generation/src/Models.scuzz). The process budgets are 4 GiB and 8 GiB. Linux enforces their virtual memory limits. The default finite proposal changes `greeting.txt` from `Hello` to `Hello Scuzz` under simulation. Its target has no registered claims. Its review completes all required workloads and 32 search cases. Finite Headless review retains a zero-event witness and exits normally. Frozen card preparation takes 1.736 s in one sample. This interval excludes generation, editor startup, and input collection before the card freezes. The target has one required idle workload and no seeds or corpus files. It changes no source before a choice. A console-only proposal has no recorded difference. These cases do not measure human preference or application quality.

## Cuts and later work

Do not add user FFI, `extern`, plugins, library publishing, git or registry dependencies, or `scuzz add`. Do not start a provider framework, remote generator service, telemetry, or model training. Limits and exclusions for the current arc live in `vision.md`.

Generated setup inputs. Multiple named scenarios and campaign selection. Session event journal and live time ops. Stable scroll keys. Windows desktop. OS IME candidate windows. macOS release packaging in default CI. Developer ID signing and notarization. Full web accessibility. Real phone and screen-reader checks. Hot reload on web. Multiple UI factories in host hot reload. General model browsing. Metal and Vulkan performance measurements. Gated-model login. Model conversion. Automatic oracle mining. Semantic timeline alignment. Divergence attribution to source defs. Pixel previews. Emit scalar fallbacks. Project IDE: native file dialogs, menus, multi-window, multi-cursor, minimap, Git UI, debugger, plugin host, custom canvas kit.
