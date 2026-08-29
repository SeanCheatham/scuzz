# Scuzz Lang

Scuzz is a Scala-inspired language for native CLI, server, desktop, and mobile apps. It compiles to LLVM. Builtin `IO` is ZIO-shaped. It is not a ZIO port. UI is a `Ui` effect. Headless, Desktop, and Mobile are real runtimes.

Scuzz is not Scala 3. It is not the JVM. It is not a cats-effect port. Scala Native is a reference, not a dependency. Web is not a current target.

## Goals

- **Language:** Scala-inspired subset for CLI, server, desktop, and mobile. `for` is the binder (`=` pure, `<-` effect). I/O goes through `IO`. Dense source aims for Scala-like token efficiency.
- **Runtime:** native LLVM. No VM, no Java, no classpath.
- **UI:** `View` is pure. State lives in `Signal`. `Ui.run` is the session. Canvas is Skia-shaped. Headless is a peer runtime.
- **Tooling:** one CLI (`scuzz`) for build, run, format, check, fuzz, and package. One formatter. One linter (`scuzz check`). `[ui] run --watch` is hot reload.
- **Testing:** mutation, fuzz, properties, simulation, and determinism are built in. Oracles live in source. Drivers are oracle-free. Simulation is hermetic. Do not add a third-party harness.
- **Batteries:** the language and standard kits cover common cases. No ecosystem library sprawl.

Product intent: [docs/vision.md](docs/vision.md). App path: [docs/guide.md](docs/guide.md). Checkout setup: [docs/developer-environment.md](docs/developer-environment.md).

## Install

```bash
curl -fsSL https://github.com/SeanCheatham/scuzz/releases/latest/download/install.sh | sh
```

The script installs `scuzz` under `~/.local/share/scuzz`. It puts a wrapper at `~/.local/bin/scuzz`. Put that `bin` dir on `PATH`:

```bash
export PATH="$HOME/.local/bin:$PATH"
```

Apps need `clang` and `make`. Linux `[ui]` linking also needs zlib and bzip2 (`zlib1g-dev libbz2-dev`). Pin a release with `SCUZZ_VERSION=v0.1.0` on the same `curl | sh` line. From a checkout, `./scripts/install.sh` compiles `examples/cli` with tagged `v0.2.0` `scuzz` and installs the tree.

```bash
scuzz new myapp --ui
cd myapp
scuzz test --update
scuzz test
scuzz run --headless    # scaffold default; Desktop: default_runtime = "desktop" in scuzz.toml
scuzz ide --headless .  # bundled editor; Desktop without --headless
```

`install.sh --help` lists flags and env vars.

## Example

A multi-page Desktop app. Radios switch pages with `showWhen`. Tasks live in a `Signal.list` and persist through `Fs`. The window stays open. Close the window to quit. The snippet condenses [`examples/studio`](examples/studio). The full example adds the widget catalog, properties, and drivers.

```scala
@main def main: IO[Unit] =
  Sys.getenv("SCUZZ_TODO_PATH").flatMap(envPath =>
    for {
      path = if (Str.len(envPath) == 0) "/tmp/scuzz_studio.txt" else envPath
      draft = Signal.str("")
      items = Signal.list([])
      page = Signal.int(0)
      text <- Fs.read(path).handleErrorWith(_ => IO.pure(""))
      _ = Signal.setList(items, Tasks.loadList(text))
      _ <- Ui.run(_ => View.column(
        View.row(View.radio(page, 0, "Home"), View.radio(page, 1, "Tasks")),
        View.showWhen(page, 0, View.text("Studio")),
        View.row(
          View.textField(draft, "item"),
          View.button("Add", _ => for {
            d = Str.trim(Signal.getStr(draft))
          } yield if (Str.len(d) == 0) () else Signal.setList(items, List.append(Signal.getList(items), d)))
        ),
        View.expanded(View.scroll(View.each(items, s => View.row(
          View.expanded(View.text(Tasks.itemLabel(s))),
          View.button("Del", _ => Signal.setList(items, List.filter(Signal.getList(items), x => x != s)))
        )))),
        View.button("Save", _ => Fs.write(path, Tasks.saveBody(Signal.getList(items))))
      ))
    } yield ()
  )
```

```bash
scuzz run examples/studio            # Desktop window; close the window to quit
scuzz run --headless examples/studio
```

## License

Apache-2.0
