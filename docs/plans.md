# Current slice: trust each decision

Status: implementation not started.

Implement gate 1 of [`vision.md`](vision.md#1-trust-each-decision). Follow the target locks in [`philosophy.md`](philosophy.md#proposal-review). Missing behavior: [`gaps.md`](gaps.md#decision-integrity).

## Outcome

A developer chooses the displayed left or right lane. The IDE applies exactly the reviewed proposal when that lane contains it. Baseline choice and Can't decide change no source. A stale card, failed candidate, repeated action, interrupted write, or restart cannot silently apply another change.

## Scope

- Work in `examples/editor`, the shared review code in `examples/compiler`, and existing verification and CI paths. Change runtime code only if a recovery proof needs a missing filesystem operation. Add no general transaction framework.
- Keep manual proposal import working. Normalize complete candidate directories into one validated internal format. Define publication metadata for later local model output and external producers. Do not add an executable generator interface. Remove Keep/Reject call sites and the build-directory decision log. Do not retain dual APIs or migration readers.
- Limit behavior proposals to three allowed `src/*.scuzz` files. Support additions and replacements. Preserve manifests, path dependencies, scenarios, verification files, and unrelated sources.
- Use the existing UI kit and Headless runtime. Keep Live, Verify, Session, and Timeline available. Do not redesign the general editor or add generation in this slice.
- Update `scuzz docs ide` in `examples/manual` with the shipped controls, storage path, stale-card behavior, and Undo. Do not describe later gates as available.

## Work order

1. Define one card state with a unique identity, frozen inputs, baseline bytes, proposed bytes, fixed lane mapping, and evidence status. Choices are available only after review completes. Use canonical relative paths for identity. Keep module names only for compiler calls.
2. Validate imported paths and complete source sets. Reject traversal, escaping symbolic links, duplicate module stems, malformed metadata, excessive files, and excessive source bytes. Hash sorted records with unambiguous boundaries. Include all review inputs. Treat changed inputs as invalidation.
3. Check absolute candidate outcomes through the shared review path. Preserve baseline failures as evidence for repairs. Block the candidate if any executed claim or assertion fails, even when the baseline also fails. Block incomplete review and limit failures. Clear old lanes on failure.
4. Replace Keep/Reject with Choose left, Choose right, and Can't decide. Map the selected lane to accept or reject. Bind the action to the displayed card. Ignore duplicate and late actions. Do not fall back to the first pending proposal. Refuse dirty touched buffers and changed review inputs.
5. Store card snapshots and records in `.scuzz/ide/`. Journal acceptance before source writes. Complete the journal after all writes and the decision record succeed. Make retries and restart recovery idempotent. Preserve unrelated edits. Disable new choices while recovery is unresolved.
6. Add Undo for the last accepted operation. Require affected files to match the accepted result. Restore the baseline, including removal of files added by that operation. Journal and record Undo. Invalidate pending review after any source change. Serialize editor source writes with acceptance and recovery. Recheck file bytes before each replacement. Report the race limit for independent external writers.
7. Add scenario drivers, temporal claims, and corpus entries for the acceptance criteria. Add finite live Headless proofs to the existing CI UI slice for real writes, restart recovery, and candidate failure. Keep simulated process replacements limited to protocol behavior. Update the manual.

## Acceptance criteria

- [ ] Choosing the proposal works with each lane mapping. Choosing the baseline and Can't decide leave disk unchanged.
- [ ] Source and timeline use the same fixed mapping. No header or pre-choice status reveals which lane is the proposal.
- [ ] A double action cannot accept twice or select the next card. Choices on an unready, missing, or failed card write nothing.
- [ ] Changes to disk source, dependencies, manifest, compiler identity, verification, scenario, workloads, or proposal invalidate the reviewed result. A new proposal with the same name cannot inherit an old decision.
- [ ] Dirty touched buffers remain unchanged. Unsafe paths and duplicate module stems are rejected before review or write.
- [ ] Shared claim failure blocks a candidate. A passing repair of a broken baseline can be reviewed. A crash, timeout, or incomplete result cannot pass.
- [ ] Both source snapshots, both timelines, the witness, the mapping, and the decision survive deletion of `build/`. No second decision log remains.
- [ ] Failure after the first write of a two-file acceptance remains recoverable. Restart recovers deterministically or shows an explicit conflict. Repeated recovery does not duplicate the decision.
- [ ] Undo restores replacements and removes accepted additions. An unrelated later edit prevents overwrite and shows a conflict. Undo survives interruption under the same journal rules.
- [ ] Startup finishes or reports recovery before enabling review. Exit cancels owned jobs. A failure never leaves old actionable evidence on screen.
- [ ] The manual and existing CI proofs use the new controls and record format. No unsupported later feature appears in app-author prose.

## Required validation

Read [`developer-environment.md`](developer-environment.md) for host setup. Export the documented `LIBRARY_PATH` before a Skia link if needed. Use the checkout product CLI after rebuilding it when compiler or CLI sources change.

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

Use a scratch working directory for finite editor runs. The editor can seed files in its current directory. Do not run it at the repository root. Prove actual candidate review and journal writes through live Headless execution as well as hermetic scenarios. Do not treat mocked report files as a complete review proof.

Run targeted checks while implementing. Run the full required checks after the slice is stable. Later slices inherit this check policy and add their own targeted criteria. Run appropriate runtime checks if runtime code changes. Rebuild and rerun affected proofs after a new change or failure. Do not repeat a full suite without a reason. Do not remove a failing claim or corpus entry to make the slice pass.

## Completion and continuation

Update this file with remaining criteria if work stops. Include a reproducer and the exact blocking check. Do not keep a phase diary.

When every criterion and required check passes, remove the closed gaps. Remove gate 1 from `vision.md`. Delete this file. Create the next slice from the first remaining gate and continue the authorized arc. The full feature completes only under gate 7. Real local model access and human preference evidence remain explicit limits until measured.
