# ScalUI plan — next steps

Phases 0–6 and the post–Phase-6 credibility slice (self-host gate, dialect ergonomics, honest CLI, TOML parsing, List literals + language Todo) are landed. v0 app path works; Stage-0 remains a canary. This plan orders the next gaps so language and product credibility keep ahead of platform breadth.

Ordered by priority; each workstream lists its done-when gate.

## 1. Port `scalui fmt` to Stage 1

README already marks `fmt` as canary-only. That honesty is fine for a week; it is not fine for v1.

- Port `crates/compiler/src/format.rs` into the kernel dialect (`compiler-scalui/`), or emit a thin Stage-1 driver that pretty-prints via the same AST shapes Stage 1 already builds.
- Default `scalui fmt` / `fmt --check` must not require `SCALUI_CANARY`.
- Keep Stage-0 formatter as CI canary only.

Done when: `scalui fmt --check` on `examples/` and `compiler-scalui/src` succeeds via Stage 1/2 with no canary env, and README drops the canary caveat.

## 2. Block-shaped `if` / expression statements

List-mutation taps still contort around “`if` branches are single exprs.” Mid-block `val` exists; branch bodies do not accept the same block grammar.

- Then/else branches parse as blocks (same rules as lambda / `let` bodies): leading `val`s, mid-block statement exprs, final expr.
- No brace syntax — keep the newline/indent-free kernel style; just allow `val` after `else` the same way lambda bodies do.
- Rewrite `examples/todo` Add tap to the obvious multi-`val` else form once branches accept it.

Done when: a tap body can `else`-bind multiple `val`s without hoisting flags, and Stage 0 + Stage 1 dual-boot stays green.

## 3. Language-facing list ↔ View sync

Todo is append-only (`View.addTexts` + `addChild`). Real lists need replace/clear without a C controller.

- `View.clearChildren(view)` (or equivalent rebuild) in runtime + both compilers.
- Enough surface to rebuild a `View.list` from a `Signal.list` in ScalUI (helper def or small builtin).
- Keep Headless goldens for Todo green under replace-style updates (edit/remove at least one item in the example or a sibling).

Done when: Todo (or a successor example) can reload/replace items from `Fs.read` + `Str.lines` without append-only assumptions or new C controllers.

## 4. App developer docs slice

Vision still points at an effects/language guide that is not a durable in-tree surface for app authors.

- One short guide under `docs/` (language kernel + blessed `IO` + `View`/`Ui` Headless path) linked from README — not a second vision, not per-crate READMEs.
- Cover: `scalui new --ui` → test → run --headless; Signal/View/list taps; impurity boundary + `TestRuntime`; where Stage-0 canary still matters (`fmt` until §1 lands).

Done when: a new contributor can follow README → guide → Counter/Todo without reading ADRs first.

## Deferred (do not start)

- GC revisit beyond malloc/free (ADR 0001) — wait for long-lived interactive apps to pressure it.
- Windows embedder, device NDK/Xcode builds, Impeller (ADR 0002) — platform breadth after §1–§3.
- Reactive framework sugar (automatic View←Signal list binding) — only after explicit clear/rebuild exists.
