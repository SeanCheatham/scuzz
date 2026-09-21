# Plans

Edit this file when the short-term slice changes. Delete this file when the slice is done.

## Current slice

Verification arc slice 1: **Shrink** ([`vision.md`](vision.md#verification-arc), rank 1 in [`gaps.md`](gaps.md#verification)).

A failing search script stores as-is today. Add shrinking to the search failure path in `scuzz fuzz`:

1. Delta-debug script lines. Remove lines while the script still fails the same way.
2. Nudge Int arguments toward a `where` bound or zero while the script still fails.
3. Store the minimal script in the corpus entry.

Proof: a known failing search on `examples/reach` or `examples/api-report` stores a shorter corpus entry that still fails.

Status: not started.
