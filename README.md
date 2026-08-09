# ScalUI

**Scala-inspired native language + Flutter-like UI SDK** — builtin Cats-Effect-inspired `IO`, UI-as-effect with **Headless as a first-class peer runtime**, staged self-hosting, and Skia-shaped canvas ABI.

> Not Scala 3. Not the JVM. Not a cats-effect port. Upstream Scala Native is a reference, not a dependency.

## Status

**Phase 6 — Productize** (current). Phases 0–5 landed (Headless UI → self-host → Mobile).

- Closed impurity boundary: `Clock` / `Random` / `Fs` / `Net` / `Sys` / `IO.println` + `TestRuntime` fakes
- Animation v0 + accessibility hooks (Headless-dumpable); theme polish tokens
- Samples gallery below; Skia prebuilts via `SCALUI_SKIA_URL` (default: in-tree `sk_sw`)
- Impeller evaluated and deferred (ADR 0002)

See [docs/vision.md](docs/vision.md).

## Quick start

Requirements: Rust (stable), `clang`, `make`. Optional for Window: Linux X11 (`libx11-dev` + display / `xvfb-run`) or macOS GUI session (Cocoa).

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

# Golden PNG tests
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

| Example | Phase | What it proves |
| --- | --- | --- |
| `examples/hello` | 0 | `IO.println` |
| `examples/hello_ui` | 1 | Headless `Ui` + goldens |
| `examples/counter` | 2 | Signals + Button + goldens |
| `examples/live` | — | Stay-open Window (`Ui.runLive`; q/Esc) |
| `examples/todo` | 2 | TextField/List + IO bridge + goldens |
| `examples/effects` | 3 | Blessed effects kit |
| `examples/adt` | 3 | package / enum / match |
| `examples/fs` | 4 | Blessed `Fs.*` (live) |
| `examples/clock` | 6 | `Clock.realTime` / `monotonic` |
| `examples/impurity` | 6 | `Impurity.runKit` + TestRuntime fakes |

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
scripts/                  selfhost.sh, fetch_skia.sh
examples/                 samples gallery (table above)
third_party/skia/         prebuilt fetch notes
```

Kernel dialect: [docs/adr/0005-kernel-dialect.md](docs/adr/0005-kernel-dialect.md).

## License

Apache-2.0 (see workspace package metadata).
