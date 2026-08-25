# Next slice

Stronger mutation operators.

Boundary `where` mutations flip or drop a residual refinement bound so a live call site can pass a value the `where` rejected.

Proof:

- A `where n >= 0` residual mutates at the bound (negate or drop).
- `make -C crates/runtime test`, `cargo test -p scuzz-compiler`, `cargo test -p scuzz` pass.
- `cargo run -p scuzz -- fuzz --iterations 16 examples/counter` stays green.

Current locks: [`vision.md`](vision.md). Ranked gaps: [`gaps.md`](gaps.md).
