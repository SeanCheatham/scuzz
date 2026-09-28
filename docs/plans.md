# Current slice: keep the next card ready

Implement gate 4 of [`vision.md`](vision.md#4-keep-the-next-card-ready). Follow the proposal review locks in [`philosophy.md`](philosophy.md#proposal-review).

## Outcome

The session keeps the displayed card and at most one ready successor. It prepares the successor while the developer reviews the displayed card. Each card keeps its frozen request and evidence. Navigation uses cached data.

## Scope

Use the existing editor, importer, managed generator, and shared compiler review. Keep one generation request and one review job at most. Keep all claims, corpus entries, probe limits, and the 32-case search. Mutation stays an explicit diagnostic action. Add no provider, scheduler, renderer, or evaluator protocol.

## Work order

1. Store each card's frozen request. Validate current objective, model, scope, compiler, full input graph, and limits without invalidating a displayed card when the next request gets a new identity.
2. Add the bounded queue and separate owned generation and review reservations. Bound inbox inspection and import to queue capacity. Match completion to its reservation and baseline. Leave independent inbox candidates pending while paused.
3. Reuse baseline checking and replay where the shared compiler supports it. Cache evidence by the full review identity. Reject duplicate source sets and observed outcomes within one baseline and objective. Keep automatic exclusions separate from choices.
4. Refill after baseline choice or abstention. After acceptance, Undo, objective change, or model change, cancel stale work and refill from the current baseline. Expose the invalidation operation for rule installation. Preserve the displayed identity through repeated and late actions.
5. Show preparation, ready capacity, explicit retry, and exhausted budget. Keep cancellation and cached navigation responsive.
6. Prove late responses, registration interruption, repeated Generate, capacity, baseline changes, pause, exit, and independent publication. Measure cold readiness, warm readiness, and cached navigation separately.
7. Update the existing manual and gaps. Commit the slice and the next plan after the required proofs pass.

## Acceptance criteria

- [ ] The displayed card and one ready successor are the only prepared cards.
- [ ] Each card owns immutable request metadata, files, lane order, and evidence.
- [ ] One generation request and one review job run at most.
- [ ] Inbox inspection and import stop at available capacity.
- [ ] A late completion cannot replace the displayed card or enter a new baseline.
- [ ] Duplicate source sets and observed outcomes are excluded within the objective and baseline.
- [ ] Baseline choice and abstention can use the existing successor.
- [ ] Acceptance and Undo cancel stale work and refill from the new baseline.
- [ ] Objective and model changes invalidate pending work.
- [ ] Pause and exit stop owned preparation and inference. Independent producers remain available.
- [ ] Repeated Generate stays inside queue and durable request limits.
- [ ] A failure requires an explicit retry. Budget exhaustion has a visible status.
- [ ] Cached card, workload, and step navigation starts no probe, build, or subprocess.
- [ ] Current measurements report the 250 ms navigation target honestly.
- [ ] The manual matches the queue controls and lifecycle.

## Required validation

Rebuild the checkout product CLI after compiler or CLI changes. Run proofs in scratch directories. Export the documented `LIBRARY_PATH` for Skia links.

```bash
./scripts/bootstrap.sh
./examples/cli/build/cli check examples/cli
./examples/cli/build/cli check examples/editor
./examples/cli/build/cli fuzz --iterations 0 examples/cli
./examples/cli/build/cli fuzz --iterations 0 examples/editor
./examples/cli/build/cli fuzz --iterations 32 examples/editor
./scripts/ci.sh ui
./scripts/ci.sh delta
./scripts/ci.sh pr
git diff --check
```

Keep the queue proof hermetic. Add finite Headless cases to the existing CI path. Include source changes, dirty buffers, stale publications, job cancellation, restart, and retained pending external directories. Preserve the controlled generator and exact compiled witness parity proofs. Ordinary CI uses no weights or external network.

## Completion and continuation

Remove the completed gate from `vision.md` and close its implemented gaps. Keep human usefulness and unavailable platform evidence explicit. Replace this plan with gate 5. Commit the slice and next plan. Continue through the complete-session gate. No refinement pass starts before the full session works.
