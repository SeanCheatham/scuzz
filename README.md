# ScalUI

**Scala-inspired native language + Flutter-like UI SDK** — builtin Cats-Effect-inspired `IO`, UI-as-effect with **Headless as a first-class peer runtime**, staged self-hosting, and Skia.

> Not Scala 3. Not the JVM. Not a cats-effect port. Upstream Scala Native is a reference, not a dependency.

## Status

**Phase 0 — Foundation** (in progress)

- Repo skeleton + vision / ADRs
- Stage-0 bootstrap compiler (Rust): kernel dialect → LLVM IR → native executable
- Minimal C runtime: alloc, strings, panic, **IO fiber skeleton** (`delay`, `flatMap`, `println`, `Resource`, run loop)
- `scalui` CLI: `new` / `build` / `run` / `test`
- Headless Linux CI

Phase 1 will add `Ui` + `UiRuntime.Headless` (Skia offscreen). See [docs/vision.md](docs/vision.md).

## Quick start

Requirements: Rust (stable), `clang`, `make`.

```bash
# Runtime unit tests (IO / Resource)
make -C crates/runtime test

# Build the Stage-0 CLI
cargo build -p scalui

# Hello world → LLVM → run
cargo run -p scalui -- run examples/hello
```

Expected output:

```
Hello, ScalUI!
Phase 0 online.
```

Create a project:

```bash
cargo run -p scalui -- new counter
cargo run -p scalui -- run counter
```

## Layout

```
docs/                 vision, compatibility, ADRs, scalui.toml schema
crates/compiler/      Stage-0 parser / typer / LLVM codegen (Rust)
crates/cli/           scalui tool
crates/runtime/       C runtime (GC-v0 alloc, IO fibers)
examples/hello/       Stage-0 sample
third_party/skia/     placeholder (Phase 1)
```

## Kernel dialect (Stage 0)

```scala
@main def main: IO[Unit] =
  IO.println("Hello, ScalUI!").flatMap(_ => IO.println("Phase 0 online."))
```

See [docs/adr/0005-kernel-dialect.md](docs/adr/0005-kernel-dialect.md).

## License

Apache-2.0 (see workspace package metadata).
