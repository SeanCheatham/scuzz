# Next slice

Rust retirement.

`v0.2.0` is the last-Rust cutoff. `scripts/bootstrap.sh` fetches that tagged `scuzz` and compiles `examples/cli`. `package_release.sh` ships that binary. Then delete `crates/compiler` and `crates/cli` when CI no longer uses `cargo` for fuzz, lsp, and package. The product CLI is the Scuzz-emitted binary. A rebuild uses that tagged `scuzz` (or a later Scuzz release). Do not ship two product CLIs.

Current locks: [`vision.md`](vision.md). Ranked gaps: [`gaps.md`](gaps.md).
