# Short-term plan

## Slice: Last-use release for `View.each`, `Signal.map`, and `Ui.run` packs

Tap constructors drop the wrapper list after unpack. `View.each`, `Signal.map`, and `Ui.run` still leave `cons(fn, cons(env, nil))`.

- Mark those packs owned and drop them after unpack.
- Proof: compiler IR for `Ui.run(_ => View.text("a"))` shows `sz_release` of the rebuild pack after `sz_ui_run_rebuild`.
- Apply the same drop to `View.each` mappers and `Signal.map`.
- Out of scope: RC stream nodes, OS threads, tap capture lists on `sz_view_free`.
