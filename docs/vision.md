# ScalUI Vision

ScalUI is a **Flutter-shaped product** with a **Scala-shaped language**, not a Scala 3 / Scala Native / Maven citizen.

## Thesis

- **Language**: a purposeful subset of Scala syntax/semantics optimized for UI apps and native codegen, with **built-in effect/IO** in the Cats Effect spirit (not a cats port).
- **Runtime**: custom native (LLVM). No JVM, no Java interop, no classpath/Maven ecosystem.
- **UI**: one design language + Skia, exposed as a **`Ui` effect** with Headless/Window/Mobile interpreters.
- **Tooling**: one CLI + build system (`scalui`) that owns compile, link, assets, hot reload, and packaging.
- **Bootstrap**: self-hosting is a hard goal. Stage-0 (Rust) exists only to get there.

Upstream Scala Native is a *reference*, not a dependency. Divergence is intentional.

## Locked defaults

| Decision | Choice |
| --- | --- |
| UI testing / CI default | `UiRuntime.Headless` first; Window/mobile are alternate interpreters |
| Codegen | LLVM IR |
| Renderer (v0) | Skia via thin C ABI; Impeller later |
| Build tool | DIY Mill/Cargo-like: `scalui` (not sbt/Maven) |
| Effects | Language + runtime builtins, not an external library ecosystem |
| Impurity boundary | All non-determinism and external I/O go through blessed `IO` (clock, random, FS, network, env/args, console); `IO.delay` is not an app-level escape hatch |
| Test interpreters | `scalui test` can run with fake clock/random/FS/network for deterministic replay (peer to live interpreters, same idea as Headless `Ui`) |
| Self-host | On the critical path (Stage 0 → 1 → 2) |
| UI model | Pure `View` + effectful `Ui` session (`mount` / `pump` / `inject` / `snapshot`) |

## What ScalUI is not

- Not Scala 3, not the JVM, not Scala.js
- Not a cats / cats-effect / Typelevel port or compatibility layer
- Not SwiftUI / UIKit / WinUI wrappers
- Not “every widget rebuild is an `IO`” (frame budgets matter; `View` build stays sync/pure)

## Success bars

**v0 (app developer joy)**  
Install CLI → `scalui new` → Counter/Todo as backend-agnostic `View` + builtin `IO` → `scalui test` (Headless) and `scalui run --headless` with no display → `scalui run` opens a window when available.

**v1 (language credibility)**  
Stage-2 self-host: ScalUI compiler compiles ScalUI compiler; release builds do not require Rust Stage-0 except as CI canary.

## Phased roadmap

Vertical slices over breadth. Ship Counter before generality. No UI feature may land Window-only without a Headless path.

### Phase 0 — Foundation ✅

- Repo skeleton, vision docs, compatibility matrix, `scalui.toml` schema draft
- Stage-0 compiler (Rust): hello-world → LLVM IR → native executable
- Minimal runtime (alloc, string, panic) + IO fiber skeleton (`IO.delay`, `flatMap`, `Resource`, run loop)
- ADRs: GC v0, Skia acquisition, IO error model, `Ui` vs `View` boundary, kernel dialect for self-host
- CI on headless Linux from day one

### Phase 1 — `Ui` effect + Headless backend first ✅

- Skia-shaped offscreen (`sk_capi` + CPU software backend) + `UiRuntime.Headless`: `mount` / `pump` / `inject` / `snapshot`
- Golden PNG under `scalui test`; `scalui run --headless` works with no display
- `UiRuntime.Window` as a second interpreter wrapping the same session protocol (OS window presentation deferred to embedder)
- Rule: no feature may land Window-only without a Headless path
- Kernel builtin: `Ui.runHeadless`; example `examples/hello_ui` + goldens

### Phase 2 — Declarative UI core ✅

- Element tree, state/signals, layout, hit testing in `crates/runtime` (Views stay backend-agnostic under `Ui`)
- Theme tokens + core widgets: `Text`, `Button`, `TextField`, `List`, `Scroll`, `Image`, `Icon` (+ `Label`)
- Bridge: completed `IO` posts signal writes; `pump` flushes them (UI-thread hop on the single-threaded loop)
- Examples: `examples/counter`, `examples/todo` (Todo load/save via `IO` + `Resource`) + goldens
- Kernel builtins: `Ui.runCounter`, `Ui.runTodo` (demos drive the View tree until the language can express widgets)

### Phase 3 — Language, effects stdlib, tooling ✅

- Expand subset: `package`, multi-file modules, nullary `enum` ADTs, `match`, local `val`
- Blessed effects kit (concurrency / resource slice): `Resource` (releases on failure), `Ref`, `Deferred`, `Queue`, `handleErrorWith` / `attempt`, `sleep`, `race` / `both` — kernel demo `Effects.runKit`
- Incremental compile (fingerprint cache), `scalui watch` / `run --watch`, `scalui fmt`, basic `scalui lsp`
- Linux X11 embedder for `UiRuntime.Window` (`crates/embedder-desktop`); Headless remains CI default
- ScalUI parser bootstrap under `compiler-scalui/` (seed for Phase 4)

### Phase 4 — Self-host ✅

- Stage 1: full compiler-in-ScalUI under `compiler-scalui/` (lexer/parser/codegen/driver/CLI), built by Stage 0
- Stage 2: Stage 1 rebuilds the compiler; `scripts/selfhost.sh` + CI dual-boot smoke on `examples/hello`
- CLI moves to ScalUI (`compiler-scalui` binary name `scalui`); Stage 0 Rust CLI retained as canary (`cargo run -p scalui`)
- Blessed **filesystem** `IO` (`Fs.read` / `write` / `list` / `mkdirs`) plus host kit `Sys.args` / `exec` / `getenv` for the compiler CLI and clang link — live interpreters (TestRuntime fakes in Phase 6)
- Kernel dialect expansion for self-host: top-level `def`, `if`/`else`, `Int`/`String`/`List`, string/int ops, bound `flatMap`
- Success bar: `scalui` release binary is produced by a ScalUI-built compiler (`./scripts/selfhost.sh`)

### Phase 5 — Mobile ✅

- `UiRuntime.Mobile` peer (same `mount` / `pump` / `inject` / `snapshot` protocol)
- Touch: `SU_INPUT_POINTER` (down/move/up) + `SU_INPUT_SCROLL`; soft keyboard via TextField focus + `SU_INPUT_TEXT` / `SU_INPUT_KEYBOARD`
- App lifecycle: `SU_INPUT_LIFECYCLE` {Resume, Pause, Stop} — Headless-scriptable
- Embedder shells: `crates/embedder-mobile` host shell (CI) + Android/iOS packaging templates
- `scalui package` emits `build/package/{host,android,ios}/`
- Same examples run unmodified (`SCALUI_UI_RUNTIME=mobile`)

### Phase 6 — Productize ✅ (current)

- Design-language polish: theme `accent` / `disabled` / `radius` (default radius 0 keeps prior goldens)
- Animation v0: `SuAnimFloat` lerp ticked on `pump` via monotonic `Clock` dt
- Accessibility hooks: `SuA11yRole` + label on Views; Headless `su_view_a11y_dump`
- Samples gallery: `examples/{hello,effects,adt,fs,clock,impurity,hello_ui,counter,todo}` indexed in README
- Skia distribution: `scripts/fetch_skia.sh` + `third_party/skia/` notes; default remains in-tree `sk_sw`
- Impeller: evaluated as optional alternate behind `sk_capi` — **deferred** (software CPU backend stays v0 default; see ADR 0002)
- **Closed impurity boundary** for app + compiler code:
  - `Clock.realTime` / `Clock.monotonic`; `IO.sleep` routed through Clock (fake advances virtual time)
  - `Random.nextInt`; `Net.httpGet`; Fs/Sys/console (`IO.println`) already blessed
  - `TestRuntime` fakes: clock, seeded RNG, mem FS, stubbed network (`su_testrt_install` / `SCALUI_TESTRT=1`)
  - Kernel demo `Impurity.runKit`; examples `examples/clock`, `examples/impurity`
  - Discipline: new impurity only through blessed modules — no app-level OS thunks via `IO.delay`

## Risks and deliberate bets

| Risk | Mitigation |
| --- | --- |
| Building a language + UI + tooling is huge | Ruthless subset; vertical slices; ship Counter before generality |
| Self-host never arrives / dialect drift | Kernel dialect doc; port compiler early; Stage 0/1/2 CI gate |
| Effects too weak vs cats-effect OR too heavy for UI | Builtin IO only; pure `View` build; `Ui` effect at session boundary |
| Untracked non-determinism breaks goldens / self-host CI | Closed impurity surface (Clock/Random/Fs/Net/Sys/console); TestRuntime fakes (`su_testrt_*`, `SCALUI_TESTRT=1`) |
| “Almost Scala / almost CE” confuses users | Brand ScalUI language + effects guide; explicit non-goals (no cats port) |
| Skia build/size complexity | Prebuilt artifacts per platform; thin C ABI; CI caches |
| Window-only features sneak in | Rule: no UI feature without Headless interpreter path |
| GC + UI frame budget | `pump` as frame boundary; measure; isolate render work |
| Mobile packaging hell | Packaging shells + host Mobile peer first; device NDK/Xcode later |

See `docs/adr/` for locked design decisions and `docs/compatibility.md` for the matrix.
