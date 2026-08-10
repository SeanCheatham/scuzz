# ScalUI plan — next steps

Post–Phase 6, the gates are greener than the product. The self-host script passes on Linux CI but segfaults on macOS, the CLI surface outruns its implementations, and the language still reads like a handle-passing DSL. This plan orders the work so credibility gaps close before new breadth.

Ordered by priority; each workstream lists its done-when gate.

## 1. Fix the self-host gate

ADR 0005 names `scripts/selfhost.sh` the dialect-drift gate. It currently segfaults at Stage 2 on macOS (deeply recursive Stage-1 emit blows the main-thread stack; the `ulimit` fallback is ineffective there) and its exit-code handling can mask failures.

- Make `selfhost.sh` fail loudly: no `|| true` on stack setup, explicit exit-code checks after every stage.
- Remove the stack-depth dependency: convert recursive emit/print paths in `compiler-scalui/src/Codegen.scala` to explicit-stack iteration (or trampoline), so Stage 2 builds within the default 8 MB stack.
- Add a macOS CI job (at minimum: selfhost dual-boot + `make -C crates/runtime test` + golden tests) so the Cocoa embedder and Darwin linking are gated too.

Done when: `./scripts/selfhost.sh` passes on macOS and Linux with default stack limits, and CI fails on regression on both.

## 2. Dialect ergonomics for the v0 bar

Counter should read like UI code, not assembly. Small, high-leverage additions, each landing in Stage 0 before Stage 1 depends on it:

- String interpolation (`s"count = $count"`) — retires the `str3`/`str4`/`str5` concat helpers littering `compiler-scalui/src/CliCmds.scala`.
- Typed theme/color accessors (e.g. `Theme.accent`, `Color.rgb(...)`) — retires raw ARGB literals like `4282220198` in app code.
- `val` bindings anywhere in a block (not just block starts).
- `List` literals and enough list surface to delete the C-side Todo controller (ADR 0004, rule 1).

Done when: `scalui new --ui` templates and all `examples/` build without concat helpers or magic color ints.

## 3. Honest CLI surface

Stage-1 `fmt` and `watch` are stubs dressed as features: `fmt` only parse-validates (or delegates to the Rust canary), `watch` is a blind 2 s poll-rebuild.

- `scalui fmt`: port the Stage-0 formatter (`crates/compiler/src/format.rs`) to Stage 1, or mark `fmt` as canary-only in README until ported.
- `scalui watch`: rebuild on actual file-change detection (mtime fingerprinting over `src/**`), not a fixed sleep loop.
- `scalui test` for app projects should not rebuild the C runtime test suites; gate that behind a flag.

Done when: README's CLI claims match behavior with no canary delegation on the default path, or the claims are removed.

## 4. Real manifest parsing

`scalui.toml` is currently read via string search (`readTomlQuotedAfter` in `compiler-scalui/src/CliCmds.scala`), which breaks on any formatting variation.

- Minimal TOML subset parser in the kernel dialect (tables, strings, ints, arrays) shared by both compilers, validated against `docs/schemas/scalui-toml.md`.

Done when: reformatted-but-equivalent `scalui.toml` files produce identical behavior across Stage 0 and Stage 1.

## Deferred (do not start)

- GC revisit beyond malloc/free (ADR 0001) — wait for real long-lived interactive apps to pressure it.
- Windows embedder, device NDK/Xcode builds, Impeller (ADR 0002) — platform breadth after the language is credible.
