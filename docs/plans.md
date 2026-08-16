# Short-term plan

## Slice: Last-use release for `View.filterChip` labels

`View.chip` copies the label and drops the owned string. `View.filterChip` still leaves the owned `String`.

- Drop an owned string after `sz_lang_view_filter_chip`.
- Proof: compiler IR for `Ui.run(_ => View.filterChip(n, "a"))` shows `sz_release` of the string after the call.
- Out of scope: View lambda packs, RC stream nodes, OS threads.
