# Scuzz Lang app guide

Short path from install to a Headless UI or IO app. For product thesis, design locks, and direction see [vision.md](vision.md).

## UI path

```bash
curl -fsSL https://github.com/SeanCheatham/scuzz/releases/latest/download/install.sh | sh
# put ~/.local/bin on PATH (apps need clang + make; Linux [ui] also zlib/bz2/brotli)
# from a checkout: ./scripts/install.sh  (packages scuzz + SDK → ~/.local/share/scuzz)
scuzz new myapp --ui
cd myapp
scuzz check                  # format-verify src/ + typecheck
scuzz check --help           # flags and copy-paste examples
scuzz test                   # seeds goldens/ on first run, then compares
scuzz run --headless         # writes build/snapshot.png
scuzz fmt                    # rewrite src/ (check already verifies format)
```

Default `[ui]` link uses the pinned Skia CPU prebuilt (`third_party/skia/PIN`). Checkout builds fetch it on first `ffi-skia` make. Opt out with `SCUZZ_SKIA=sk_sw`.

From a prebuilt tarball (no checkout build): `RELEASE_TGZ=scuzz-<triple>.tar.gz ./scripts/install.sh`. Produce one with `./scripts/package_release.sh`. Publish GitHub Release assets with `git tag v0.1.0 && git push origin v0.1.0` (`linux-x86_64`, `darwin-arm64`). `install.sh --help` lists flags and the `curl | sh` invocation.

`scuzz new --ui` scaffolds `scuzz.toml` with `[ui]`, a Counter-shaped `src/Main.scuzz`, and Headless-friendly defaults.

## IO path

```bash
scuzz new mycli              # no --ui → IO hello (no [ui], Skia-free link)
cd mycli
scuzz test                   # compile + SCUZZ_TESTRT=1 exit-0 smoke
scuzz run
```

Console kit: `Sys.args(): IO[List]`, `Sys.readLine(): IO[String]` (EOF → `""`; live parks on poll), `Sys.read(n): IO[String]` (n stdin bytes; fewer at EOF), `Sys.write(s): IO[Unit]` (stdout, no newline), `Sys.exec(cmd): IO[Int]` (exit code; live parks on poll), `Sys.spawn(cmd): IO[Int]` (pid), `Sys.alive(pid): IO[Int]`, `Sys.kill(pid): IO[Unit]` (SIGTERM), `IO.println`. Under `SCUZZ_TESTRT=1`, TestRuntime scripts stdin (`SCUZZ_TESTRT_STDIN` or `sz_testrt_stdin_feed`), optionally overrides argv, and captures println (still echoes to live stdout). See `examples/cli` and `examples/hello`.

## Kernel language (what you write)

- `@main def main: IO[Unit] = …` entry; top-level `def` helpers
- **`for { x = e; y <- io } yield r`** as the primary binder (pure `=`, effect `<-`). Nested `for` in `if` / lambda arms when multi-bind is needed.
- No `val` / statement blocks
- Literals: ints, strings, `()`, `s"…$x…"`, list literals `[a, b]`
- Enums + **`record Name(f1: T1, …)`** (construct `Name(…)`, match `case Name(…)`, field `p.x` — see `examples/record` / `examples/adt`). Matching an enum or record must cover every case or include `_`. `check` reports `non-exhaustive match: missing Color.Blue`. Nested payload patterns work (`case Wrap.Box(Color.Red)`). A specialized nested case without a catch-all is non-exhaustive (`missing Wrap.Box(Color.Blue)`).
- Thin **traits** / `impl` with static dispatch (`p.show()` / `p.getOrElse(0)` — including `impl Get[Int] for Point` and `impl Get[T] for Opt`; see `examples/trait`)
- Thin **generics**: `def id[T](x: T): T = x` monomorphized at call sites (`examples/generic`); generic enums/records too — `enum Opt[T]:` with `o.getOrElse(0)` / `record Box[T](x: T): def get(): T = self.x` (type methods are indented `def`s after cases or after record `:`, same shape as `impl`). Instantiation inferred from ctor args or the expected type (`examples/genum`).
- Blessed impurity only: `IO.println` / `sleep` / `fail` / `pure` / `race` / `both` / `ensure` / `timeout` / `forever` / `repeatN` / `retryN`, `Fiber.fork` / `join` / `interrupt`, `Ref.*` / `Queue.*` / `Deferred.*` (String payloads), `Resource.make` / `Resource.use` (String payload; release on success, failure, and cancel), `Stream.emit` / `emits` / `eval` / `concat` / `map` / `evalMap` / `filter` / `take` / `takeWhile` / `drop` / `dropWhile` / `find` / `exists` / `compileToList` / `drain` (String payload; `exists` is `IO[Bool]`), `Fs.*`, `Sys.args` / `Sys.readLine` / `Sys.read` / `Sys.write` / `Sys.exec` / `Sys.spawn` / `Sys.alive` / `Sys.kill` / `Sys.getenv`, `Clock.*`, `Random.*`, `Net.httpGet` / `Net.serveOnce` / `Net.serve`
- No raw side effects in View build. Taps may run `IO` through `sz_io_unsafe_run`.

The product CLI is Rust (`crates/cli`). `scuzz --help` and `scuzz <command> --help` list flags and examples. `scuzz watch` rebuilds on source change (not Flutter hot reload). `[ui]` `scuzz run --watch` keeps the process and stamp-reloads the View tree (Signals stay). IO-only `scuzz run --watch` kills and reruns the process on source change. `[ui]` build emits `build/reload.dylib`. On source change, watch recompiles it then stamps so the session `dlopen`s new machine code (`SCUZZ_UI_RELOAD_CODE`). The process rewrites `build/debug.dump` (signal store + a11y including live `View.bindText` + `[taps]` / `[fields]` / `[scrolls]` inject indices, same format as `scuzz test` goldens) on dirty pumps so agents can read live UI state, `tap N` without guessing coordinates, and see which TextField `text` / `type` / `backspace` hit (`N* placeholder="live"`). Append `tap` / `text` / `type` / `pump` / `scroll` / `backspace` lines to `build/inject.script` to drive the session (rewrite plays the whole file). `text N s` / `type N s` / `backspace N k` / `scroll N dy` target dump index N. One-token forms (`text s`, `backspace k`, `scroll 40`) still use the starred field or first Scroll. `text 0` remains payload `"0"`. `--message-format=json` applies to `check` only. That JSON is the editor protocol. `scuzz check` format-verifies `src/` then typechecks. `scuzz fmt` rewrites.

## View + Signal + Ui

Build a pure `View` tree. Hold state in `Signal`. Run a session with `Ui.run`:

```scala
@main def main: IO[Unit] =
  for {
    count = Signal.int(0)
    label = Signal.map(count, n => s"count = $n")
    _ <- Ui.run(_ => View.column(
      View.bindText(label),
      View.button("+1", _ => Signal.set(count, Signal.get(count) + 1))
    ))
  } yield ()
```

Lists: keep a `Signal.list`, render with `View.each(items)` (framework rebuilds children at layout). Wrap a scroll list in `View.expanded(…)` inside a Column so it fills leftover height. In a Row, `View.expanded` takes leftover width. `View.center(child)` fills the max slot and centers the child. `View.align(ax, ay, child)` places the child (`0` start / `1` center / `2` end). `View.stack(…)` overlays children. `View.positioned(x, y, child)` offsets a Stack child. `View.padding(n, child)` insets uniformly. `View.sized(w, h, child)` is a tight slot. `View.minSize(w, h, child)` raises min size (`0` = no floor on that axis). `View.background(color, child)` paints `color` and sizes to the child. `View.aspectRatio(rw, rh, child)` is the largest `rw:rh` box that fits incoming max. `View.fraction(wpct, hpct, child)` takes that percent of incoming max (`0` = size to child on that axis). See `examples/counter`, `examples/todo`, and `examples/nav`.

`Ui.run` under Headless: mount → optional scripted text/tap (`tap_text` in toml / env) → snapshot → unmount. Desktop stays open when `[ui].default_runtime = "desktop"`. Prefer `Ui.run(_ => view)` so construction can re-run on stamp-watch. `Ui.run(view)` still works.

## Shared packages (path dependencies)

Reusable local packages are ordinary projects without `@main`. Depend on them from `scuzz.toml`:

```toml
[dependencies]
shared = { path = "../shared" }
```

Dependency sources are merged into one program with the root (and any transitive path deps). See `examples/shared` + `examples/counter`, and [scuzz-toml.md](schemas/scuzz-toml.md). Same-package files are modules by stem (`Foo.scuzz` → `Foo`). `private def` stays in-module (default public). `import Module.name` brings a public def or enum into bare scope. Enums are namespaced like defs (same bare name in two modules is allowed). See `examples/modules` and [vision.md](vision.md#modules-and-source-shape).

## Laws, sim, and impurity

App correctness prefers **mutation, fuzzing, property-oriented laws, simulation, and determinism**. All are built into `scuzz` / the language. Not classical unit tests. See [vision.md](vision.md#laws-simulation-mutation-and-verification).

```text
src/
  Todo.scuzz           # live module: defs + laws (oracles)
  Todo.scuzz_sim       # same-name defs replace live under fuzz / TestRuntime / mutation
  Todo.scuzz_drivers   # oracle-free IO workloads composed by scuzz fuzz
```

- Prefer sim overlays for app policy (API base URL, a `Backend` value). Blessed kits stay one implementation with TestRuntime fakes on the wire.
- Laws are top-level `law name: Bool = …` in the live module (reusable nullary predicates). Apply them explicitly with `.require(pred)` (or `.require("name", pred)`). Live `build` / `run` erase laws and `.require` to the receiver.
- `.require` preserves the receiver type. Pure values residualize to `Law.check`. `IO[A]` sequences the check after the effect (`Law.assert`). Predicates may be `Bool`/`Int`, `IO[Bool]`/`IO[Int]`, `x => pred`, or a nullary `law` name.
- `where` on `def` params and `record` fields is a Bool predicate. The checker inserts `Law.check` at call / construction under the verify graph and erases it live.
- `Law.check(name, ok, value)` is identity live and panics under TestRuntime when `ok` is false. `Law.sometimes(name)` records path reachability (`Unit`, not a value method). Fuzz fails the campaign if a string-literal name is never hit.
- Observation builtins: `Law.signalInt(id)`, `Law.signalStr(id)`, `Law.signalListLen(id)`, `Law.signalListAt(id, i)`, `Law.a11yHas(needle)` (signal store + stashed a11y dump)
- Drivers are new `IO[Unit]` defs (0 or 1 `Int`/`String`/`Bool` param). `check` rejects `Law.*` and `.require` inside them. Fuzz scripts gain `drive <name> [args]` (`true`/`false` for Bool).
- No `src/test` twin trees. Only stem-paired `*.scuzz_sim` / `*.scuzz_drivers`. No third-party test or mutation frameworks.
- Example: `examples/counter` + `examples/shared` (`Shared.scuzz_sim` swaps `counterTitle`; `countLabel` uses `.require`; `noteDrive(n: Int where n >= 0)` residualizes at `plusN`; `+1` records `Law.sometimes("tappedPlus")`; `Main.scuzz` applies count / label / button / sim-title oracles through `.require` on `Ui.run`). `examples/record` uses `where` on `Point.x` / `Point.y`.

**Now:**

- `[ui]` packages: `scuzz test` is Headless **structural** goldens on the **live** graph (signal store + a11y dump + tap/field/scroll indices). PNG optional through `--pixels`. `scuzz fuzz` compiles the **verify** graph (sim + residual `.require` / laws + drivers).
- IO packages (no `[ui]`): `scuzz test` compiles and runs under `SCUZZ_TESTRT=1`, requiring exit 0
- `scuzz check` format-verifies `src/` and typechecks live + sim twins + laws + drivers + `where` + `.require` (every `law` must be applied). `--message-format=json` is the editor protocol (`check` only). `scuzz lsp` wraps that JSON over stdin/stdout (disk `check` on didOpen/didSave/didChange; no hover/completion; unsaved buffers wait for save)
- `scuzz fuzz --iters N` searches event scripts × schedules (`[ui]`) or schedule seeds only (IO-only). `[ui]` scripts that hit new `Law.sometimes` names or a new Headless `dump.txt` are kept and later iters extend those prefixes. IO-only keeps schedule seeds that hit new sometimes names and perturbs them. `--exhaust --depth N` is `[ui]` event alphabet (including `drive`) with FIFO schedule. `--replay repro.toml` restores events + optional `schedule_seed`. Oracles are residual `.require` / `Law.check`, panic/`SzError`, and campaign `Law.sometimes` reachability.
- `scuzz mutate --limit N` mutates residual `Law.check` / `Law.assert` / `.require` predicates (negate, flip `==`/`</<=/>/>=/&&/||`, swap `+`/`-` and `*`/`/`, replace `%` with `*`, drop `&&` conjuncts, swap `0`↔`1`). Each mutant gets an idle TestRuntime probe, then `--iters` fuzz scripts (`[ui]`) or schedule seeds (IO). Kill = any probe fails. Survivors mean weak or unreached oracles. `--iters 0` is idle only. No sites (for example `examples/hello`) exits 0. Do not add external mutators.
- Deterministic fakes: `TestRuntime` / `SCUZZ_TESTRT=1` for clock/random/FS/network/console in app binaries
- Put non-determinism behind blessed `IO`. Keep View construction pure.

## Examples to read next

| Example | Shows |
| --- | --- |
| `examples/hello` | IO println hello |
| `examples/cli` | `Sys.args` + `Sys.readLine` |
| `examples/counter` | `Signal.map` + `View.bindText` + `View.center` / `View.stack` / `View.positioned` / `View.sized` / `View.minSize` / `View.background` + button lambda + `Ui.run(_ => view)` factory + path dep on `shared` + `.require` / sim + `Law.sometimes` |
| `examples/shared` | Library package (`{ path = "..." }`) with helpers + optional `*.scuzz_sim` + `.require` |
| `examples/todo` | `Signal.list` + `View.each`, Column `View.expanded` scroll, Rename with `setAt`, Fs load/save + laws |
| `examples/nav` | `showWhen`, multi-page, Row `View.expanded` title, `View.align` Other page, `View.padding` Home, `View.aspectRatio` / `View.fraction` Home banner + `.require` |
| `examples/live` | Stay-open Desktop (`Ui.run`; q/Esc) |
| `examples/impurity` | Clock / Random / Fs / Net / Sys console kit |
| `examples/concurrency` | `Ref` / `Queue` / `Deferred` park under `IO.both` / `IO.race` + `Fiber.fork` / `join` / `interrupt` + `IO.forever` / `repeatN` / `retryN` + `Law.sometimes("queueParked")` |
| `examples/resource` | `Resource.make` / `use` bracket (release on success, `IO` failure, race cancel, and `IO.timeout`) + `IO.ensure` |
| `examples/stream` | `Stream.emit` / `map` / `evalMap` / `filter` / `take` / `takeWhile` / `drop` / `dropWhile` / `find` / `exists` / `compileToList` / `drain` |
| `examples/server` | `Net.serve` persistent HTTP/1.0 GET (TestRuntime drains injected paths) |
| `examples/record` | `record Point(…)` + `p.x` field access + `where` on `x` / `y` |
| `examples/trait` | `trait` / `impl` + `p.show()` / `p.getOrElse(0)` / `o.getOrElse(0)`, including `impl Get[Int] for Point` and `impl Get[T] for Opt` |
| `examples/generic` | `def id[T](…)` monomorphized generics |
| `examples/genum` | generic `enum Opt[T]` with `o.getOrElse(0)` / `record Box[T]` with `b.get()` |
| `examples/modules` | stem modules, `private def`, `import`, enum-per-module |

Edit [vision.md](vision.md) when changing GC, Skia, effects, UI boundaries, or language direction.
