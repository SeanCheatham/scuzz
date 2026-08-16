# Short-term plan

## Slice: Last-use release for `Signal.list` inputs

`Stream.eval` retains its IO. `Signal.list` still stores the list without a retain.

- Retain the list in `sz_signal_list`. Drop an owned list after the call.
- Proof: compiler IR for `Signal.list(["a"])` shows `sz_release` of the list after `sz_lang_signal_list`.
- Out of scope: RC stream nodes, View lambda packs, OS threads.
