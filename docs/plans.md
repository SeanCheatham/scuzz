# Next slice

Self-hosting staged rewrite 4 remainder: **compile `examples/studio`, compile itself, and Rust retirement** ([`vision.md`](vision.md#self-hosting)).

CLI core, driver core, test/fuzz core, and hello package compile are in (`examples/cli`, `examples/compiler`, `cli-ok` / `ir-ok`). Hello-shaped IR clang-links against `libscuzz_rt.a`. Scuzz emit of `Drive.emitDir` clang-links and compiles `examples/fmt`, `examples/hello`, `examples/tyck`, `examples/codegen`, `examples/cli`, `examples/io`, `examples/kernel`, `examples/scale`, and `examples/counter` from disk. Those IR clang-link. The fmt through cli binaries print `fmt-ok` / `Hello, Scuzz!` / `tyck-ok` / `ir-ok` / `cli-ok`. The io binary runs clock, concurrency, stream, and Json kits. Helpers match a record once. They do not use `.field` on a call. `Emit.emit` lowers if-else and match self-tail to a `tco_loop` (`srcTco` / `srcTcoMatch`). Scuzz-emitted kernel clang-links and prints `tco:0` / `tcom:0` / `build:2097152`. Scuzz-emitted scale clang-links and prints `mapn:3048` / `maps:2098176` / `hit:0:49`. Scuzz-emitted counter clang-links with Skia. A headless two-frame run writes the same structural dump as `examples/counter/goldens/counter.dump`. `Drive.emitDir` lists `src/` and nested path-dep `src/`. The product CLI is still the Rust binary.

- Finish the slice: compile studio, then itself. Scuzz emit packs first-class lambdas as `cons(fn, cons(env, nil))` (`srcLam`; `plusOne()(5)` prints `6`). Kernel through TCO and Builder is in. Scale is in. Counter UI kits are in.
- Land the last Rust `scuzz` on main, tag it, and ship a GitHub Release. That binary is the bootstrap compiler.
- The Rust crates retire when that tagged `scuzz` compiles the Scuzz toolchain, and that toolchain compiles `examples/` and itself.

Current locks: [`vision.md`](vision.md). Ranked gaps: [`gaps.md`](gaps.md).
