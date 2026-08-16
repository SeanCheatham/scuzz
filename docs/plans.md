# Short-term plan

## Slice: Last-use release for `View.inputChip` labels

`View.filterChip` copies the label and drops the owned string. `View.inputChip` still leaves the owned `String`.

- Drop an owned string after `sz_lang_view_input_chip`.
- Proof: compiler IR for `Ui.run(_ => View.inputChip(n, "a"))` shows `sz_release` of the string after the call.
- Out of scope: View lambda packs, RC stream nodes, OS threads.
