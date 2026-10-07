# Scuzz Lang vision

Open work and implementation order. Design locks: [`philosophy.md`](philosophy.md). Ranked gaps: [`gaps.md`](gaps.md). Current slice: [`plans.md`](plans.md). Platforms and toolchain: [`compatibility.md`](compatibility.md). Later tune work: [`optimization.md`](optimization.md).

Edit this file when the order changes. Remove completed work. Keep only the next slice in `plans.md`.

## First: evaluator reliability and parity

Reduce the remaining compiler evaluator costs before local choice feedback. Measure server startup and probe execution separately. Preserve exact timeline and claim comparisons for selected compiler and editor workloads. Keep probe deadlines, claims, corpus entries, and search scope. A compiled fallback does not prove evaluator parity.

## Primary arc: development through decisions

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

Use `plans.md` for the first remaining gate. Complete its software proofs before advancing. Keep unavailable external and human checks explicit. They do not prevent independent later implementation work. Remove the completed slice from this file and from `gaps.md`. Delete its `plans.md`. If arc work remains, create the next slice from this order and continue. Do not wait for a new instruction between authorized slices.

Change compiler or runtime code only for a demonstrated blocker or a required shared review operation. Keep one clear implementation. Do not rewrite the compiler or editor for architecture alone. Preserve the newest-release bootstrap constraints. Do not reduce probe limits, skip claims, remove corpus entries, or bypass replay failures to finish.

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
