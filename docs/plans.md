# Plans

Edit this file when the short-term slice changes. Delete this file when the slice is done.

## Current slice

Verification arc slice 1: **Schedule replay** ([`vision.md`](vision.md#verification-arc), rank 1 in [`gaps.md`](gaps.md#verification)).

`schedule_seed` replays a PRNG walk over fiber creation order and contention steps, not recorded decisions. A code change elsewhere in the program can shift the interleaving under the same seed and turn a pinned concurrency failure green. Close that:

1. Record the fiber picked at each contention step in the corpus entry.
2. Replay follows the recorded picks, not the PRNG walk.
3. Report drift when the recorded picks no longer match the program.

Do it in the evaluator scheduler first.

Proof: a pinned `examples/bad-sched` or `examples/webhook` concurrent entry stays red after an unrelated edit, or the campaign reports drift.

Status: not started.
