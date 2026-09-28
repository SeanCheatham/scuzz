# Current slice: ask one behavioral question

Status: implementation in progress.

Implement gate 2 of [`vision.md`](vision.md#2-ask-one-behavioral-question). Follow the proposal review locks in [`philosophy.md`](philosophy.md#proposal-review).

## Outcome

Each behavior card opens a short recorded difference at its first useful step. Playback starts paused. The developer can inspect both results, the trigger, other workloads, recorded structure, and changed source. Evidence states its finite scope.

## Scope

Use the existing editor, shared `Diff` review, evaluator probes, scenario claims, and CI paths. Add no checker, probe protocol, renderer, or alignment engine. Keep idle and all seeds. Add complete corpus replay and 32 differential search iterations with seed 1. Keep existing per-probe limits. Freeze the review budget and evaluator host identity in each card. Native witness replay stays an explicit parity proof outside navigation.

## Work order

1. Read the corpus and shared driver definitions for evaluator review. Replay the complete required workload set. Run bounded differential search. Reuse the existing shrinker. Preserve every candidate verdict. A shared failure, timeout, crash, drift, or incomplete report blocks readiness.
2. Keep the shortest divergent witnesses with both timelines and scripts. Select the shortest available question. Open its first useful difference, paused. Keep source and behavior under the same fixed lane mapping.
3. Show planned and completed workloads, search work, registered claims, reached triggers, and untriggered claims. Show evaluator and index alignment limits. Label accessibility tiles as recorded structure.
4. Remove proposals with no observed difference after the complete budget. Store an automatic exclusion with the frozen evidence. Do not count it as a choice or claim equivalence.
5. Add scenario claims and corpus entries for presentation, lane order, evidence scope, failures, and source-only exclusion. Add finite real UI and IO review proofs. Replay selected witnesses compiled and compare the evaluator evidence.
6. Update `scuzz docs ide` with the shipped question view and evidence limits. Measure readiness and cached navigation on one host.

## Acceptance criteria

- [ ] Corpus-only differences explain or block the candidate.
- [ ] Search finds a difference absent from idle and seeds. Shrinking preserves it.
- [ ] Every candidate workload passes its absolute gate. Incomplete evidence cannot pass.
- [ ] A shared claim failure blocks the card. Untriggered claims remain visible.
- [ ] The shortest available witness opens at its first difference, paused.
- [ ] Both lane orders keep source, results, and recorded structure aligned.
- [ ] Workload and step navigation uses cached evidence without IO.
- [ ] Source-only proposals leave the behavior queue with a durable No observed difference result.
- [ ] UI and IO witnesses have compiled parity proofs.
- [ ] The manual matches the controls and finite evidence scope.

## Required validation

Use the checkout product CLI. Rebuild it after compiler or CLI changes. Use scratch working directories for finite editor runs. Export the documented `LIBRARY_PATH` for Skia links.

```bash
./scripts/bootstrap.sh
./examples/cli/build/cli check examples/editor
./examples/cli/build/cli fuzz --iterations 0 examples/editor
./examples/cli/build/cli fuzz --iterations 32 examples/editor
./scripts/ci.sh ui
./scripts/ci.sh delta
./scripts/ci.sh pr
git diff --check
```

Run targeted proofs during implementation. Run required checks on stable source. Do not remove claims or corpus entries to pass. Do not reduce probe limits. Report unavailable external validation separately.

## Completion and continuation

Remove the closed behavioral question gap and gate 2 after all software criteria pass. Replace this plan with the first remaining gate. Commit the completed slice and the next plan. Continue the authorized arc through the complete session endpoint. Keep external model and human evidence limits explicit.
