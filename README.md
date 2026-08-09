# ScalUI

**Scala-inspired native language + Flutter-like UI SDK** — builtin Cats-Effect-inspired `IO`, UI-as-effect with **Headless as a first-class peer runtime**, staged self-hosting, and Skia-shaped canvas ABI.

> Not Scala 3. Not the JVM. Not a cats-effect port. Upstream Scala Native is a reference, not a dependency.

## Status

**Phase 1 — `Ui` effect + Headless** (current)

- `UiRuntime.Headless`: `mount` / `pump` / `inject` / `snapshot` (PNG)
- Thin `sk_capi` + CPU software backend (`crates/ffi-skia`); fetch script for future Skia prebuilts
- Golden PNGs under `scalui test`; `scalui run --headless` works with no display
- `UiRuntime.Window` peer interpreter (same session protocol; OS window in embedder later)
- Stage-0 builtin: `Ui.runHeadless("…")`

Phase 0 foundation (compiler, IO runtime, CLI) is complete. Phase 2 adds declarative widgets. See [docs/vision.md](docs/vision.md).

## Quick start

Requirements: Rust (stable), `clang`, `make`.

```bash
# Runtime + UI unit tests
make -C crates/runtime test

# Build the Stage-0 CLI
cargo build -p scalui

# Headless UI demo → snapshot PNG (no display)
cargo run -p scalui -- run --headless examples/hello_ui

# Golden PNG tests
cargo run -p scalui -- test examples/hello_ui

# IO hello (Phase 0)
cargo run -p scalui -- run examples/hello
```

Create a Headless UI project:

```bash
cargo run -p scalui -- new --ui counter
cargo run -p scalui -- run --headless counter
```

## Layout

```
docs/                 vision, compatibility, ADRs, scalui.toml schema
crates/compiler/      Stage-0 parser / typer / LLVM codegen (Rust)
crates/cli/           scalui tool
crates/runtime/       C runtime (GC-v0 alloc, IO fibers, Ui session)
crates/ffi-skia/      sk_capi + CPU software backend (Phase 1)
crates/embedder-desktop/  Window OS surface (stub; peer protocol in runtime)
examples/hello/       Stage-0 IO sample
examples/hello_ui/    Phase 1 Headless + goldens
third_party/skia/     prebuilt fetch notes
scripts/fetch_skia.sh optional Skia prebuilt fetch
```

## Kernel dialect (Stage 0 / Phase 1)

```scala
@main def main: IO[Unit] =
  Ui.runHeadless("Hello Headless")
```

See [docs/adr/0005-kernel-dialect.md](docs/adr/0005-kernel-dialect.md).

## License

Apache-2.0 (see workspace package metadata).
