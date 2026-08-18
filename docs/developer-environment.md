# Developer environment

Host setup for a checkout. App author path: [`guide.md`](guide.md). Product locks: [`vision.md`](vision.md).

Fail on the first missing tool with one install line.

## Required

| Tool | Role |
| --- | --- |
| Rust stable (`cargo`, `rustc`, `rustfmt`, `clippy`) | compiler + CLI (`rust-toolchain.toml`) |
| `clang` | C11 runtime, Skia ABI, embedders, LLVM IR link |
| `make` | `crates/runtime`, `ffi-skia`, embedders |
| `zlib` + `bzip2` | Linux Skia CPU prebuilt |

```bash
# Debian/Ubuntu
sudo apt-get install -y clang make zlib1g-dev libbz2-dev
```

If `clang` cannot find libstdc++ when it links Skia:

```bash
export LIBRARY_PATH=/usr/lib/gcc/x86_64-linux-gnu/13
```

## Optional CI slices

| Slice | Packages |
| --- | --- |
| ASan (`make -C crates/runtime test-asan`) | `libclang-rt-18-dev` (match host clang) |
| GPU (`SCUZZ_SKIA=gpu`) | `libegl1-mesa-dev libgles2-mesa-dev libgl1-mesa-dri xvfb` |
| Desktop X11 | `libx11-dev xvfb` |

ASan skips when the ASan runtime cannot link. GPU needs `LIBGL_ALWAYS_SOFTWARE=1` when the host has no GPU.

## Prove the host

```bash
cargo fmt --all -- --check
cargo clippy -p scuzz -p scuzz-compiler --all-targets -- \
  -D warnings -A clippy::too_many_arguments -A clippy::type_complexity \
  -A clippy::if_same_then_else -A clippy::collapsible_if \
  -A clippy::redundant_guards -A clippy::useless_format \
  -A clippy::identity_op -A clippy::len_zero -A clippy::unnecessary_to_owned
make -C crates/runtime test CC=clang
make -C crates/runtime test-asan CC=clang   # skip if ASan cannot link
cargo test -p scuzz-compiler
cargo build -p scuzz
cargo run -p scuzz -- test examples/hello
```

Full Linux job: `.github/workflows/ci.yml` (`linux-headless`).

| Env | Paint |
| --- | --- |
| unset | Skia CPU prebuilt |
| `SCUZZ_SKIA=sk_sw` | in-tree software |
| `SCUZZ_SKIA=gpu` | software paint, OpenGL present |
