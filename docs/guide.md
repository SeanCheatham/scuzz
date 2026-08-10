# ScalUI app guide

Short path from install to a Headless UI app. For product thesis, design locks, and direction see [vision.md](vision.md).

## Happy path

```bash
./scripts/install.sh          # Stage-1 CLI → ~/.local/bin/scalui
scalui new myapp --ui
cd myapp
scalui check                  # parse + typecheck only
scalui test                   # seeds goldens/ on first run, then compares
scalui run --headless         # writes build/snapshot.png
scalui fmt --check
```

`scalui new --ui` scaffolds `scalui.toml`, a Counter-shaped `src/Main.scala`, and Headless-friendly defaults.

## Kernel language (what you write)

- `@main def main: IO[Unit] = …` entry; top-level `def` helpers
- **`for { x = e; y <- io } yield r`** as the primary binder (pure `=`, effect `<-`). Nested `for` in `if` / lambda arms when multi-bind is needed.
- No `val` / statement blocks
- Literals: ints, strings, `()`, `s"…$x…"`, list literals `[a, b]`
- Blessed impurity only: `IO.println` / `sleep` / `fail` / `pure` / `race` / `both`, `Fs.*`, `Sys.*`, `Clock.*`, `Random.*`, `Net.httpGet`
- No raw side effects in View build — taps may run `IO` via `su_io_unsafe_run`

Product `fmt` / `build` / `run` / `test` / `check` / `fuzz` go through Stage 1/2 (`compiler-scalui`). Stage-0 Rust hosts the bootstrap compiler.

## View + Signal + Ui

Build a pure `View` tree, hold state in `Signal`, run a session with `Ui.run`:

```scala
@main def main: IO[Unit] =
  for {
    count = Signal.int(0)
    label = Signal.map(count, n => s"count = $n")
    root = View.column(
      View.bindText(label),
      View.button("+1", _ => Signal.set(count, Signal.get(count) + 1))
    )
    _ <- Ui.run(root)
  } yield ()
```

Lists: keep a `Signal.list`, render with `View.each(items)` (framework rebuilds children at layout). See `examples/todo`.

`Ui.run` under Headless: mount → optional scripted text/tap (`tap_text` in toml / env) → snapshot → unmount. Window stays open when `[ui].default_runtime = "window"`.

## Tests and impurity

- `scalui test` is Headless **structural** goldens (signal store + a11y dump); PNG optional via `--pixels`
- `scalui check` typechecks without codegen; `--message-format=json` for agents/editors
- Deterministic fakes: `TestRuntime` / `SCALUI_TESTRT=1` for clock/random/FS/network in app binaries
- Put non-determinism behind blessed `IO`; keep View construction pure

## Examples to read next

| Example | Shows |
| --- | --- |
| `examples/counter` | `Signal.map` + `View.bindText` + button lambda + `Ui.run` |
| `examples/todo` | `Signal.list` + `View.each`, Rename via `setAt`, Fs load/save |
| `examples/nav` | `showWhen`, multi-page |
| `examples/impurity` | Clock / Random / Fs / Net kit |

Edit [vision.md](vision.md) when changing GC, Skia, effects, UI boundaries, or language direction.
