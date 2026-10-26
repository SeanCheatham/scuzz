# Scuzz Lang vision

Open work and implementation order. Design locks: [`philosophy.md`](philosophy.md). Ranked gaps: [`gaps.md`](gaps.md). Platforms and toolchain: [`compatibility.md`](compatibility.md). Later tune work: [`optimization.md`](optimization.md).

Edit this file when the order changes. Remove completed work. Keep only the next slice in `plans.md`.

## UI framework capability

Improve shared UI capabilities for web, desktop, and mobile applications.
Headless remains a required peer for layout, input, and verification.

Select work from these application needs:

1. Expose shared semantics to native screen readers.
   Check Android raw touch exploration and real screen-reader behavior.
   Add iOS and Desktop screen-reader bridges.
   Complete keyboard behavior for remaining composite controls.
   Add editable option search when an application needs it.
2. Add wider font coverage, other contextual scripts across font changes, and bidi caret affinity.
3. Rotation transitions with replay and cancellation.

Use working applications to check each capability.
Check native and evaluator behavior with the same inputs.
Keep browser, mobile, and assistive technology proof limits explicit.
The complete framework goal needs more than one layout constructor.

## First: language and verification guarantees

Prioritize application correctness before local choice feedback.
Select each slice from a demonstrated gap in the seven topics below.
Use `Result` for external input errors. Keep domain validation active in live code.
Verification assumptions do not validate live input.

Keep one compiler, one verification command, and the existing simulation runtime.
Preserve exact evaluator and native replay, probe deadlines, claims, corpus entries, and search scope.
General compiler speed work needs a demonstrated application or proof blocker.
Reference models remain optional. They are not the default verification path.

Keep current work on language, standard kits, and verification tooling.
The decision loop gates below remain later work.
Generation usefulness and review efficiency need evidence from a real application session.

### Improvement work order

Use these seven topics to select and verify application correctness work.
Complete one slice before starting the next slice.

1. Prevent invalid states. Use enums and validated domain types. Extend Result checks for demonstrated container errors.
2. Keep input validation active. Diagnose clear uses of erased checks as input guards. Extend typed decoding for demonstrated input needs. Define arithmetic and collection boundary behavior.
3. Establish claim reach. Separate claim execution, trigger reach, and verdicts. Require relevant workloads for conditional and absence rules.
4. Examine rule strength with mutation. Connect survivors to source and claims. Separate invalid, excluded, incomplete, and inert mutations. Do not count infrastructure failures as detected defects.
5. Exercise failure and recovery. Cover cancellation, timeout, duplicate requests, interrupted writes, and recovery. Check safety, bounded progress, and relations between executions.
6. Improve tool evidence. Separate passed, failed, incomplete, and unguarded results. Retain small replay workloads and full input identities. Use the same project checks in the IDE and CLI.
7. Check compiler and runtime behavior independently. Extend generated compiler checks and native replay. Use independent expected results. Run undefined behavior checks on the C runtime. Keep live platform proof limits explicit.

For each topic, inspect existing behavior before changing it. Preserve working guarantees. Record the exact scope of each proof. Finite campaigns do not prove all programs correct.

Use the [recovery comparison](gaps.md#formal-verification) to measure author effort.
Extend checks for external writes during recovery under the stated ownership limits.
Select further relation checks from demonstrated application gaps.
Keep wider incomplete scopes explicit.
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

## Later arc: development through decisions

A developer sets one objective and reviews a stream of small changes. Each card compares one proposal with the current working tree. Scuzz finds a recorded execution that shows the difference. The developer chooses a blind lane. Accepted changes become the baseline for the next request. Claims constrain the choices. Locks: [`philosophy.md`](philosophy.md#proposal-review).

The first complete feature needs the remaining gates below. Implement them in order. A generator transport proof does not prove useful generation. A deterministic session does not prove human preference or speed. Keep those unknowns explicit in `gaps.md`.

## Implementation gates

### 5. Learn from local choices

Send bounded recent preferences, abstention reasons, and Undo results with local model requests and exported generation requests. Keep candidate rationale hidden before a blind choice. Show the reveal and a short result after the choice. Let the developer retain region focus or select another region.

Add a local session summary. Report accepted choices, baseline choices, abstentions by reason, automatic exclusions, duplicates, Undo, readiness time, and review time. For the first metric, count an acceptance as retained only when it is not undone and its touched files still match its accepted bytes at session end. Report this as a conservative source measure. It does not establish that later edits preserve or remove the intended behavior. Calculate retained acceptances per active review minute. Active review time starts when a ready card appears and ends at its choice or abstention. Exclude paused time and generation waits. Report waiting time separately. Keep source hashes so this measure is auditable. Do not add telemetry or model training.

**Gate:** Skip does not become a negative preference. Automatic exclusions do not count as human choices. Undo and later replacement change the retention result. The next request reflects the current baseline and bounded feedback. Session records reproduce the displayed totals. A reveal cannot expose the next card's lane mapping.

### 6. Offer a rule from a preference

Add an explicit Suggest rule action for a recorded choice. Use the same local model path or external import with a rule request kind. External mode exports the rule request and waits for a matching published draft. A rule response contains added claim source and the supporting witness identity. It can add a root `*.scuzz_verify` file or add claims to one existing verify file. It cannot alter existing definitions or the scenario.

Show rule source and the witness in a separate rule review. Check and replay all required workloads with the rule. Install only after the developer selects Install rule. Use the same stale-input, dirty-buffer, journal, and Undo checks. Invalidate behavior cards after installation. A failed or unsupported draft stays a draft with a diagnostic.

**Gate:** A behavior choice never writes verification source. Rule installation needs its own action. A rejected rule changes no file. A weakened existing claim is rejected. An untriggered new claim cannot be offered as verified. Installation and Undo preserve existing claims and recover after interruption. The installed rule constrains a later behavior proposal. Finite evidence does not become a universal proof label.

### 7. Prove and refine the complete session

Use existing examples for the proof. Exercise one UI objective and one IO objective in scratch workspaces. Include generation, candidate checks, a short witness, each lane choice, abstention, next-baseline generation, Undo, rule review, restart, and a session summary. Include invalid, stale, and interrupted operations. Keep the simulation proof hermetic. Add live finite Headless proofs for the real local process, private HTTP, and filesystem paths.

Run the required checks in `plans.md` for each slice. Update `scuzz docs ide` when behavior ships. Measure review latency and queue behavior on the same host with the same limits. Make at most two measured refinement passes after the full session works. Each pass addresses an observed failure, delay, or confusing control. Rerun affected proofs. Do not add unrelated language or editor features.

**Gate:** All functional gates pass, the product CLI is rebuilt, and required CI passes. The manual matches the shipped controls and protocol. It gives managed model setup, shared-cache behavior, resource limits, the model list and download commands, the finite generation command, an external publication recipe, and sample request and response. Keep these instructions provider-independent. Record current measurements and remaining external or human validation in `gaps.md`. Human evidence requires a real review session; do not invent keep rates or preferences. If configuration is unavailable, report the real-generator check as unverified. Do not claim the complete feature passes all gates.

## Execution boundary

Use `plans.md` for the next language or tooling slice. Complete its software proofs before advancing. The decision loop gates remain later work. Keep unavailable external and human checks explicit. They do not prevent independent later implementation work. Remove the completed slice from this file and from `gaps.md`. Delete its `plans.md`. If arc work remains, create the next slice from this order and continue. Do not wait for a new instruction between authorized slices.

Change compiler or runtime code for a language guarantee, a demonstrated application blocker, or a required shared review operation. Keep one clear implementation. Do not rewrite the compiler or editor for architecture alone. Preserve the newest-release bootstrap constraints. Do not reduce probe limits, skip claims, remove corpus entries, or bypass replay failures to finish.

Unavailable local model resources or human availability do not prevent deterministic, loopback HTTP, and directory import proofs. They remain explicit completion limits. On a hard blocker, preserve a working subset and leave `plans.md` with remaining criteria and a reproducer. Do not start unrelated work or mark an incomplete gate complete. Stop implementation at the complete-session endpoint. Human usefulness remains an unknown until a developer session measures it.

## Supporting work and exclusions

Compile time and evaluator parity support this arc. Measure changes that affect them. Standard kits follow when an ordinary program or this review loop needs them. Ranked work: [`gaps.md`](gaps.md).

Do not start FFI, a registry, plugins, provider SDKs, a remote generator service, model training, physical-device packaging, or `*.scuzz_tune`. Keep one pinned llama.cpp release and one model format. Use Metal on Apple Silicon and Vulkan on Linux when a supported GPU is available. Fall back to CPU inference when the platform backend or GPU is unavailable. General Hub browsing, gated-model login, model conversion, and training stay later. Do not add pixel preview infrastructure, semantic timeline alignment, social accounts, public feeds, ranking algorithms, or swipe animation frameworks. Keep keyboard and shared Headless input as peers. Native file dialogs, menus, multi-window, debugger, and general editor expansion stay later.

## Risks

| Risk | Required control |
| --- | --- |
| A blind control selects an unseen state | Select a lane by fixed card identity |
| Acceptance writes bytes other than those reviewed | Freeze inputs and revalidate before writing |
| A multi-file write stops halfway | Journal, restart recovery, and conditional Undo |
| A proposal passes because both sides fail | Check absolute candidate verdicts |
| A narrow workload misses a consequence | Corpus, bounded search, and visible evidence scope |
| A claim never triggers | Show trigger reach; do not call an untriggered rule verified |
| A generator rewrites accepted work | Bind requests to a baseline and discard stale results |
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
