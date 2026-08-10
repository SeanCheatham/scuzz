# ScalUI plan — next steps

Phases 0–6 landed; v0 app path works; Stage-0 is a canary. Ordered by priority; each item has a done-when gate. Product intent and design locks: [`vision.md`](vision.md).

## Landed (this cycle)

- **Stage-1 `fmt`**: `compiler-scalui/src/Format.scala`; `scalui fmt` / `fmt --check` need no `SCALUI_CANARY`. Stage-0 formatter stays CI canary.
- **List ↔ View sync**: `View.clearChildren` / `View.setTexts` in runtime + both compilers; Todo load/Add/Clear use replace-style updates.
- **App guide**: [docs/guide.md](guide.md) linked from README.
- **Match / enum codegen on Stage 1**: nullary `enum` tags + `match` → `su_adt_tag` / `switch` / `phi` in `compiler-scalui`; `examples/adt` runs under Stage 1/2; dual-boot smokes adt.
- **Deeper list editing**: Todo seeds one row when empty; `setAt` + Rename tap edits index 0 and rebuilds via `View.setTexts`; Headless goldens cover the rename path (`tap_button`).

## Next

### 1. Todo tap ergonomics (expr-shaped)

Todo Add is readable today via **interim** val-led `if` branches (landed in both compilers). That is **not** the long-term shape — prefer the [language direction](vision.md#language-direction): bind before the branch, expression arms, nested `for` when an arm needs names. Do not grow statement-block / mid-branch `val` grammar further.

- Rewrite Todo Add (and similar taps) toward expr-shaped control flow when ready, without flag hoisting.
- Keep Stage 0 + Stage 1 dual-boot green.

Done when: Todo Add tap is readable without relying on val-led branch blocks as the intended dialect; dual-boot green.

## Deferred

- GC beyond malloc/free — when long-lived interactive apps pressure it ([vision](vision.md#gc-v0))
- Windows embedder, device NDK/Xcode, Impeller — after language credibility ([vision](vision.md#skia))
- Reactive View←Signal list sugar — after explicit clear/rebuild (now landed; sugar still deferred)
- `scalui fuzz` — after stable tap labels + deterministic event scripts ([vision](vision.md#scalui-fuzz-aspirational))
- Expr-dialect/`for` binder cutover — when ready to churn kernel forwards-only ([vision](vision.md#language-direction)); retires interim val-led `if` branches and closes Next §1
