# Scuzz Lang

Scuzz is a Scala-inspired language for native UI, CLI, and server apps. It compiles to LLVM, has builtin `IO` (ZIO-shaped, not a ZIO port), and treats UI as a `Ui` effect. Headless, Window, and Mobile are all real runtimes.

Not Scala 3, not the JVM, not a cats-effect port. Scala Native is a reference, not a dependency.

## Goals

- **Language:** Scala-inspired subset for UI, CLI, server, and mobile. `for` is the binder (`=` pure, `<-` effect). I/O goes through `IO`.
- **Runtime:** native LLVM. No VM, no Java, no classpath.
- **UI:** `View` is pure, state lives in `Signal`, `Ui.run` is the session. Skia-shaped canvas.
- **Tooling:** one CLI (`scuzz`) for build, run, format, check, fuzz, and package.
- **Testing:** laws, fuzz, simulation, and determinism are built in, not a third-party harness.

Product intent: [docs/vision.md](docs/vision.md). App path: [docs/guide.md](docs/guide.md).

## Install

```bash
curl -fsSL https://github.com/SeanCheatham/scuzz/releases/latest/download/install.sh | sh
```

Installs `scuzz` under `~/.local/share/scuzz` with a wrapper at `~/.local/bin/scuzz`. Put that `bin` dir on `PATH`:

```bash
export PATH="$HOME/.local/bin:$PATH"
```

Apps need `clang` and `make`. Linux `[ui]` linking also needs zlib, bzip2, and brotli (`zlib1g-dev libbz2-dev libbrotli-dev`). Pin a release with `SCUZZ_VERSION=v0.1.0` on the same `curl | sh` line. From a checkout, `./scripts/install.sh` packages and installs it.

```bash
scuzz new myapp --ui
cd myapp
scuzz test
scuzz run --headless    # Window: scuzz run
```

`install.sh --help` lists flags and env vars.

## Example

A file-backed todo list (`Fs`, `Signal.list`, `View`). Full source: [`examples/todo`](examples/todo).

```scala
@main def main: IO[Unit] =
  for {
    envPath <- Sys.getenv("SCUZZ_TODO_PATH")
    path = if (Str.len(envPath) == 0) "/tmp/scuzz_todo.txt" else envPath
    draft = Signal.str("")
    items = Signal.list([])
    text <- Fs.read(path).handleErrorWith(_ => IO.pure(""))
    loaded = Str.lines(text)
    _ = Signal.setList(items, if (List.isEmpty(loaded) == 1) ["milk"] else loaded)
    _ <- Ui.run(_ => View.column(
      View.text("Todo"),
      View.row(
        View.textField(draft, "item"),
        View.button("Add", _ => for {
          d = Signal.getStr(draft)
        } yield if (Str.len(d) == 0) () else for {
          xs = List.append(Signal.getList(items), d)
          _ = Signal.setList(items, xs)
        } yield Signal.setStr(draft, ""))
      ),
      View.expanded(View.scroll(View.each(items))),
      View.button("Save", _ => for {
        xs = Signal.getList(items)
        body = if (List.isEmpty(xs) == 1) "" else Str.concat(List.join(xs, "\n"), "\n")
        _ <- Fs.write(path, body)
      } yield ()),
      View.button("Clear", _ => Signal.setList(items, []))
    ))
  } yield ()
```

```bash
scuzz run --headless examples/todo
```

## License

Apache-2.0
