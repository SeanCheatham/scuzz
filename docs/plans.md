# Next slice

Self-hosting staged rewrite 4 remainder: **Rust retirement** ([`vision.md`](vision.md#self-hosting)).

Compile itself is in. Scuzz-emitted `examples/cli` clang-links and prints `cli-ok`. That binary compiles `examples/hello`, `examples/compiler` (`clang -c`), and `examples/cli` itself. The self-emitted CLI compiles hello. Empty `Sys.args` keeps `cli-ok`. `scuzz build --out-dir` writes `Drive.emitDir`. Nested `else if` self-tails join `tco_loop` (`srcTcoNest`). A library package skips dummy `@main` (`srcLib`). The product CLI is still the Rust binary.

- Land the last Rust `scuzz` on main, tag it, and ship a GitHub Release. That binary is the bootstrap compiler.
- The Rust crates retire when that tagged `scuzz` compiles the Scuzz toolchain, and that toolchain compiles `examples/` and itself.

Current locks: [`vision.md`](vision.md). Ranked gaps: [`gaps.md`](gaps.md).
