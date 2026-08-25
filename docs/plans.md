# Next slice

Coverage strength.

Mutation survivors that change observable behavior split into weak claims (claimed field) and missing claims (unclaimed field). Survivors with bit-identical replayed timelines are inert and unreported.

Proof:

- A surviving mutant that changes a claimed `State` field reports a weak claim.
- A surviving mutant that changes an unclaimed `State` field reports a missing claim.
- A mutant whose replayed timeline is bit-identical is inert and is not a survivor.
- `make -C crates/runtime test`, `cargo test -p scuzz-compiler`, `cargo test -p scuzz` pass.
- `cargo run -p scuzz -- fuzz --iterations 16 examples/counter` stays green.

Current locks: [`vision.md`](vision.md). Ranked gaps: [`gaps.md`](gaps.md).
