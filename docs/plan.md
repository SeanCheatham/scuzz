# ScalUI plan — next steps

Phases 0–6 and the post–Phase-6 credibility slice are landed. Plan items §1–§4 below are landed on this branch (Stage-1 `fmt`, val-led `if` branches, `View.clearChildren` / `setTexts`, app guide). v0 app path works; Stage-0 remains a canary. This plan orders further gaps so language and product credibility keep ahead of platform breadth.

Ordered by priority; each workstream lists its done-when gate.

## Landed (this cycle)

- **§1 Stage-1 `fmt`**: `compiler-scalui/src/Format.scala`; `scalui fmt` / `fmt --check` need no `SCALUI_CANARY`. Stage-0 formatter stays CI canary.
- **§2 Val-led `if` branches**: then/else may start with `val` and use block bindings; mid-block `val x = if … else e` stays single-expr so following vals are not stolen. Todo Add tap uses multi-`val` else.
- **§3 List ↔ View sync**: `View.clearChildren` / `View.setTexts` in runtime + both compilers; Todo load/Add/Clear use replace-style updates.
- **§4 App guide**: [docs/guide.md](guide.md) linked from README.

## Next

### 1. Match / enum codegen on Stage 1

Stage-1 parser round-trips `enum` / `match` for `fmt`; emit still Stage-0-only for ADT programs.

- Port match lowering (or a thin subset) into `compiler-scalui` so `examples/adt` builds under Stage 1/2.
- Keep Stage 0 as canary for the same goldens.

Done when: `scalui build examples/adt` via Stage 1/2 runs and prints the adt line; dual-boot stays green.

### 2. Deeper list editing demos

Todo Clear proves replace/clear. Edit-in-place (rename one row) still wants a small ScalUI helper over `getList` / update / `setTexts`.

Done when: an example tap edits one item and Headless goldens stay green without new C controllers.

## Deferred (do not start)

- GC revisit beyond malloc/free (ADR 0001) — wait for long-lived interactive apps to pressure it.
- Windows embedder, device NDK/Xcode builds, Impeller (ADR 0002) — platform breadth after language credibility.
- Reactive framework sugar (automatic View←Signal list binding) — only after explicit clear/rebuild exists (now landed; sugar still deferred).
- Full `if` branch = unrestricted `parse_block` (including non-`val`-led statement exprs) — requires parenthesizing mid-block if RHS or indent rules; val-led branches cover the Todo shape.
