# Short-term plan

## Slice: Last-use release for `View.actionChip` labels

`View.textButton` copies the label and drops the owned string. `View.actionChip` still leaves the owned `String`.

- Drop an owned string after `sz_lang_view_action_chip`.
- Proof: compiler IR for `Ui.run(_ => View.actionChip("a", _ => ()))` shows `sz_release` of the string after the call.
- Out of scope: View lambda packs, RC stream nodes, OS threads.
