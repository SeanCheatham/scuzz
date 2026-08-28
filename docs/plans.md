# Next slice

Self-hosting staged rewrite 4 remainder: **compile the rest of `examples/`, compile itself, and Rust retirement** ([`vision.md`](vision.md#self-hosting)).

CLI core, driver core, test/fuzz core, and hello package compile are in (`examples/cli`, `examples/compiler`, `cli-ok` / `ir-ok`). Hello-shaped IR clang-links against `libscuzz_rt.a`. Scuzz emit of `examples/hello` clang-links and prints `Hello, Scuzz!` / `ready.` Scuzz emit of `examples/codegen` clang-links and prints `ir-ok`. Scuzz emit of `examples/cli` clang-links and prints `cli-ok`. The product CLI is still the Rust binary.

- Finish the slice: compile the rest of `examples/`, then itself.
- Land the last Rust `scuzz` on main, tag it, and ship a GitHub Release. That binary is the bootstrap compiler.
- The Rust crates retire when that tagged `scuzz` compiles the Scuzz toolchain, and that toolchain compiles `examples/` and itself.

Current locks: [`vision.md`](vision.md). Ranked gaps: [`gaps.md`](gaps.md).
