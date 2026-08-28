# Next slice

Human cutoff tag.

`v0.1.0` already shipped. Land this branch on main. Tag `v0.2.0`. Push the tag. `release.yml` publishes `linux-x86_64` and `darwin-arm64`.

That tagged Rust `scuzz` is the cutoff compiler. After the tag, that binary compiles the Scuzz toolchain. Do not delete Rust crates before that tag. Do not tag from an agent.

Current locks: [`vision.md`](vision.md). Ranked gaps: [`gaps.md`](gaps.md).
