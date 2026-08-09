# ScalUI

**Scala-inspired native language + Flutter-like UI SDK** — builtin Cats-Effect-inspired `IO`, UI-as-effect with **Headless as a first-class peer runtime**, staged self-hosting, and Skia-shaped canvas ABI.

> Not Scala 3. Not the JVM. Not a cats-effect port. Upstream Scala Native is a reference, not a dependency.

## Status

**Phase 3 — Language, effects stdlib, tooling** (current)

- Packages / multi-file modules, nullary `enum` ADTs, `match`, local `val`
- Blessed effects kit: `Resource`, `Ref`, `Deferred`, `Queue`, `handleErrorWith` / `attempt`, `sleep`, `race` / `both`
- Tooling: incremental compile, `scalui watch`, `scalui fmt`, basic `scalui lsp`
- Linux X11 embedder for `UiRuntime.Window` (Headless still the CI default)
- ScalUI parser bootstrap in `compiler-scalui/` (built by Stage 0)

Phase 1–2 Headless UI + Counter/Todo goldens remain. See [docs/vision.md](docs/vision.md).

## Quick start

Requirements: Rust (stable), `clang`, `make`. Optional for Window: X11 (`libx11-dev`) + a display (or `xvfb-run`).

```bash
# Runtime + UI unit tests
make -C crates/runtime test

# Build the Stage-0 CLI
cargo build -p scalui

# Effects kit demo
cargo run -p scalui -- run examples/effects

# ADT / package / match (multi-file)
cargo run -p scalui -- run examples/adt

# ScalUI-written parser smoke (Stage 0 host)
cargo run -p scalui -- run compiler-scalui

# Counter (Headless snapshot, no display)
cargo run -p scalui -- run --headless examples/counter

# Golden PNG tests
cargo run -p scalui -- test examples/counter

# Format / LSP / watch
cargo run -p scalui -- fmt examples/adt
cargo run -p scalui -- lsp
cargo run -p scalui -- watch examples/hello
```

Window peer (presents via X11 when `DISPLAY` is set):

```bash
xvfb-run -a env SCALUI_UI_RUNTIME=window cargo run -p scalui -- run examples/hello_ui
```

## Layout

```
docs/                 vision, compatibility, ADRs, scalui.toml schema
crates/compiler/      Stage-0 parser / typer / LLVM codegen (Rust)
crates/cli/           scalui tool (build/run/test/fmt/lsp/watch)
crates/runtime/       C runtime (GC-v0, IO kit, View tree, Ui session, demos)
crates/ui/            design-language home (Phase 2 widgets live in runtime C for now)
crates/ffi-skia/      sk_capi + CPU software backend
crates/embedder-desktop/  Linux X11 present for Window peer
compiler-scalui/      ScalUI parser bootstrap (Stage 0 → Phase 4)
examples/hello/       Stage-0 IO sample
examples/hello_ui/    Phase 1 Headless label + goldens
examples/counter/     Phase 2 Counter + goldens
examples/todo/        Phase 2 Todo + goldens
examples/effects/     Phase 3 effects kit
examples/adt/         Phase 3 package + enum + match
third_party/skia/     prebuilt fetch notes
scripts/fetch_skia.sh optional Skia prebuilt fetch
```

## Kernel dialect (Stage 0 / Phase 3)

```scala
package demo.color

enum Color:
  case Red
  case Blue

@main def main: IO[Unit] =
  val c = Color.Red
  c match {
    case Color.Red => IO.println("red")
    case Color.Blue => IO.println("blue")
  }
```

Also: `Effects.runKit`, `Ui.runCounter` / `Ui.runTodo` / `Ui.runHeadless`, `IO.sleep` / `race` / `both` / `fail`, `.handleErrorWith` / `.attempt`, `Lexer.classify`. See [docs/adr/0005-kernel-dialect.md](docs/adr/0005-kernel-dialect.md).

## License

Apache-2.0 (see workspace package metadata).
