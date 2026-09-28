# Current slice: learn from local choices

Status: implementation is open. Implement gate 5 of [`vision.md`](vision.md#5-learn-from-local-choices). Read `HUMANS.md`. Follow the proposal review locks in [`philosophy.md`](philosophy.md#proposal-review). Keep human usefulness and unavailable platform evidence explicit in [`gaps.md`](gaps.md).

## Outcome

The next request uses bounded local feedback. A choice shows its reveal and result without exposing the next card. A local summary reports choices, exclusions, Undo, readiness, waiting time, and retained acceptances per active review minute.

## Scope

Use the existing editor, generator request, and primary local records under `.scuzz/ide/`. Keep one objective, the two-card queue, separate owned jobs, and all current work limits. Feedback uses at most 16 recent records. Add no telemetry, model training, provider, or second log. Rule suggestion and installation belong to gate 6.

## Work order

1. Add optional abstention reasons: No visible difference, Need another scenario, and Outside my goal. An abstention is not a rejection. An automatic exclusion is not a human choice.
2. Use recent preferences, reasons, and Undo results in local and exported requests. Bind each request to the current baseline and scope. Keep candidate rationale hidden before the choice.
3. Show a short reveal and result for the decided card. Preserve that card's identity. Do not reveal the next card's mapping. Let the developer keep or change region focus.
4. Record active review intervals, paused intervals, and waiting time. Active review starts when a ready card appears and ends at its choice or abstention. Exclude paused time and generation waits.
5. Add the local session summary. Report accepted choices, baseline choices, abstentions by reason, automatic exclusions, duplicates, Undo, readiness, active review time, and waiting time. Count an acceptance as retained only if it is not undone and all touched files still match its accepted bytes at session end. Keep source hashes. Label this as a conservative source measure. It does not prove that later edits preserve the intended behavior.
6. Prove bounded feedback, abstentions without negative preference labels, isolated reveals, region changes, restart, pause, Undo, later replacement, and totals from primary records. Add finite Headless proof through the existing CI path. Preserve all claims, corpus entries, replay checks, and the 32-case review search.
7. Update `scuzz docs ide` and `gaps.md`. Complete the required checks before commit.

## Acceptance criteria

- [ ] Both local and exported requests contain bounded feedback for the current baseline.
- [ ] Abstentions and automatic exclusions do not become negative preferences.
- [ ] The reveal names only the decided card's lane mapping.
- [ ] Region focus remains explicit and changes the next request safely.
- [ ] Primary local records reproduce all summary totals.
- [ ] Undo and later source replacement change retention correctly.
- [ ] Active review time excludes paused time and generation waits.
- [ ] Zero active review time has a defined result without division by zero.
- [ ] Restart preserves durable results and does not count old waiting time as active review.
- [ ] The manual matches the shipped controls and summary limits.

## Required validation

Rebuild the checkout product CLI after compiler, CLI, or manual changes. Run proofs in scratch directories. Export the documented `LIBRARY_PATH` for Skia links.

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

## Completion and continuation

Remove gate 5 from `vision.md` after its software proofs pass. Close only implemented gaps. Keep real model quality, human usefulness, and unavailable host evidence open. Replace this plan with gate 6 and commit the slice. Continue in order through gate 7 when the full arc is authorized. Measured refinement passes used: 0. No measured refinement pass starts before the complete session works. At most two measured refinement passes are allowed. Stop at the complete-session endpoint.
