# ScalUI plan — next steps

Phases 0–6 landed; v0 app path works; Stage-0 is a canary. Ordered by priority; each item has a done-when gate. Product intent and design locks: [`vision.md`](vision.md).

## 1. Port `scalui fmt` to Stage 1

- Port formatter into the kernel dialect (`compiler-scalui/`), or a thin Stage-1 driver over AST shapes Stage 1 already builds.
- Default `scalui fmt` / `fmt --check` must not require `SCALUI_CANARY`.
- Keep Stage-0 formatter as CI canary only.

Done when: `scalui fmt --check` on `examples/` and `compiler-scalui/src` succeeds via Stage 1/2 with no canary env, and README drops the canary caveat.

## 2. Todo tap ergonomics (expr-shaped)

List-mutation taps still contort around single-expr `if` arms. Prefer the [language direction](vision.md#language-direction): bind before the branch, expression arms, nested `for` if needed — **do not** grow statement-block / mid-branch `val` grammar as the long-term fix.

- Unblock `examples/todo` Add tap without hoisting flags.
- Keep Stage 0 + Stage 1 dual-boot green.

Done when: Todo Add tap is readable in-kernel without flag hoisting, dual-boot green.

## 3. Language-facing list ↔ View sync

Todo is append-only (`View.addTexts` + `addChild`). Need replace/clear without a C controller.

- `View.clearChildren` (or equivalent) in runtime + both compilers.
- Rebuild a `View.list` from a `Signal.list` in ScalUI.
- Headless goldens stay green under replace-style updates (edit/remove at least one item).

Done when: Todo (or sibling) reloads/replaces items from `Fs.read` + `Str.lines` without append-only assumptions or new C controllers.

## 4. Short app guide

- One short guide under `docs/` (kernel + blessed `IO` + `View`/`Ui` Headless path) linked from README.
- Cover: `scalui new --ui` → test → run --headless; Signal/View/list taps; TestRuntime; where canary still matters (`fmt` until §1).

Done when: README → guide → Counter/Todo works without reading all of `vision.md`.

## Deferred

- GC beyond malloc/free — when long-lived interactive apps pressure it ([vision](vision.md#gc-v0))
- Windows embedder, device NDK/Xcode, Impeller — after §1–§3 ([vision](vision.md#skia))
- Reactive View←Signal list sugar — after explicit clear/rebuild
- `scalui fuzz` — after stable tap labels + deterministic event scripts ([vision](vision.md#scalui-fuzz-aspirational))
- Expr-dialect/`for` binder cutover — when ready to churn kernel forwards-only ([vision](vision.md#language-direction))
