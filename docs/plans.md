# Next slice

## Corpus persistence and fast replay tier

Pin fixed bugs forever. Give fast inner-loop feedback. Direction: [`vision.md`](vision.md#open-work).

- `scuzz fuzz` promotes each shrunk `repro.toml` into a checked-in regression corpus next to `scuzz.toml`.
- Every campaign replays the corpus before search. A corpus failure fails fast with the stored repro.
- A corpus-only tier replays the corpus and stops. It runs no search and no mutation. It answers in seconds.
- `build/fuzz/summary.toml` separates a `Property.sometimes` name never reached in any stored prefix from a name not reached in this budget.

Done when: the `examples/` corpora replay in seconds; a fixed `examples/bad-example` bug stays pinned across campaigns; corpus replay counts toward `sometimes` reachability.
