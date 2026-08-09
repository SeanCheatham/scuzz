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

| Target | Language/runtime/UI-core | Window embedder | Notes |
| --- | --- | --- |
| Linux headless (CI/cloud) | Yes | N/A | Default development & CI |
| Linux desktop | Yes | Secondary | X11/Wayland later |
| macOS desktop | Yes | Secondary | Metal/GL surface later |
| Windows desktop | Yes | Secondary | Later |
| iOS / Android | Shared app code later | Phase 5 | Packaging shells only |

## Self-host stages

| Stage | Host | Role |
| --- | --- | --- |
| 0 | Rust | Bootstrap compiler + early CLI |
| 1 | ScalUI (built by Stage 0) | Proves language can express the compiler |
| 2 | ScalUI (built by Stage 1) | True self-host; release compilers |

Kernel dialect constraints for self-host: `docs/adr/0005-kernel-dialect.md`.
