# ScalUI app guide

Short path from install to a Headless UI app. For product thesis and design locks see [vision.md](vision.md); ordered next steps in [plan.md](plan.md).

## Happy path

```bash
./scripts/install.sh          # Stage-1 CLI → ~/.local/bin/scalui
scalui new myapp --ui
cd myapp
scalui test                   # seeds goldens/ on first run, then compares
scalui run --headless         # writes build/snapshot.png
scalui fmt --check            # Stage-1 pretty-printer (no canary env)
```

`scalui new --ui` scaffolds `scalui.toml`, a Counter-shaped `src/Main.scala`, and Headless-friendly defaults.

## Kernel language (what you write)

- `@main def main: IO[Unit] = …` entry; top-level `def` helpers; local `val` in blocks
- `if (cond) then else else` — branches may start with `val` for multi-binding bodies (same bindings as lambda / `def` blocks)
- Literals: ints, strings, `()`, `s"…$x…"`, list literals `[a, b]`
- Blessed impurity only: `IO.println` / `sleep` / `fail` / `pure` / `race` / `both`, `Fs.*`, `Sys.*`, `Clock.*`, `Random.*`, `Net.httpGet`
- No raw side effects in View build — taps may run `IO` via `su_io_unsafe_run`

Stage-0 Rust remains a CI canary for the compiler host; product `fmt` / `build` / `run` / `test` go through Stage 1/2.

## View + Signal + Ui

Build a pure `View` tree, hold state in `Signal`, run a session with `Ui.run`:

```scala
val count = Signal.int(0)
val root = View.column()
val _ = View.addChild(root, View.textSignal(count, "count = "))
val _ = View.addChild(root, View.button("+1", _ =>
  Signal.set(count, Signal.get(count) + 1)
))
Ui.run(root)
```

Lists: keep a `Signal.list`, render with `View.list` + `View.setTexts` / `View.clearChildren` when the model changes (replace-style, not append-only). See `examples/todo`.

`Ui.run` under Headless: mount → optional scripted text/tap (`tap_text` in toml / env) → snapshot → unmount. Window stays open when `[ui].default_runtime = "window"`.

## Tests and impurity

- `scalui test` is Headless goldens (peer to Window, not a shim)
- Deterministic fakes: `TestRuntime` / `SCALUI_TESTRT=1` for clock/random/FS/network in app binaries
- Put non-determinism behind blessed `IO`; keep View construction pure

## Examples to read next

| Example | Shows |
| --- | --- |
| `examples/counter` | Signal + button lambda + `Ui.run` |
| `examples/todo` | `Signal.list`, `View.setTexts` / `clearChildren`, Rename via `setAt`, Fs load/save |
| `examples/nav` | `showWhen`, multi-page |
| `examples/impurity` | Clock / Random / Fs / Net kit |

Then skim [plan.md](plan.md) for remaining gaps; edit [vision.md](vision.md) when changing GC, Skia, effects, UI boundaries, or language direction.
