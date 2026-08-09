# ScalUI

**Scala-inspired native language + Flutter-like UI SDK** — builtin Cats-Effect-inspired `IO`, UI-as-effect with **Headless as a first-class peer runtime**, staged self-hosting, and Skia-shaped canvas ABI.

> Not Scala 3. Not the JVM. Not a cats-effect port. Upstream Scala Native is a reference, not a dependency.

## Status

**v0 app path** (current). Phases 0–6 landed; next steps live in [docs/plan.md](docs/plan.md).

- **ScalUI-authored Views**: Counter/Todo build widget trees via `Signal` / `View` / `Ui.run` (C demos remain for Live / kits)
- Closed impurity boundary: `Clock` / `Random` / `Fs` / `Net` / `Sys` / `IO.println` + `TestRuntime` fakes
- Animation v0 + accessibility hooks (Headless-dumpable); theme polish tokens
- Stage-1 CLI: `build|run|test|watch|new|package` (`compiler-scalui`); Stage-0 Rust is canary (`fmt` stays canary until pretty-printer is ported)
- Samples gallery below; Skia prebuilts via `SCALUI_SKIA_URL` (default: in-tree `sk_sw`)
- Impeller evaluated and deferred (ADR 0002)

See [docs/vision.md](docs/vision.md).

## Quick start

Requirements: Rust (stable), `clang`, `make`. Optional for Window: Linux X11 (`libx11-dev` + display / `xvfb-run`) or macOS GUI session (Cocoa).

```bash
# Install Stage-1 CLI (wrapper sets SCALUI_HOME to this checkout)
./scripts/install.sh
# ensure ~/.local/bin is on PATH

# v0 happy path
scalui new myapp --ui
cd myapp
scalui test              # seeds goldens/ on first run, then compares
scalui run --headless    # writes build/snapshot.png
# scalui run             # Window when [ui].default_runtime = "window"

# Or use the Stage-0 canary without installing
cargo run -p scalui -- new --ui --path /tmp myapp
cargo run -p scalui -- test /tmp/myapp
cargo run -p scalui -- run --headless /tmp/myapp
```

Other useful commands:

```bash
# Runtime + UI unit tests (includes TestRuntime / anim / a11y)
make -C crates/runtime test

# Stage-0 canary CLI
cargo build -p scalui

# Impurity kit (fake Clock/Random/Fs/Net — no wall wait, no network)
cargo run -p scalui -- run examples/impurity

# Live clock
cargo run -p scalui -- run examples/clock

# Counter (Headless snapshot, no display)
cargo run -p scalui -- run --headless examples/counter

# Golden PNG tests (use --update to rewrite goldens)
cargo run -p scalui -- test examples/counter

# Optional: install process-wide TestRuntime for an app binary
env SCALUI_TESTRT=1 cargo run -p scalui -- run examples/fs

# Dual-boot (Stage 1 rebuilds itself → Stage 2)
./scripts/selfhost.sh
```

Window peer (blits via X11 on Linux or Cocoa on macOS):

```bash
# macOS / Linux with a display
env SCALUI_UI_RUNTIME=window cargo run -p scalui -- run examples/live

# Linux CI / no display
xvfb-run -a env SCALUI_UI_RUNTIME=window cargo run -p scalui -- run examples/hello_ui
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
| `examples/counter` | ScalUI `Signal`/`View`/`Ui.run` + goldens |
| `examples/nav` | `buttonSet` + `showWhen` + stay-open `Ui.run` + goldens |
| `examples/live` | Stay-open Window (`Ui.runLive`; q/Esc) |
| `examples/todo` | ScalUI Todo tree + `Ui.runWithTodo` + goldens |
| `examples/effects` | Blessed effects kit |
| `examples/adt` | package / enum / match |
| `examples/fs` | Blessed `Fs.*` (live) |
| `examples/clock` | `Clock.realTime` / `monotonic` |
| `examples/impurity` | `Impurity.runKit` + TestRuntime fakes |

## Layout

```
docs/                     vision, compatibility, ADRs, scalui.toml schema
crates/compiler/          Stage-0 parser / typer / LLVM codegen (Rust canary)
crates/cli/               Stage-0 scalui tool (canary)
crates/runtime/           C runtime (IO kit, impurity, View/Ui — widgets live here for now)
crates/ffi-skia/          sk_capi + CPU software backend
crates/embedder-desktop/  Linux X11 / macOS Cocoa present for Window peer
crates/embedder-mobile/   Mobile host shell + Android/iOS packaging templates
compiler-scalui/          Stage 1/2 ScalUI compiler + CLI (release path)
scripts/                  selfhost.sh, install.sh, fetch_skia.sh, run_goldens.sh
examples/                 samples gallery (table above)
third_party/skia/         prebuilt fetch notes
```

Kernel dialect: [docs/adr/0005-kernel-dialect.md](docs/adr/0005-kernel-dialect.md).

## License

Apache-2.0 (see workspace package metadata).
