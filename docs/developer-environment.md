# Developer environment

Host setup for a checkout. App author path: [`guide.md`](guide.md). Product locks: [`vision.md`](vision.md).

Fail on the first missing tool with one install line.

## Required

| Tool | Role |
| --- | --- |
| `clang` | C11 runtime, Skia ABI, embedders, LLVM IR link |
| `make` | `crates/runtime`, `ffi-skia`, embedders |
| `curl` | fetch tagged `v0.2.0` `scuzz` (`scripts/bootstrap.sh`) |
| `zlib` + `bzip2` | Linux Skia CPU prebuilt |

```bash
# Debian/Ubuntu
sudo apt-get install -y clang make curl zlib1g-dev libbz2-dev
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

## Git hooks

```bash
./scripts/install-githooks.sh
```

A pre-commit hook checks conflict markers. When you stage runtime, ffi-skia, or embedder `.c`, it compiles with `-Werror`. Bypass with `git commit --no-verify`.

## Prove the host

```bash
./scripts/bootstrap.sh
./examples/cli/build/cli test examples/hello
make -C crates/runtime test CC=clang
make -C crates/runtime test-asan CC=clang   # skip if ASan cannot link
```

Full Linux job: `.github/workflows/ci.yml` (`linux-headless`).

| Env | Paint |
| --- | --- |
| unset | Skia CPU prebuilt |
| `SCUZZ_SKIA=sk_sw` | in-tree software |
| `SCUZZ_SKIA=gpu` | software paint, OpenGL present |
