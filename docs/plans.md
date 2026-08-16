# Short-term plan

## Slice: Last-use release for `Signal.str` inputs

`Signal.setList` retains its list. `Signal.str` copies the bytes and still leaves the owned `String`.

- Drop an owned string after `sz_lang_signal_str`.
- Proof: compiler IR for `Signal.str("a")` shows `sz_release` of the string after the call.
- Out of scope: RC stream nodes, View lambda packs, OS threads.
