# ScalUI app guide

Short path from install to a Headless UI or IO app. For product thesis, design locks, and direction see [vision.md](vision.md).

## Happy path (UI)

```bash
./scripts/install.sh          # packages Stage-1 + SDK → ~/.local/share/scalui; wrapper → ~/.local/bin/scalui
# ensure ~/.local/bin is on PATH (apps need clang + make; Rust not required after install)
scalui new myapp --ui
cd myapp
scalui check                  # parse + typecheck only
scalui test                   # seeds goldens/ on first run, then compares
scalui run --headless         # writes build/snapshot.png
scalui fmt --check
```

From a prebuilt tarball (no checkout build): `RELEASE_TGZ=scalui-<triple>.tar.gz ./scripts/install.sh`. Produce one with `./scripts/package_release.sh`.

`scalui new --ui` scaffolds `scalui.toml` with `[ui]`, a Counter-shaped `src/Main.scala`, and Headless-friendly defaults.

## Happy path (IO)

```bash
scalui new mycli              # no --ui → IO hello (no [ui], Skia-free link)
cd mycli
scalui test                   # compile + SCALUI_TESTRT=1 exit-0 smoke
scalui run
```

Console kit: `Sys.args(): IO[List]`, `Sys.readLine(): IO[String]` (EOF → `""`), `IO.println`. Under `SCALUI_TESTRT=1`, TestRuntime scripts stdin (`SCALUI_TESTRT_STDIN` or `su_testrt_stdin_feed`), optionally overrides argv, and captures println (still echoes to live stdout). See `examples/cli` and `examples/hello`.

## Kernel language (what you write)

- `@main def main: IO[Unit] = …` entry; top-level `def` helpers
- **`for { x = e; y <- io } yield r`** as the primary binder (pure `=`, effect `<-`). Nested `for` in `if` / lambda arms when multi-bind is needed.
- No `val` / statement blocks
- Literals: ints, strings, `()`, `s"…$x…"`, list literals `[a, b]`
- Blessed impurity only: `IO.println` / `sleep` / `fail` / `pure` / `race` / `both`, `Fs.*`, `Sys.args` / `Sys.readLine` / `Sys.exec` / `Sys.getenv`, `Clock.*`, `Random.*`, `Net.httpGet`
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

- `[ui]` packages: `scalui test` is Headless **structural** goldens (signal store + a11y dump); PNG optional via `--pixels`
- IO packages (no `[ui]`): `scalui test` compiles and runs under `SCALUI_TESTRT=1`, requiring exit 0
- `scalui check` typechecks without codegen; `--message-format=json` for agents/editors
- `scalui fuzz --iters N` / `scalui fuzz --exhaust --depth N` on TestRuntime + Headless; `--replay repro.toml` on failure (requires `[ui]`)
- Deterministic fakes: `TestRuntime` / `SCALUI_TESTRT=1` for clock/random/FS/network/console in app binaries
- Put non-determinism behind blessed `IO`; keep View construction pure

## Examples to read next

| Example | Shows |
| --- | --- |
| `examples/hello` | IO println hello |
| `examples/cli` | `Sys.args` + `Sys.readLine` |
| `examples/counter` | `Signal.map` + `View.bindText` + button lambda + `Ui.run` |
| `examples/todo` | `Signal.list` + `View.each`, Rename via `setAt`, Fs load/save |
| `examples/nav` | `showWhen`, multi-page |
| `examples/impurity` | Clock / Random / Fs / Net / Sys console kit |

Edit [vision.md](vision.md) when changing GC, Skia, effects, UI boundaries, or language direction.
