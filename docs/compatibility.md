# Compatibility matrix

ScalUI deliberately diverges from the Scala Center / Typelevel / JVM ecosystems.

## Language

| Feature | v0 stance |
| --- | --- |
| Scala-like vals/defs, case classes / ADTs, pattern matching | Keep |
| Traits-as-interfaces, `Option`/`Either`-style enums | Keep |
| Local type inference, generics | Keep (monomorphize early) |
| Higher-kinded types | Only where effects need them |
| Immutability-by-default + explicit `var` | Keep |
| Built-in `IO` / fibers / `Resource` | Keep (blessed) |
| Null | Null-free surface; explicit null only at FFI |
| Full Scala 3 metaprogramming / given-using heavy | Defer / cut |
| Implicit conversions | Cut |
| Java interop / Scala.js / JVM backends | Cut |
| cats / cats-effect library compatibility | Cut |

## Runtime & tooling

| Area | Compatible with | Not compatible with |
| --- | --- | --- |
| Binary format | Native ELF/Mach-O/PE via LLVM | JVM classfiles / JARs |
| Dependencies | `scalui.toml` path/git/versioned artifacts we host | Maven Central / Ivy |
| Effects | Builtin `IO` / `Resource` / concurrent kit | cats-effect runtime, ZIO, Future-as-default |
| UI | ScalUI `View` + `Ui` + Skia | Swing, JavaFX, Compose Multiplatform, Flutter widgets |

## Platforms (headless-first)

| Target | Language/runtime/UI-core | Window / Mobile embedder | Notes |
| --- | --- | --- | --- |
| Linux headless (CI/cloud) | Yes | N/A | Default development & CI; Phase 1–5 golden PNGs + self-host |
| Linux desktop | Yes | Phase 3 X11 | `crates/embedder-desktop` blits Window peer frames when `DISPLAY` is set |
| Linux mobile host shell | Yes | Phase 5 host | `crates/embedder-mobile` + `SCALUI_MOBILE_SHELL=1` exercises Mobile peer |
| macOS desktop | Yes | Secondary | Metal/GL surface later |
| Windows desktop | Yes | Secondary | Later (same session protocol) |
| iOS / Android | Shared app code | Phase 5 packaging shells | `scalui package`; NDK/Xcode for device builds |

## Self-host stages

| Stage | Host | Role |
| --- | --- | --- |
| 0 | Rust | Bootstrap compiler + canary CLI (`crates/cli`) |
| 1 | ScalUI (built by Stage 0) | `compiler-scalui/` — proves language can express the compiler |
| 2 | ScalUI (built by Stage 1) | True self-host; release `scalui` binary |

Dual-boot gate: `scripts/selfhost.sh` (CI). Kernel dialect: `docs/adr/0005-kernel-dialect.md`.
