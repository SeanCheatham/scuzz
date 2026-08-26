# Next slice

Stronger mutation operators.

Handler-swap on UI taps rewires a tap handler to a sibling handler so the wrong control fires. Signal-map identity replaces a `Signal.map` transform with the identity so a derived display stops updating. Then a mutation score in `summary.toml`.

Proof:

- A handler-swap mutant on `examples/counter` fires the wrong handler and is reported with its label.
- A Signal-map identity mutant makes `View.bindText` stale and is killed by a claim or golden face.
- `make -C crates/runtime test`, `cargo test -p scuzz-compiler`, `cargo test -p scuzz` pass.
- `cargo run -p scuzz -- fuzz --iterations 16 examples/counter` stays green.

Current locks: [`vision.md`](vision.md). Ranked gaps: [`gaps.md`](gaps.md).
