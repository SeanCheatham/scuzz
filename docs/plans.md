# Short-term plan

## Slice: Last-use release for View tap lambda packs

`View.button` unpacks `cons(fn, cons(env, nil))` and drops the owned label. The closure list stays allocated.

- Mark the tap lambda pack owned and drop it after unpack.
- Proof: compiler IR for `Ui.run(_ => View.button("a", _ => ()))` shows `sz_release` of the closure list after the call.
- Apply the same drop to `iconButton`, `fab`, `outlinedButton`, `textButton`, `actionChip`, and `inkWell`.
- Out of scope: RC stream nodes, OS threads.
