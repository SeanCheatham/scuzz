# Plans

Edit this file when the short-term slice changes. Delete this file when the slice is done.

## Current slice

Verification arc slice 1: **Generators** ([`vision.md`](vision.md#verification-arc), rank 1 in [`gaps.md`](gaps.md#verification)).

Generated values stay in a small band around zero or a `where` bound. Close that:

1. Boundary Ints: zero, one, neg one, min, max, overflow edges, and `where` bound edges.
2. Size grows with the iteration: longer strings, lists, and deeper ADTs later in the campaign.
3. A string alphabet with empty, quotes, newlines, delimiters, and non-ASCII.
4. Parse `where` as an expression, not a substring test. A compound `where` keeps its bounds.

Proof: generated oracles on `examples/kernel` or `examples/tyck` reach those shapes; a compound `where` keeps its bounds.

Status: not started.
