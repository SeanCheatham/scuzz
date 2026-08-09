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

### Phase 2 — Declarative UI core ✅ (current)

- Element tree, state/signals, layout, hit testing in `crates/runtime` (Views stay backend-agnostic under `Ui`)
- Theme tokens + core widgets: `Text`, `Button`, `TextField`, `List`, `Scroll`, `Image`, `Icon` (+ `Label`)
- Bridge: completed `IO` posts signal writes; `pump` flushes them (UI-thread hop on the single-threaded loop)
- Examples: `examples/counter`, `examples/todo` (Todo load/save via `IO` + `Resource`) + goldens
- Kernel builtins: `Ui.runCounter`, `Ui.runTodo` (demos drive the View tree until the language can express widgets)

### Phase 3 — Language, effects stdlib, tooling

- Expand subset: modules, packages, ADTs
- Complete blessed effects kit: `Resource`, `Ref`, `Deferred`, `Queue`, race/par helpers, time
- Incremental compile + hot reload; formatter; basic LSP
- Linux (then Windows) embedders
- **Start rewriting compiler modules in ScalUI** (parser first), still built by Stage 0

### Phase 4 — Self-host

- Stage 1: full compiler-in-ScalUI builds under Stage 0
- Stage 2: Stage 1 compiles the compiler; CI dual-boot + self-rebuild check
- CLI moves to ScalUI; Stage 0 retained as canary only
- Success bar: `scalui` release binary is produced by a ScalUI-built compiler

### Phase 5 — Mobile

- iOS/Android embedder shells + packaging (`scalui package`)
- Touch gestures, soft keyboard, app lifecycle
- Same examples run unmodified across platforms

### Phase 6 — Productize

- Design-language polish, animation system, accessibility hooks
- Docs, samples gallery, distribution of prebuilt Skia + toolchains
- Optional: evaluate Impeller as alternate backend behind the same canvas API

## Risks and deliberate bets

| Risk | Mitigation |
| --- | --- |
| Building a language + UI + tooling is huge | Ruthless subset; vertical slices; ship Counter before generality |
| Self-host never arrives / dialect drift | Kernel dialect doc; port compiler early; Stage 0/1/2 CI gate |
| Effects too weak vs cats-effect OR too heavy for UI | Builtin IO only; pure `View` build; `Ui` effect at session boundary |
| “Almost Scala / almost CE” confuses users | Brand ScalUI language + effects guide; explicit non-goals (no cats port) |
| Skia build/size complexity | Prebuilt artifacts per platform; thin C ABI; CI caches |
| Window-only features sneak in | Rule: no UI feature without Headless interpreter path |
| GC + UI frame budget | `pump` as frame boundary; measure; isolate render work |
| Mobile packaging hell | Defer until desktop UI core + self-host path are credible |

See `docs/adr/` for locked design decisions and `docs/compatibility.md` for the matrix.
