# ScalUI

**Scala-inspired native language + Flutter-like UI SDK** — builtin Cats-Effect-inspired `IO`, UI-as-effect with **Headless as a first-class peer runtime**, staged self-hosting, and Skia-shaped canvas ABI.

> Not Scala 3. Not the JVM. Not a cats-effect port. Upstream Scala Native is a reference, not a dependency.

## Status

**Phase 2 — Declarative UI core** (current)

- Element tree, signals, layout, hit testing under the `Ui` session protocol
- Theme tokens + widgets: `Text`, `Button`, `TextField`, `List`, `Scroll`, `Image`, `Icon` (+ Phase 1 `Label`)
- IO → UI bridge: completed `IO` posts signal writes; `pump` applies them (UI-thread hop)
- Examples: `examples/counter`, `examples/todo` (Todo load/save via `IO` + `Resource`)
- Kernel dialect: `Ui.runCounter`, `Ui.runTodo`, `Ui.runHeadless`

Phase 1 Headless/Window session + goldens remain. See [docs/vision.md](docs/vision.md).

## Quick start

Requirements: Rust (stable), `clang`, `make`.

```bash
# Runtime + UI unit tests
make -C crates/runtime test

# Build the Stage-0 CLI
cargo build -p scalui

# Counter (Headless snapshot, no display)
cargo run -p scalui -- run --headless examples/counter

# Todo (List + Resource persistence)
cargo run -p scalui -- run --headless examples/todo

# Golden PNG tests
cargo run -p scalui -- test examples/counter
cargo run -p scalui -- test examples/todo

# Phase 1 label demo still works
cargo run -p scalui -- test examples/hello_ui
```

Create a Counter UI project:

```bash
cargo run -p scalui -- new --ui counter
cargo run -p scalui -- run --headless counter
```

## Layout

```
docs/                 vision, compatibility, ADRs, scalui.toml schema
crates/compiler/      Stage-0 parser / typer / LLVM codegen (Rust)
crates/cli/           scalui tool
crates/runtime/       C runtime (GC-v0, IO, View tree, Ui session, demos)
crates/ui/            design-language home (Phase 2 widgets live in runtime C for now)
crates/ffi-skia/      sk_capi + CPU software backend
crates/embedder-desktop/  Window OS surface (stub; peer protocol in runtime)
examples/hello/       Stage-0 IO sample
examples/hello_ui/    Phase 1 Headless label + goldens
examples/counter/     Phase 2 Counter + goldens
examples/todo/        Phase 2 Todo + goldens
third_party/skia/     prebuilt fetch notes
scripts/fetch_skia.sh optional Skia prebuilt fetch
```

## Kernel dialect (Stage 0 / Phase 2)

```scala
@main def main: IO[Unit] =
  Ui.runCounter
```

Also: `Ui.runTodo`, `Ui.runHeadless("…")`, `IO.println` / `flatMap`. See [docs/adr/0005-kernel-dialect.md](docs/adr/0005-kernel-dialect.md).

## License

Apache-2.0 (see workspace package metadata).
