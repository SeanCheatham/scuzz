# Scuzz Lang

**Scala-inspired native language + Flutter-like UI SDK** — builtin Cats-Effect-inspired `IO`, UI-as-effect with **Headless as a first-class peer runtime**, staged self-hosting, and Skia-shaped canvas ABI.

> Not Scala 3. Not the JVM. Not a cats-effect port. Upstream Scala Native is a reference, not a dependency.

## Status

**v0 app path.** Product intent and direction: [docs/vision.md](docs/vision.md).

- Counter/Todo/nav as Scuzz Lang `Signal` / `View` / `Ui.run`; IO-only apps via `scuzz new` (no `--ui`)
- Closed impurity boundary: `Clock` / `Random` / `Fs` / `Net` / `Sys` (args/readLine) / `IO.println` + `TestRuntime` fakes
- Animation + accessibility hooks (Headless-dumpable); theme polish tokens
- Stage-2 CLI (release): `build|run|test|check|fuzz|watch|new|package|fmt` (`compiler-scuzz`); Stage-0 Rust for bootstrap only
- Prebuilt Stage-2 release tree (`scripts/package_release.sh` → `dist/scuzz-<triple>.tar.gz`); `install.sh` installs under `PREFIX/share/scuzz`
- Deterministic fuzz: `scuzz fuzz` (seeded `--iters`, bounded `--exhaust --depth N`, `--replay repro.toml`) on TestRuntime + Headless; residual module **laws** + sim overlays as the primary oracle
- Structural goldens (signal store + a11y dump); PNG optional via `scuzz test --pixels`; IO packages use TESTRT exit-0 smoke
- Skia linked for `[ui]` packages only (IO-only link is Skia-free); default pinned Skia CPU via `third_party/skia/PIN` (`scripts/fetch_skia.sh`); opt out with `SCUZZ_SKIA=sk_sw` (in-tree software backend); as-needed `skia-cpu` workflow rebuilds the pin
- Impeller deferred (see `docs/vision.md`)

App authors: [docs/guide.md](docs/guide.md).

## Quick start

Requirements to **build** from this checkout: Rust (stable), `clang`, `make`, network once to fetch the pinned Skia CPU prebuilt (or `SCUZZ_SKIA=sk_sw` to skip). Optional for Window: Linux X11 (`libx11-dev` + display / `xvfb-run`) or macOS GUI session (Cocoa).

Installed apps need `clang` + `make` (and the Stage-2 release tree under `SCUZZ_HOME`). Linking `[ui]` apps against the packaged Skia CPU prebuilt also needs zlib / bzip2 / brotli on Linux (`zlib1g-dev libbz2-dev libbrotli-dev`); on macOS, Homebrew `brotli` / `bzip2` if the linker cannot find them.

```bash
# Package + install Stage-2 CLI + SDK (SCUZZ_HOME → ~/.local/share/scuzz)
./scripts/install.sh
# ensure ~/.local/bin is on PATH

# Or install a prebuilt tarball without rebuilding:
#   ./scripts/package_release.sh
#   RELEASE_TGZ=dist/scuzz-$(uname -s | tr A-Z a-z)-$(uname -m).tar.gz ./scripts/install.sh

# v0 happy path
scuzz new myapp --ui
cd myapp
scuzz test              # seeds goldens/ on first run, then compares
scuzz run --headless    # writes build/snapshot.png
# scuzz run             # Window when [ui].default_runtime = "window"

# Or use the Stage-0 bootstrap CLI without installing
cargo run -p scuzz -- new --ui --path /tmp myapp
cargo run -p scuzz -- test /tmp/myapp
cargo run -p scuzz -- run --headless /tmp/myapp
```

Other useful commands:

```bash
# Runtime + UI unit tests (includes TestRuntime / anim / a11y)
make -C crates/runtime test

# Stage-0 bootstrap CLI
cargo build -p scuzz

# Impurity kit (fake Clock/Random/Fs/Net — no wall wait, no network)
cargo run -p scuzz -- run examples/impurity

# Live clock
cargo run -p scuzz -- run examples/clock

# Counter (Headless snapshot, no display)
cargo run -p scuzz -- run --headless examples/counter

# Golden tests (structural dumps; use --update to rewrite; --pixels for PNGs)
cargo run -p scuzz -- test examples/counter

# Optional: install process-wide TestRuntime for an app binary
env SCUZZ_TESTRT=1 cargo run -p scuzz -- run examples/fs

# Deterministic fuzz (seeded / exhaustive; --replay build/fuzz/repro.toml on failure)
scuzz fuzz --iters 16 examples/todo
scuzz fuzz --exhaust --depth 1 examples/counter

# Dual-boot gate (Stage 1 → Stage 2: smoke + goldens + fmt parity + IR fixpoint)
./scripts/selfhost.sh
```

Window peer (blits via X11 on Linux or Cocoa on macOS):

```bash
# macOS / Linux with a display
env SCUZZ_UI_RUNTIME=window cargo run -p scuzz -- run examples/live

# Linux CI / no display (LIVE_FRAMES exits after N pumps; omit for interactive)
xvfb-run -a env SCUZZ_UI_RUNTIME=window SCUZZ_LIVE_FRAMES=2 \
  cargo run -p scuzz -- run examples/hello_ui
```

Stay-open Window demo (`examples/live`, default `[ui].default_runtime = "window"`):

```bash
cargo run -p scuzz -- run examples/live
# Press q or Escape to quit. Headless one-shot:
cargo run -p scuzz -- run --headless examples/live
```

## Samples gallery

| Example | What it proves |
| --- | --- |
| `examples/hello` | `IO.println` |
| `examples/cli` | `Sys.args` + `Sys.readLine` |
| `examples/hello_ui` | Headless `Ui` + goldens |
| `examples/counter` | `View.center` / `View.stack` / `View.positioned` + `Signal`/`View`/`Ui.run` + path dep + goldens |
| `examples/nav` | `showWhen` + Row `View.expanded` + `View.align` + goldens |
| `examples/live` | Stay-open Window (`Ui.run`; q/Esc) |
| `examples/todo` | Todo — `Signal.list` / `View.expanded` scroll / Add·Save + goldens |
| `examples/concurrency` | `Ref` / `Queue` / `Deferred` + `IO.race` / `IO.both` |
| `examples/adt` | package / enum / match |
| `examples/modules` | stem modules, `private def`, `import`, enum-per-module |
| `examples/record` | `record` + `p.x` field access |
| `examples/trait` | `trait` / `impl` + static-dispatch methods |
| `examples/generic` | Monomorphized `def id[T](…)` |
| `examples/fs` | Blessed `Fs.*` (live) |
| `examples/clock` | `Clock.realTime` / `monotonic` |
| `examples/impurity` | `Impurity.runKit` + TestRuntime fakes |

## Layout

```
docs/                     vision, gaps, guide, compatibility, scuzz.toml schema
crates/compiler/          Stage-0 parser / typer / LLVM codegen
crates/cli/               Stage-0 scuzz tool (bootstrap)
crates/runtime/           C runtime (IO kit, impurity, View/Ui)
crates/ffi-skia/          sk_capi + CPU software backend
crates/embedder-desktop/  Linux X11 / macOS Cocoa present for Window peer
crates/embedder-mobile/   Mobile host shell + Android/iOS packaging templates
compiler-scuzz/          Stage 1/2 Scuzz Lang compiler + CLI (release path)
scripts/                  selfhost.sh, install.sh, package_release.sh, package_project.sh, fetch_skia.sh, run_goldens.sh
examples/                 samples gallery (table above)
third_party/skia/         prebuilt fetch notes
```

Kernel dialect: [docs/vision.md](docs/vision.md#kernel-dialect).

## License

Apache-2.0 (see workspace package metadata).
