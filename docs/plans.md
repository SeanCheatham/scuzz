# Next slice

Differential structural dumps.

`scuzz test --differential` runs a `[ui]` package's golden scenarios (base + after_tap) once per render backend (`skia`, `sk_sw`, `gpu`) and byte-compares the structural dumps across backends. Goldens stay the single regression face. The differential is a universal oracle: backends must not change the `Ui` session or the structural dump. A backend that cannot build or run on the host skips with a note. Fewer than two runnable backends fails. `--update` / `--pixels` reject under `--differential`. After the run the original `SCUZZ_SKIA` backend is restored.

Proof:

- `scuzz test --differential examples/counter` compares `skia`, `sk_sw`, and `gpu` dumps on this host and passes.
- A compare unit test fails on differing dumps and passes on identical ones.
- CI `linux-headless` gains a differential step over `examples/counter`.
- `make -C crates/runtime test`, `cargo test -p scuzz-compiler`, `cargo test -p scuzz` pass.
- `cargo run -p scuzz -- fuzz --iterations 16 examples/counter` stays green.

Current locks: [`vision.md`](vision.md). Ranked gaps: [`gaps.md`](gaps.md).
