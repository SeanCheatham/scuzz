# Next slice

Swarm testing + corpus hygiene.

Today every fuzz run draws from the same event-alphabet weights, so a campaign explores one point of the strategy space. Vary the weights per run (swarm): each run picks a random weighting over taps, text, scrolls, drivers, and fault seeds, so a campaign covers strategies a fixed mix would miss. Then corpus hygiene: `scuzz fuzz --minimize-corpus` rewrites stored entries to their shortest forms that keep the same sometimes coverage.

Proof:

- Two same-seed campaigns over `examples/studio` with swarm enabled still replay the stored corpus deterministically; corpus replay stays byte-stable.
- `--minimize-corpus` on `examples/studio` keeps every declared `sometimes` name reached with entries no longer than before.
- `make -C crates/runtime test`, `cargo test -p scuzz-compiler`, `cargo test -p scuzz` pass.
- `cargo run -p scuzz -- fuzz --iterations 16 examples/counter` and `examples/studio` stay green.

Current locks: [`vision.md`](vision.md). Ranked gaps: [`gaps.md`](gaps.md).
