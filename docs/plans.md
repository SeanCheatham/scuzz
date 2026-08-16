# Short-term plan

## Slice: Last-use release for `View.text` labels

`Signal.setStr` copies bytes and drops the owned string. `View.text` still leaves the owned `String`.

- Drop an owned string after `sz_lang_view_text`.
- Proof: compiler IR for `Ui.run(_ => View.text("a"))` shows `sz_release` of the string after the call.
- Out of scope: RC stream nodes, View lambda packs, OS threads.
