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
| `var` | Cut. UI state uses `Signal` / blessed cells |
| Built-in `IO` / fibers / `Resource` | Keep (blessed) |
| Null | Null-free surface. Explicit null only at FFI |
| Scala 3 metaprogramming / given-using | Cut |
| Implicit conversions | Cut |
| Java interop / Scala.js / JVM backends | Cut |
| cats / cats-effect library compatibility | Cut |
| ZIO library compatibility | Cut |

## Runtime and tooling

| Area | Compatible with | Not compatible with |
| --- | --- | --- |
| Binary format | Native ELF/Mach-O/PE through LLVM | JVM classfiles / JARs |
| Dependencies | `scuzz.toml` path deps (data only; no plugin DSL) | Maven Central / Ivy; git/versioned hosted artifacts (deferred); sbt/Gradle/`pubspec` plugins |
| Effects | Builtin `IO` / `Resource` / concurrent kit + Clock/Random/Fs/Net/Sys | cats-effect runtime, ZIO-as-library, Future-as-default |
| Test interpreters | Built-in verification: hermetic TestRuntime fakes (`SCUZZ_TESTRT=1`; no live sockets; HTTP client stubs plus virtual loopback mailbox; `Net.serve` injects and mailbox; TCP/UDP mailbox; `Sys.exec` / `Sys.spawn` fail; `Sys.getenv` sealed; `Sys.alive` / `Sys.kill` fake); `*.scuzz_verify` + `.require` + stem-paired `*.scuzz_sim` / `*.scuzz_drivers`; `scuzz fuzz --iterations` (search + mutation; shrink + dump determinism; terminal heap baseline; acquire/release pairing; finalizer-on-cancel; leftover park; live/verify differential; `--oracles` for residual predicates; `corpus/` replay; `--iterations 0` corpus-only) | Wall-clock-only harnesses; ad-hoc FFI mocks; app-level Mockito / `src/test` unit trees; third-party mutation/fuzz frameworks; live network beyond stubs and virtual loopback under sim |
| Deterministic campaign (Antithesis-shaped) | Generator-independent `corpus/*.toml` event lists; file/env observation (`sometimes.reached`, scripts, dumps) so an external conductor can drive the same binary; `Property.sometimes` as campaign aggregation (a name must occur at least once) | Antithesis SDK JSONL; a second conductor-specific wire protocol |
| UI | `View` + `Ui` + Skia (`sk_capi`; Impeller deferred) | Swing, JavaFX, Compose Multiplatform, Flutter widgets |
| Watch | Rebuild on source change (`scuzz watch`); IO-only `run --watch` kills and reruns; `[ui] run --watch` is hot reload (stamp-reload Views) | Flutter DevTools / VM patching |
| Diagnostics | `scuzz check` is the linter (`--message-format=json`); `scuzz lsp` wraps `check` | Separate IDE typer; analyze-vs-check; `lint` subcommand; `*.g.scuzz` codegen |
| Dogfood IDE | `scuzz ide` launches the bundled `[ui]` package. The app consumes `check` / `lsp`. Headless is a peer. Prerequisites: [`gaps.md`](gaps.md) | A compiler inside the IDE app; second typer; Desktop-only editor; a `scuzz-ide` binary |
| Self-hosting | Staged rewrite slices behind prerequisites ([`vision.md`](vision.md#self-hosting)); `VERSION` names the product; `bootstrap.sh` fetches the newest GitHub `v*` release; `package_release.sh` compiles `examples/cli` with that tagged binary; product CLI is Scuzz | Big-bang rewrite; dual shipped product CLIs; cargo as the product compiler |
| Packaging | Android debug APK through `scuzz package --target android` (installs when adb lists a device); iOS sim `.app` through `scuzz package --target ios` | Gradle/CocoaPods as Scuzz APIs; Flutter platform channels |

## Platforms (Headless first)

| Target | Language/runtime/UI-core | Desktop / Mobile embedder | Notes |
| --- | --- | --- | --- |
| Linux Headless (CI/cloud) | Yes | N/A | Default CI; goldens + TestRuntime |
| Linux desktop | Yes | X11 | `embedder-desktop` when `DISPLAY` is set |
| Linux mobile host shell | Yes | Host shell | `embedder-mobile` + `SCUZZ_MOBILE_SHELL=1` |
| macOS desktop | Yes | Cocoa blit | peer to Linux X11 |
| Windows desktop | Yes | Secondary | Later (same session protocol) |
| iOS / Android | Shared app code | Packaging shells | `scuzz package`; NDK/Xcode for device |
| Web / browser | No | N/A | Not a current target |

## Toolchain

The compiler and CLI are Scuzz (`examples/compiler`, `examples/cli`). `scripts/bootstrap.sh` compiles `examples/cli` with the newest GitHub `v*` release ([vision.md](vision.md#self-hosting)). Kernel surface: [vision.md](vision.md#kernel-dialect). One formatter. One linter (`scuzz check`). One testing strategy. HTTP `https://` links OpenSSL (`libssl` / `libcrypto`). There is no second TLS stack.
