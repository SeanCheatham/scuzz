# Compatibility matrix

What we keep vs cut. Product locks and language direction: [`vision.md`](vision.md).

## Language

| Feature | Stance |
| --- | --- |
| Scala-like defs, ADTs, pattern matching | Keep |
| Local `val` / blocks | Cut — use `for` with `=` / `<-` ([vision](vision.md#language-direction)) |
| Traits-as-interfaces, `Option`/`Either`-style enums | Keep |
| Local type inference, generics | Keep (monomorphize early) |
| Higher-kinded types | Only where effects need them (`IO`) |
| `var` | Cut directionally; UI state via `Signal` / blessed cells |
| Built-in `IO` / fibers / `Resource` | Keep (blessed) |
| Null | Null-free surface; explicit null only at FFI |
| Scala 3 metaprogramming / given-using | Cut |
| Implicit conversions | Cut |
| Java interop / Scala.js / JVM backends | Cut |
| cats / cats-effect library compatibility | Cut |
| ZIO library compatibility | Cut |

## Runtime & tooling

| Area | Compatible with | Not compatible with |
| --- | --- | --- |
| Binary format | Native ELF/Mach-O/PE via LLVM | JVM classfiles / JARs |
| Dependencies | `scuzz.toml` path deps (data-only; no plugin DSL) | Maven Central / Ivy; git/versioned hosted artifacts (deferred); sbt/Gradle/`pubspec` plugins |
| Effects | Builtin `IO` / `Resource` / concurrent kit + Clock/Random/Fs/Net/Sys | cats-effect runtime, ZIO-as-library, Future-as-default |
| Test interpreters | TestRuntime fakes (clock/random/FS/net + argv/stdin/println capture); `SCUZZ_TESTRT=1`; stem-paired `*.scuzz_sim` / `*.scuzz_laws` | Wall-clock-only harnesses; ad-hoc FFI mocks; app-level Mockito / `src/test` unit trees |
| UI | `View` + `Ui` + Skia (`sk_capi`; Impeller deferred) | Swing, JavaFX, Compose Multiplatform, Flutter widgets |
| Watch | Rebuild on source change (`scuzz watch`) | Flutter hot reload / DevTools VM patching |
| Diagnostics | `scuzz check --message-format=json` (LSP wraps `check`) | Separate IDE typer; analyze-vs-check; `*.g.scuzz` codegen |
| Packaging | Copy-patched `shells/android` / `shells/ios` templates | Gradle/CocoaPods as Scuzz APIs; Flutter platform channels |

## Platforms (headless-first)

| Target | Language/runtime/UI-core | Window / Mobile embedder | Notes |
| --- | --- | --- | --- |
| Linux headless (CI/cloud) | Yes | N/A | Default CI; goldens + self-host + TestRuntime |
| Linux desktop | Yes | X11 | `embedder-desktop` when `DISPLAY` is set |
| Linux mobile host shell | Yes | Host shell | `embedder-mobile` + `SCUZZ_MOBILE_SHELL=1` |
| macOS desktop | Yes | Cocoa blit | peer to Linux X11 |
| Windows desktop | Yes | Secondary | Later (same session protocol) |
| iOS / Android | Shared app code | Packaging shells | `scuzz package`; NDK/Xcode for device |

## Self-host stages

| Stage | Host | Role |
| --- | --- | --- |
| 0 | Rust | Bootstrap only (`crates/cli`); CI / fresh checkout |
| 1 | Scuzz Lang (built by Stage 0) | First Scuzz Lang-built CLI; rebuilds the compiler |
| 2 | Scuzz Lang (built by Stage 1) | True self-host; **shipped** `scuzz` (`package_release.sh`) |

Dual-boot gate: `scripts/selfhost.sh`. Kernel surface: [vision.md](vision.md#kernel-dialect). Release tarballs record `stage=2` in `VERSION`.
