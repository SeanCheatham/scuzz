# Scuzz Lang app guide

Short path from install to a Headless UI or IO app. For product thesis, design locks, and direction see [vision.md](vision.md).

## Happy path (UI)

```bash
./scripts/install.sh          # packages Stage-2 + SDK → ~/.local/share/scuzz; wrapper → ~/.local/bin/scuzz
# ensure ~/.local/bin is on PATH (apps need clang + make; Linux [ui] also zlib/bz2/brotli)
scuzz new myapp --ui
cd myapp
scuzz check                  # parse + typecheck only
scuzz check --help           # flags and copy-paste examples
scuzz test                   # seeds goldens/ on first run, then compares
scuzz run --headless         # writes build/snapshot.png
scuzz fmt --check
```

Default `[ui]` link uses the pinned Skia CPU prebuilt (`third_party/skia/PIN`). Checkout builds fetch it on first `ffi-skia` make; opt out with `SCUZZ_SKIA=sk_sw`.

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
- Enums + **`record Name(f1: T1, …)`** (construct `Name(…)`, match `case Name(…)`, field `p.x` — see `examples/record` / `examples/adt`)
- Thin **traits** / `impl` with static dispatch (`p.show()` — see `examples/trait`)
- Thin **generics**: `def id[T](x: T): T = x` monomorphized at call sites (`examples/generic`); generic enums/records too — `enum Opt[T]:` / `record Box[T](x: T)`, instantiation inferred from ctor args or the expected type (`examples/genum`)
- Blessed impurity only: `IO.println` / `sleep` / `fail` / `pure` / `race` / `both`, `Ref.*` / `Queue.*` / `Deferred.*` (String payloads), `Resource.make` / `Resource.use` (String payload), `Fs.*`, `Sys.args` / `Sys.readLine` / `Sys.exec` / `Sys.getenv`, `Clock.*`, `Random.*`, `Net.httpGet`
- No raw side effects in View build — taps may run `IO` via `sz_io_unsafe_run`

Product `fmt` / `build` / `run` / `test` / `check` / `fuzz` go through Stage 1/2 (`compiler-scuzz`). Stage-0 Rust hosts the bootstrap compiler. `scuzz --help` and `scuzz <command> --help` list flags and examples. `scuzz watch` rebuilds on source change (not Flutter hot reload; it does not preserve `Signal` state). `--message-format=json` applies to `check` only — that JSON is the editor protocol.

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

Lists: keep a `Signal.list`, render with `View.each(items)` (framework rebuilds children at layout). Wrap a scroll list in `View.expanded(…)` inside a Column so it fills leftover height; in a Row, `View.expanded` takes leftover width. `View.center(child)` fills the max slot and centers the child; `View.align(ax, ay, child)` places the child (`0` start / `1` center / `2` end); `View.stack(…)` overlays children; `View.positioned(x, y, child)` offsets a Stack child; `View.padding(n, child)` insets uniformly; `View.sized(w, h, child)` is a tight slot; `View.minSize(w, h, child)` raises min size (`0` = no floor on that axis); `View.background(color, child)` paints `color` and sizes to the child; `View.aspectRatio(rw, rh, child)` is the largest `rw:rh` box that fits incoming max; `View.fraction(wpct, hpct, child)` takes that percent of incoming max (`0` = size to child on that axis). See `examples/counter`, `examples/todo`, and `examples/nav`.

`Ui.run` under Headless: mount → optional scripted text/tap (`tap_text` in toml / env) → snapshot → unmount. Window stays open when `[ui].default_runtime = "window"`.

## Shared packages (path dependencies)

Reusable local packages are ordinary projects without `@main`. Depend on them from `scuzz.toml`:

```toml
[dependencies]
shared = { path = "../shared" }
```

Dependency sources are merged into one program with the root (and any transitive path deps). See `examples/shared` + `examples/counter`, and [scuzz-toml.md](schemas/scuzz-toml.md). Same-package files are modules by stem (`Foo.scuzz` → `Foo`); `private def` stays in-module (default public); `import Module.name` brings a public def or enum into bare scope. Enums are namespaced like defs (same bare name in two modules is allowed). See `examples/modules` and [vision.md](vision.md#modules-and-source-shape).

## Laws, sim, and impurity

App correctness is **laws** searched by `scuzz fuzz`, with stem-paired overlays — not example-based unit tests (see [vision.md](vision.md#laws-simulation-and-verification)).

```text
src/
  Todo.scuzz           # live module
  Todo.scuzz_sim       # same-name defs replace live under fuzz / TestRuntime
  Todo.scuzz_laws      # pure laws over signals / a11y dump / module vals
```

- Prefer sim overlays for app policy (API base URL, a `Backend` value); blessed kits stay one implementation with TestRuntime fakes on the wire
- Laws are nullary pure `Bool`/`Int` defs; residual `Law.assert` runs only under `SCUZZ_TESTRT=1` (fuzz)
- Observation builtins: `Law.signalInt(id)`, `Law.a11yHas(needle)` (signal store + stashed a11y dump)
- No `src/test` twin trees — only stem-paired `*.scuzz_sim` / `*.scuzz_laws`
- Example: `examples/counter` + `examples/shared` (`Shared.scuzz_sim` swaps `counterTitle`; `Main.scuzz_laws` checks count / button / sim title)

**Today:**

- `[ui]` packages: `scuzz test` is Headless **structural** goldens on the **live** graph (signal store + a11y dump); PNG optional via `--pixels`. `scuzz fuzz` compiles the **verify** graph (sim + residual laws).
- IO packages (no `[ui]`): `scuzz test` compiles and runs under `SCUZZ_TESTRT=1`, requiring exit 0
- `scuzz check` typechecks live + sim twins + laws; `--message-format=json` is the editor protocol (`check` only; LSP wraps this)
- `scuzz fuzz --iters N` searches event scripts × schedules (`[ui]`) or schedule seeds only (IO-only); `--exhaust --depth N` is `[ui]` event alphabet with FIFO schedule; `--replay repro.toml` restores events + optional `schedule_seed`; oracle is residual laws + panic/`SzError`
- Deterministic fakes: `TestRuntime` / `SCUZZ_TESTRT=1` for clock/random/FS/network/console in app binaries
- Put non-determinism behind blessed `IO`; keep View construction pure

## Examples to read next

| Example | Shows |
| --- | --- |
| `examples/hello` | IO println hello |
| `examples/cli` | `Sys.args` + `Sys.readLine` |
| `examples/counter` | `Signal.map` + `View.bindText` + `View.center` / `View.stack` / `View.positioned` / `View.sized` / `View.minSize` / `View.background` + button lambda + `Ui.run` + path dep on `shared` + laws/sim |
| `examples/shared` | Library package (`{ path = "..." }`) with helpers + optional `*.scuzz_sim` |
| `examples/todo` | `Signal.list` + `View.each`, Column `View.expanded` scroll, Rename via `setAt`, Fs load/save |
| `examples/nav` | `showWhen`, multi-page, Row `View.expanded` title, `View.align` Other page, `View.padding` Home, `View.aspectRatio` / `View.fraction` Home banner |
| `examples/live` | Stay-open Window (`Ui.run`; q/Esc) |
| `examples/impurity` | Clock / Random / Fs / Net / Sys console kit |
| `examples/concurrency` | `Ref` / `Queue` / `Deferred` park under `IO.both` / `IO.race` |
| `examples/resource` | `Resource.make` / `use` bracket (release on success and `IO` failure) |
| `examples/record` | `record Point(…)` + `p.x` field access |
| `examples/trait` | `trait` / `impl` + `p.show()` static dispatch |
| `examples/generic` | `def id[T](…)` monomorphized generics |
| `examples/modules` | stem modules, `private def`, `import`, enum-per-module |

Full gallery: [README.md](../README.md#samples-gallery). Edit [vision.md](vision.md) when changing GC, Skia, effects, UI boundaries, or language direction.
