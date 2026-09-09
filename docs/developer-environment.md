# Developer environment

Host setup for a checkout. App author path: run `scuzz docs start`. Product locks: [`vision.md`](vision.md).

Fail on the first missing tool with one install line.

## Required

| Tool | Role |
| --- | --- |
| `clang` | C11 runtime, Skia ABI, embedders, LLVM IR link |
| `make` | `crates/runtime`, `ffi-skia`, embedders |
| `curl` | fetch tagged bootstrap `scuzz` (`scripts/bootstrap.sh`; newest GitHub `v*` release) |
| `zlib` + `bzip2` | Linux Skia CPU prebuilt |
| OpenSSL | Live `https://` on the blessed HTTP kit |

```bash
# Debian/Ubuntu
sudo apt-get install -y clang make curl zlib1g-dev libbz2-dev libssl-dev

# macOS (Homebrew keeps OpenSSL keg-only; the Makefiles and link lines use its prefix)
brew install openssl@3
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

Same slices as GitHub. List them, then run the required PR path:

```bash
./scripts/ci.sh --help
./scripts/ci.sh pr
```

`pr` runs macos-smoke, oracles, kernel, ui, and fuzz. A local run wipes example `build/` dirs except `examples/cli/build`. That wipe stops a rebuilt CLI from a fingerprint hit on a stale `.ll`. GitHub sets `CI=true` and skips the wipe.

Run all Linux slices in sequence with `./scripts/ci.sh linux-headless`. CI builds the product CLI once. Five jobs use that artifact to run compiler, fixed-point, native, app, and fuzz checks in separate checkouts. The `linux-headless` check requires all five jobs and the build to pass. Each slice prints its elapsed time. A new PR update cancels its older CI run. Apt install and artifact upload stay in `.github/workflows/ci.yml`.

| Env | Paint |
| --- | --- |
| unset | Skia CPU prebuilt |
| `SCUZZ_SKIA=sk_sw` | in-tree software |
| `SCUZZ_SKIA=gpu` | software paint, OpenGL present |

## Web build tools

The browser package target needs Python 3. It downloads the pinned Emscripten
SDK on the first build and reuses it from the host cache. It does not need
`emcc` on `PATH` or a shell activation command. Cache paths and cleanup:
run `scuzz docs web`.
The native install script does not install this SDK.

Build the product CLI with `./scripts/bootstrap.sh`. Then run:

```bash
./examples/cli/build/cli package --target web examples/docs
```

The `web` CI slice also needs Node.js and Playwright 1.63.0 with Chromium, Firefox, and WebKit.
Install the browser binaries and host libraries with
`playwright install --with-deps chromium firefox webkit`.
Set `NODE_PATH` to the directory that contains the installed Playwright module.
Run `./scripts/ci.sh web`. The browser check starts a temporary local HTTP
server and closes it when the check ends. The checks include phone emulation.

For a real phone check, serve the web output through HTTPS. Open Docs on the
phone. Copy a command with a long press. Zoom with two fingers. Scroll the page.
Open GUI and focus each edit field. Check that the keyboard does not cover the
field. Enter accented text, emoji, and IME text. Paste text. Rotate the phone.
Switch sections and return to check the stored text. Emulation does not prove
these OS keyboard and selection behaviors.

## Compiler campaigns

Default `./scripts/ci.sh fuzz` replays `examples/tyck` and `examples/codegen` with `scuzz fuzz --iterations 0`. That path uses generated-program oracles plus fixture seeds.

Set `SCUZZ_COMPILER_FUZZ=1` to run a short search campaign on a source copy. That path is slower. Use it for a nightly or local extra check. The env var is the real opt-in. `./scripts/ci-fuzz.sh` invokes it.
