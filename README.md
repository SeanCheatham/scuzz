# ScalUI

**Scala-inspired native language + Flutter-like UI SDK** — builtin Cats-Effect-inspired `IO`, UI-as-effect with **Headless as a first-class peer runtime**, staged self-hosting, and Skia-shaped canvas ABI.

> Not Scala 3. Not the JVM. Not a cats-effect port. Upstream Scala Native is a reference, not a dependency.

## Status

**Phase 4 — Self-host** (current)

- Stage 1 compiler + CLI in ScalUI (`compiler-scalui/`), hosted by Stage 0 then self-rebuilt (Stage 2)
- Blessed `Fs` IO + `Sys.args` / `exec` / `getenv` for the compiler path
- Kernel dialect: `def`, `if`/`else`, `Int`/`String`/`List`, string/int ops, bound `flatMap`
- Dual-boot: `./scripts/selfhost.sh` (Stage 0 → 1 → 2 → `examples/hello`)
- Stage 0 Rust CLI remains the CI canary (`cargo run -p scalui`)

Phase 1–3 Headless UI + Counter/Todo goldens remain. See [docs/vision.md](docs/vision.md).

## Quick start

Requirements: Rust (stable), `clang`, `make`. Optional for Window: X11 (`libx11-dev`) + a display (or `xvfb-run`).

```bash
# Runtime + UI unit tests
make -C crates/runtime test

# Stage-0 canary CLI
cargo build -p scalui

# Blessed FS demo
cargo run -p scalui -- run examples/fs

# Build Stage 1 (ScalUI compiler/CLI)
cargo run -p scalui -- build --full compiler-scalui
./compiler-scalui/build/scalui run examples/hello

# Dual-boot (Stage 1 rebuilds itself → Stage 2)
./scripts/selfhost.sh

# Effects / ADT (Stage 0 canary)
cargo run -p scalui -- run examples/effects
cargo run -p scalui -- run examples/adt

# Counter (Headless snapshot, no display)
cargo run -p scalui -- run --headless examples/counter

# Golden PNG tests
cargo run -p scalui -- test examples/counter
```

Window peer (presents via X11 when `DISPLAY` is set):

```bash
xvfb-run -a env SCALUI_UI_RUNTIME=window cargo run -p scalui -- run examples/hello_ui
```

## Layout

```
docs/                 vision, compatibility, ADRs, scalui.toml schema
crates/compiler/      Stage-0 parser / typer / LLVM codegen (Rust canary)
crates/cli/           Stage-0 scalui tool (canary: build/run/test/fmt/lsp/watch)
crates/runtime/       C runtime (GC-v0, IO kit, Fs/Sys, View tree, Ui session)
crates/ui/            design-language home (Phase 2 widgets live in runtime C for now)
crates/ffi-skia/      sk_capi + CPU software backend
crates/embedder-desktop/  Linux X11 present for Window peer
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

## Kernel dialect (Stage 0 / Phase 4)

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
