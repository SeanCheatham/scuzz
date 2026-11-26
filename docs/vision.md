# Scuzz Lang vision

Open work and implementation order. Design locks: [`philosophy.md`](philosophy.md). Ranked gaps: [`gaps.md`](gaps.md). Platforms and toolchain: [`compatibility.md`](compatibility.md). Next slice and later tune work: [`optimization.md`](optimization.md).

Edit this file when the order changes. Remove completed work. Keep only the next slice in `plans.md`.

## Next: type-checker evaluation cost

This slice is next. Do it before further language work. Do it before the other open work in this file.

One idle type-checker probe on the evaluator takes about 1785 seconds. The slowest sample takes 1866 seconds. One code-generation idle probe takes 969 seconds. CLI idle probes take 46 seconds and 45 seconds. `scuzz fuzz --iterations 0` replays the corpus and finishes that one probe. The campaign compares the evaluator with the compiled program. The command does not check more programs. Accept a long run only when it checks more programs. This cost is slow evaluation. Start this work after the probe deadline change lands. Keep that deadline until a measured faster probe replaces it. Detail: [`optimization.md`](optimization.md#type-checker-evaluation-cost).

## UI framework capability

Improve shared UI capabilities for web, desktop, and mobile applications.
Headless remains a required peer for layout, input, and verification.

Select work from these application needs:

1. Expose shared semantics to native screen readers.
   Check Android raw touch exploration and real screen-reader behavior.
   Add iOS and Desktop screen-reader bridges.
   Add editable option search when an application needs it.
2. Add wider font coverage and other contextual scripts across font changes.

Use working applications to check each capability.
Check native and evaluator behavior with the same inputs.
Keep browser, mobile, and assistive technology proof limits explicit.
The complete framework goal needs more than one layout constructor.

## Language and verification guarantees

Start further language work after the type-checker evaluation slice.
Prioritize application correctness before local choice feedback.
Select each slice from a demonstrated gap in the seven topics below.
Use `Result` for external input errors. Keep domain validation active in live code.
Verification assumptions do not validate live input.

Keep one compiler, one verification command, and the existing simulation runtime.
Preserve exact evaluator and native replay, probe deadlines, claims, corpus entries, and search scope.
General compiler speed work needs a demonstrated application or proof blocker. The idle type-checker probe is that blocker, and it is the next slice.
Reference models remain optional. They are not the default verification path.
Generation usefulness and review efficiency need evidence from a real application session.

### Improvement work order

Use these seven topics to select and verify application correctness work.
Complete one slice before starting the next slice.

1. Prevent invalid states. Use enums and validated domain types. Extend Result checks for demonstrated container errors.
2. Keep input validation active. Diagnose clear uses of erased checks as input guards. Extend typed decoding for demonstrated input needs. Define arithmetic and collection boundary behavior.
3. Establish claim reach. Separate claim execution, trigger reach, and verdicts. Require relevant workloads for conditional and absence rules.
4. Examine rule strength with mutation. Survivor records report the source span, mutation kind, diverged workloads, and passing claims. Reports separate invalid, excluded, incomplete, and inert mutations, and list incomplete and excluded site indices. Infrastructure failures do not count as detected defects. Equivalent-mutant detection beyond timeline comparison remains open.
5. Exercise failure and recovery. Cover cancellation, timeout, duplicate requests, interrupted writes, and recovery. Check safety, bounded progress, and relations between executions.
6. Improve tool evidence. Separate passed, failed, incomplete, and unguarded results. Failure records report the source span, rule kind, rule name, failing workload, replay path, and shrunken witness events. Retain small replay workloads and full input identities. Use the same project checks in the IDE and CLI.
7. Check compiler and runtime behavior independently. Generated compiler checks compare evaluator results with an independent model for matches, closures, records, traits, nested interpolation, copy, or-patterns, and both module orders. Extend native replay. Use independent expected results. Run undefined behavior checks on the C runtime. Keep live platform proof limits explicit.

For each topic, inspect existing behavior before changing it. Preserve working guarantees. Record the exact scope of each proof. Finite campaigns do not prove all programs correct.

Use the [recovery comparison](gaps.md#formal-verification) to measure author effort.
Recovery and Undo checks cover external writes under the stated ownership limits.
Select further relation checks from demonstrated application gaps.
Keep concurrent race windows and wider incomplete scopes explicit.
Keep formal methods optional until they pass the product method gate.

## Formal verification methods

Extend the seven correctness topics above with formal verification methods.
These ideas are proposals. They are not shipped guarantees.
Use the optional experiment to select small changes for demonstrated correctness gaps.
Keep one compiler, one verification command, and the existing simulation runtime.
Reference models remain optional. Current design locks remain in `philosophy.md`.

### Direction: one claim, several methods

Let an author write a property once.
Use the property for fuzzing, bounded symbolic checking, and later proof by induction.
Return evidence through the same report.
Turn concrete counterexamples into ordinary replay workloads.
Define the supported expression and effect subset for each method.
An arbitrary `Timeline => Verdict` function can need proof annotations or remain unsupported.
Do not silently replace an author claim with a weaker solver claim.

### Seven proposals

1. **Types that carry proof.** Start with validated domain types.
   A live constructor checks input and returns Result.
   Prevent other code from constructing an invalid value.
   Select one demonstrated application need before adding solver checks.
   General refinement types with predicates remain later work.
   Examples include positive amounts, valid list indices, and sorted collections.
   Prove that each constructor and function preserves the predicate.
   Check external input in live code before it enters the type.
   Use actual fixed-width integer behavior in arithmetic proofs.
   Mathematical integers require a separate proof that overflow cannot occur.
   Study [F* refinement types](https://fstar-lang.org/tutorial/book/part1/part1_getting_off_the_ground.html).

2. **Function contracts and effect protocols.** State the input conditions and output guarantees of a function.
   Check each call and the function body.
   Let resource operations consume one protocol state and produce the next.
   Use states such as open, committed, and closed to prevent invalid operations.
   Check release obligations on error, cancellation, and timeout paths.
   An erased `.require` check does not establish live input validation.
   Study [Dafny contracts](https://dafny.org/dafny/OnlineTutorial/guide).

3. **Exhaustive bounded simulation.** Explore all permitted inputs, schedules, and faults within stated bounds.
   Derive transitions from executable Scuzz code.
   Start with two requests, one cancellation, and one interrupted write.
   Report input, schedule, fault, and step bounds separately.
   State exploration is exhaustive only for the supported finite scope.
   An abstraction needs evidence that it preserves the checked property.
   Study [CBMC bounded checking](https://diffblue.github.io/cbmc/man/cbmc.html).

4. **Useful specifications.** Check that assumptions admit inputs.
   Check that required claim triggers are reachable.
   Distinguish an impossible trigger from a failed reach search.
   Mutate assumptions and claims to expose specifications that admit bad behavior.
   Keep specification mutation separate from application mutation.
   Do not weaken the installed claims or assumptions automatically.

5. **Inductive safety and explicit progress.** Prove that an invariant holds initially.
   Prove that each permitted transition preserves it.
   Start with balance conservation or the absence of effects after cancellation.
   Treat eventual completion as a separate obligation.
   State scheduler fairness and external response assumptions beside progress results.
   Bounded completion checks do not prove eventual completion for all executions.
   Study [TLA+ safety and liveness](https://lamport.azurewebsites.net/tla/high-level-view.html).

6. **Evidence for each guarantee.** Distinguish sampled tests, exhaustive bounded checks, and proofs.
   Record assumptions, input identities, supported operations, platform scope, and open obligations.
   Solver timeouts and unknown results remain incomplete.
   Extend relations between executions toward proofs of idempotence and information flow.
   Define the observed output and the relation between inputs and schedules.
   Equal public output does not establish protection against timing leaks.

7. **Compilation validation.** Check that native output preserves the proved source behavior.
   Start with arithmetic, enums, records, and matches.
   Validate individual compilation results before attempting a whole compiler proof.
   State which compiler, runtime, foreign code, and operating system behavior remains trusted.
   Study [Alive2 translation validation](https://github.com/AliveToolkit/alive2).

### Product method gate

`examples/proof` supplies optional bit-vector and finite protocol checks.
Run instructions live in
[`developer-environment.md`](developer-environment.md#formal-verification-experiment).
Proof scopes, measured results, and open limits live in [`gaps.md`](gaps.md#formal-verification).
The default app path remains `scuzz fuzz`.
The wider recovery scopes still exceed their execution limits.
Resolve these limits before adding a public exhaustive application method.
Select a new product method only for a demonstrated application need.
Keep its supported subset explicit before adding syntax or dependencies.
Reuse the author rule. Preserve claims, hermetic simulation, and exact replay.
Reject deliberate defects with ordinary counterexample workloads.
Check assumptions and required trigger reach separately.
Keep unsupported operations, unknown results, and unmet limits incomplete.
Record exact input identities, platform scope, trusted components, and cost.
Require a measured author workflow and budget.
Compare methods with equal inputs, schedules, faults, rules, and budgets.
Measure author work, counterexample latency, completed scope, and edit cost.
Keep method, verdict, scope, and open obligations separate for each rule.
A completed finite scope can add value when both methods detect the same defect.
Use small optional reference models when they make an application rule clearer.
Keep reductions out of the search until they preserve all claim observations.
A finite check cannot produce an unbounded proof label.
Do not add a general proof language, a second compiler, or a mandatory app model.

## Active arc: split laptop daemon and native mobile review

The laptop runs unattended local GGUF generation (`scuzz models`) against the workspace through `scuzz ide crank` and `scuzz ide serve --generate`.
Every candidate passes `scuzz check`, `scuzz fuzz`, `scuzz diff`, and mutation checks before it enters the ready card buffer.
`scuzz ide serve` exposes the card feed, idempotent decisions, and steer prompts over local Wi-Fi HTTP with a bearer pairing token.
`examples/ide-mobile` runs the native mobile review client on Headless, Desktop, iOS, and Android as peers.
The client prefetches verified cards on local Wi-Fi, records swipe accept, swipe reject, skip, and short steer prompts in `.scuzz/ide-mobile/state.json`, and flushes its FIFO outbox on the next Wi-Fi connection.
`examples/editor` folds bounded choices, abstention reasons, and Undo state into exported and local requests, shows isolated post-choice reveals with region focus controls, and reports an auditable local session summary.
Locks: [`philosophy.md`](philosophy.md#proposal-review).

Implement the remaining slices in order.
A transport proof does not prove useful generation.
A deterministic session does not prove human preference or speed.
Keep those unknowns explicit in `gaps.md`.

## Implementation gates

### 1. Prove and refine the complete session

Software session proofs exercise one UI objective (`examples/counter`) and one IO objective (`examples/webhook`) in scratch workspaces under hermetic simulation and live finite Headless runs over local processes, private loopback HTTP, and filesystem paths. They cover inbox and controlled local generation, candidate checks, short witnesses, compiled witness replay, both lane buttons, abstention reasons, region focus, next-baseline request feedback, behavior and rule Undo, rule suggestion, rule verification, rule rejection and installation, interrupted journal recovery, external-edit conflict preservation, paused restart, and the conservative session retention summary. `scuzz docs` matches the shipped IDE, language, GUI, verification, and CLI behavior.

**Gate:** All software functional gates, CLI rebuilds, and required CI slices pass. Uncached real-model generation and human usefulness remain unverified. Run a real developer review session on one small app and one objective with a real generator to evaluate keep rate, retained acceptances per active review minute, abstention reasons, Undo rate, and readiness latency. Do not claim that the complete feature passes human validation until that session runs.

## Execution boundary

The next slice is type-checker evaluation. Do that slice before further language work and before the other open work in this file. Use `plans.md` for the active slice. Complete its software proofs before advancing. Keep unavailable external and human checks explicit. They do not prevent independent implementation work. Remove the completed slice from this file and from `gaps.md`. Delete its `plans.md`. If arc work remains, create the next slice from this order and continue. Do not wait for a new instruction between authorized slices.

Change compiler or runtime code for a language guarantee, a demonstrated application blocker, or a required shared review operation. Keep one clear implementation. Do not rewrite the compiler or editor for architecture alone. Preserve the newest-release bootstrap constraints. Do not reduce probe limits, skip claims, remove corpus entries, or bypass replay failures to finish.

Unavailable local model resources or human availability do not prevent deterministic, loopback HTTP, and directory import proofs. They remain explicit completion limits. On a hard blocker, preserve a working subset and leave `plans.md` with remaining criteria and a reproducer. Do not start unrelated work or mark an incomplete gate complete. Run the type-checker evaluation slice before the complete-session endpoint. Stop implementation at the complete-session endpoint. Human usefulness remains an unknown until a developer session measures it.

## Supporting work and exclusions

Compile time and evaluator parity support this arc. The next slice is the type-checker evaluation cost above. Measure changes that affect them. Standard kits follow when an ordinary program or this review loop needs them. Ranked work: [`gaps.md`](gaps.md).

Do not start FFI, a registry, plugins, provider SDKs, a remote generator service, Tailscale or external relay tunnels, model training, physical-device packaging, or `*.scuzz_tune`. Keep one pinned llama.cpp release and one model format. Use Metal on Apple Silicon and Vulkan on Linux when a supported GPU is available. Fall back to CPU inference when the platform backend or GPU is unavailable. General Hub browsing, gated-model login, model conversion, and training stay later. Do not add pixel preview infrastructure, semantic timeline alignment, social accounts, public feeds, ranking algorithms, or third-party gesture frameworks. Keep keyboard and shared Headless input as peers. Native file dialogs, menus, multi-window, debugger, and general editor expansion stay later.

## Risks

| Risk | Required control |
| --- | --- |
| A blind control selects an unseen state | Select a lane by fixed card identity |
| Acceptance writes bytes other than those reviewed | Freeze inputs and revalidate before writing |
| An offline mobile acceptance targets an outdated baseline | Require the expected baseline hash on `accept` and return `stale` without changing files |
| A retried mobile outbox flushes an action twice | Key every sync action by client ID and return stored receipts idempotently |
| An unauthenticated peer on shared Wi-Fi calls the sync server | Require a bearer pairing token and reject unauthorized requests before workspace access |
| A multi-file write stops halfway | Journal, restart recovery, and conditional Undo |
| A proposal passes because both sides fail | Check absolute candidate verdicts |
| A narrow workload misses a consequence | Corpus, bounded search, and visible evidence scope |
| A claim never triggers | Show trigger reach; do not call an untriggered rule verified |
| A generator rewrites accepted work | Bind requests to a baseline and discard stale results |
| An interrupted generation round drops or repeats a steer prompt | Persist active steer transitions, recover completed or interrupted items on restart, and bound retries per prompt |
| The stream consumes unlimited work | Explicit limits, bounded queues, and cancellation |
| Model setup expands into an inference platform | One pinned llama.cpp release, Metal and Vulkan device discovery, CPU fallback, two catalog entries, one format, and explicit setup limits |
| Model weights duplicate or damage another tool's cache | Use a Scuzz-owned cache. Verify each temporary download before rename. Do not prune |
| A local model cannot generate useful Scuzz changes | Measure a small model through the same behavioral gates |
| A model consumes the target probe budget | Separate inference limits from correctness probe limits |
| An external candidate is read before publication ends | Complete metadata, content hashes, and one importer |
| Structural tiles imply pixel fidelity | Label the view as recorded structure |
| A quick choice does not reflect a useful preference | Measure retention and obtain human evidence |
| Evaluator and emitted behavior differ | Shared witness replay; preserve parity failures |
| Review cost makes the loop unusable | Measure readiness separately from cached navigation |
