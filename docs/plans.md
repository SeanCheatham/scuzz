# Next slice

Self-hosting staged rewrite 4 remainder: **verification stack and Rust retirement** ([`vision.md`](vision.md#self-hosting)).

CLI core and driver core are in (`examples/cli`, `examples/compiler`, `cli-ok`): argv parse, help, fmt dispatch, `scuzz.toml` parse, typecheck-then-emit, clang argv, check dispatch (human and JSON). The product CLI is still the Rust binary.

- Finish the slice: test/fuzz, compile `examples/`, compile itself.
- The Rust binary retires when that campaign is green.

Current locks: [`vision.md`](vision.md). Ranked gaps: [`gaps.md`](gaps.md).
