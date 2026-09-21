# Plans

Edit this file when the short-term slice changes. Delete this file when the slice is done.

## Current slice

Verification arc slice 1: **Mutation gate** ([`vision.md`](vision.md#verification-arc), rank 1 in [`gaps.md`](gaps.md#verification)).

A survivor does not fail the campaign unless `[fuzz].score_floor` is set. Mutation reruns every site every campaign. Close that:

1. Diff-scoped mutation: mutate only defs that changed since the last fingerprint.
2. Persist per-site kill results keyed by compiler SHA-256; a second campaign reuses prior kills.
3. Default `[fuzz].score_floor` on.

Proof: a small edit in `examples/counter` mutates that def; a second campaign reuses prior kills; a surviving mutant fails the floor.

Status: not started.
