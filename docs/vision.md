# Scuzz Lang vision

Scuzz Lang is a Flutter-shaped product with a Scala-inspired language. It is not a Scala 3, Scala Native, or Maven citizen.

One doc for product intent, design locks, language direction, and open work. Keep/cut tables: [`compatibility.md`](compatibility.md). Manifest schema: [`schemas/scuzz-toml.md`](schemas/scuzz-toml.md). App path and surface catalogs: [`guide.md`](guide.md). Future empirical pre-optimization: [`optimization.md`](optimization.md).

Edit this file when a decision or next-step order changes.

## Thesis

- **Language**: purposeful Scala-inspired subset for native UI, CLI, server, and mobile apps, with **built-in effect/IO/Streaming** (ZIO-inspired, not a ZIO or cats/fs2 port). Aim: denser expr dialect (`for` as primary binder). See [Language direction](#language-direction).
- **Runtime**: custom native (LLVM). Native binaries, not a VM. No JVM. No Java interop. No classpath/Maven.
- **UI**: a primary product path, not the only one (Dart-shaped: GUI is first-class; so are CLI and server). One design language + Skia, as a **`Ui` effect** with Headless/Desktop/Mobile interpreters. Headless is a product runtime (agents, CI), not a test-only shim.
- **Tooling**: one opinionated CLI (`scuzz`) — compile, link, assets, watch, packaging, format, check, and the whole verification stack. One formatter. One check surface. One testing strategy. Mutation, fuzzing, property/laws, simulation, and determinism are **first-class in the language and `scuzz`**. They are not a third-party harness. Static hygiene is `scuzz check` (format-verify + typecheck; further lints emit here; no `lint` subcommand). `scuzz fmt` rewrites. Compiler, CLI, and toolchain are **Rust** (`crates/compiler`, `crates/cli`).
- **Language proof**: examples that exercise the surface (`examples/`), not a self-hosted compiler.
- **AI-Friendly**: Headless, hot reload, and debugging tools aid agents. Headless is a peer runtime. `watch` only rebuilds. `[ui] run --watch` stamp-reloads Views, writes `build/debug.dump` (including `[taps]` frames / `[fields]` live strings / `[scrolls]` / `[last_hit]` after a TAP, and live `View.bindText`), and plays `build/inject.script`. Desktop/Mobile `scuzz run` records live OS input to `build/record.script` and writes `build/debug.dump`. Replay Headless with `scuzz run --headless --script build/record.script --dump build/debug.dump`. `[ui]` build emits `build/reload.dylib`. Stamp-watch `dlopen`s it so a source View-label change appears live (Signals stay). IO-only `run --watch` kills and reruns on source change. `scuzz lsp` wraps `scuzz check` JSON diagnostics.

Upstream Scala Native is a *reference*, not a dependency. Divergence is intentional.

## Defaults

| Topic | Choice |
| --- | --- |
| UI testing / CI | In-source laws + composed drivers through `scuzz fuzz` (primary); mutation as law-strength gate; structural goldens as regression face; PNG optional through `--pixels` |
| Static hygiene | `scuzz check` (format-verify + typecheck; lints on this command; no `lint` subcommand). `scuzz fmt` rewrites |
| Codegen | LLVM IR |
| Renderer (v0) | Skia through thin C ABI; Impeller deferred |
| Build tool | DIY Mill/Cargo-like: `scuzz` (not sbt/Maven) |
| Effects | Language + runtime builtins |
| Impurity | All nondeterminism / external I/O through blessed `IO`; no app-level `IO.delay` escape hatch |
| Tests | One built-in strategy: **mutation + fuzz + laws (property) + sim + determinism** (TestRuntime). Oracles live **in source** (laws, inline checks, refinements). **Drivers** are oracle-free workloads the fuzzer composes. No classical unit-test culture. No external test frameworks |
| Modules | `scuzz.toml` package = crate; `Foo.scuzz` = module (not JVM packages) |
| Toolchain | Rust (`crates/cli`); one compiler |
| UI model | Pure `View` + effectful `Ui` session (`mount` / `pump` / `inject` / `snapshot`) |

## What Scuzz Lang is not

- Not Scala 3, not the JVM, not Scala.js
- Not a ZIO library port
- Not a cats / cats-effect / Typelevel port
- Not SwiftUI / UIKit / WinUI wrappers
- Not “every widget rebuild is an `IO`” (`View` build stays sync/pure)
- Not imperative View trees (`View.addChild`); nested constructors only
- Not classical / example-based unit-test culture (`src/test`, Mockito, assert-equal fixtures, third-party test runners) for apps. Use **mutation + fuzz + laws + sim + determinism**, all in `scuzz`, instead. Examples survive only as oracle-free **drivers**. The objection is example-based *oracles*, not examples-as-workloads
- Not Flutter DevTools / VM patching. In-process reload, a live structural dump, and stamp-driven inject are in. `watch` only rebuilds. IO-only `run --watch` kills and reruns.
- Not an sbt / Gradle / `pubspec` plugin DSL (`scuzz.toml` is data)
- Not Flutter platform channels
- Not a self-hosted compiler as a product bar. `scuzz` is Rust. Scuzz programs are apps and examples.

## Success bars

**v0** — Install CLI (`curl …/install.sh | sh`, or checkout `./scripts/install.sh`) → `scuzz new` (IO) or `scuzz new --ui` (Counter as `View` + builtin `IO`) → `scuzz test` and `scuzz run` (`--headless` for UI). Desktop when available. Language `Resource` / `Stream` / `Net.serve` ship (`examples/io`).

**v1** — Shipped `scuzz` is the Rust CLI (GitHub Releases; `package_release.sh` / `install.sh`). Kernel surface is proven by examples. `fuzz` / `mutate` live on that CLI.

## Decisions

### Product name

Brand in prose: **Scuzz Lang** (short form **Scuzz**). CLI / cargo package `scuzz`; compiler crate `scuzz-compiler`; manifest `scuzz.toml`; sources `*.scuzz` (plus stem-paired `*.scuzz_sim` / `*.scuzz_drivers`); C ABI `sz_` / `Sz*` / `SZ_*`. No dual names or legacy aliases.

### Tooling

One CLI. One typer. One formatter. One check surface. One testing strategy. No second analyze frontend. No `*.g.scuzz` codegen. No `src/test` runner. No bolted-on mutation/fuzz/property ecosystems.

- **Watch** rebuilds when sources or `scuzz.toml` change. It does not patch running machine code. `[ui]` `run --watch` recompiles `build/reload.dylib`, stamps, and swaps the View tree without resetting Signals (see [`guide.md`](guide.md)). IO-only `run --watch` kills and reruns the process on source change. Do not document `watch` as hot reload.
- **Static hygiene** is `scuzz check`. `scuzz fmt` rewrites. No `lint` subcommand.
- **Verification** is built into `scuzz` and the language (laws, sim overlays, deterministic TestRuntime, fuzz search, mutation). Not optional crates or Maven/npm test plugins.
- **JSON diagnostics** (`scuzz check --message-format=json`) are the editor protocol. `scuzz lsp` wraps that, overlays open buffers, and serves hover, completion, and definition from the same parse. Do not grow a second typer or schema.
- **`scuzz.toml` is data** — package, path deps, `[ui]`. No plugin DSL. No `build.scuzz` hooks. Unknown keys rejected. Do not add `[plugins]`.
- **Fingerprint** (incremental): miss → rebuild. No `scuzz clean` ritual.
- **Missing tools:** fail on the first missing tool with one install line. No `flutter doctor` mega-checklist.
- **`scuzz package` shells** are copy-patched templates (`shells/android`, `shells/ios`), not a Gradle/CocoaPods API.

### GC (v0)

libc `malloc`/`free` through `sz_alloc` / `sz_free`. No moving collector yet. Clear ownership frees strings/IO/`Resource`/Views, unshared `Signal.list` cons spines, and list string heads on signal free. Panic may leak. Revisit when long-lived interactive graphs demand it.

### Skia

No vendored Skia tree. Thin `sk_capi` (measure + draw). **Default UI backend** is the pinned Skia CPU prebuilt (`third_party/skia/PIN` → `scripts/fetch_skia.sh`). In-tree `sk_sw` is the explicit opt-out (`SCUZZ_SKIA=sk_sw`). Impeller deferred (GPU presenters later; must not change `Ui` session or logical goldens). Callers depend only on `sk_capi.h`.

### IO and impurity

One failure channel: `SzError` on `IO`. Blessed kits only. No app-level `IO.delay`. Cooperative single-threaded fibers (no OS threads for IO). `sleep` / empty `Queue.take` / incomplete `Deferred.get` / fd poll park. Cancel (race loser / `IO.timeout` / `Fiber.interrupt`) runs `IO.ensure` / `Resource` finalizers. `Fiber.fork` starts a supervised child (`join` parks; unjoined children cancel when the root completes). `IO.timeout(ms, inner)` is a blessed race of sleep-fail vs inner and keeps inner's `IO[T]`. `IO.forever(inner)` reruns until failure or cancel (`IO[Unit]`, never succeeds). `IO.repeatN(n, inner)` runs inner once plus `n` extra times (last success). `IO.retryN(n, inner)` retries on failure up to `n` extra times. TestRuntime (`SCUZZ_TESTRT=1`) fakes clock/random/FS/net/console. Surface catalogs: [`guide.md`](guide.md). Panics abort through `sz_panic`.

### `Ui` vs `View`

| Layer | Role | Purity |
| --- | --- | --- |
| **`View`** | Widget tree | Sync/pure `build` |
| **`Ui` / `UiSession`** | `mount` / `pump` / `inject` / `snapshot` | Effectful (`UiRuntime`) |

Headless is a **peer** of Desktop/Mobile — product runtime for agents and CI. Frame boundary is `pump`. World effects stay blessed `IO`. Bridge into signals through `sz_ui_bridge_post_*`. No UI feature without a Headless path. Prefer `Signal.list` + `View.each`. Derived display through `Signal.map` + `View.bindText`. Nested declarative construction only. `Ui.run(_ => view)` re-runs construction on stamp-watch. `Ui.run(view)` stays. Live Desktop/Mobile drain appends to `build/record.script` (`tap N` / `xy x y` / `type` / `backspace`). Headless replays that file through `SCUZZ_UI_SCRIPT` (`--script`). Dump `[taps]` includes frames; `[last_hit]` shows the last TAP target or `NULL`.

### IO apps vs Headless

| Path | Meaning |
| --- | --- |
| **Headless** | `UiRuntime` peer — still `View` / Skia / structural dumps |
| **IO-only** | No `[ui]`, no `Ui.run`; plain `@main: IO[Unit]` exec |

Missing `[ui]` ⇒ Skia omitted from the link. `scuzz test` is TESTRT exit-0 smoke (not a11y goldens). IO-only is **not** a fourth runtime peer.

### Kernel dialect

The language `scuzz` implements. Proof is examples that exercise each construct (`examples/hello`, `kernel`, `io`, `counter`, `studio`).

Locks (not an API catalog — see [`guide.md`](guide.md)):

- Expression dialect only: `for` primary binder (`=` pure, `<-` effect); no `val` / statement blocks / `var`
- Optional `package`; top-level `def` / `private def` / `import`; `@main def …: IO[Unit]`
- Payload enums + `record` sugar (methods on generic records and enums) + thin traits/`impl` (static dispatch, including `trait Get[T]` / `impl Get[Int] for Point` / `impl Get[T] for Opt`) + monomorphized generics on defs/enums/records
- File-stem modules; enums namespaced by stem; `import Module.name` for bare disambiguation
- Types: `Unit`, `Int`, `String`, `Bool`, `List`, `IO[T]`, nominal enums
- Blessed kits + `Signal` / `View` / `Ui` / `Law.*` / `.require` as documented in the guide. Kit lambdas bind `String` (`List.filter` / `List.map` / `View.each` / `Stream.*` / `Resource` / `Net.serve`) or `Int` (`Signal.map`). The lambda body must return the kit result (`View`, `Bool`, `String`, or `IO`)
- No macros, no implicits, no HKT beyond `IO`, no null

## Language direction

Expression-only, effect-sequenced dialect. Dense. Deterministic. Verification-friendly without becoming a proof assistant. **`for` is the kernel binder**. No `val`/statement blocks.

### Expr dialect

- **`for` as primary binder**: `x = e` (pure alias), `x <- e` (effect)
- **No statement blocks**, no `var`, no `val`
- Branch arms stay expressions. Nested `for` when an arm needs names.
- Surface sugar elaborates to a small core (`Let`, `FlatMap`, `match`, ADTs, `IO`). The compiler and checkers target the core.

App-shaped Counter (full surface: [`guide.md`](guide.md)):

```scala
@main def main: IO[Unit] =
  for {
    count = Signal.int(0)
    label = Signal.map(count, n => s"count = $n")
    _    <- Ui.run(_ => View.column(
              View.bindText(label),
              View.button("+1", _ => Signal.set(count, Signal.get(count) + 1))
            ))
  } yield ()
```

### Density

Clear-dense, not cryptic-dense: nested declarative `View`s, inference, single-expr forms, short update verbs, enums + match. Avoid implicits, deep HKT, “everything is `IO`,” and imperative `View.addChild`.

### Modules and source shape

Scala **nouns**, Rust/Cargo **verbs**. No JVM packages. Rust `struct`/`impl` is not the primary story.

| Layer | Meaning |
| --- | --- |
| Package (`scuzz.toml`) | Dependency / link boundary (crate) |
| File module (`Todo.scuzz`) | Namespacing + visibility |
| Optional deeper `mod` tree | Only when a single file gets heavy |

Direction: payload **enums** / **`record`** + thin **traits**-as-interfaces. Monomorphize generics early. No classes (mutable identity). No `var`. No classpath packages. Path deps remain the unit of reuse. `private def` is module-local (default public). No `pub` yet. Details and examples: [`guide.md`](guide.md). Keep/cut: [`compatibility.md`](compatibility.md).

### Laws, simulation, mutation, and verification

App correctness is **not** classical unit tests. Prefer **mutation, fuzzing, property-oriented laws, simulation, and determinism**. All are first-class in the language and `scuzz` tooling. The split is **oracles in source, drivers as the test surface**:

- **Oracles live in the live module.** Top-level `law` declarations (pure, nullary `Bool` predicates), explicit `.require(pred)` on values / `IO` (type-preserving; residual `Law.check` / sequenced `Law.assert` under verify), reachability `Law.sometimes(name)`, and `where` refinements on `def` params and `record` fields. All erase from live builds. Armed under TestRuntime / fuzz / mutation. Application is at the call site through `.require`.
- **Drivers (`*.scuzz_drivers`) do things.** Impure, parameterized, oracle-free steps. `scuzz fuzz` composes them (generated args, random order / interleaving) alongside the UI event alphabet. `check` rejects `Law.*` and `.require` in driver files. An assert inside a driver is a unit test in disguise.
- **`Law.sometimes` keeps composition honest.** Reachability accumulates across a fuzz *campaign*. Declared-but-never-reached states fail the campaign. Oracle-free drivers cannot pass vacuously. It is a coverage/fitness signal for corpus guidance, alongside Headless dump novelty. It is a path marker (`Unit`), not a value method.
- **Mutation pressures the oracles.** Surviving mutants mean weak laws/refinements.

| Phase | Role |
| --- | --- |
| `scuzz check` | Format-verify `src/`; typecheck live + laws/refinements + sim + drivers; laws pure, drivers `IO`, no oracles in drivers; every `law` must appear in a `.require`; sim bindings match live types/purity |
| `scuzz build` (verify graph) | Layer `*.scuzz_sim` over live defs; compile drivers; residualize `.require` / `where` checks (armed under TestRuntime / fuzz / mutation only) |
| `scuzz fuzz` | Compose drivers + event scripts / IO schedules. Per-run oracles: `.require` residuals, refinements, panic/`SzError`. Per-campaign oracle: `Law.sometimes` reachability → `repro.toml` |
| `scuzz mutate` | Negate residual `Law.check` / `Law.assert` / `.require`; flip relational/boolean ops, swap `+`/`-`, `*`/`/`, and `%`→`*`, drop `&&` conjuncts, and `0`↔`1` inside those predicates; idle probe plus `--iters` fuzz; surviving mutants mean weak or unreached oracles |
| Later (optional) | Discharge trivial law/refinement fragments statically; leave the rest as search |

Direction beyond this (not current work): fuzz-verified `*.scuzz_tune` — [`optimization.md`](optimization.md). Pivot slices and status: [`gaps.md`](gaps.md).

**File convention (stem-paired, no attribute tags):**

```text
src/
  Todo.scuzz              # live module: defs + laws + refinements (all oracles)
  Todo.scuzz_sim          # same names replace live defs under sim / fuzz / mutation
  Todo.scuzz_drivers      # oracle-free workloads composed by scuzz fuzz
```

- Live/`scuzz run` loads `*.scuzz` only. Laws, `.require`, inline checks, and refinements erase.
- No separate laws file. Oracles belong next to the code they constrain, in `*.scuzz`.
- Fuzz / mutation / test layers sim, then arms oracles. Rename drift fails `check`.
- No free-floating `tests/` package roots. No third-party test or mutation frameworks.
- Prefer swapping values you own through `*.scuzz_sim`. Blessed kits stay one implementation + TestRuntime fakes. Do not stub pure helpers, `View` builders, or `Signal` cells.
- Driver params stay generator-friendly (`Int` / `String` / `Bool`). Drivers may call live defs but never assert correctness.
- Observation surface: signal store + View/a11y dump — not Skia pixels. Kernel/runtime keep ordinary example tests. This strategy is for **Scuzz apps**.

### Verification posture

Keep purity checkable (pure `A` vs `IO` vs session). Total expr core. Signals as an explicit store. Immutable data by default. Errors as values. Refinements attach to **positions** (`def` params, `record` fields), not a refined-type algebra. Checked dynamically at call / construction under the verify graph. Erased live. `.require(pred)` attaches an oracle to a **value** (or `IO[A]` sequenced after the effect) while preserving the receiver type — even when `pred` is `IO[Bool]`. They are the fragment that migrates to static discharge later with no author rewrite. Defer dependent types and runtime-heap proofs. Residual oracles + fuzz + mutation first. Static proof only where cheap.

### `scuzz fuzz`

Deterministic TestRuntime + (for `[ui]`) Headless event scripts (plus sim overlays when present). The fuzz alphabet is the typed event surface (buttons, text fields) **plus declared drivers** (`drive <name> [args]` extends the script line protocol; the verify build publishes the driver table alongside the a11y dump). Oracles: in-source **laws/refinements** first. Panic/`SzError` still fails. `Law.sometimes` reachability judges the campaign. Structural dumps aid diagnosis (PNG last). `repro.toml` records events + driver invocations verbatim, so replay is generator-independent. Needs stable tap order, `pump` as time, no hidden nondeterminism. Determinism makes any failing prefix replayable. Seeded `--iters` keeps `[ui]` prefixes that hit new `Law.sometimes` names or a new Headless `dump.txt`, and IO-only schedule seeds that hit new sometimes names, then extends/perturbs them (CLI-only; no runtime machinery). Flags, script verbs, and schedule seeds: [`guide.md`](guide.md).

### Layout model

**Flutter-style constraints** (constraints down, sizes up). Tight slots: `sized`, `aspectRatio`, percent axes on `fraction`, `expanded` flex, and opt-in `stretch` (cross axis). Scroll content is unbounded on the pan axis (`max` 0). `View.scroll` pans on y. `View.scrollH` pans on x and sizes height to the child. `expanded` in a Row inside that content keeps an intrinsic height. `minSize` raises min. `maxSize` lowers max (`0` = no cap). Incoming max still wins when tighter. `View.clip` clips paint to its frame. Scroll uses the same clip. `View.opacity` scales paint alpha (`0`–`100`). Nested opacity multiplies. `View.maxLines` caps wrapped text lines (`0` = no cap). Nested caps take the tighter value. `View.ellipsis` keeps extra lines off the paint. Without a positive `maxLines` it keeps one line. With `maxLines` it paints `...` on the last visible line when more text remains. `View.textColor` paints `View.text` with an ARGB color. Nested `textColor` uses the inner color. `View.gap` sets Column/Row/Wrap/Grid/List spacing (`0` = none). Nested `gap` uses the inner value. `View.wrap` lays out children left to right and starts a new run when a child does not fit. `View.grid(n, …)` lays out children in `n` columns. A new row starts after `n` shown children. Bounded width uses equal column slots. `View.fontSize` sets `View.text` measure and paint size in logical points (min `1`). Nested `fontSize` uses the inner size. Device-pixel paint multiplies author px (`fontSize`, `padding`, `gap`, `sized`, `minSize`, `maxSize`, `positioned`, `image`, `border`, `radius`) by the backing scale so taps match the pixels. `View.border` paints an `n` px stroke in `color` inside the frame (`0` = none). Nested border paints both. `View.radius` clips paint to a rounded rect of `n` px (`0` = square). Nested radius uses the inner value. `View.checkbox(sig, label)` is a tap target. Tap flips `Signal.int` `0` / `1`. A11y dumps `checkbox:label=0` or `checkbox:label=1`. `View.radio(sig, value, label)` is a tap target. Tap writes `value` into `sig`. Radios that share `sig` form a group. A11y dumps `radio:label=0` or `radio:label=1`. `View.slider(sig)` is a tap target. Tap or pointer drag writes `Signal.int` from the hit x, clamped `0`–`100`. A11y dumps `slider:n`. `View.progress(sig)` paints a bar from `Signal.int`, clamped `0`–`100`. It is not a tap target. A11y dumps `progress:n`. `View.switch(sig, label)` is a tap target. Tap flips `Signal.int` `0` / `1`. A11y dumps `switch:label=0` or `switch:label=1`. `View.chip(sig, label)` is a tap target. Tap flips `Signal.int` `0` / `1`. A11y dumps `chip:label=0` or `chip:label=1`. `View.filterChip(sig, label)` is a tap target. Tap flips `Signal.int` `0` / `1`. It paints a leading check when on. A11y dumps `filterchip:label=0` or `filterchip:label=1`. `[taps]` includes filter chips. `View.choiceChip(sig, value, label)` is a tap target. Tap writes `value` into `sig`. Choice chips that share `sig` form a group with radios. A11y dumps `choicechip:label=0` or `choicechip:label=1`. `[taps]` includes choice chips. `View.actionChip(label, onTap)` is a tap target. Tap runs the same closure as `View.button`. A11y dumps `actionchip:label`. `[taps]` includes action chips. `View.inputChip(sig, label)` is a tap target. Tap flips `Signal.int` `0` / `1`. It paints a trailing X. A11y dumps `inputchip:label=0` or `inputchip:label=1`. `[taps]` includes input chips. `View.listTile(title)` is a full-width title row. `View.listTile(title, trailing)` places `trailing` on the right. It is not a tap target. A11y dumps `listtile:title`. `View.badge(sig, child)` paints a count from `sig` on the top-right of `child`. It sizes to the child. It is not a tap target. A11y dumps `badge:n`. `View.card(child)` paints a surface pad and a 1 px border around `child`. It is not a tap target. A11y dumps `card:card`. `View.divider()` paints a muted hairline in an 8 px slot. It is not a tap target. A11y dumps `divider:divider`. `View.expansionTile(sig, title, child)` paints a full-width header. Tap flips `sig` between `0` and `1`. `child` shows when `sig` is not `0`. A11y dumps `expansion:title=0` or `expansion:title=1`. `[taps]` includes expansion tiles. `View.iconButton(label, onTap)` is a tap target. Tap runs the same closure as `View.button`. A11y dumps `iconbutton:label`. `[taps]` includes icon buttons. `View.verticalDivider()` paints a muted hairline in an 8 px slot. Height is the control height. It is not a tap target. A11y dumps `vdiv:vdiv`. `View.circularProgress(sig)` paints a square ring from `Signal.int`, clamped `0`–`100`. It is not a tap target. A11y dumps `circular:n`. `View.avatar(label)` paints a disc with `label`. Height is the control height. It is not a tap target. A11y dumps `avatar:label`. `View.checkboxListTile(sig, title)` paints a full-width row with a box plus `title`. Tap flips `sig` between `0` and `1`. A11y dumps `checktile:title=0` or `checktile:title=1`. `[taps]` includes checkbox list tiles. `View.switchListTile(sig, title)` paints a full-width row with `title` and a trailing switch. Tap flips `sig` between `0` and `1`. A11y dumps `switchtile:title=0` or `switchtile:title=1`. `[taps]` includes switch list tiles. `View.radioListTile(sig, value, title)` paints a full-width row with a radio plus `title`. Tap writes `value` into `sig`. Radios that share `sig` form a group. A11y dumps `radiotile:title=0` or `radiotile:title=1`. `[taps]` includes radio list tiles. `View.segmented(sig, left, right)` paints a full-width row with two halves. Tap left writes `0` into `sig`. Tap right writes `1`. A11y dumps `segmented:0` or `segmented:1`. `[taps]` includes segmented rows. `View.fab(label, onTap)` is a circular tap. Tap runs the same closure as `View.button`. A11y dumps `fab:label`. `[taps]` includes fabs. `View.outlinedButton(label, onTap)` paints a surface fill and a border. Tap runs the same closure as `View.button`. A11y dumps `outlined:label`. `[taps]` includes outlined buttons. `View.textButton(label, onTap)` paints `label` with no fill. Tap runs the same closure as `View.button`. A11y dumps `textbutton:label`. `[taps]` includes text buttons. `View.tooltip(message, child)` sizes to `child`. It is not a tap target. A11y dumps `tooltip:message`. Child taps still fire. `View.placeholder(child)` sizes to `child`. It paints a muted box mark. It is not a tap target. A11y dumps `placeholder:ph`. Child taps still fire. `View.semantics(label, child)` sizes to `child`. It is not a tap target. A11y dumps `semantics:label`. Child taps still fire. `View.each(items)` rebuilds `- item` texts at layout. `View.each(items, s => view)` builds one child per list string. `List.filter(xs, pred)` keeps strings for which `pred` is true. `List.map(xs, f)` builds a new list of strings. `List.setAt(xs, i, v)` replaces the string at `i` (out of range leaves the list). `Str.startsWith(s, prefix)` is `1` when `s` begins with `prefix`. `Str.trim(s)` drops leading and trailing ASCII space. `Color.rgb` is opaque. `Color.rgba` sets alpha. `View.ignorePointer` skips hit-test so taps pass through. `View.absorbPointer` blocks taps without firing the child. `View.excludeSemantics` omits the subtree from the a11y dump and from TextField collect. Taps still hit the child. `View.text` wraps at newlines and at max width; height sizes up. Column/row do not stretch non-flex children unless wrapped in `View.stretch`. Do not drift into CSS-ish ad-hoc rules. Do not grow Flutter-style constraint-overflow dumps. Diagnose through structural dumps + laws. Shipped widgets (`column` / `row` / `wrap` / `grid` / `stack` / `each` / `expanded` / `stretch` / `center` / `align` / `positioned` / `padding` / `sized` / `minSize` / `maxSize` / `clip` / `opacity` / `maxLines` / `ellipsis` / `textColor` / `gap` / `fontSize` / `border` / `radius` / `checkbox` / `radio` / `slider` / `progress` / `switch` / `chip` / `filterChip` / `choiceChip` / `actionChip` / `inputChip` / `listTile` / `badge` / `card` / `divider` / `expansionTile` / `iconButton` / `verticalDivider` / `circularProgress` / `avatar` / `checkboxListTile` / `switchListTile` / `radioListTile` / `segmented` / `fab` / `outlinedButton` / `textButton` / `tooltip` / `placeholder` / `semantics` / `scroll` / `scrollH` / `ignorePointer` / `absorbPointer` / `excludeSemantics` / `background` / `aspectRatio` / `fraction`): [`guide.md`](guide.md).

### UI testing

**In-source laws + composed drivers through `scuzz fuzz` + built-in mutation** are primary for `[ui]` apps. **Structural goldens** (Headless signal + a11y dumps) are a regression face — few, live graph, not a substitute for laws. PNG optional (`scuzz test --pixels`). IO packages: laws + drivers + sim under TestRuntime when present. Otherwise compile + TESTRT exit-0 smoke.

## Open work

Unknowns and known gaps: [`gaps.md`](gaps.md). Next slices: [`plans.md`](plans.md). Open unknowns: device Mobile (blocked on NDK/Xcode), GPU presenters.

App authors: [`guide.md`](guide.md). Vertical slices over breadth. No Desktop-only UI features. UI is a primary path among CLI/server/desktop/mobile. It is not the only v0 bar.

## Risks

| Risk | Mitigation |
| --- | --- |
| Language + UI + tooling is huge | Small subset. Vertical slices. Counter before generality |
| Dialect unexercised by apps | Kernel examples that stress each construct. `check` / `test` / `fuzz` on `examples/` |
| Effects too weak or too heavy | Builtin IO. Pure `View`. `Ui` at session boundary |
| Hidden nondeterminism | Closed impurity + TestRuntime + deterministic `*.scuzz_sim` |
| Laws become brittle dump goldens | Laws talk to named module/signal surface. Strict sim/live pairing in `check`. Mutation kills weak oracles |
| Sim becomes Mockito | Only top-level same-name overlays. No stubbing pure `View`/`Signal`. Kits stay TestRuntime |
| Drivers become integration tests | `check` rejects `Law.*` in driver files. Correctness lives only in live-module oracles |
| Drivers pass vacuously | `Law.sometimes` reachability fails the campaign when declared states are never reached |
| Verification tool sprawl | One `scuzz` strategy — mutation/fuzz/laws/sim/determinism in-tree. No external test frameworks |
| “Almost Scala” confusion | Explicit non-goals. Language direction above. [guide.md](guide.md) |
| Watch sold as hot reload | `watch` rebuilds. `[ui]` `run --watch` stamp-reloads Views. IO-only `run --watch` kills and reruns |
| IDE typer ≠ batch typer | One JSON schema. LSP wraps `scuzz check` |
| Skia weight | pinned CPU prebuilt default. `sk_sw` opt-out |
| Desktop-only features | Headless peer rule |
| Treating UI as the only product | UI is a primary path (Dart-shaped). CLI/server/desktop/mobile are peers |
| GC vs frame budget | `pump` boundary. Measure |
| Mobile packaging | Host Mobile peer first. Device toolchains later |
