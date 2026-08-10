# ScalUI

**Scala-inspired native language + Flutter-like UI SDK** — builtin Cats-Effect-inspired `IO`, UI-as-effect with **Headless as a first-class peer runtime**, staged self-hosting, and Skia-shaped canvas ABI.

> Not Scala 3. Not the JVM. Not a cats-effect port. Upstream Scala Native is a reference, not a dependency.

## Status

**v0 app path.** Product intent and direction: [docs/vision.md](docs/vision.md).

- Counter/Todo/nav as ScalUI `Signal` / `View` / `Ui.run`; IO-only apps via `scalui new` (no `--ui`)
- Closed impurity boundary: `Clock` / `Random` / `Fs` / `Net` / `Sys` (args/readLine) / `IO.println` + `TestRuntime` fakes
- Animation + accessibility hooks (Headless-dumpable); theme polish tokens
- Stage-1 CLI: `build|run|test|check|fuzz|watch|new|package|fmt` (`compiler-scalui`); Stage-0 Rust for bootstrap only
- Prebuilt Stage-1 release tree (`scripts/package_release.sh` → `dist/scalui-<triple>.tar.gz`); `install.sh` installs under `PREFIX/share/scalui`
- Deterministic fuzz: `scalui fuzz` (seeded `--iters`, bounded `--exhaust --depth N`, `--replay repro.toml`) on TestRuntime + Headless
- Structural goldens (signal store + a11y dump); PNG optional via `scalui test --pixels`; IO packages use TESTRT exit-0 smoke
- Skia linked for `[ui]` packages only (IO-only link is Skia-free); prebuilts via `SCALUI_SKIA_URL` (default: in-tree `sk_sw`)
- Impeller deferred (see `docs/vision.md`)

App authors: [docs/guide.md](docs/guide.md).

## Quick start

Requirements to **build** from this checkout: Rust (stable), `clang`, `make`. Optional for Window: Linux X11 (`libx11-dev` + display / `xvfb-run`) or macOS GUI session (Cocoa).

Installed apps only need `clang` + `make` (and the Stage-1 release tree under `SCALUI_HOME`).

```bash
# Package + install Stage-1 CLI + SDK (SCALUI_HOME → ~/.local/share/scalui)
./scripts/install.sh
# ensure ~/.local/bin is on PATH

# Or install a prebuilt tarball without rebuilding:
#   ./scripts/package_release.sh
#   RELEASE_TGZ=dist/scalui-$(uname -s | tr A-Z a-z)-$(uname -m).tar.gz ./scripts/install.sh

# v0 happy path
scalui new myapp --ui
cd myapp
scalui test              # seeds goldens/ on first run, then compares
scalui run --headless    # writes build/snapshot.png
# scalui run             # Window when [ui].default_runtime = "window"

# Or use the Stage-0 bootstrap CLI without installing
cargo run -p scalui -- new --ui --path /tmp myapp
cargo run -p scalui -- test /tmp/myapp
cargo run -p scalui -- run --headless /tmp/myapp
```

Other useful commands:

```bash
# Runtime + UI unit tests (includes TestRuntime / anim / a11y)
make -C crates/runtime test

# Stage-0 bootstrap CLI
cargo build -p scalui

# Impurity kit (fake Clock/Random/Fs/Net — no wall wait, no network)
cargo run -p scalui -- run examples/impurity

# Live clock
cargo run -p scalui -- run examples/clock

# Counter (Headless snapshot, no display)
cargo run -p scalui -- run --headless examples/counter

# Golden tests (structural dumps; use --update to rewrite; --pixels for PNGs)
cargo run -p scalui -- test examples/counter

# Optional: install process-wide TestRuntime for an app binary
env SCALUI_TESTRT=1 cargo run -p scalui -- run examples/fs

# Deterministic fuzz (seeded / exhaustive; --replay build/fuzz/repro.toml on failure)
scalui fuzz --iters 16 examples/todo
scalui fuzz --exhaust --depth 1 examples/counter

# Dual-boot gate (Stage 1 → Stage 2: smoke + goldens + fmt parity + IR fixpoint)
./scripts/selfhost.sh
```

Window peer (blits via X11 on Linux or Cocoa on macOS):

```bash
# macOS / Linux with a display
env SCALUI_UI_RUNTIME=window cargo run -p scalui -- run examples/live

# Linux CI / no display (LIVE_FRAMES exits after N pumps; omit for interactive)
xvfb-run -a env SCALUI_UI_RUNTIME=window SCALUI_LIVE_FRAMES=2 \
  cargo run -p scalui -- run examples/hello_ui
```

Stay-open Window demo (`examples/live`, default `[ui].default_runtime = "window"`):

```bash
cargo run -p scalui -- run examples/live
# Press q or Escape to quit. Headless one-shot:
cargo run -p scalui -- run --headless examples/live
```

## Samples gallery

| Example | What it proves |
| --- | --- |
| `examples/hello` | `IO.println` |
| `examples/hello_ui` | Headless `Ui` + goldens |
| `examples/counter` | `Signal`/`View`/`Ui.run` + goldens |
| `examples/nav` | Lambda taps + `showWhen` + stay-open `Ui.run` + goldens |
| `examples/live` | Stay-open Window (`Ui.run`; q/Esc) |
| `examples/todo` | Todo — `List` / `Signal.list` / lambda Add·Save + goldens |
| `examples/effects` | Blessed effects kit |
| `examples/adt` | package / enum / match |
| `examples/fs` | Blessed `Fs.*` (live) |
| `examples/clock` | `Clock.realTime` / `monotonic` |
| `examples/impurity` | `Impurity.runKit` + TestRuntime fakes |

## Layout

```
docs/                     vision, guide, compatibility, scalui.toml schema
crates/compiler/          Stage-0 parser / typer / LLVM codegen
crates/cli/               Stage-0 scalui tool (bootstrap)
crates/runtime/           C runtime (IO kit, impurity, View/Ui)
crates/ffi-skia/          sk_capi + CPU software backend
crates/embedder-desktop/  Linux X11 / macOS Cocoa present for Window peer
crates/embedder-mobile/   Mobile host shell + Android/iOS packaging templates
compiler-scalui/          Stage 1/2 ScalUI compiler + CLI (release path)
scripts/                  selfhost.sh, install.sh, package_release.sh, fetch_skia.sh, run_goldens.sh
examples/                 samples gallery (table above)
third_party/skia/         prebuilt fetch notes
```

Kernel dialect: [docs/vision.md](docs/vision.md#kernel-dialect).

## License

Apache-2.0 (see workspace package metadata).
