# Short-term plan

## Slice: Last-use release for `View.choiceChip` labels

`View.inputChip` copies the label and drops the owned string. `View.choiceChip` still leaves the owned `String`.

- Drop an owned string after `sz_lang_view_choice_chip`.
- Proof: compiler IR for `Ui.run(_ => View.choiceChip(n, 0, "a"))` shows `sz_release` of the string after the call.
- Out of scope: View lambda packs, RC stream nodes, OS threads.
