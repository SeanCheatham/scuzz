# Next slice

Timeline claim kernel, first cut. Prove claims judged over a recorded timeline at a quiesced terminal point.

1. **Quiesce phase.** Each `scuzz fuzz` run ends the same way: stop new events, inject shutdown where it applies, pump until fibers settle or a quiesce budget trips. A budget trip is a campaign diagnostic, not a claim failure.
2. **Observation `State`.** Record one `State` per pump, in memory: signal store, a11y rows, `[last_hit]`, drive records. Deterministic capture: creation-order ids, no pointers, no hash-map order. A run aborted by `.require` or panic fails on the abort; its timeline is not judged.
3. **Claim kernel.** Lower `The <signal> stays at <n> or more.` and `After a tap on the "<label>" control, eventually the "<needle>" control is visible.` to timeline predicates evaluated at the terminal point. Delete the runtime response latch. A pending `eventually` at a settled terminal point fails. A failing claim reports the violating state index.
4. **One universal oracle.** Double-run same-seed determinism: replay each kept run once; a timeline mismatch fails the campaign.

Proof:

- `make -C crates/runtime test`, `cargo test -p scuzz-compiler`, `cargo test -p scuzz` pass.
- `cargo run -p scuzz -- fuzz --iterations 16 examples/counter` stays green with claims judged at terminal points.
- `examples/bad-response` still fails with the same shrunk repro.

Current locks: [`vision.md`](vision.md). Ranked gaps: [`gaps.md`](gaps.md).
