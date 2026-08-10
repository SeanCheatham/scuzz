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

## Roadmap

Phases 0–6 landed (foundation, Headless `Ui`, declarative Views, effects/tooling, self-host Stage 1/2, Mobile peer, productize). Phase history lives in git; next steps are in [docs/plan.md](plan.md). Vertical slices over breadth; no UI feature may land Window-only without a Headless path.

## Risks and deliberate bets

| Risk | Mitigation |
| --- | --- |
| Building a language + UI + tooling is huge | Ruthless subset; vertical slices; ship Counter before generality |
| Self-host never arrives / dialect drift | Kernel dialect doc; port compiler early; Stage 0/1/2 CI gate |
| Effects too weak vs cats-effect OR too heavy for UI | Builtin IO only; pure `View` build; `Ui` effect at session boundary |
| Untracked non-determinism breaks goldens / self-host CI | Closed impurity surface (Clock/Random/Fs/Net/Sys/console); TestRuntime fakes (`su_testrt_*`, `SCALUI_TESTRT=1`) |
| “Almost Scala / almost CE” confuses users | Brand ScalUI language + [docs/guide.md](guide.md); explicit non-goals (no cats port) |
| Skia build/size complexity | Prebuilt artifacts per platform; thin C ABI; CI caches |
| Window-only features sneak in | Rule: no UI feature without Headless interpreter path |
| GC + UI frame budget | `pump` as frame boundary; measure; isolate render work |
| Mobile packaging hell | Packaging shells + host Mobile peer first; device NDK/Xcode later |

See `docs/adr/` for locked design decisions and `docs/compatibility.md` for the matrix.
