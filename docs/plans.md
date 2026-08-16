# Short-term plan

## Slice: Last-use release for `View.listTile` titles

`View.choiceChip` copies the label and drops the owned string. `View.listTile` still leaves the owned `String`.

- Drop an owned string after `sz_lang_view_list_tile`.
- Proof: compiler IR for `Ui.run(_ => View.listTile("a"))` shows `sz_release` of the string after the call.
- Out of scope: View lambda packs, RC stream nodes, OS threads.
