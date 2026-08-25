# Next slice

Coverage breadth.

`scuzz check` reports defs, signals, and controls with no claim. The campaign reports reached states that vary in `State` fields no claim reads.

Proof:

- `scuzz check` on a package lists unclaimed defs, signals, and controls.
- A fuzz campaign reports reached states that vary in unclaimed `State` fields.
- `make -C crates/runtime test`, `cargo test -p scuzz-compiler`, `cargo test -p scuzz` pass.
- `cargo run -p scuzz -- fuzz --iterations 16 examples/counter` stays green.

Current locks: [`vision.md`](vision.md). Ranked gaps: [`gaps.md`](gaps.md).
