# Next slice

Remaining terminal-point universal oracles.

Double-run same-seed determinism is in. Grow the mechanical tier next. Effect-log and fiber-census fields enter `State` when one of these oracles claims them.

1. **Heap baseline.** At the quiesced terminal point, live heap bytes and counts return to the session baseline (after mount, before the workload). Growth is a run failure.
2. **Acquire/release pairing.** Within one timeline, each acquire has a matching release. A leftover retain or a double release fails.
3. **Finalizer-on-cancel.** Cancel (race loser / `IO.timeout` / `Fiber.interrupt`) runs `IO.ensure` / `Resource` finalizers. A skipped finalizer fails.
4. **No parked fibers at quiescence.** After the quiesce phase, no fiber stays parked. A leftover park is a run failure (deadlock already covers "all parked with no timer"; this covers a mixed busy/parked leftover).
5. **Live/verify differential.** The same seed on the live graph and the verify graph produces matching timelines (or a declared, mechanical delta). A silent split fails.

Proof:

- `make -C crates/runtime test`, `cargo test -p scuzz-compiler`, `cargo test -p scuzz` pass.
- `cargo run -p scuzz -- fuzz --iterations 16 examples/counter` stays green.
- A planted leak, unpaired acquire, skipped finalizer, leftover park, or live/verify split fails the campaign.

Current locks: [`vision.md`](vision.md). Ranked gaps: [`gaps.md`](gaps.md).
