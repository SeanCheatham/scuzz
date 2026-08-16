# Short-term plan

## Slice: Last-use release for `View.chip` labels

`View.actionChip` copies the label and drops the owned string. `View.chip` still leaves the owned `String`.

- Drop an owned string after `sz_lang_view_chip`.
- Proof: compiler IR for `Ui.run(_ => View.chip(n, "a"))` shows `sz_release` of the string after the call.
- Out of scope: View lambda packs, RC stream nodes, OS threads.
