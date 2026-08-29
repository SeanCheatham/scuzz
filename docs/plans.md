# Next slice

Rust retirement.

`v0.2.0` is the last-Rust cutoff. That tagged `scuzz` compiles the Scuzz compiler. That compiler compiles `examples/` and itself. Then delete `crates/compiler` and `crates/cli`. The product CLI is the Scuzz-emitted binary. A rebuild uses that tagged `scuzz` (or a later Scuzz release). Do not ship two product CLIs.

Current locks: [`vision.md`](vision.md). Ranked gaps: [`gaps.md`](gaps.md).
