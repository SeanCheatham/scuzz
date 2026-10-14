# Scuzz Lang

Scuzz is a functional programming language for native applications.
Its syntax takes ideas from Scala. Its UI design takes ideas from Flutter.
Scuzz does not support Scala or JVM libraries.

One toolchain builds, runs, formats, checks, verifies, and packages applications.
The compiler and command-line tools are written in Scuzz.
The standard library includes IO operations and a UI toolkit.
Use properties, fuzzing, mutation, and deterministic simulation to check behavior.
Run UI applications without a display. Record input and replay it.
Use hot reload to update a running UI.

Scuzz is in active development.
See [compatibility](docs/compatibility.md) for platforms and
[known gaps](docs/gaps.md) for open work.

## A small example

This Counter displays a number and a button. Each click adds one to the number.

```scala
@main def main: IO[Unit] =
  for {
    count = Signal.make(0)
    label = Signal.map(count, n => s"count = $n")
    _ <- Ui.run(_ => View.column(
      View.text("Counter"),
      View.bindText(label),
      View.button("+1", _ => Signal.set(count, Signal.get(count) + 1))
    ))
  } yield ()
```

A `Signal` holds a value that can change. The label updates when the count changes.
A `View` describes the interface. `Ui.run` runs it through `IO`.
IO effects include user input and file access.

The commands below create this Counter with verification code.
For an application with pages and saved tasks, see [Studio](examples/studio).

## Install and run

Release packages support Linux x86-64 and macOS Apple Silicon.
Application builds need `clang` and `make`.
Linux also needs the zlib, bzip2, and OpenSSL development packages.
See [host setup](docs/developer-environment.md#required) for package commands.
The installer does not install these tools or libraries.

```bash
curl -fsSL https://github.com/SeanCheatham/scuzz/releases/latest/download/install.sh | sh
export PATH="$HOME/.local/bin:$PATH"
```

The script installs Scuzz under `~/.local/share/scuzz`.
It puts the `scuzz` command in `~/.local/bin`.
Add the `PATH` line to your shell configuration for new terminals.

Create and run a Counter app:

```bash
scuzz new myapp --ui
cd myapp
scuzz run --target headless --exec ""
```

Headless mode runs the interface without a window.
`--exec ""` sends no input events and exits after the first paint.
Omit `--exec` to keep the session active. Send input with `scuzz exec`.

To open a desktop window, set `default_runtime = "desktop"` in `[ui]` in `scuzz.toml`.
Then run `scuzz run`. Linux desktop applications need X11 and its development libraries.
Close the window to exit.

For a command-line application, omit `--ui`. Then use `scuzz run`.

## Learn more and contribute

- Open the [Counter walkthrough](https://scuzz.build).
- Run `scuzz docs start` for application instructions.
- Run `scuzz docs language`, `scuzz docs gui`, or `scuzz docs verify` for the technical manual.
- Read [product intent](docs/philosophy.md) and [open work](docs/vision.md).
- Follow [checkout setup](docs/developer-environment.md) to work on Scuzz.
- Report problems through [GitHub issues](https://github.com/SeanCheatham/scuzz/issues).

## License

[Apache-2.0](LICENSE)
