# Short-term plan

## Slice: Last-use release for `View.iconButton` labels

`View.button` copies the label and drops the owned string. `View.iconButton` still leaves the owned `String`.

- Drop an owned string after `sz_lang_view_icon_button`.
- Proof: compiler IR for `Ui.run(_ => View.iconButton("i", _ => ()))` shows `sz_release` of the string after the call.
- Out of scope: View lambda packs, RC stream nodes, OS threads.
