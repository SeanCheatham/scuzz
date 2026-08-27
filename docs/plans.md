# Next slice

Self-hosting staged rewrite 4 remainder: **compile the rest of `examples/`, compile itself, and Rust retirement** ([`vision.md`](vision.md#self-hosting)).

CLI core, driver core, test/fuzz core, and hello package compile are in (`examples/cli`, `examples/compiler`, `cli-ok` / `ir-ok`). Hello-shaped IR clang-links against `libscuzz_rt.a`. The product CLI is still the Rust binary.

- Finish the slice: compile the rest of `examples/`, compile itself.
- The Rust binary retires when that campaign is green.

Current locks: [`vision.md`](vision.md). Ranked gaps: [`gaps.md`](gaps.md).
