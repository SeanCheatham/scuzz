# Compatibility matrix

Supported features and exclusions. Product decisions and language direction: [`philosophy.md`](philosophy.md).

## Language

| Feature | Decision |
| --- | --- |
| Scala-inspired definitions, algebraic data types, and pattern matching | Keep |
| Local `val` / blocks | Cut. Use `for` with `=` or `<-` ([philosophy](philosophy.md#language-direction)) |
| Traits as interfaces; `Option` and `Either` forms as enums | Keep |
| Local type inference, generics | Keep |
| Higher-kinded types | Only where effects need them (`IO`) |
| `var` | Cut. UI state uses `Signal` or built-in cells |
| Built-in `IO` / fibers / `Resource` | Keep |
| Null | No null in application code. Explicit null only at the foreign interface |
| Scala 3 metaprogramming / given-using | Cut |
| Implicit conversions | Cut |
| Java interop / Scala.js / JVM backends | Cut |
| cats / cats-effect library compatibility | Cut |
| ZIO library compatibility | Cut |

## Runtime and tooling

| Area | Supported | Excluded |
| --- | --- | --- |
| Binary format | Native ELF and Mach-O through LLVM. WebAssembly for GUI applications | JVM class files, JARs, and a JavaScript language backend |
| Dependencies | Path dependencies in `scuzz.toml` | Maven, Ivy, git or registry dependencies, library publishing, and build plugins |
| Effects | Built-in `IO`, fibers, `Resource`, and the standard kits | cats-effect, a ZIO library port, and Future as the default effect |
| Verification | Hermetic TestRuntime, claims, live assertions, simulation, search, mutation, and corpus replay | App unit-test trees, third-party verification frameworks, live external networks in simulation, and expected dump fixtures |
| External campaigns | `corpus/*.toml`, recorded events, environment and file observations, and `Property.sometimes` | A second conductor protocol or an Antithesis SDK adapter |
| UI | `View`, `Ui`, and the Skia C interface | Swing, JavaFX, Compose Multiplatform, Flutter widgets, SwiftUI, UIKit widgets, WinUI wrappers, and platform channels |
| Watch | Source rebuilds; restart for IO apps; View reload for UI apps | Flutter DevTools and VM patching |
| Diagnostics | `scuzz check`, JSON diagnostics, and `scuzz lsp` | A second checker, a separate `lint` command, and generated `*.g.scuzz` sources |
| IDE | One bundled UI app with shared compiler modules and evaluator probes | A separate `scuzz-ide` binary, Desktop-only editor behavior, and a second evaluator |
| Generation | Pinned local llama.cpp, a fixed GGUF catalog, explicit limits, and complete external proposal directories | Provider SDKs, remote inference configuration, arbitrary models, conversion, telemetry, and training |
| Self-hosting | One Scuzz product CLI; newest GitHub `v*` release for bootstrap; product version in `VERSION` | A second shipped toolchain or cargo as the product compiler |
| Packaging | macOS app bundles, Android debug APKs, iOS simulator apps, and WebAssembly GUI apps | Gradle or CocoaPods as Scuzz APIs |

Verification uses one scenario world, `*.scuzz_verify`, and assertions in live bodies.
`scuzz fuzz --iterations 0` replays the corpus.
`--differential` compares live dumps, repeated pixel snapshots, and GPU-to-software channel differences.
`--session` checks URLSession certificate and TLS behavior on loopback.
A `Property.sometimes` label must occur at least once in a campaign.
Commands and file formats: run `scuzz docs verify`.

The IDE keeps Headless as a peer runtime.
Its UI host supplies native editor effects. The IO compiler does not link a UI renderer.
Local generation and external import use the same validation and review path.
Model management and finite CLI generation use the same catalog and cache as the IDE.
IDE behavior, generation limits, and exclusions are defined in
[`philosophy.md`](philosophy.md#proposal-review).
Current proof limits live in [`gaps.md`](gaps.md).
Run `scuzz docs ide` for controls and commands.
Run `scuzz docs packages`, `scuzz docs android`, or `scuzz docs ios` for app packaging.

## Platforms (Headless first)

| Target | Language/runtime/UI-core | Desktop / Mobile embedder | Notes |
| --- | --- | --- | --- |
| Linux Headless (CI/cloud) | Yes | N/A | Default CI; Headless + TestRuntime |
| Linux desktop | Yes | X11 | `embedder-desktop` when `DISPLAY` is set. Native UI builds need X11 and D-Bus development files. Motion preferences use the Settings portal |
| Linux mobile host shell | Yes | Host shell | `embedder-mobile` + `SCUZZ_MOBILE_SHELL=1` |
| macOS desktop | Yes | Cocoa blit | UI host packages are signed `.app` bundles. Non-system libraries are included. Finder launch uses Desktop. AppKit handles window borders before it sends content input to the shared UI. Content input uses top-left coordinates. |
| Windows desktop | Later | Later | Shared session design. No supported host build |
| iOS / Android | Shared app code | Packaging shells | iOS simulator run needs Xcode on Apple Silicon. Android needs the NDK. iOS HTTP clients use platform trust. Physical device proof stays open |
| Web / browser | WebAssembly GUI | Canvas presentation | `scuzz package --target web`; cached Emscripten 4.0.23; Python 3 on the build host |

## Toolchain

The compiler and CLI are Scuzz (`examples/compiler` and `examples/cli`).
Bootstrap uses the newest GitHub `v*` release to build a temporary checkout compiler.
That compiler builds the product CLI.
See [`philosophy.md`](philosophy.md#self-hosting) for bootstrap constraints.
Use one formatter, one checker, and one verification strategy.
Host build, run, and package commands support paths with spaces.

Host and iOS simulator reload need one direct `Ui.run` factory.
Reload checks captured binding names, order, and types, plus record and enum layouts.
An incompatible reload or a failed build preserves the running app.
An incompatible reload needs a restart.
Captured values retain their current state.
Code stays loaded until the session ends so active IO handlers can finish.
Enter `r` to rebuild and restart. Enter `q` to stop.
The app stops when its CLI session ends.
iOS stops the console launch process before the selected simulator app.

CLI and server HTTPS use OpenSSL.
iOS and macOS GUI clients use URLSession with platform certificate trust.
GUI clients verify loopback certificates.
Both transports use the same Net API and hermetic simulation dispatch.
iOS supports HTTP clients and TCP/UDP. It does not support HTTP servers.
App Transport Security permits local networking. Use HTTPS for remote services.

## Browser target

The browser target uses the shared Signals, View layout, and software paint
path. Browser font measurement and rasterization use the same font stack.
Pinned Noto fonts supply Devanagari, Thai, and Simplified Chinese fallback.
Other characters use the browser monospace font.
Font files load when their Unicode ranges occur.
The three files use 18.6 MB before compression. The CJK file uses 17.8 MB.
Font loading updates layout and preserves input state.
The browser chooses the base direction from the first strong source character.
DOM text ranges supply field source positions.
Text wrapping and ellipsis keep browser grapheme clusters together.
The browser needs `Intl.Segmenter`.
Emscripten keeps the 64-bit runtime layout and lowers the module to wasm32.
The package stamps `app.js` and `app.wasm` with one content version.
The page loads that pair together.
The glue lists EM_ASM addresses from that module.
A cache that keeps the glue longer than the module cannot load a new module with older glue.
The app runs on the browser thread. The UI loop paints when the session is
dirty. The frame loop pauses when nothing changes. An input event or a
signal write resumes it. The output needs no worker threads or
cross-origin isolation headers.

The first target supports GUI sessions with pointer, touch, keyboard, wheel,
and resize events. Wheel and canvas touch listeners that cancel the event
register as non-passive. The HTTP client and the HTTP servers fail at
the call (`Net: not available on web`); a process effect fails at `fork`
(`Sys.exec: fork failed`). The web build links every def in the package and its path
dependencies; the linker drops the defs `@main` does not reach. Files use Emscripten memory storage. They do not persist across page reloads.
Static text supports browser selection and copying through a DOM text layer.
A removed focused control moves focus to the text layer.
Walkthrough tabs and Index Book sections use URL fragments for links and
browser history. The fragment key is `#stage=id`. In-content `View.link` and
`View.navTile` controls use the same `#stage=` fragment. An `http://` or
`https://` route is a real URL. Icons and remaining images expose `role=img`.
Docs does not paint placeholder images. A breadcrumb is a navigation group.
The browser exposes links, buttons, headings, images, and editable fields through DOM
controls. Fields support native clipboard actions and IME composition.
Chromium, Firefox, and WebKit run the browser proof. Chromium and WebKit also
run phone viewport and touch emulation. Real phone keyboards, long-press
selection, pinch zoom, and screen-reader sessions still need device checks.
Full accessibility conformance and hot reload remain open.
