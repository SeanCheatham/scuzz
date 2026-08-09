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

## Phases (summary)

0. Foundation — skeleton, Stage-0 hello→LLVM, runtime + IO fiber skeleton  
1. `Ui` effect + Headless (then Window)  
2. Declarative UI core  
3. Effects stdlib + tooling; begin ScalUI rewrite of compiler  
4. Self-host Stage 1/2  
5. Mobile embedders  
6. Productize  

See `docs/adr/` for locked design decisions and `docs/compatibility.md` for the matrix.
