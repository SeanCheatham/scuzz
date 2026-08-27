# Next slice

Self-hosting prerequisite 1: **tail calls** ([`gaps.md`](gaps.md#3-scuzz-at-compiler-scale-self-hosting)).

- Codegen lowers a self-tail call in a `def` body to a loop (`match` and `if` arms included).
- `scuzz check` stays unchanged: a tail call is not new surface, it is a lowering.
- Proof: a kernel example recurses to depth 1,000,000 and runs headless without stack overflow. `scuzz check` / `scuzz test` / `scuzz fuzz` stay green on `examples/`.

Later prerequisites and the staged rewrite slices: [`vision.md`](vision.md#self-hosting).

Current locks: [`vision.md`](vision.md). Ranked gaps: [`gaps.md`](gaps.md).
