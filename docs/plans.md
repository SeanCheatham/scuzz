# Next slice

Studio heap growth under fuzz.

`scuzz fuzz` on `examples/studio` fails the heap-baseline oracle on corpus replay: live count stable, bytes grow by 6 per session. Pre-existing leak in a widget path the studio corpus exercises. Counter and the bad-* examples stay green.

Proof:

- `cargo run -p scuzz -- fuzz --iterations 8 examples/studio` passes the heap baseline.
- The leaked allocation is named in the fix commit (widget or kit, not a suppressed oracle).
- `make -C crates/runtime test`, `cargo test -p scuzz-compiler`, `cargo test -p scuzz` pass.
- `cargo run -p scuzz -- fuzz --iterations 16 examples/counter` stays green.

Current locks: [`vision.md`](vision.md). Ranked gaps: [`gaps.md`](gaps.md).
