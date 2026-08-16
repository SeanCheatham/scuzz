# Short-term plan

## Slice: Last-use release for `Signal.setList` inputs

`Signal.list` retains its list. `Signal.setList` still stores the new list without a retain.

- Retain the list in `sz_signal_list_set`. Drop an owned list after the call.
- Proof: compiler IR for `Signal.setList(items, ["a"])` shows `sz_release` of the list after `sz_lang_signal_list_set`.
- Out of scope: RC stream nodes, View lambda packs, OS threads.
