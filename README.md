# Scuzz Lang

Scuzz is a functional programming language for native applications. It takes inspiration from Scala’s concise syntax and Flutter’s approach to user interfaces.

Scuzz includes a compiler, a UI toolkit, and command-line tools to build, run, format, and test apps. It targets command-line tools, servers, desktop apps, and mobile apps. GUI apps can also run in a browser through WebAssembly.

Scuzz is in active development. See [Status and platforms](#status-and-platforms) for current support and limits.

## Why Scuzz?

- **Native applications.** Write concise, functional code that compiles to native binaries through LLVM.
- **An integrated UI toolkit.** Build interfaces with shared app code. Use hot reload to update a UI while it runs.
- **UI automation.** Run UI apps without a display. Record and replay input for debugging and testing.
- **Built-in verification.** Define properties that your code must satisfy. Use fuzzing, mutation testing, and deterministic simulation to check them.
- **One toolchain.** Use `scuzz` to build, run, format, check, test, and package apps. The standard library covers common app tasks.

Scuzz takes inspiration from Scala but does not support Scala or JVM libraries. See the [compatibility details](docs/compatibility.md).

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

A `Signal` holds a value that can change. The label updates when the count changes. A `View` describes the interface. `Ui.run` runs the interface through `IO`, which represents effects such as user interaction and file access.

Create a Counter project with the commands below. The generated app includes verification code. For a larger app with pages and saved tasks, see [Studio](examples/studio).

## Install and run

Release packages support Linux x86-64 and macOS Apple Silicon. App builds need `clang` and `make`. Linux also needs the zlib, bzip2, and OpenSSL development packages. See [host setup](docs/developer-environment.md#required) for package commands. The installer does not install these tools or libraries.

```bash
curl -fsSL https://github.com/SeanCheatham/scuzz/releases/latest/download/install.sh | sh
export PATH="$HOME/.local/bin:$PATH"
```

The script installs Scuzz under `~/.local/share/scuzz`. It puts the `scuzz` command in `~/.local/bin`. Add the `PATH` line to your shell configuration to use it in new terminals.

Create and run a Counter app:

```bash
scuzz new myapp --ui
cd myapp
scuzz run --headless
```

Headless mode runs the interface without a window and then exits. To open a desktop window, set `default_runtime = "desktop"` in the `[ui]` section of `scuzz.toml`, then run `scuzz run`. Linux desktop apps need X11 and its development libraries. Close the window to exit.

For a command-line app, omit `--ui` when you create the project, then use `scuzz run`.

## Explore the tools

Run these commands from your app directory:

```bash
scuzz fmt
scuzz check
scuzz fuzz --iterations 100
```

These commands format the source, check the code, and run a verification campaign. Fuzzing searches for property failures. Mutation testing checks whether the properties detect changes to the code.

Use `scuzz run --watch` for UI hot reload. Use `scuzz ide .` to open the bundled editor in a desktop window.

## Status and platforms

The compiler and command-line tools are written in Scuzz. Platform support has these limits:

| Platform | Current support |
| --- | --- |
| Linux | Native apps, headless UI, and X11 desktop windows. Release package for x86-64. |
| macOS | Native apps and desktop windows. Release package for Apple Silicon. |
| Android and iOS | Packaging tools and platform shells. Android needs the NDK. iOS needs Xcode. Hardware device checks remain open. |
| Browser | WebAssembly GUI apps. Native network and process effects are unavailable. Files do not persist across page reloads. |
| Windows | Desktop support is planned. |

See [compatibility](docs/compatibility.md) for platform details and [known gaps](docs/gaps.md) for open work.

## Learn more and contribute

- **Start building:** run `scuzz docs start` after installation.
- **Learn the language:** run `scuzz docs language`.
- **Build interfaces:** run `scuzz docs gui`.
- **Write properties:** run `scuzz docs verify`.
- **Browse working code:** see the [examples](examples).
- **Understand the direction:** read the [product vision](docs/vision.md).
- **Report a problem or suggest a change:** open a [GitHub issue](https://github.com/SeanCheatham/scuzz/issues).
- **Work on Scuzz:** follow the [checkout setup](docs/developer-environment.md).

## License

[Apache-2.0](LICENSE)
