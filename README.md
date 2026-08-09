# ScalUI

**Scala-inspired native language + Flutter-like UI SDK** — builtin Cats-Effect-inspired `IO`, UI-as-effect with **Headless as a first-class peer runtime**, staged self-hosting, and Skia-shaped canvas ABI.

> Not Scala 3. Not the JVM. Not a cats-effect port. Upstream Scala Native is a reference, not a dependency.

## Status

**Phase 5 — Mobile** (current)

- `UiRuntime.Mobile` peer + host shell (`crates/embedder-mobile`); Android/iOS packaging templates
- Touch pointer / scroll, soft keyboard (TextField focus), app lifecycle inject — all Headless-scriptable
- `scalui package` → `build/package/{host,android,ios}/`
- Same Counter/Todo/hello_ui examples via `SCALUI_UI_RUNTIME=mobile`
- Phase 4 self-host path remains (`./scripts/selfhost.sh`; Stage 0 Rust CLI is the feature canary)

See [docs/vision.md](docs/vision.md).

## Quick start

Requirements: Rust (stable), `clang`, `make`. Optional for Window: X11 (`libx11-dev`) + a display (or `xvfb-run`).

```bash
# Runtime + UI unit tests (includes Mobile peer / gestures)
make -C crates/runtime test

# Stage-0 canary CLI
cargo build -p scalui

# Counter (Headless snapshot, no display)
cargo run -p scalui -- run --headless examples/counter

# Golden PNG tests
cargo run -p scalui -- test examples/counter

# Mobile peer via host shell (same example binary)
env SCALUI_UI_RUNTIME=mobile SCALUI_MOBILE_SHELL=1 \
  cargo run -p scalui -- run examples/counter

# Packaging shells
cargo run -p scalui -- package examples/counter
./examples/counter/build/package/host/run.sh

# Dual-boot (Stage 1 rebuilds itself → Stage 2)
./scripts/selfhost.sh
```

Window peer (presents via X11 when `DISPLAY` is set):

```bash
xvfb-run -a env SCALUI_UI_RUNTIME=window cargo run -p scalui -- run examples/hello_ui
```

## Layout

```
docs/                 vision, compatibility, ADRs, scalui.toml schema
crates/compiler/      Stage-0 parser / typer / LLVM codegen (Rust canary)
crates/cli/           Stage-0 scalui tool (canary: build/run/test/fmt/lsp/watch/package)
crates/runtime/       C runtime (GC-v0, IO kit, Fs/Sys, View tree, Ui session)
crates/ui/            design-language home (Phase 2 widgets live in runtime C for now)
crates/ffi-skia/      sk_capi + CPU software backend
crates/embedder-desktop/  Linux X11 present for Window peer
crates/embedder-mobile/   Mobile host shell + Android/iOS packaging templates
compiler-scalui/      Stage 1/2 ScalUI compiler + CLI (release path)
scripts/selfhost.sh   dual-boot Stage 0 → 1 → 2
examples/hello/       Stage-0 IO sample
examples/fs/          Phase 4 blessed Fs.read/write
examples/hello_ui/    Phase 1 Headless label + goldens
examples/counter/     Phase 2 Counter + goldens
examples/todo/        Phase 2 Todo + goldens
examples/effects/     Phase 3 effects kit
examples/adt/         Phase 3 package + enum + match
third_party/skia/     prebuilt fetch notes
scripts/fetch_skia.sh optional Skia prebuilt fetch
```

## Kernel dialect (Stage 0 / Phase 4+)

```scala
def greet(name: String): String = Str.concat("hi:", name)

@main def main: IO[Unit] =
  Fs.read("note.txt").flatMap(s =>
    IO.println(greet(s))
  )
```

Also: top-level `def`, `if`/`else`, `List`/`Str`/`Fs`/`Sys`, packages/enums/`match` (Stage 0), UI/effects demos. See [docs/adr/0005-kernel-dialect.md](docs/adr/0005-kernel-dialect.md).

## License

Apache-2.0 (see workspace package metadata).
