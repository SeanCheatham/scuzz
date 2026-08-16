# Short-term plan

## Slice: Last-use release for `View.outlinedButton` labels

`View.fab` copies the label and drops the owned string. `View.outlinedButton` still leaves the owned `String`.

- Drop an owned string after `sz_lang_view_outlined_button`.
- Proof: compiler IR for `Ui.run(_ => View.outlinedButton("a", _ => ()))` shows `sz_release` of the string after the call.
- Out of scope: View lambda packs, RC stream nodes, OS threads.
