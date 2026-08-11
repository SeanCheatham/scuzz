# Scuzz Lang app guide

Short path from install to a Headless UI or IO app. For product thesis, design locks, and direction see [vision.md](vision.md).

## Happy path (UI)

```bash
./scripts/install.sh          # packages Stage-2 + SDK → ~/.local/share/scuzz; wrapper → ~/.local/bin/scuzz
# ensure ~/.local/bin is on PATH (apps need clang + make; Rust not required after install)
scuzz new myapp --ui
cd myapp
scuzz check                  # parse + typecheck only
scuzz test                   # seeds goldens/ on first run, then compares
scuzz run --headless         # writes build/snapshot.png
scuzz fmt --check
```

From a prebuilt tarball (no checkout build): `RELEASE_TGZ=scuzz-<triple>.tar.gz ./scripts/install.sh`. Produce one with `./scripts/package_release.sh` (always Stage 2 — Scuzz Lang builds Scuzz Lang; Stage 0 only if no bootstrap CLI is present).

`scuzz new --ui` scaffolds `scuzz.toml` with `[ui]`, a Counter-shaped `src/Main.scuzz`, and Headless-friendly defaults.

## Happy path (IO)

```bash
scuzz new mycli              # no --ui → IO hello (no [ui], Skia-free link)
cd mycli
scuzz test                   # compile + SCUZZ_TESTRT=1 exit-0 smoke
scuzz run
```

Console kit: `Sys.args(): IO[List]`, `Sys.readLine(): IO[String]` (EOF → `""`), `IO.println`. Under `SCUZZ_TESTRT=1`, TestRuntime scripts stdin (`SCUZZ_TESTRT_STDIN` or `sz_testrt_stdin_feed`), optionally overrides argv, and captures println (still echoes to live stdout). See `examples/cli` and `examples/hello`.

## Kernel language (what you write)

- `@main def main: IO[Unit] = …` entry; top-level `def` helpers
- **`for { x = e; y <- io } yield r`** as the primary binder (pure `=`, effect `<-`). Nested `for` in `if` / lambda arms when multi-bind is needed.
- No `val` / statement blocks
- Literals: ints, strings, `()`, `s"…$x…"`, list literals `[a, b]`
- Blessed impurity only: `IO.println` / `sleep` / `fail` / `pure` / `race` / `both`, `Fs.*`, `Sys.args` / `Sys.readLine` / `Sys.exec` / `Sys.getenv`, `Clock.*`, `Random.*`, `Net.httpGet`
- No raw side effects in View build — taps may run `IO` via `sz_io_unsafe_run`

Product `fmt` / `build` / `run` / `test` / `check` / `fuzz` go through Stage 1/2 (`compiler-scuzz`). Stage-0 Rust hosts the bootstrap compiler.

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

## Shared packages (path dependencies)

Reusable local packages are ordinary projects without `@main`. Depend on them from `scuzz.toml`:

```toml
[dependencies]
shared = { path = "../shared" }
```

Dependency sources are merged into one program with the root (and any transitive path deps). See `examples/shared` + `examples/counter`, and [scuzz-toml.md](schemas/scuzz-toml.md). Direction: package = crate, file = module — see [vision.md](vision.md#modules-and-source-shape).

## Laws, sim, and impurity

Direction (see [vision.md](vision.md#laws-simulation-and-verification)): app correctness is **laws** searched by `scuzz fuzz`, with stem-paired overlays — not example-based unit tests.

```text
src/
  Todo.scuzz           # live module
  Todo.scuzz_sim       # same-name defs replace live under fuzz / TestRuntime
  Todo.scuzz_laws      # pure laws over signals / a11y dump / module vals
```

- Prefer sim overlays for app policy (API base URL, a `Backend` value); blessed kits stay one implementation with TestRuntime fakes on the wire
- Laws are pure; armed only under TestRuntime / fuzz
- No `src/test` twin trees — only stem-paired `*.scuzz_sim` / `*.scuzz_laws`

**Today (until laws land):**

- `[ui]` packages: `scuzz test` is Headless **structural** goldens (signal store + a11y dump); PNG optional via `--pixels`. Goldens stay a regression face once laws exist.
- IO packages (no `[ui]`): `scuzz test` compiles and runs under `SCUZZ_TESTRT=1`, requiring exit 0
- `scuzz check` typechecks without codegen; `--message-format=json` for editors and tooling
- `scuzz fuzz --iters N` / `scuzz fuzz --exhaust --depth N` on TestRuntime + Headless; `--replay repro.toml` on failure (requires `[ui]`); oracle is still panic/`SzError`
- Deterministic fakes: `TestRuntime` / `SCUZZ_TESTRT=1` for clock/random/FS/network/console in app binaries
- Put non-determinism behind blessed `IO`; keep View construction pure

## Examples to read next

| Example | Shows |
| --- | --- |
| `examples/hello` | IO println hello |
| `examples/cli` | `Sys.args` + `Sys.readLine` |
| `examples/counter` | `Signal.map` + `View.bindText` + button lambda + `Ui.run` + path dep on `shared` |
| `examples/shared` | Library package (`{ path = "..." }`) with helpers, no `@main` |
| `examples/todo` | `Signal.list` + `View.each`, Rename via `setAt`, Fs load/save |
| `examples/nav` | `showWhen`, multi-page |
| `examples/live` | Stay-open Window (`Ui.run`; q/Esc) |
| `examples/impurity` | Clock / Random / Fs / Net / Sys console kit |

Full gallery: [README.md](../README.md#samples-gallery). Edit [vision.md](vision.md) when changing GC, Skia, effects, UI boundaries, or language direction.
