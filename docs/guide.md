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

Console kit: `Sys.args(): IO[List]`, `Sys.readLine(): IO[String]` (EOF → `""`; live parks on poll), `Sys.read(n): IO[String]` (n stdin bytes; fewer at EOF), `Sys.write(s): IO[Unit]` (stdout, no newline), `Sys.exec(cmd): IO[Int]` (exit code; live parks on poll), `Sys.spawn(cmd): IO[Int]` (pid), `Sys.alive(pid): IO[Int]`, `Sys.kill(pid): IO[Unit]` (SIGTERM), `IO.println`. Under `SCUZZ_TESTRT=1`, TestRuntime scripts stdin (`SCUZZ_TESTRT_STDIN` or `sz_testrt_stdin_feed`), optionally overrides argv, and captures println (still echoes to live stdout). See `examples/io` and `examples/hello`.

## Kernel language (what you write)

- `@main def main: IO[Unit] = …` entry; top-level `def` helpers
- **`for { x = e; y <- io } yield r`** as the primary binder (pure `=`, effect `<-`). Nested `for` in `if` / lambda arms when multi-bind is needed.
- No `val` / statement blocks
- Literals: ints, strings, `()`, `s"…$x…"`, list literals `[a, b]`
- Enums + **`record Name(f1: T1, …)`** (construct `Name(…)`, match `case Name(…)`, field `p.x` — see `examples/kernel`). Matching an enum or record must cover every case or include `_`. `check` reports `non-exhaustive match: missing Color.Blue`. Nested payload patterns work (`case Wrap.Box(Color.Red)`). A specialized nested case without a catch-all is non-exhaustive (`missing Wrap.Box(Color.Blue)`).
- Thin **traits** / `impl` with static dispatch (`p.show()` / `p.getOrElse(0)` — including `impl Get[Int] for Point` and `impl Get[T] for Opt`; see `examples/kernel`)
- Thin **generics**: `def id[T](x: T): T = x` monomorphized at call sites (`examples/kernel`); generic enums/records too — `enum Opt[T]:` with `o.getOrElse(0)` / `record Box[T](x: T): def get(): T = self.x` (type methods are indented `def`s after cases or after record `:`, same shape as `impl`). Instantiation inferred from ctor args or the expected type (`examples/kernel`).
- Blessed impurity only: `IO.println` / `sleep` / `fail` / `pure` / `race` / `both` / `ensure` / `timeout` / `forever` / `repeatN` / `retryN`, `Fiber.fork` / `join` / `interrupt`, `Ref.*` / `Queue.*` / `Deferred.*` (String payloads), `Resource.make` / `Resource.use` (String payload; release on success, failure, and cancel), `Stream.emit` / `emits` / `eval` / `concat` / `map` / `evalMap` / `filter` / `take` / `takeWhile` / `drop` / `dropWhile` / `find` / `exists` / `compileToList` / `drain` (String payload; `exists` is `IO[Bool]`), `Fs.*`, `Sys.args` / `Sys.readLine` / `Sys.read` / `Sys.write` / `Sys.exec` / `Sys.spawn` / `Sys.alive` / `Sys.kill` / `Sys.getenv`, `Clock.*`, `Random.*`, `Net.httpGet` / `Net.serveOnce` / `Net.serve`
- No raw side effects in View build. Taps may run `IO` through `sz_io_unsafe_run`.

The product CLI is Rust (`crates/cli`). `scuzz --help` and `scuzz <command> --help` list flags and examples. `scuzz watch` rebuilds on source change (not Flutter hot reload). `[ui]` `scuzz run --watch` keeps the process and stamp-reloads the View tree (Signals stay). IO-only `scuzz run --watch` kills and reruns the process on source change. `[ui]` build emits `build/reload.dylib`. On source change, watch recompiles it then stamps so the session `dlopen`s new machine code (`SCUZZ_UI_RELOAD_CODE`). The process rewrites `build/debug.dump` (signal store + a11y including live `View.bindText` + `[taps]` with frames / `[fields]` / `[scrolls]` inject indices, same format as `scuzz test` goldens; `[last_hit]` after a TAP) on dirty pumps so agents can read live UI state, `tap N` without guessing coordinates, and see which TextField `text` / `type` / `backspace` hit (`N* placeholder="live"`). Append `tap` / `xy` / `text` / `type` / `pump` / `scroll` / `backspace` lines to `build/inject.script` to drive the session (rewrite plays the whole file). `text N s` / `type N s` / `backspace N k` / `scroll N dy` target dump index N. One-token forms (`text s`, `backspace k`, `scroll 40`) still use the starred field or first Scroll. `text 0` remains payload `"0"`. `xy x y` injects a TAP at a logical point; a miss does not panic. Desktop/Mobile `scuzz run` records live OS clicks and keys to `build/record.script` (not `inject.script`) and writes `build/debug.dump`. Replay with `scuzz run --headless --script build/record.script --dump build/debug.dump`. `--message-format=json` applies to `check` only. That JSON is the editor protocol. `scuzz check` format-verifies `src/` then typechecks. `scuzz fmt` rewrites.

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

Lists: keep a `Signal.list`, render with `View.each(items)` (framework rebuilds `- item` texts at layout). `View.each(items, s => view)` builds one child per list string. `List.filter(xs, pred)` keeps strings for which `pred` is true. `List.map(xs, f)` builds a new list of strings. `List.setAt(xs, i, v)` replaces the string at `i`. An index outside the list leaves the list. `Str.startsWith(s, prefix)` is `1` when `s` begins with `prefix`. `Str.trim(s)` drops leading and trailing ASCII space, tab, CR, and LF. Kit lambdas bind the list or stream item as String. `Signal.map` binds Int. The lambda body must return the kit result: `View` (`View.each` / `Ui.run`), `Bool` (filter), `String` (map; Int stringifies), or `IO` (`Resource` / `Net` / `Stream.evalMap`). `View.wrap(…)` lays out children left to right. A child that does not fit the remaining width starts a new run. Wrap sizes to the runs. `View.grid(n, …)` lays out children in `n` columns (`n` < `1` is one column). A new row starts after `n` shown children. Bounded width uses equal column slots. Height sizes to the rows. `View.scroll(child)` pans on y. Content lays out with unbounded height. Wrap a scroll list in `View.expanded(…)` inside a Column so it fills leftover height. `View.scrollH(child)` pans on x. Content lays out with unbounded width. Height sizes to the child. `scroll N dy` pans that scroll on its axis. In a Row, `View.expanded` takes leftover width. Scroll content is unbounded on the pan axis, so a Row inside a List keeps an intrinsic height. Expanded flex slots are tight. `View.stretch(child)` tightens the cross axis in a Column (width) or Row (height); the main axis stays intrinsic. Column and row do not stretch non-flex children unless wrapped in `View.stretch`. `View.center(child)` fills the max slot and centers the child. `View.align(ax, ay, child)` places the child (`0` start / `1` center / `2` end). `View.stack(…)` overlays children. `View.positioned(x, y, child)` offsets a Stack child. `View.padding(n, child)` insets uniformly. `View.sized(w, h, child)` is a tight slot. `View.minSize(w, h, child)` raises min size (`0` = no floor on that axis). `View.maxSize(w, h, child)` lowers max size (`0` = no cap on that axis). Incoming max still wins when tighter. `View.clip(child)` clips paint to the clip frame. Scroll uses the same clip. Do not add constraint-overflow dumps. `View.opacity(pct, child)` scales paint alpha (`0` = transparent, `100` = opaque). Nested opacity multiplies. `View.maxLines(n, child)` keeps at most `n` wrapped text lines (`0` = no cap). Nested caps take the tighter value. A11y still dumps the full string. Buttons and TextField stay one line. `View.ellipsis(child)` keeps extra lines off the paint. Without a positive `maxLines` it keeps one line. With `maxLines` it paints `...` on the last visible line when more text remains. A11y still dumps the full string. `View.textColor(color, child)` paints `View.text` / `View.bindText` with `color`. Nested `textColor` uses the inner color. Buttons and TextField stay on the theme. `View.gap(n, child)` sets Column/Row/Wrap/Grid/List spacing to `n` px (`0` = none). Nested `gap` uses the inner value. Without `View.gap`, Column/Row/Wrap/Grid/List use the theme gap. `View.fontSize(n, child)` sets `View.text` / `View.bindText` measure and paint size (`n` px, min `1`). Nested `fontSize` uses the inner size. Buttons and TextField stay on the theme font. `View.border(n, color, child)` paints an `n` px stroke in `color` inside the child frame (`0` = none). Nested border paints both. `View.radius(n, child)` clips paint to a rounded rect of `n` px (`0` = square). Nested radius uses the inner value. `View.checkbox(sig, label)` paints a box plus `label`. Tap flips `sig` between `0` and `1`. A11y dumps `checkbox:label=0` or `checkbox:label=1`. `[taps]` includes checkboxes so `tap N` hits them. `View.radio(sig, value, label)` paints a mark plus `label`. Tap writes `value` into `sig`. Radios that share `sig` form a group. A11y dumps `radio:label=0` or `radio:label=1`. `[taps]` includes radios. `View.slider(sig)` paints a track. Tap or pointer drag writes `sig` from the hit x, clamped `0`–`100`. A11y dumps `slider:n`. `[taps]` includes sliders so `tap N` hits them. `View.progress(sig)` paints a bar from `sig`, clamped `0`–`100`. It is not a tap target. A11y dumps `progress:n`. `View.switch(sig, label)` paints a track plus `label`. Tap flips `sig` between `0` and `1`. A11y dumps `switch:label=0` or `switch:label=1`. `[taps]` includes switches. `View.chip(sig, label)` paints `label` as a chip. Tap flips `sig` between `0` and `1`. A11y dumps `chip:label=0` or `chip:label=1`. `[taps]` includes chips. `View.listTile(title)` is a full-width title row. `View.listTile(title, trailing)` places `trailing` on the right. It is not a tap target. A11y dumps `listtile:title`. `View.badge(sig, child)` paints a count from `sig` on the top-right of `child`. It sizes to the child. It is not a tap target. A11y dumps `badge:n`. `View.card(child)` paints a surface pad and a 1 px border around `child`. It is not a tap target. A11y dumps `card:card`. `View.divider()` paints a muted hairline in an 8 px slot. It is not a tap target. A11y dumps `divider:divider`. `View.expansionTile(sig, title, child)` paints a full-width header. Tap flips `sig` between `0` and `1`. `child` shows when `sig` is not `0`. A11y dumps `expansion:title=0` or `expansion:title=1`. `[taps]` includes expansion tiles. `View.iconButton(label, onTap)` paints a square control with `label`. Tap runs `onTap`. A11y dumps `iconbutton:label`. `[taps]` includes icon buttons. `View.verticalDivider()` paints a muted hairline in an 8 px slot. Height is the control height. It is not a tap target. A11y dumps `vdiv:vdiv`. `View.circularProgress(sig)` paints a square ring from `sig`, clamped `0`–`100`. It is not a tap target. A11y dumps `circular:n`. `View.avatar(label)` paints a disc with `label`. Height is the control height. It is not a tap target. A11y dumps `avatar:label`. `View.checkboxListTile(sig, title)` paints a full-width row with a box plus `title`. Tap flips `sig` between `0` and `1`. A11y dumps `checktile:title=0` or `checktile:title=1`. `[taps]` includes checkbox list tiles. `View.switchListTile(sig, title)` paints a full-width row with `title` and a trailing switch. Tap flips `sig` between `0` and `1`. A11y dumps `switchtile:title=0` or `switchtile:title=1`. `[taps]` includes switch list tiles. `Color.rgb(r, g, b)` is opaque ARGB. `Color.rgba(r, g, b, a)` sets alpha (`0`–`255`). `View.ignorePointer(child)` skips hit-test on the subtree so taps pass through. `View.absorbPointer(child)` blocks taps without firing the child. `View.excludeSemantics(child)` omits the subtree from the a11y dump and from TextField collect. Taps still hit the child. `View.background(color, child)` paints `color` and sizes to the child. `View.text` wraps at newlines and at incoming max width; height sizes up. Buttons and TextField stay one line. `View.aspectRatio(rw, rh, child)` is the largest `rw:rh` box that fits incoming max. `View.fraction(wpct, hpct, child)` takes that percent of incoming max (`0` = size to child on that axis). See `examples/counter` and `examples/studio`.

`Ui.run` under Headless: mount → optional scripted text/tap (`tap_text` in toml / env) → snapshot → unmount. Desktop stays open when `[ui].default_runtime = "desktop"` (`examples/studio`; q or Esc quits). Prefer `Ui.run(_ => view)` so construction can re-run on stamp-watch. `Ui.run(view)` still works. After a Desktop session, replay the recorded script Headless:

```bash
scuzz run examples/studio
scuzz run --headless --script examples/studio/build/record.script --dump examples/studio/build/debug.dump examples/studio
```

Read `[last_hit]` and `[signals]` in the dump to see whether a click hit a control.

## Shared packages (path dependencies)

Reusable local packages are ordinary projects without `@main`. Depend on them from `scuzz.toml`:

```toml
[dependencies]
shared = { path = "../shared" }
```

Dependency sources are merged into one program with the root (and any transitive path deps). See `examples/shared` + `examples/counter`, and [scuzz-toml.md](schemas/scuzz-toml.md). Same-package files are modules by stem (`Foo.scuzz` → `Foo`). `private def` stays in-module (default public). `import Module.name` brings a public def or enum into bare scope. Enums are namespaced like defs (same bare name in two modules is allowed). See `examples/kernel` and [vision.md](vision.md#modules-and-source-shape).

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
- Example: `examples/counter` + `examples/shared` (`Shared.scuzz_sim` swaps `counterTitle`; `countLabel` uses `.require`; `noteDrive(n: Int where n >= 0)` residualizes at `plusN`; `+1` records `Law.sometimes("tappedPlus")`; `Main.scuzz` applies count / label / button / sim-title oracles through `.require` on `Ui.run`). `examples/kernel` uses `where` on `Point.x` / `Point.y`.

**Now:**

- `[ui]` packages: `scuzz test` is Headless **structural** goldens on the **live** graph (signal store + a11y dump + tap/field/scroll indices). PNG optional through `--pixels`. `scuzz fuzz` compiles the **verify** graph (sim + residual `.require` / laws + drivers).
- IO packages (no `[ui]`): `scuzz test` compiles and runs under `SCUZZ_TESTRT=1`, requiring exit 0
- `scuzz check` format-verifies `src/` and typechecks live + sim twins + laws + drivers + `where` + `.require` (every `law` must be applied). `--message-format=json` is the editor protocol (`check` only). `scuzz lsp` wraps that JSON over stdin/stdout (open buffers overlay disk on didOpen/didChange; didClose uses disk; hover, completion, and definition use the same parse)
- `scuzz fuzz --iters N` searches event scripts × schedules (`[ui]`) or schedule seeds only (IO-only). `[ui]` scripts that hit new `Law.sometimes` names or a new Headless `dump.txt` are kept and later iters extend those prefixes. IO-only keeps schedule seeds that hit new sometimes names and perturbs them. `--exhaust --depth N` is `[ui]` event alphabet (including `drive`) with FIFO schedule. `--replay repro.toml` restores events + optional `schedule_seed`. Oracles are residual `.require` / `Law.check`, panic/`SzError`, and campaign `Law.sometimes` reachability.
- `scuzz mutate --limit N` mutates residual `Law.check` / `Law.assert` / `.require` predicates (negate, flip `==`/`</<=/>/>=/&&/||`, swap `+`/`-` and `*`/`/`, replace `%` with `*`, drop `&&` conjuncts, swap `0`↔`1`). Each mutant gets an idle TestRuntime probe, then `--iters` fuzz scripts (`[ui]`) or schedule seeds (IO). Kill = any probe fails. Survivors mean weak or unreached oracles. `--iters 0` is idle only. No sites (for example `examples/hello`) exits 0. Do not add external mutators.
- Deterministic fakes: `TestRuntime` / `SCUZZ_TESTRT=1` for clock/random/FS/network/console in app binaries
- Put non-determinism behind blessed `IO`. Keep View construction pure.

## Examples to read next

| Example | Shows |
| --- | --- |
| `examples/hello` | IO println hello |
| `examples/counter` | Small UI: `Signal.map` + `View.bindText` + layout widgets + button lambda + `Ui.run(_ => view)` factory + path dep on `shared` + `.require` / sim + `Law.sometimes` |
| `examples/shared` | Library package (`{ path = "..." }`) with helpers + optional `*.scuzz_sim` + `.require` |
| `examples/studio` | Desktop stay-open app: `showWhen` pages, `Signal.list` + `View.each`, Done/Add/Del/Rename, `View.checkbox` / `View.radio` / `View.slider` / `View.progress` / `View.switch` / `View.chip` / `View.listTile` / `View.badge` / `View.card` / `View.divider` / `View.expansionTile` / `View.iconButton` / `View.verticalDivider` / `View.circularProgress` / `View.avatar` / `View.checkboxListTile` / `View.switchListTile` / `View.scrollH` / `View.grid`, Fs load/save, `record` / `trait` / stem modules, laws + drivers / `Law.sometimes`. `scuzz run` opens a window (q or Esc quits). `--headless` snapshots. |
| `examples/kernel` | Language constructs: enums, `record` + `where`, `trait` / `impl`, generics, generic enum/record, stem modules, `private def`, `import` |
| `examples/io` | Blessed kits: Clock / Fs / `Impurity.runKit` / `Ref` / `Queue` / `Deferred` / `Fiber` / `Resource` / `Stream` / `Net.serve` (serve under `scuzz test`) |

Edit [vision.md](vision.md) when changing GC, Skia, effects, UI boundaries, or language direction.
