# Short-term plan

## Slice: Last-use release for `View.fab` labels

`View.iconButton` copies the label and drops the owned string. `View.fab` still leaves the owned `String`.

- Drop an owned string after `sz_lang_view_fab`.
- Proof: compiler IR for `Ui.run(_ => View.fab("+", _ => ()))` shows `sz_release` of the string after the call.
- Out of scope: View lambda packs, RC stream nodes, OS threads.
